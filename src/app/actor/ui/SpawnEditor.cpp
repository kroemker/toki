#include "SpawnEditor.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/EntryNames.h"
#include "app/actor/ui/EditWidgets.h"

#include <imgui.h>

#include <optional>
#include <span>
#include <string>

namespace toki::actor {

namespace {

[[nodiscard]] bool editFlagField(SpawnCondition& spawn, const std::span<const ParamField> fields) {
    bool isChanged = false;
    const char* shown = spawn.flagField ? spawn.flagField->c_str() : "no flag";
    if (ImGui::BeginCombo("Flag field", shown)) {
        if (ImGui::Selectable("no flag", !spawn.flagField)) {
            isChanged = spawn.flagField.has_value();
            spawn.flagField.reset();
        }
        for (const ParamField& field : fields) {
            if (ImGui::Selectable(field.name.c_str(), spawn.flagField == field.name)) {
                isChanged = spawn.flagField != field.name;
                spawn.flagField = field.name;
            }
        }
        ImGui::EndCombo();
    }
    if (spawn.flagField) {
        ImGui::SameLine();
        isChanged = ImGui::Checkbox("must be set", &spawn.isFlagSet) || isChanged;
    }
    return isChanged;
}

} // namespace

bool drawSpawnEditor(std::optional<SpawnCondition>& spawn,
                     const std::span<const ParamField> fields) {
    bool hasSpawn = spawn.has_value();
    bool isChanged = ImGui::Checkbox("Spawns only when", &hasSpawn);
    if (isChanged) {
        spawn = hasSpawn ? std::optional(SpawnCondition{}) : std::nullopt;
    }
    if (!spawn) {
        return isChanged;
    }
    ImGui::Indent();
    isChanged = editOptionalName("Age", spawn->age, AgeNames, "any age") || isChanged;
    isChanged = editOptionalName("Time", spawn->time, TimeNames, "any time") || isChanged;
    isChanged = editFlagField(*spawn, fields) || isChanged;
    ImGui::Unindent();
    return isChanged;
}

} // namespace toki::actor
