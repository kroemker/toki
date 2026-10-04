#include "PngImage.h"

#include "n64/Color.h"
#include "z64/Types.h"

#include <spng.h>

#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace z64 {

namespace {

struct ContextDeleter {
    void operator()(spng_ctx* context) const {
        spng_ctx_free(context);
    }
};

using Context = std::unique_ptr<spng_ctx, ContextDeleter>;

std::unexpected<std::string> makeError(const int code) {
    return std::unexpected(std::string(spng_strerror(code)));
}

std::expected<Context, std::string> openPng(const std::span<const u8> bytes) {
    Context context(spng_ctx_new(0));
    if (!context) {
        return std::unexpected(std::string("could not start the PNG decoder"));
    }
    if (const int code = spng_set_png_buffer(context.get(), bytes.data(), bytes.size());
        code != 0) {
        return makeError(code);
    }
    return context;
}

std::expected<std::vector<n64::Color>, std::string> readPalette(spng_ctx* context) {
    spng_plte plte{};
    if (const int code = spng_get_plte(context, &plte); code != 0) {
        return makeError(code);
    }
    spng_trns trns{};
    const bool hasTransparency = spng_get_trns(context, &trns) == 0;
    std::vector<n64::Color> palette;
    palette.reserve(plte.n_entries);
    for (u32 i = 0; i < plte.n_entries; ++i) {
        const u8 alpha = hasTransparency && i < trns.n_type3_entries ? trns.type3_alpha[i]
                                                                     : static_cast<u8>(0xFF);
        palette.push_back(
            {plte.entries[i].red, plte.entries[i].green, plte.entries[i].blue, alpha});
    }
    return palette;
}

std::vector<u8> unpackIndices(const std::vector<u8>& raw, const u32 width, const u32 height,
                              const u32 bitDepth) {
    if (bitDepth == 8) {
        return raw;
    }
    std::vector<u8> indices;
    indices.reserve(static_cast<usize>(width) * height);
    const usize stride = ((static_cast<usize>(width) * bitDepth) + 7) / 8;
    const u32 mask = (1U << bitDepth) - 1;
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const usize bit = static_cast<usize>(x) * bitDepth;
            const u32 byte = raw[(y * stride) + (bit / 8)];
            const u32 shift = 8 - bitDepth - static_cast<u32>(bit % 8);
            indices.push_back(static_cast<u8>((byte >> shift) & mask));
        }
    }
    return indices;
}

} // namespace

std::expected<PngImage, std::string> decodePng(const std::span<const u8> bytes) {
    std::expected<Context, std::string> context = openPng(bytes);
    if (!context) {
        return std::unexpected(context.error());
    }
    spng_ihdr header{};
    if (const int code = spng_get_ihdr(context->get(), &header); code != 0) {
        return makeError(code);
    }
    PngImage image{.width = header.width, .height = header.height};
    const usize texelCount = static_cast<usize>(header.width) * header.height;

    if (header.color_type == SPNG_COLOR_TYPE_INDEXED) {
        std::expected<std::vector<n64::Color>, std::string> palette = readPalette(context->get());
        if (!palette) {
            return std::unexpected(palette.error());
        }
        usize size = 0;
        if (const int code = spng_decoded_image_size(context->get(), SPNG_FMT_RAW, &size);
            code != 0) {
            return makeError(code);
        }
        std::vector<u8> raw(size);
        if (const int code =
                spng_decode_image(context->get(), raw.data(), raw.size(), SPNG_FMT_RAW, 0);
            code != 0) {
            return makeError(code);
        }
        image.palette = std::move(*palette);
        image.indices = unpackIndices(raw, header.width, header.height, header.bit_depth);
        image.texels.reserve(texelCount);
        for (const u8 index : image.indices) {
            image.texels.push_back(index < image.palette.size() ? image.palette[index]
                                                                : n64::Color{0, 0, 0, 0});
        }
        return image;
    }

    std::vector<u8> rgba(texelCount * 4);
    if (const int code =
            spng_decode_image(context->get(), rgba.data(), rgba.size(), SPNG_FMT_RGBA8, 0);
        code != 0) {
        return makeError(code);
    }
    image.texels.reserve(texelCount);
    for (usize i = 0; i < texelCount; ++i) {
        image.texels.push_back(
            {rgba[i * 4], rgba[(i * 4) + 1], rgba[(i * 4) + 2], rgba[(i * 4) + 3]});
    }
    return image;
}

std::expected<std::vector<n64::Color>, std::string>
decodePngPalette(const std::span<const u8> bytes) {
    std::expected<Context, std::string> context = openPng(bytes);
    if (!context) {
        return std::unexpected(context.error());
    }
    spng_ihdr header{};
    if (const int code = spng_get_ihdr(context->get(), &header); code != 0) {
        return makeError(code);
    }
    if (header.color_type != SPNG_COLOR_TYPE_INDEXED) {
        return std::vector<n64::Color>{};
    }
    return readPalette(context->get());
}

} // namespace z64
