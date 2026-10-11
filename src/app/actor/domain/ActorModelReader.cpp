#include "ActorModelReader.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/ModelRules.h"
#include "app/actor/domain/OverlayCode.h"
#include "app/actor/domain/ParamRules.h"
#include "base/Diagnostic.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace toki::actor {

namespace {

[[nodiscard]] ActorModel makeMissing(const hackeroot::ActorEntry& actor,
                                     base::Diagnostic diagnostic) {
    diagnostic.severity = base::Severity::Note;
    return ActorModel{.actorName = actor.name,
                      .gaps = {ModelGap{.kind = GapKind::NoOverlay, .diagnostic = diagnostic}}};
}

void linkDisplayListTables(ActorModel& model) {
    if (model.displayListTables.empty()) {
        return;
    }
    const DisplayListTable& table = model.displayListTables.front();
    if (std::ranges::contains(model.params, table.mask, &ActorParam::mask)) {
        return;
    }
    const auto overlapping = std::ranges::find_if(
        model.params, [&](const ActorParam& param) { return (param.mask & table.mask) != 0; });
    if (overlapping == model.params.end()) {
        model.params.push_back(ActorParam{
            .name = std::string(ModelParam), .mask = table.mask, .isSigned = table.isSigned});
        std::ranges::stable_sort(model.params, {},
                                 [](const ActorParam& param) { return findMaskShift(param.mask); });
        return;
    }
    model.gaps.push_back(ModelGap{
        .kind = GapKind::DisplayListNotLiteral,
        .diagnostic = base::makeNote(
            table.location,
            std::format("the display lists of '{}' are picked by bits 0x{:04X}, which share "
                        "bits with the param '{}', so which one is drawn is not followed",
                        table.table, table.mask, overlapping->name))});
    model.displayListTables.clear();
}

} // namespace

ActorModel readActorModel(const hackeroot::Workspace& workspace,
                          const hackeroot::ActorEntry& actor) {
    const std::expected<hackeroot::OverlayFiles, base::Diagnostic> overlay =
        hackeroot::findOverlayFiles(workspace.getRoot(), actor.name);
    if (!overlay) {
        return makeMissing(actor, overlay.error());
    }
    const std::expected<cparser::TranslationUnit, base::Diagnostic> unit =
        hackeroot::preprocessOverlay(workspace, *overlay);
    if (!unit) {
        return makeMissing(actor, unit.error());
    }
    const OverlayCode code(*unit, overlay->main);
    ActorModel model =
        applyModelRules(actor.name, code, [&workspace](const std::string_view enumName) {
            const auto& objects = workspace.getTables().objects;
            const auto found =
                std::ranges::find(objects, enumName, &hackeroot::ObjectEntry::enumName);
            return found == objects.end() ? std::nullopt : std::optional<std::string>(found->name);
        });
    model.params = findActorParams(code);
    linkDisplayListTables(model);
    return model;
}

} // namespace toki::actor
