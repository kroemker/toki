#include "Memory.h"

#include "base/Diagnostic.h"
#include "n64/Types.h"

#include <algorithm>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <span>

namespace n64 {

namespace {

constexpr u64 AddressSpaceSize = 0x0100'0000;

} // namespace

std::expected<void, base::Diagnostic> Memory::map(const u32 physicalAddress,
                                                  const std::span<const u8> bytes) {
    const u64 end = u64{physicalAddress} + bytes.size();
    if (end > AddressSpaceSize) {
        return std::unexpected(base::makeError(
            {}, std::format("a region of {} bytes at 0x{:06X} runs past the 24-bit address space",
                            bytes.size(), physicalAddress)));
    }
    const auto next = std::ranges::upper_bound(regions, physicalAddress, {}, &Region::address);
    const bool overlapsNext = next != regions.end() && next->address < end;
    const bool overlapsPrevious =
        next != regions.begin() &&
        u64{std::prev(next)->address} + std::prev(next)->bytes.size() > physicalAddress;
    if (overlapsNext || overlapsPrevious) {
        return std::unexpected(base::makeError(
            {}, std::format("a region of {} bytes at 0x{:06X} overlaps one already mapped",
                            bytes.size(), physicalAddress)));
    }
    regions.insert(next, Region{.address = physicalAddress, .bytes = bytes});
    return {};
}

std::optional<std::span<const u8>> Memory::findBytes(const u32 physicalAddress,
                                                     const usize size) const {
    const auto next = std::ranges::upper_bound(regions, physicalAddress, {}, &Region::address);
    if (next == regions.begin()) {
        return std::nullopt;
    }
    const Region& region = *std::prev(next);
    const usize offset = physicalAddress - region.address;
    if (offset + size > region.bytes.size()) {
        return std::nullopt;
    }
    return region.bytes.subspan(offset, size);
}

} // namespace n64
