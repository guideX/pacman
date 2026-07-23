#include <guidexos/ui.h>

#include "bitmap_loader.h"
#include "game.h"
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
static uint32_t g_backgroundPixels[kPacManWidth * kPacManFrameHeight];
static uint32_t g_framePixels[kPacManWidth * kPacManFrameHeight];

static const uint64_t kFixedStepMs = 10u;
static const uint64_t kMaxElapsedMs = 250u;
static const uint32_t kMaxCatchUpSteps = 8u;
static const uint64_t kVisualIntervalMs = 16u;
static const int kPacManRestartKeyEnter = 13;
static const int kPacManRestartKeySpace = 32;

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

static bool render_and_present(gx_app_context* ctx, gx_handle window, const PacImage& sprites,
                              const GameState& game) {
    if (!render_game_scene(&sprites, &game, g_backgroundPixels, g_framePixels,
                           kPacManWidth * kPacManFrameHeight)) return false;
    return present(ctx, window) == GX_OK;
}

static void log_direction_request(gx_app_context* ctx, Direction direction) {
    if (!ctx || !ctx->host || !ctx->host->log) return;
    char message[64];
    const char* name = game_direction_name(direction);
    uint32_t index = 0;
    const char* prefix = "PacMan requested direction: ";
    while (prefix[index] && index + 1u < sizeof(message)) {
        message[index] = prefix[index];
        ++index;
    }
    for (uint32_t i = 0; name[i] && index + 1u < sizeof(message); ++i) message[index++] = name[i];
    message[index] = '\0';
    ctx->host->log(ctx, message);
}

static void log_game_value(gx_app_context* ctx, const char* prefix, uint32_t value) {
#if PACMAN_ENABLE_DIAGNOSTICS
    if (!ctx || !ctx->host || !ctx->host->log || !prefix) return;
    char message[96];
    uint32_t index = 0;
    while (prefix[index] && index + 1u < sizeof(message)) message[index++] = prefix[index];
    char digits[10];
    uint32_t length = 0;
    do {
        digits[length++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && length < sizeof(digits));
    while (length > 0 && index + 1u < sizeof(message)) message[index++] = digits[--length];
    message[index] = '\0';
    ctx->host->log(ctx, message);
#else
    (void)ctx;
    (void)prefix;
    (void)value;
#endif
}

static void log_game_events(gx_app_context* ctx, const GameState& game) {
#if PACMAN_ENABLE_DIAGNOSTICS
    if (!ctx || !ctx->host || !ctx->host->log) return;
    if (game.normalPillConsumed) ctx->host->log(ctx, "PacMan normal pill consumed");
    if (game.powerPillConsumed) ctx->host->log(ctx, "PacMan power pill consumed");
    if (game.scoreChanged) log_game_value(ctx, "PacMan score updated: ", game.score);
    if (game.normalPillConsumed || game.powerPillConsumed) {
        log_game_value(ctx, "PacMan remaining consumables: ", game.level.totalConsumablesRemaining);
    }
    if (game.countUnderflow) ctx->host->log(ctx, "PacMan consumable count underflow prevented");
    if (game.levelCompleteEntered) ctx->host->log(ctx, "PacMan level complete");
    if (game.levelReset) {
        log_game_value(ctx, "PacMan level reset: ", game.levelNumber);
        log_game_value(ctx, "PacMan remaining consumables: ", game.level.totalConsumablesRemaining);
    }
    if (game.collisionDetected) ctx->host->log(ctx, "PacMan ghost collision detected");
    if (game.deathEntered) ctx->host->log(ctx, "PacMan death state entered");
    if (game.lifeDecremented) log_game_value(ctx, "PacMan life decremented; lives remaining: ", game.lives);
    if (game.actorReset) ctx->host->log(ctx, "PacMan actors reset after death");
    if (game.gameOverEntered) ctx->host->log(ctx, "PacMan Game Over entered");
#else
    (void)ctx;
    (void)game;
#endif
}

#if PACMAN_HOSTED_DANGER_TEST
static void apply_hosted_danger_test_placement(GameState* game) {
    if (!game || game->playState != PlayState::Playing || game->simulationSteps < 100u) return;
    game->ghosts[0].x = game->pacman.x;
    game->ghosts[0].y = game->pacman.y;
    game->ghosts[0].active = true;
    game->visualDirty = true;
}
#endif

}

extern "C" gx_result GX_CALL gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host || !ctx->host->log || !ctx->host->request_window || !ctx->host->poll_event ||
        !ctx->host->get_ticks_ms) return GX_ERROR_INVALID_ARGUMENT;
    ctx->host->log(ctx, "Nexgen PacMan Native ELF starting");

    PacImage level{};
    PacImage sprites{};
    if (!load_gximg(ctx, "resources/level1.gximg", g_levelPixels, kPacManWidth * kPacManMazeHeight, &level) ||
        !load_gximg(ctx, "resources/pacpics.gximg", g_spritePixels, 256u * 352u, &sprites)) {
        ctx->host->log(ctx, "PacMan resource load failed");
        return GX_ERROR_FAILED;
    }
    if (!build_background_frame(&level, g_backgroundPixels, kPacManWidth * kPacManFrameHeight)) {
        ctx->host->log(ctx, "PacMan background render failed");
        return GX_ERROR_FAILED;
    }

    GameState game;
    game_initialize(&game);
#if PACMAN_ENABLE_DIAGNOSTICS
    ctx->host->log(ctx, "PacMan ghosts initialized: 4 stationary entities");
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        const GhostState& ghost = game.ghosts[index];
        ctx->host->log(ctx, ghost_kind_name(ghost.kind));
    }
#endif
#if PACMAN_HOSTED_DANGER_TEST
    ctx->host->log(ctx, "PacMan hosted danger test placement enabled");
#endif

    gx_handle window = 0;
    gx_result windowResult = GX_ERROR_UNSUPPORTED;
    if (ctx->host->request_window_ex) {
        windowResult = ctx->host->request_window_ex(ctx, "Nexgen PacMan", 480, 640,
            GX_WINDOW_FLAG_FIXED_SIZE | GX_WINDOW_FLAG_CENTERED, &window);
    } else {
        windowResult = ctx->host->request_window(ctx, "Nexgen PacMan", 480, 640, &window);
    }
    if (windowResult != GX_OK) {
        ctx->host->log(ctx, "PacMan window creation failed");
        return windowResult;
    }
    if (!render_and_present(ctx, window, sprites, game)) {
        ctx->host->log(ctx, "PacMan frame presentation failed");
        return GX_ERROR_FAILED;
    }
    game.visualDirty = false;
    ctx->host->log(ctx, "PacMan interactive frame presented");

    uint64_t previousTicks = gx_get_ticks_ms(ctx);
    uint64_t lastPresentedTicks = previousTicks;
    uint64_t accumulatorMs = 0;
    bool running = true;
    bool simulationStartedLogged = false;

    while (running) {
        gx_event event;
        clear_event(&event);
        gx_result eventResult = ctx->host->poll_event(ctx, &event, 10);
        if (eventResult == GX_OK && event.window == window) {
            if (gx_event_is_paint(&event)) {
                if (!render_and_present(ctx, window, sprites, game)) running = false;
                game.visualDirty = false;
                lastPresentedTicks = gx_get_ticks_ms(ctx);
            } else if (gx_event_is_close(&event)) {
                ctx->host->log(ctx, "PacMan close event received");
                running = false;
            } else if (gx_event_is_escape_down(&event)) {
                ctx->host->log(ctx, "PacMan Escape pressed");
                running = false;
            } else if (event.type == GX_EVENT_WINDOW_BLUR) {
                game_focus_lost(&game);
                ctx->host->log(ctx, "PacMan focus lost; directional state cleared");
            } else if (event.type == GX_EVENT_WINDOW_FOCUS) {
                game_focus_gained(&game);
                ctx->host->log(ctx, "PacMan focus gained; waiting for new direction");
            } else if (event.type == GX_EVENT_KEY) {
                if (event.param2 == GX_KEY_ACTION_DOWN && game.playState == PlayState::GameOver &&
                    (event.param1 == kPacManRestartKeyEnter || event.param1 == kPacManRestartKeySpace)) {
                    if (game_restart_session(&game)) {
#if PACMAN_ENABLE_DIAGNOSTICS
                        ctx->host->log(ctx, "PacMan session restarted");
#endif
                    }
                    continue;
                }
                const Direction direction = game_direction_for_key(event.param1);
                if (direction != Direction::None) {
                    if (event.param2 == GX_KEY_ACTION_DOWN) {
                        game_press_direction(&game, direction);
                        log_direction_request(ctx, direction);
                    } else if (event.param2 == GX_KEY_ACTION_UP) {
                        game_release_direction(&game, direction);
                    }
                }
            }
        } else if (eventResult != GX_OK && eventResult != GX_ERROR_TIMEOUT) {
            ctx->host->log(ctx, "PacMan poll_event failed");
            running = false;
        }

        if (!running) break;

        const uint64_t currentTicks = gx_get_ticks_ms(ctx);
        uint64_t elapsedMs = currentTicks - previousTicks;
        previousTicks = currentTicks;
        if (elapsedMs > kMaxElapsedMs) {
            elapsedMs = kMaxElapsedMs;
            ctx->host->log(ctx, "PacMan timing interval clamped");
        }
        accumulatorMs += elapsedMs;
        if (accumulatorMs > kMaxElapsedMs) accumulatorMs = kMaxElapsedMs;

        uint32_t updates = 0;
        while (accumulatorMs >= kFixedStepMs && updates < kMaxCatchUpSteps) {
#if PACMAN_HOSTED_DANGER_TEST
            apply_hosted_danger_test_placement(&game);
#endif
            game_update(&game);
            accumulatorMs -= kFixedStepMs;
            ++updates;
            if (!simulationStartedLogged) {
                ctx->host->log(ctx, "PacMan fixed-step simulation started");
                simulationStartedLogged = true;
            }
            if (game.turnAccepted) ctx->host->log(ctx, "PacMan buffered turn accepted");
            if (game.becameBlocked) ctx->host->log(ctx, "PacMan direction blocked by maze wall");
            if (game.tunnelWrapped) ctx->host->log(ctx, "PacMan tunnel wrap");
            log_game_events(ctx, game);
        }
        if (updates == kMaxCatchUpSteps && accumulatorMs >= kFixedStepMs) {
            accumulatorMs = 0;
            ctx->host->log(ctx, "PacMan simulation catch-up clamped");
        }

        if (game.visualDirty && currentTicks - lastPresentedTicks >= kVisualIntervalMs) {
            if (!render_and_present(ctx, window, sprites, game)) {
                ctx->host->log(ctx, "PacMan frame presentation failed");
                running = false;
            } else {
                game.visualDirty = false;
                lastPresentedTicks = currentTicks;
            }
        }
    }

    if (ctx->host->exit) return ctx->host->exit(ctx, GX_OK);
    return GX_OK;
}
