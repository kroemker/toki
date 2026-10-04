#pragma once

#include "app/assets/domain/AssetReader.h"
#include "app/assets/domain/AssetRows.h"
#include "app/core/ui/Panel.h"
#include "app/workspace/domain/WorkspaceService.h"
#include "hackeroot/Assets.h"
#include "hackeroot/Workspace.h"

#include <array>
#include <filesystem>
#include <vector>

namespace toki::assets {

class AssetBrowserPanel : public core::Panel {
public:
    AssetBrowserPanel(const workspace::WorkspaceService& workspaceService,
                      AssetReader& assetReader);

    void draw() override;

private:
    enum class Kind : u8 {
        Scenes,
        Objects,
    };

    void drawScenes(const hackeroot::Workspace& workspace, float height);
    void drawObjects(const hackeroot::Workspace& workspace, float height);
    void drawAssetRow(const hackeroot::AssetDirectory& asset, const char* name);
    void drawSceneDetails(const hackeroot::Workspace& workspace);
    void drawObjectDetails(const hackeroot::Workspace& workspace);
    void drawReport(const hackeroot::AssetDirectory& asset);
    void drawActions(const hackeroot::AssetDirectory& asset);
    void openAsset(const hackeroot::AssetDirectory& asset);

    const workspace::WorkspaceService& workspaceService;
    AssetReader& assetReader;
    std::array<char, 128> filter{};
    Kind kind = Kind::Scenes;
    std::filesystem::path selected;
};

} // namespace toki::assets
