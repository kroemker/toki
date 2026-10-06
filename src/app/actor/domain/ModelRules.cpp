#include "ModelRules.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/OverlayCode.h"
#include "base/Diagnostic.h"
#include "cparser/Calls.h"
#include "cparser/Initializer.h"
#include "cparser/Parser.h"
#include "cparser/Token.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array ProfileTypes = {std::string_view("ActorProfile"),
                                     std::string_view("ActorInit")};
constexpr usize ProfileObject = 3;
constexpr usize ProfileInit = 5;
constexpr usize ProfileDraw = 8;
constexpr f64 InitChainScaleDivisor = 1000.0;

constexpr std::array SkeletonInits = {std::string_view("SkelAnime_Init"),
                                      std::string_view("SkelAnime_InitFlex"),
                                      std::string_view("SkelAnime_InitLink")};
constexpr usize SkeletonArgument = 2;
constexpr usize AnimationArgument = 3;

using Functions = std::vector<const cparser::FunctionDefinition*>;

[[nodiscard]] std::vector<cparser::Call>
findCallsIn(const Functions& functions, const std::span<const std::string_view> names) {
    std::vector<cparser::Call> calls;
    for (const cparser::FunctionDefinition* function : functions) {
        std::ranges::move(cparser::findCalls(function->body,
                                             [names](const std::string_view callee) {
                                                 return std::ranges::contains(names, callee);
                                             }),
                          std::back_inserter(calls));
    }
    return calls;
}

[[nodiscard]] std::string spell(const std::span<const cparser::Token> tokens) {
    std::string spelled;
    for (const cparser::Token& token : tokens) {
        spelled += token.text;
    }
    return spelled;
}

[[nodiscard]] std::optional<std::string_view>
findIdentifier(const std::span<const cparser::Token> tokens) {
    if (tokens.size() != 1 || tokens.front().kind != cparser::TokenKind::Identifier ||
        cparser::isIdentifier(tokens.front(), "NULL")) {
        return std::nullopt;
    }
    return tokens.front().text;
}

[[nodiscard]] std::optional<std::string_view> findDrawAssignment(const Functions& functions) {
    for (const cparser::FunctionDefinition* function : functions) {
        const std::span<const cparser::Token> body = function->body;
        for (usize at = 1; at + 3 < body.size(); ++at) {
            const bool isMember = cparser::isPunctuator(body[at - 1], ".") ||
                                  cparser::isPunctuator(body[at - 1], "->");
            if (isMember && cparser::isIdentifier(body[at], "draw") &&
                cparser::isPunctuator(body[at + 1], "=") &&
                cparser::isPunctuator(body[at + 3], ";")) {
                if (std::optional<std::string_view> name =
                        findIdentifier(body.subspan(at + 2, 1))) {
                    return name;
                }
            }
        }
    }
    return std::nullopt;
}

class RuleReader {
public:
    RuleReader(const std::string_view actorName, const OverlayCode& code,
               const FindObjectFolder& findObjectFolder)
        : code(code), findObjectFolder(findObjectFolder) {
        model.actorName = actorName;
    }

    [[nodiscard]] ActorModel read() && {
        if (!readProfile()) {
            return std::move(model);
        }
        readSkeleton();
        if (!model.skeleton) {
            readDisplayLists();
        }
        readScale();
        readHeightOffset();
        if (!model.hasModel() && model.gaps.empty()) {
            addGap(GapKind::NothingDrawn, {},
                   "neither Init nor Draw names a skeleton or a display list");
        }
        return std::move(model);
    }

private:
    void addGap(const GapKind kind, base::SourceLocation location, std::string message) {
        model.gaps.push_back(ModelGap{
            .kind = kind, .diagnostic = base::makeNote(std::move(location), std::move(message))});
    }

    [[nodiscard]] bool readProfile();

    [[nodiscard]] std::optional<ObjectSymbol> findSymbol(const cparser::Token& name,
                                                         std::string_view what, GapKind notSymbol);

    [[nodiscard]] std::optional<ObjectSymbol> findChangedAnimation(const Functions& functions);

    void readSkeleton();

    [[nodiscard]] bool readSkeletonIn(const cparser::FunctionDefinition& function);

    void readAnimation(const cparser::Call& skeletonInit,
                       const cparser::FunctionDefinition& setUpIn);

    void readDisplayLists();

    void readDisplayList(const cparser::Call& call, std::span<const cparser::Token> argument,
                         Pass pass);

    void readScale();

    void readHeightOffset();

    const OverlayCode& code;
    const FindObjectFolder& findObjectFolder;
    ActorModel model;
    Functions init;
    Functions draw;
};

bool RuleReader::readProfile() {
    const cparser::Declaration* profile = code.findDefinitionOfType(ProfileTypes);
    const std::vector<std::span<const cparser::Token>> elements =
        profile == nullptr ? std::vector<std::span<const cparser::Token>>{}
                           : cparser::splitElements(profile->initializer);
    if (elements.size() <= ProfileDraw) {
        addGap(GapKind::NoProfile, profile == nullptr ? base::SourceLocation{} : profile->location,
               "the overlay defines no ActorProfile that can be read");
        return false;
    }
    if (const std::optional<std::string_view> object = findIdentifier(elements[ProfileObject])) {
        model.profileObject = findObjectFolder(*object).value_or("");
    }
    if (const std::optional<std::string_view> name = findIdentifier(elements[ProfileInit])) {
        init = code.listReachable(*name);
    }
    std::optional<std::string_view> drawName = findIdentifier(elements[ProfileDraw]);
    if (!drawName) {
        drawName = findDrawAssignment(code.listFunctions());
    }
    if (drawName) {
        draw = code.listReachable(*drawName);
    }
    if (draw.empty()) {
        addGap(GapKind::NoDraw, profile->location,
               "the profile names no Draw, and the overlay sets none, so the actor draws "
               "nothing");
    }
    return true;
}

std::optional<ObjectSymbol> RuleReader::findSymbol(const cparser::Token& name,
                                                   const std::string_view what,
                                                   const GapKind notSymbol) {
    std::optional<ObjectSymbol> symbol = code.findObjectSymbol(name);
    if (symbol) {
        return symbol;
    }
    if (code.isOverlayData(name.text)) {
        addGap(GapKind::OverlayData, cparser::locate(name),
               std::format("{} '{}' is the overlay's own data, which is not drawn yet", what,
                           name.text));
    } else if (code.isDeclared(name.text)) {
        addGap(GapKind::SymbolNotInObject, cparser::locate(name),
               std::format("{} '{}' is declared by no object header the overlay includes", what,
                           name.text));
    } else {
        addGap(notSymbol, cparser::locate(name),
               std::format("{} '{}' is a variable, not a symbol", what, name.text));
    }
    return std::nullopt;
}

std::optional<ObjectSymbol> RuleReader::findChangedAnimation(const Functions& functions) {
    constexpr usize ChangedAnimation = 1;
    for (const cparser::FunctionDefinition* function : functions) {
        const std::vector<cparser::Call> changes =
            cparser::findCalls(function->body, [](const std::string_view callee) {
                return callee.starts_with("Animation_");
            });
        for (const cparser::Call& change : changes) {
            const cparser::Token* name = change.arguments.size() > ChangedAnimation
                                             ? findLiteralSymbol(change.arguments[ChangedAnimation])
                                             : nullptr;
            std::optional<ObjectSymbol> animation =
                name == nullptr ? std::nullopt : code.findObjectSymbol(*name);
            if (animation && animation->type == "AnimationHeader") {
                return animation;
            }
        }
    }
    return std::nullopt;
}

void RuleReader::readSkeleton() {
    constexpr std::array SkinInits = {std::string_view("Skin_Init")};
    std::array<std::string_view, SkeletonInits.size() + 1> anyInit{};
    std::ranges::copy(SkeletonInits, anyInit.begin());
    anyInit.back() = SkinInits.front();
    const Functions searched = findCallsIn(init, anyInit).empty() ? code.listFunctions() : init;
    for (const cparser::Call& call : findCallsIn(searched, SkinInits)) {
        addGap(GapKind::SkinSkeleton, call.location,
               "the actor's skeleton is a skin (Skin_Init), which is not drawn yet");
    }
    for (const cparser::FunctionDefinition* function : searched) {
        if (readSkeletonIn(*function)) {
            return;
        }
    }
}

bool RuleReader::readSkeletonIn(const cparser::FunctionDefinition& function) {
    const std::vector<cparser::Call> calls =
        cparser::findCalls(function.body, [](const std::string_view callee) {
            return std::ranges::contains(SkeletonInits, callee);
        });
    for (const cparser::Call& call : calls) {
        if (call.arguments.size() <= AnimationArgument) {
            continue;
        }
        const cparser::Token* name = findLiteralSymbol(call.arguments[SkeletonArgument]);
        if (name == nullptr) {
            addGap(GapKind::SkeletonNotLiteral, call.location,
                   std::format("the skeleton {} is given, '{}', is not a symbol", call.callee,
                               spell(call.arguments[SkeletonArgument])));
            continue;
        }
        std::optional<ObjectSymbol> skeleton =
            findSymbol(*name, "the skeleton", GapKind::SkeletonNotLiteral);
        if (!skeleton) {
            continue;
        }
        const bool isFlexible =
            skeleton->type == "FlexSkeletonHeader" ||
            (skeleton->type != "SkeletonHeader" && call.callee == "SkelAnime_InitFlex");
        model.skeleton = SkeletonModel{.skeleton = std::move(*skeleton), .isFlexible = isFlexible};
        if (call.callee != "SkelAnime_InitLink") {
            readAnimation(call, function);
        }
        return true;
    }
    return false;
}

void RuleReader::readAnimation(const cparser::Call& skeletonInit,
                               const cparser::FunctionDefinition& setUpIn) {
    if (const cparser::Token* name = findLiteralSymbol(skeletonInit.arguments[AnimationArgument])) {
        model.skeleton->animation =
            findSymbol(*name, "the animation", GapKind::AnimationNotLiteral);
        return;
    }
    Functions searched = init;
    if (!std::ranges::contains(searched, &setUpIn)) {
        searched.push_back(&setUpIn);
    }
    model.skeleton->animation = findChangedAnimation(searched);
    if (model.skeleton->animation) {
        return;
    }
    addGap(GapKind::AnimationNotLiteral, skeletonInit.location,
           "no animation is named for the skeleton, so it is drawn at rest");
}

void RuleReader::readDisplayLists() {
    constexpr std::array Draws = {std::string_view("Gfx_DrawDListOpa"),
                                  std::string_view("Gfx_DrawDListXlu"),
                                  std::string_view("gSPDisplayList")};
    for (const cparser::Call& call : findCallsIn(draw, Draws)) {
        if (call.arguments.size() != 2) {
            continue;
        }
        if (call.callee == "gSPDisplayList") {
            const std::string target = spell(call.arguments[0]);
            if (target.contains("POLY_OPA_DISP")) {
                readDisplayList(call, call.arguments[1], Pass::Opaque);
            } else if (target.contains("POLY_XLU_DISP")) {
                readDisplayList(call, call.arguments[1], Pass::Translucent);
            }
            continue;
        }
        readDisplayList(call, call.arguments[1],
                        call.callee == "Gfx_DrawDListXlu" ? Pass::Translucent : Pass::Opaque);
    }
}

void RuleReader::readDisplayList(const cparser::Call& call,
                                 const std::span<const cparser::Token> argument, const Pass pass) {
    const cparser::Token* name = findLiteralSymbol(argument);
    if (name == nullptr) {
        addGap(GapKind::DisplayListNotLiteral, call.location,
               std::format("the display list {} is given, '{}', is not a symbol", call.callee,
                           spell(argument)));
        return;
    }
    const bool isDrawn = std::ranges::any_of(model.displayLists, [&](const DisplayListDraw& drawn) {
        return drawn.displayList.name == name->text && drawn.pass == pass;
    });
    if (isDrawn) {
        return;
    }
    if (std::optional<ObjectSymbol> symbol =
            findSymbol(*name, "the display list", GapKind::DisplayListNotLiteral)) {
        model.displayLists.push_back(
            DisplayListDraw{.displayList = std::move(*symbol), .pass = pass});
    }
}

void RuleReader::readScale() {
    constexpr std::array SetScale = {std::string_view("Actor_SetScale")};
    constexpr std::array ProcessInitChain = {std::string_view("Actor_ProcessInitChain")};
    std::optional<cparser::Call> unread;
    for (const cparser::Call& call : findCallsIn(init, SetScale)) {
        const std::optional<f64> scale =
            call.arguments.size() == 2 ? code.evaluateNumber(call.arguments[1]) : std::nullopt;
        if (scale) {
            model.scale = static_cast<f32>(*scale);
            model.scaleSource = call.location;
        } else if (!model.scaleSource) {
            unread = call;
        }
    }
    if (model.scaleSource) {
        return;
    }
    for (const cparser::Call& call : findCallsIn(init, ProcessInitChain)) {
        const std::optional<std::string_view> name =
            call.arguments.size() == 2 ? findIdentifier(call.arguments[1]) : std::nullopt;
        const cparser::Declaration* chain = name ? code.findDefinition(*name) : nullptr;
        if (chain == nullptr) {
            continue;
        }
        const std::vector<cparser::Call> entries =
            cparser::findCalls(chain->initializer, [](const std::string_view callee) {
                return callee == "ICHAIN_VEC3F_DIV1000";
            });
        for (const cparser::Call& entry : entries) {
            if (entry.arguments.size() < 2 || !findIdentifier(entry.arguments[0]).has_value() ||
                entry.arguments[0].front().text != "scale") {
                continue;
            }
            if (const std::optional<f64> value = code.evaluateNumber(entry.arguments[1])) {
                model.scale = static_cast<f32>(*value / InitChainScaleDivisor);
                model.scaleSource = entry.location;
            }
        }
    }
    if (!model.scaleSource && unread) {
        addGap(GapKind::ScaleNotLiteral, unread->location,
               std::format("the scale Actor_SetScale is given, '{}', is not a constant, so the "
                           "actor is drawn at the default scale",
                           spell(unread->arguments.back())));
    }
}

void RuleReader::readHeightOffset() {
    constexpr std::array ShapeInit = {std::string_view("ActorShape_Init")};
    for (const cparser::Call& call : findCallsIn(init, ShapeInit)) {
        if (call.arguments.size() < 2) {
            continue;
        }
        if (const std::optional<f64> offset = code.evaluateNumber(call.arguments[1])) {
            model.yOffset = static_cast<f32>(*offset);
        }
    }
}

} // namespace

ActorModel applyModelRules(const std::string_view actorName, const OverlayCode& code,
                           const FindObjectFolder& findObjectFolder) {
    return RuleReader(actorName, code, findObjectFolder).read();
}

} // namespace toki::actor
