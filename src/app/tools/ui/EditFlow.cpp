#include "EditFlow.h"

#include "app/core/ui/Icons.h"
#include "app/core/ui/Theme.h"
#include "app/tools/Types.h"
#include "app/tools/domain/EditWorker.h"
#include "hackeroot/CheckoutEdits.h"

#include <imgui.h>

#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace toki::tools {

namespace {

[[nodiscard]] ImVec4 findStatusColor(const hackeroot::EditStatus status) {
    switch (status) {
    case hackeroot::EditStatus::Add:
        return core::palette::Success;
    case hackeroot::EditStatus::Update:
        return core::palette::Warning;
    case hackeroot::EditStatus::Present:
        return core::palette::TextMuted;
    }
    return core::palette::Text;
}

[[nodiscard]] const char* findStatusWord(const hackeroot::EditStatus status) {
    switch (status) {
    case hackeroot::EditStatus::Add:
        return "add";
    case hackeroot::EditStatus::Update:
        return "change";
    case hackeroot::EditStatus::Present:
        return "there";
    }
    return "";
}

} // namespace

void EditFlow::requestPlan(std::filesystem::path repository, EditWorker::MakePlan makePlan) {
    plannedRepository = std::move(repository);
    reviewed.reset();
    status = "Looking at the checkout…";
    isFailure = false;
    editWorker.plan(std::move(makePlan));
}

bool EditFlow::takeResult() {
    std::optional<EditResult> result = editWorker.update();
    if (!result) {
        return false;
    }
    if (result->plan) {
        if (!*result->plan) {
            status = result->plan->error();
            isFailure = true;
        } else if (!(*result->plan)->hasWork()) {
            status = "Nothing to change: the checkout has it all already.";
            isFailure = false;
        } else {
            reviewed = std::move(**result->plan);
            status.clear();
        }
        return false;
    }
    if (result->applied && !*result->applied) {
        status = result->applied->error();
        isFailure = true;
        return false;
    }
    if (result->applied) {
        const usize count = (*result->applied)->written.size();
        status = std::format("Wrote {} {}.", count, count == 1 ? "file" : "files");
        isFailure = false;
        return true;
    }
    return false;
}

bool EditFlow::draw(const std::filesystem::path& repository) {
    const bool isWritten = takeResult();
    if (reviewed && repository != plannedRepository) {
        reviewed.reset();
        status = "Another checkout was opened, so the plan was dropped; review again.";
        isFailure = false;
    }
    if (reviewed) {
        drawReview();
    }
    if (!status.empty()) {
        if (isFailure) {
            ImGui::TextColored(core::palette::Danger, "%s %s", core::icons::Warning,
                               status.c_str());
        } else {
            ImGui::TextColored(core::palette::TextMuted, "%s", status.c_str());
        }
    }
    return isWritten;
}

void EditFlow::drawReview() {
    ImGui::SeparatorText("What will change");
    for (const hackeroot::FileEdit& file : reviewed->files) {
        ImGui::TextUnformatted(file.file.generic_string().c_str());
        if (!file.before) {
            ImGui::SameLine();
            ImGui::TextColored(core::palette::Success, "new file");
        }
        ImGui::Indent();
        for (const hackeroot::EditItem& item : file.items) {
            ImGui::TextColored(findStatusColor(item.status), "%-6s", findStatusWord(item.status));
            ImGui::SameLine();
            ImGui::TextUnformatted(item.label.c_str());
        }
        ImGui::Unindent();
    }
    for (const std::string& warning : reviewed->warnings) {
        ImGui::TextColored(core::palette::Warning, "%s", core::icons::Warning);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", warning.c_str());
    }
    ImGui::BeginDisabled(editWorker.isBusy());
    if (ImGui::Button("Write")) {
        editWorker.apply(plannedRepository, std::move(*reviewed));
        reviewed.reset();
        status = "Writing…";
        isFailure = false;
    }
    ImGui::EndDisabled();
    if (reviewed) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            reviewed.reset();
        }
    }
}

} // namespace toki::tools
