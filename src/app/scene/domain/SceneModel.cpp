#include "SceneModel.h"

#include "app/scene/domain/Scene.h"
#include "hackeroot/AssetImage.h"
#include "z64/Relocation.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace toki::scene {

SceneModel::SceneModel(const Scene& scene) {
    if (!scene.images.empty()) {
        gameCode = scene.images.front();
    }
    for (usize index = SceneImage; index < scene.images.size(); ++index) {
        const z64::MemoryImage& image = *scene.images[index];
        const u8 segment = image.segments.empty() ? 0 : image.segments.front().number;
        segments.push_back(segment);
        images.push_back(z64::listSymbolData(image, segment));
    }
}

std::optional<usize> SceneModel::findImage(const std::string_view name) const {
    for (usize image = 0; image < images.size(); ++image) {
        if (std::ranges::contains(images[image], name, &z64::SymbolData::name)) {
            return image;
        }
    }
    return std::nullopt;
}

const z64::SymbolData* SceneModel::findSymbol(const std::string_view name) const {
    for (const std::vector<z64::SymbolData>& symbols : images) {
        const auto found = std::ranges::find(symbols, name, &z64::SymbolData::name);
        if (found != symbols.end()) {
            return &*found;
        }
    }
    return nullptr;
}

z64::SymbolData* SceneModel::findMutable(const std::string_view name) {
    return const_cast<z64::SymbolData*>(std::as_const(*this).findSymbol(name));
}

std::expected<void, base::Diagnostic> SceneModel::setSymbol(z64::SymbolData symbol) {
    z64::SymbolData* found = findMutable(symbol.name);
    if (found == nullptr) {
        return std::unexpected(base::makeError(
            {}, std::format("the scene has no symbol '{}' to change", symbol.name)));
    }
    symbol.segment = found->segment;
    *found = std::move(symbol);
    edited = true;
    return {};
}

std::expected<void, base::Diagnostic> SceneModel::addSymbol(const usize image,
                                                            z64::SymbolData symbol) {
    if (image >= images.size()) {
        return std::unexpected(base::makeError(
            {}, std::format("the scene has no image {} to add '{}' to", image, symbol.name)));
    }
    if (findSymbol(symbol.name) != nullptr) {
        return std::unexpected(base::makeError(
            {}, std::format("the scene already has a symbol named '{}'", symbol.name)));
    }
    symbol.segment = segments[image];
    images[image].push_back(std::move(symbol));
    edited = true;
    return {};
}

std::expected<void, base::Diagnostic> SceneModel::removeSymbol(const std::string_view name) {
    std::vector<std::string> pointing;
    for (const std::vector<z64::SymbolData>& symbols : images) {
        for (const z64::SymbolData& symbol : symbols) {
            if (symbol.name != name &&
                std::ranges::contains(symbol.relocations, name, &z64::Relocation::symbol)) {
                pointing.push_back(symbol.name);
            }
        }
    }
    if (!pointing.empty()) {
        std::string listed;
        for (const std::string& symbol : pointing) {
            listed += listed.empty() ? symbol : ", " + symbol;
        }
        return std::unexpected(base::makeError(
            {}, std::format("'{}' cannot be removed while {} point at it", name, listed)));
    }
    const std::optional<usize> image = findImage(name);
    if (!image) {
        return std::unexpected(
            base::makeError({}, std::format("the scene has no symbol '{}' to remove", name)));
    }
    std::erase_if(images[*image],
                  [name](const z64::SymbolData& symbol) { return symbol.name == name; });
    edited = true;
    return {};
}

std::vector<std::shared_ptr<const z64::MemoryImage>> SceneModel::layOut() const {
    std::vector<std::shared_ptr<const z64::MemoryImage>> laidOut;
    if (gameCode) {
        laidOut.push_back(gameCode);
    }
    for (const std::vector<z64::SymbolData>& symbols : images) {
        std::vector<z64::ImageObject> objects;
        objects.reserve(symbols.size());
        for (const z64::SymbolData& symbol : symbols) {
            objects.push_back(z64::makeImageObject(symbol));
        }
        z64::MemoryImage image = z64::layOutImage(
            std::move(objects),
            z64::LayoutOptions{
                .findExternal = [&laidOut](const std::string_view name) -> std::optional<u32> {
                    for (const std::shared_ptr<const z64::MemoryImage>& before : laidOut) {
                        if (const std::optional<u32> address = before->findAddress(name)) {
                            return address;
                        }
                    }
                    return hackeroot::isLinkerSymbol(name) ? std::optional<u32>(0) : std::nullopt;
                }});
        laidOut.push_back(std::make_shared<const z64::MemoryImage>(std::move(image)));
    }
    return laidOut;
}

} // namespace toki::scene
