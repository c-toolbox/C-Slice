/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sunden
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "slicewuffs.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__DISABLE_MSVC_CPU_ARCH__X86_64_FAMILY
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__BMP
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__TARGA
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ENABLE_ALLOWLIST
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_Y
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGR
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGRA_NONPREMUL
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGRA_PREMUL
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGB
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGBA_NONPREMUL
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGBA_PREMUL
#include <wuffs-unsupported-snapshot.c>

namespace {

enum class DecoderKind {
    Bmp,
    Jpeg,
    Png,
    Targa
};

struct DecoderSlot {
    void* decoder = nullptr;
};

bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& bytes, std::string* errorMessage)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        if (errorMessage) {
            *errorMessage = "Could not open image file '" + path.string() + "'";
        }
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        if (errorMessage) {
            *errorMessage = "Image file is empty: '" + path.string() + "'";
        }
        return false;
    }
    else if (size > static_cast<std::streamsize>(std::numeric_limits<int>::max())) {
        if (errorMessage) {
            *errorMessage = "Image file is too large for Wuffs STB-compatible loading: '" + path.string() + "'";
        }
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        if (errorMessage) {
            *errorMessage = "Could not read image file '" + path.string() + "'";
        }
        return false;
    }
    return true;
}

bool decoderKind(wuffs_base__slice_u8 bytes, DecoderKind& kind)
{
    const int32_t fourcc = wuffs_base__magic_number_guess_fourcc(bytes, true);
    switch (fourcc) {
        case WUFFS_BASE__FOURCC__BMP:
            kind = DecoderKind::Bmp;
            return true;
        case WUFFS_BASE__FOURCC__JPEG:
            kind = DecoderKind::Jpeg;
            return true;
        case WUFFS_BASE__FOURCC__PNG:
            kind = DecoderKind::Png;
            return true;
        case WUFFS_BASE__FOURCC__TGA:
            kind = DecoderKind::Targa;
            return true;
        default:
            return false;
    }
}

void freeDecoder(DecoderSlot& slot)
{
    std::free(slot.decoder);
    slot.decoder = nullptr;
}

template <typename Decoder, typename Alloc, typename Initialize>
wuffs_base__image_decoder* resetDecoder(DecoderSlot& slot, Alloc alloc, Initialize initialize)
{
    if (!slot.decoder) {
        slot.decoder = alloc();
        return reinterpret_cast<wuffs_base__image_decoder*>(slot.decoder);
    }

    Decoder* decoder = static_cast<Decoder*>(slot.decoder);
    const wuffs_base__status status = initialize(decoder, sizeof(Decoder), WUFFS_VERSION, WUFFS_INITIALIZE__DEFAULT_OPTIONS);
    if (status.repr) {
        freeDecoder(slot);
        slot.decoder = alloc();
    }
    return reinterpret_cast<wuffs_base__image_decoder*>(slot.decoder);
}

struct PixelFormatChoice {
    uint32_t wuffsFormat = WUFFS_BASE__PIXEL_FORMAT__RGB;
    CSlice::Wuffs::Image::PixelDataFormat imageFormat = CSlice::Wuffs::Image::PixelDataFormat::Rgb8;
};

PixelFormatChoice choosePixelFormat(wuffs_base__pixel_format sourceFormat)
{
    switch (sourceFormat.repr) {
        case WUFFS_BASE__PIXEL_FORMAT__Y:
            return { WUFFS_BASE__PIXEL_FORMAT__Y, CSlice::Wuffs::Image::PixelDataFormat::Gray8 };
        case WUFFS_BASE__PIXEL_FORMAT__BGR:
            return { WUFFS_BASE__PIXEL_FORMAT__BGR, CSlice::Wuffs::Image::PixelDataFormat::Bgr8 };
        case WUFFS_BASE__PIXEL_FORMAT__RGB:
            return { WUFFS_BASE__PIXEL_FORMAT__RGB, CSlice::Wuffs::Image::PixelDataFormat::Rgb8 };
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL:
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL:
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_BINARY:
            return { sourceFormat.repr == WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL
                    ? WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL
                    : WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL,
                CSlice::Wuffs::Image::PixelDataFormat::Bgra8 };
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL:
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL:
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_BINARY:
            return { sourceFormat.repr == WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL
                    ? WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL
                    : WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL,
                CSlice::Wuffs::Image::PixelDataFormat::Rgba8 };
        default:
            break;
    }

    const bool hasAlpha = wuffs_base__pixel_format__transparency(&sourceFormat) != WUFFS_BASE__PIXEL_ALPHA_TRANSPARENCY__OPAQUE;
    return hasAlpha
        ? PixelFormatChoice{ WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL, CSlice::Wuffs::Image::PixelDataFormat::Bgra8 }
        : PixelFormatChoice{ WUFFS_BASE__PIXEL_FORMAT__BGR, CSlice::Wuffs::Image::PixelDataFormat::Bgr8 };
}

std::string statusMessage(const char* fallback, const wuffs_base__status& status, const std::filesystem::path& path)
{
    std::string message = fallback;
    if (status.repr) {
        message += ": ";
        message += status.repr;
    }
    message += " '" + path.string() + "'";
    return message;
}

} // namespace

namespace CSlice::Wuffs {

struct DecodeContext::Impl {
    std::vector<unsigned char> encodedBytes;
    std::vector<unsigned char> workBuffer;
    DecoderSlot bmp;
    DecoderSlot jpeg;
    DecoderSlot png;
    DecoderSlot targa;

    ~Impl()
    {
        freeDecoder(bmp);
        freeDecoder(jpeg);
        freeDecoder(png);
        freeDecoder(targa);
    }

    wuffs_base__image_decoder* decoder(DecoderKind kind)
    {
        switch (kind) {
            case DecoderKind::Bmp:
                return resetDecoder<wuffs_bmp__decoder>(bmp, wuffs_bmp__decoder__alloc, wuffs_bmp__decoder__initialize);
            case DecoderKind::Jpeg:
                return resetDecoder<wuffs_jpeg__decoder>(jpeg, wuffs_jpeg__decoder__alloc, wuffs_jpeg__decoder__initialize);
            case DecoderKind::Png:
                return resetDecoder<wuffs_png__decoder>(png, wuffs_png__decoder__alloc, wuffs_png__decoder__initialize);
            case DecoderKind::Targa:
                return resetDecoder<wuffs_targa__decoder>(targa, wuffs_targa__decoder__alloc, wuffs_targa__decoder__initialize);
        }
        return nullptr;
    }
};

DecodeContext::DecodeContext()
    : m_impl(std::make_unique<Impl>())
{
}

DecodeContext::~DecodeContext() = default;

std::size_t Image::bytesPerPixel() const
{
    switch (format) {
        case PixelDataFormat::Gray8:
            return 1;
        case PixelDataFormat::Rgb8:
        case PixelDataFormat::Bgr8:
            return 3;
        case PixelDataFormat::Rgba8:
        case PixelDataFormat::Bgra8:
            return 4;
    }
    return 0;
}

std::size_t Image::byteSize() const
{
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytesPerPixel();
}

bool decodeFile(const std::filesystem::path& path, Image& image, std::string* errorMessage)
{
    DecodeContext context;
    return decodeFile(path, image, context, errorMessage);
}

bool readImageInfo(const std::filesystem::path& path, ImageInfo& info, std::string* errorMessage)
{
    DecodeContext context;
    return readImageInfo(path, info, context, errorMessage);
}

bool readImageInfo(const std::filesystem::path& path, ImageInfo& info, DecodeContext& context, std::string* errorMessage)
{
    std::vector<unsigned char>& bytes = context.m_impl->encodedBytes;
    if (!readFile(path, bytes, errorMessage)) {
        return false;
    }

    wuffs_base__slice_u8 sourceSlice = wuffs_base__make_slice_u8(bytes.data(), bytes.size());
    DecoderKind kind = DecoderKind::Bmp;
    if (!decoderKind(sourceSlice, kind)) {
        if (errorMessage) {
            *errorMessage = "Wuffs could not identify image file '" + path.string() + "'";
        }
        return false;
    }

    wuffs_base__image_decoder* decoder = context.m_impl->decoder(kind);
    if (!decoder) {
        if (errorMessage) {
            *errorMessage = "Wuffs could not allocate image decoder '" + path.string() + "'";
        }
        return false;
    }

    decoder->set_quirk(WUFFS_BASE__QUIRK_IGNORE_CHECKSUM, 1);
    wuffs_base__io_buffer source = wuffs_base__ptr_u8__reader(bytes.data(), bytes.size(), true);
    wuffs_base__image_config imageConfig = wuffs_base__null_image_config();
    const wuffs_base__status status = decoder->decode_image_config(&imageConfig, &source);
    if (status.repr || !wuffs_base__image_config__is_valid(&imageConfig)) {
        if (errorMessage) {
            *errorMessage = statusMessage("Wuffs could not read image configuration", status, path);
        }
        return false;
    }

    const uint32_t width = wuffs_base__pixel_config__width(&imageConfig.pixcfg);
    const uint32_t height = wuffs_base__pixel_config__height(&imageConfig.pixcfg);
    if (width == 0 || height == 0 || width > static_cast<uint32_t>(std::numeric_limits<int>::max()) || height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        if (errorMessage) {
            *errorMessage = "Wuffs image dimensions are invalid or too large '" + path.string() + "'";
        }
        return false;
    }

    info.width = static_cast<int>(width);
    info.height = static_cast<int>(height);
    return true;
}

bool decodeFile(const std::filesystem::path& path, Image& image, DecodeContext& context, std::string* errorMessage)
{
    std::vector<unsigned char>& bytes = context.m_impl->encodedBytes;
    if (!readFile(path, bytes, errorMessage)) {
        return false;
    }

    wuffs_base__slice_u8 sourceSlice = wuffs_base__make_slice_u8(bytes.data(), bytes.size());
    DecoderKind kind = DecoderKind::Bmp;
    if (!decoderKind(sourceSlice, kind)) {
        if (errorMessage) {
            *errorMessage = "Wuffs could not identify image file '" + path.string() + "'";
        }
        return false;
    }

    wuffs_base__image_decoder* decoder = context.m_impl->decoder(kind);
    if (!decoder) {
        if (errorMessage) {
            *errorMessage = "Wuffs could not allocate image decoder '" + path.string() + "'";
        }
        return false;
    }

    decoder->set_quirk(WUFFS_BASE__QUIRK_IGNORE_CHECKSUM, 1);
    wuffs_base__io_buffer source = wuffs_base__ptr_u8__reader(bytes.data(), bytes.size(), true);
    wuffs_base__image_config imageConfig = wuffs_base__null_image_config();
    wuffs_base__status status = decoder->decode_image_config(&imageConfig, &source);
    if (status.repr || !wuffs_base__image_config__is_valid(&imageConfig)) {
        if (errorMessage) {
            *errorMessage = statusMessage("Wuffs could not read image configuration", status, path);
        }
        return false;
    }

    const uint32_t width = wuffs_base__pixel_config__width(&imageConfig.pixcfg);
    const uint32_t height = wuffs_base__pixel_config__height(&imageConfig.pixcfg);
    if (width == 0 || height == 0 || width > static_cast<uint32_t>(std::numeric_limits<int>::max()) || height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        if (errorMessage) {
            *errorMessage = "Wuffs image dimensions are invalid or too large '" + path.string() + "'";
        }
        return false;
    }

    const PixelFormatChoice pixelFormat = choosePixelFormat(wuffs_base__pixel_config__pixel_format(&imageConfig.pixcfg));
    wuffs_base__pixel_config pixelConfig = wuffs_base__null_pixel_config();
    wuffs_base__pixel_config__set(&pixelConfig, pixelFormat.wuffsFormat, WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, width, height);
    const uint64_t pixelBufferLength = wuffs_base__pixel_config__pixbuf_len(&pixelConfig);
    const uint64_t workBufferLength = decoder->workbuf_len().max_incl;
    if (pixelBufferLength == 0 || pixelBufferLength > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        workBufferLength > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
        if (errorMessage) {
            *errorMessage = "Wuffs image buffer is too large '" + path.string() + "'";
        }
        return false;
    }

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.format = pixelFormat.imageFormat;
    image.pixels.resize(static_cast<std::size_t>(pixelBufferLength));

    context.m_impl->workBuffer.resize(static_cast<std::size_t>(workBufferLength));
    wuffs_base__pixel_buffer pixelBuffer = wuffs_base__null_pixel_buffer();
    status = wuffs_base__pixel_buffer__set_from_slice(&pixelBuffer,
        &pixelConfig,
        wuffs_base__make_slice_u8(image.pixels.data(), image.pixels.size()));
    if (status.repr) {
        if (errorMessage) {
            *errorMessage = statusMessage("Wuffs could not prepare image buffer", status, path);
        }
        return false;
    }

    status = decoder->decode_frame(&pixelBuffer,
        &source,
        WUFFS_BASE__PIXEL_BLEND__SRC,
        wuffs_base__make_slice_u8(context.m_impl->workBuffer.data(), context.m_impl->workBuffer.size()),
        nullptr);
    if (status.repr) {
        if (errorMessage) {
            *errorMessage = statusMessage("Wuffs could not decode image file", status, path);
        }
        return false;
    }

    return true;
}

bool decodeRgbFile(const std::filesystem::path& path, Image& image, std::string* errorMessage)
{
    return decodeFile(path, image, errorMessage);
}

bool decodeRgbaFile(const std::filesystem::path& path, Image& image, std::string* errorMessage)
{
    return decodeFile(path, image, errorMessage);
}

} // namespace CSlice::Wuffs
