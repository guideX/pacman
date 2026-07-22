#include "bitmap_loader.h"

namespace {

static const uint32_t kGximgHeaderBytes = 28u;
static const uint32_t kGximgVersion = 1u;
static const uint32_t kXrgb8888 = GX_PIXEL_FORMAT_XRGB8888;

static uint32_t read_u32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

static bool read_exact(gx_app_context* ctx, const char* path, uint64_t offset, void* destination, uint32_t bytes) {
    if (!ctx || !ctx->host || !ctx->host->file_read || !destination || bytes == 0) return false;
    uint8_t* output = static_cast<uint8_t*>(destination);
    uint32_t remaining = bytes;
    while (remaining > 0) {
        uint32_t chunk = 0;
        gx_result result = file_read(ctx, path, offset, output, remaining > 65536u ? 65536u : remaining, &chunk);
        if (result != GX_OK || chunk == 0 || chunk > remaining) return false;
        offset += chunk;
        output += chunk;
        remaining -= chunk;
    }
    return true;
}

}

bool load_gximg(gx_app_context* ctx, const char* path, uint32_t* destination, uint32_t capacityPixels, PacImage* outImage) {
    uint8_t header[kGximgHeaderBytes];
    if (!destination || !outImage || !read_exact(ctx, path, 0, header, sizeof(header))) return false;
    if (header[0] != 'G' || header[1] != 'X' || header[2] != 'I' || header[3] != 'M') return false;

    const uint32_t version = read_u32(header + 4);
    const uint32_t width = read_u32(header + 8);
    const uint32_t height = read_u32(header + 12);
    const uint32_t strideBytes = read_u32(header + 16);
    const uint32_t pixelFormat = read_u32(header + 20);
    const uint32_t payloadBytes = read_u32(header + 24);
    const uint64_t expectedStride = static_cast<uint64_t>(width) * 4u;
    const uint64_t expectedPayload = expectedStride * static_cast<uint64_t>(height);
    const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (version != kGximgVersion || pixelFormat != kXrgb8888 || width == 0 || height == 0 ||
        expectedStride > 0xFFFFFFFFull || expectedPayload > 0xFFFFFFFFull ||
        strideBytes != static_cast<uint32_t>(expectedStride) || payloadBytes != static_cast<uint32_t>(expectedPayload) ||
        pixelCount > capacityPixels) return false;

    if (!read_exact(ctx, path, kGximgHeaderBytes, destination, payloadBytes)) return false;
    outImage->width = width;
    outImage->height = height;
    outImage->strideBytes = strideBytes;
    outImage->pixels = destination;
    return true;
}
