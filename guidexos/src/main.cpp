#include <guidexos/ui.h>

#include "bitmap_loader.h"
#include "game_types.h"
#include "renderer.h"

extern "C" void* memset(void* destination, int value, uint64_t bytes) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < bytes; ++i) output[i] = static_cast<uint8_t>(value);
    return destination;
}

namespace {

static uint32_t g_levelPixels[kPacManWidth * kPacManMazeHeight];
static uint32_t g_spritePixels[256 * 352];
static uint32_t g_framePixels[kPacManWidth * kPacManFrameHeight];

static void clear_event(gx_event* event) {
    if (!event) return;
    event->size = 0;
    event->type = GX_EVENT_NONE;
    event->window = 0;
    event->param1 = 0;
    event->param2 = 0;
    event->param3 = 0;
    event->param4 = 0;
}

static gx_result present(gx_app_context* ctx, gx_handle window) {
    return gx_present_frame(ctx, window, 0, 0, kPacManWidth, kPacManFrameHeight,
        kPacManWidth * 4u, GX_PIXEL_FORMAT_XRGB8888, g_framePixels,
        kPacManWidth * kPacManFrameHeight * 4u);
}

}

extern "C" gx_result GX_CALL gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host || !ctx->host->log || !ctx->host->request_window || !ctx->host->poll_event) return GX_ERROR_INVALID_ARGUMENT;
    ctx->host->log(ctx, "Nexgen PacMan Native ELF starting");

    PacImage level{};
    PacImage sprites{};
    if (!load_gximg(ctx, "resources/level1.gximg", g_levelPixels, kPacManWidth * kPacManMazeHeight, &level) ||
        !load_gximg(ctx, "resources/pacpics.gximg", g_spritePixels, 256u * 352u, &sprites)) {
        ctx->host->log(ctx, "PacMan resource load failed");
        return GX_ERROR_FAILED;
    }
    if (!render_static_scene(&level, &sprites, g_framePixels, kPacManWidth * kPacManFrameHeight)) {
        ctx->host->log(ctx, "PacMan static scene render failed");
        return GX_ERROR_FAILED;
    }

    gx_handle window = 0;
    gx_result windowResult = GX_ERROR_UNSUPPORTED;
    if (ctx->host->request_window_ex) {
        windowResult = ctx->host->request_window_ex(ctx, "Nexgen PacMan", 480, 640, GX_WINDOW_FLAG_FIXED_SIZE, &window);
    } else {
        windowResult = ctx->host->request_window(ctx, "Nexgen PacMan", 480, 640, &window);
    }
    if (windowResult != GX_OK) {
        ctx->host->log(ctx, "PacMan window creation failed");
        return windowResult;
    }
    if (present(ctx, window) != GX_OK) {
        ctx->host->log(ctx, "PacMan frame presentation failed");
        return GX_ERROR_FAILED;
    }
    ctx->host->log(ctx, "PacMan static frame presented");

    while (1) {
        gx_event event;
        clear_event(&event);
        gx_result eventResult = ctx->host->poll_event(ctx, &event, 500);
        if (eventResult == GX_OK && event.window == window) {
            if (gx_event_is_paint(&event)) {
                present(ctx, window);
            } else if (gx_event_is_close(&event)) {
                ctx->host->log(ctx, "PacMan close event received");
                break;
            } else if (gx_event_is_escape_down(&event)) {
                ctx->host->log(ctx, "PacMan Escape pressed");
                break;
            }
        } else if (eventResult != GX_OK && eventResult != GX_ERROR_TIMEOUT) {
            ctx->host->log(ctx, "PacMan poll_event failed");
            break;
        }
    }

    if (ctx->host->exit) return ctx->host->exit(ctx, GX_OK);
    return GX_OK;
}
