#include "MaterialBindingsFile.h"

#include "app/scene/Types.h"
#include "app/scene/domain/CollisionEdits.h"
#include "app/scene/domain/CollisionMaterials.h"
#include "app/scene/domain/MaterialBindings.h"
#include "base/Diagnostic.h"

#include <toml++/toml.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace toki::scene {

namespace {

struct FlagName {
    PolygonFlag flag;
    std::string_view name;
};

constexpr std::array<FlagName, 4> FlagNames{{
    {PolygonFlag::IgnoreCamera, "ignore_camera"},
    {PolygonFlag::IgnoreEntity, "ignore_entity"},
    {PolygonFlag::IgnoreProjectiles, "ignore_projectiles"},
    {PolygonFlag::Conveyor, "conveyor"},
}};

[[nodiscard]] std::optional<PolygonFlag> findFlag(const std::string_view name) {
    for (const FlagName& flag : FlagNames) {
        if (flag.name == name) {
            return flag.flag;
        }
    }
    return std::nullopt;
}

class BindingReader {
public:
    explicit BindingReader(std::filesystem::path file) : file(std::move(file)) {}

    [[nodiscard]] ReadBindings read(const toml::table& root) {
        const std::optional<s64> format = root["format"].value<s64>();
        if (format && *format > SceneFileFormat) {
            report(root, std::format("is of format {}, newer than this build reads ({})", *format,
                                     SceneFileFormat));
            return std::move(made);
        }
        const toml::table* materials = root["materials"].as_table();
        if (materials == nullptr) {
            return std::move(made);
        }
        for (const auto& [key, node] : *materials) {
            const toml::table* table = node.as_table();
            if (table == nullptr) {
                report(node, std::format("material '{}' is not a table", key.str()));
                continue;
            }
            if (const std::optional<MaterialBinding> binding = readBinding(key.str(), *table)) {
                made.bindings.materials.emplace(std::string(key.str()), *binding);
            }
        }
        return std::move(made);
    }

private:
    [[nodiscard]] std::optional<MaterialBinding> readBinding(const std::string_view key,
                                                             const toml::table& table) {
        const std::optional<bool> isSolid = table["solid"].value<bool>();
        if (!isSolid) {
            report(table, std::format("material '{}' says not whether it is solid", key));
            return std::nullopt;
        }
        if (!*isSolid) {
            return MaterialBinding{};
        }
        MaterialBinding binding{.isSolid = true};
        const toml::array* surface = table["surface"].as_array();
        if (surface == nullptr || surface->size() != binding.surface.data.size()) {
            report(table, std::format("material '{}' has no surface type of two words", key));
            return std::nullopt;
        }
        for (usize word = 0; word < binding.surface.data.size(); ++word) {
            const std::optional<s64> value = (*surface)[word].value<s64>();
            if (!value || *value < 0 || *value > s64{0xFFFFFFFF}) {
                report(table,
                       std::format("material '{}' has a surface type word that is not one", key));
                return std::nullopt;
            }
            binding.surface.data[word] = static_cast<u32>(*value);
        }
        CollisionMaterial flagged;
        if (const toml::array* flags = table["flags"].as_array()) {
            for (const toml::node& flag : *flags) {
                const std::optional<std::string_view> name = flag.value<std::string_view>();
                const std::optional<PolygonFlag> found = name ? findFlag(*name) : std::nullopt;
                if (!found) {
                    report(flag, std::format("material '{}' has a flag no solid face has", key));
                    continue;
                }
                flagged = setMaterialFlag(flagged, *found, true);
            }
        }
        binding.flags = flagged.flags;
        return binding;
    }

    void report(const toml::node& node, std::string message) {
        made.diagnostics.push_back(
            base::makeWarning({.file = file, .line = static_cast<u32>(node.source().begin.line)},
                              std::move(message)));
    }

    std::filesystem::path file;
    ReadBindings made;
};

[[nodiscard]] toml::value<s64> makeHex(const u32 value) {
    toml::value<s64> hex(s64{value});
    hex.flags(toml::value_flags::format_as_hexadecimal);
    return hex;
}

} // namespace

std::filesystem::path makeSceneFilePath(const std::filesystem::path& root,
                                        const std::string_view scene) {
    return root / ".toki" / "scenes" / std::format("{}.toml", scene);
}

ReadBindings parseMaterialBindings(const std::string_view text, const std::filesystem::path& file) {
    toml::table root;
    try {
        root = toml::parse(text, file.string());
    } catch (const toml::parse_error& error) {
        ReadBindings failed;
        failed.diagnostics.push_back(
            base::makeError({.file = file, .line = static_cast<u32>(error.source().begin.line)},
                            std::format("cannot be read as TOML: {}", error.description())));
        return failed;
    }
    return BindingReader(file).read(root);
}

ReadBindings readMaterialBindings(const std::filesystem::path& file) {
    std::error_code status;
    if (!std::filesystem::is_regular_file(file, status)) {
        return {};
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        ReadBindings failed;
        failed.diagnostics.push_back(base::makeError({.file = file}, "cannot be opened"));
        return failed;
    }
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    return parseMaterialBindings(text, file);
}

std::string formatMaterialBindings(const MaterialBindings& bindings) {
    toml::table root;
    root.insert("format", SceneFileFormat);
    toml::table materials;
    for (const auto& [key, binding] : bindings.materials) {
        toml::table table;
        table.insert("solid", binding.isSolid);
        if (binding.isSolid) {
            toml::array surface;
            for (const u32 word : binding.surface.data) {
                surface.push_back(makeHex(word));
            }
            table.insert("surface", std::move(surface));
            toml::array flags;
            for (const FlagName& flag : FlagNames) {
                if (hasMaterialFlag(CollisionMaterial{.flags = binding.flags}, flag.flag)) {
                    flags.push_back(std::string(flag.name));
                }
            }
            table.insert("flags", std::move(flags));
        }
        materials.insert(key, std::move(table));
    }
    root.insert("materials", std::move(materials));
    std::ostringstream text;
    text << toml::toml_formatter(root, toml::toml_formatter::default_flags &
                                           ~toml::format_flags::indentation)
         << '\n';
    return text.str();
}

} // namespace toki::scene
