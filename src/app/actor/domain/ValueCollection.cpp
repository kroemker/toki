#include "ValueCollection.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"
#include "cparser/SourceFile.h"
#include "cparser/TypeTable.h"
#include "hackeroot/OverlayFiles.h"
#include "hackeroot/Prelude.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace toki::actor {

namespace {

constexpr std::array<SharedCollection, 8> SharedCollections{{
    {.name = "GetItemID", .header = "item.h"},
    {.name = "ItemID", .header = "item.h"},
    {.name = "GetItemDrawID", .header = "item.h"},
    {.name = "Item00Type", .header = "z_en_item00.h"},
    {.name = "ActorID", .header = {}},
    {.name = "ObjectID", .header = {}},
    {.name = "SceneID", .header = {}},
    {.name = "EntranceIndex", .header = {}},
}};

[[nodiscard]] ValueCollection makeCollection(const std::string_view name,
                                             const cparser::Enumeration& enumeration) {
    ValueCollection collection{.name = std::string(name)};
    collection.values.reserve(enumeration.constants.size());
    for (const cparser::Enumerator& constant : enumeration.constants) {
        collection.values.push_back(ChoiceValue{.value = constant.value, .label = constant.name});
    }
    return collection;
}

template <typename Entry>
[[nodiscard]] ValueCollection makeTableCollection(const std::string_view name,
                                                  const std::vector<Entry>& entries) {
    ValueCollection collection{.name = std::string(name)};
    for (const Entry& entry : entries) {
        if constexpr (requires { entry.isUnset; }) {
            if (entry.isUnset) {
                continue;
            }
        }
        collection.values.push_back(
            ChoiceValue{.value = static_cast<s64>(entry.id), .label = entry.enumName});
    }
    return collection;
}

[[nodiscard]] ValueCollection makeTableCollection(const hackeroot::Tables& tables,
                                                  const std::string_view name) {
    if (name == "ActorID") {
        return makeTableCollection(name, tables.actors);
    }
    if (name == "ObjectID") {
        return makeTableCollection(name, tables.objects);
    }
    if (name == "SceneID") {
        return makeTableCollection(name, tables.scenes);
    }
    return makeTableCollection(name, tables.entrances);
}

void listFiles(const hackeroot::Prelude& prelude, CollectionsRead& read) {
    for (const cparser::SourceFile* file : prelude.sources.getFiles()) {
        if (!std::ranges::contains(read.files, file->path)) {
            read.files.push_back(file->path);
        }
    }
}

[[nodiscard]] bool isSameFile(const std::filesystem::path& first,
                              const std::filesystem::path& second) {
    return first.lexically_normal() == second.lexically_normal();
}

} // namespace

std::vector<base::Diagnostic> checkCollections(const ActorEntry& entry,
                                               const FindCollection& findCollection) {
    std::vector<base::Diagnostic> problems;
    const auto report = [&](const base::SourceLocation& location, const std::string& message) {
        problems.push_back(base::makeWarning(location, std::format("{}.{}", entry.actor, message)));
    };
    for (const ActorParam& param : entry.params) {
        if (!param.collection || param.mask == 0) {
            continue;
        }
        const ValueCollection* collection = findCollection(*param.collection);
        if (collection == nullptr) {
            report(param.location, std::format("params.{}: the checkout has no collection {}",
                                               param.name, *param.collection));
            continue;
        }
        const u8 width = findMaskWidth(param.mask);
        const s64 lowest = (param.isSigned ? -(s64{1} << (width - 1)) : 0) + param.offset;
        const s64 highest =
            (param.isSigned ? (s64{1} << (width - 1)) - 1 : (s64{1} << width) - 1) + param.offset;
        const auto outside =
            std::ranges::count_if(collection->values, [&](const ChoiceValue& value) {
                return value.value < lowest || value.value > highest;
            });
        if (outside > 0) {
            report(param.location,
                   std::format("params.{}: {} of the {} values of {} do not fit in {} bits",
                               param.name, outside, collection->values.size(), collection->name,
                               width));
        }
        for (usize i = 0; i < entry.variants.size(); ++i) {
            for (const ParamMatch& match : entry.variants[i].match) {
                if (match.param != param.name) {
                    continue;
                }
                for (const MatchValue& value : match.values) {
                    const s64* number = std::get_if<s64>(&value);
                    if (number != nullptr &&
                        !std::ranges::contains(collection->values, *number + param.offset,
                                               &ChoiceValue::value)) {
                        report(entry.variants[i].location,
                               std::format("variants[{}].match.{}: {} is not one of {}", i,
                                           param.name, *number, collection->name));
                    }
                }
            }
        }
    }
    return problems;
}

std::span<const SharedCollection> getSharedCollections() {
    return SharedCollections;
}

CollectionsRead readSharedCollections(const hackeroot::Workspace& workspace) {
    std::vector<std::string> headers;
    for (const SharedCollection& shared : SharedCollections) {
        if (!shared.header.empty() && !std::ranges::contains(headers, shared.header)) {
            headers.emplace_back(shared.header);
        }
    }
    const hackeroot::Prelude prelude = hackeroot::readPrelude(workspace, headers);
    CollectionsRead read;
    listFiles(prelude, read);
    for (const SharedCollection& shared : SharedCollections) {
        if (shared.header.empty()) {
            read.collections.push_back(makeTableCollection(workspace.getTables(), shared.name));
        } else if (const cparser::Enumeration* enumeration =
                       prelude.types.findEnumeration(shared.name)) {
            read.collections.push_back(makeCollection(shared.name, *enumeration));
        } else {
            read.diagnostics.push_back(base::makeWarning(
                {.file = workspace.getRoot() / "include" / shared.header},
                std::format("defines no enumeration {}, so no param can choose from it",
                            shared.name)));
        }
    }
    return read;
}

CollectionsRead readActorCollections(const hackeroot::Workspace& workspace,
                                     const hackeroot::ActorEntry& actor) {
    CollectionsRead read;
    const std::expected<hackeroot::OverlayFiles, base::Diagnostic> overlay =
        hackeroot::findOverlayFiles(workspace.getRoot(), actor.name);
    if (!overlay || overlay->headers.empty()) {
        return read;
    }
    const std::filesystem::path sources = workspace.getRoot() / "src";
    std::vector<std::string> includes;
    for (const std::filesystem::path& header : overlay->headers) {
        includes.push_back(header.lexically_relative(sources).generic_string());
    }
    const hackeroot::Prelude prelude = hackeroot::readPrelude(workspace, includes);
    listFiles(prelude, read);
    std::vector<std::string_view> names = prelude.types.listEnumerationNames();
    std::ranges::sort(names);
    for (const std::string_view name : names) {
        const cparser::Enumeration* enumeration = prelude.types.findEnumeration(name);
        const bool isOwn =
            std::ranges::any_of(overlay->headers, [&](const std::filesystem::path& header) {
                return isSameFile(header, enumeration->location.file);
            });
        if (isOwn) {
            read.collections.push_back(makeCollection(name, *enumeration));
        }
    }
    return read;
}

} // namespace toki::actor
