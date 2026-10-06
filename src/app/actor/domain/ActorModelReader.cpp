#include "ActorModelReader.h"

#include "app/actor/domain/ActorModel.h"
#include "app/actor/domain/ModelRules.h"
#include "app/actor/domain/OverlayCode.h"
#include "base/Diagnostic.h"
#include "cparser/Preprocessor.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <expected>
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
    return applyModelRules(actor.name, code, [&workspace](const std::string_view enumName) {
        const auto& objects = workspace.getTables().objects;
        const auto found = std::ranges::find(objects, enumName, &hackeroot::ObjectEntry::enumName);
        return found == objects.end() ? std::nullopt : std::optional<std::string>(found->name);
    });
}

} // namespace toki::actor
