#include <guidexos/ui.h>

#include "bitmap_loader.h"
#include "game.h"
#include "game_types.h"
#include "renderer.h"

#ifndef PACMAN_ENABLE_DIAGNOSTICS
#define PACMAN_ENABLE_DIAGNOSTICS 0
#endif

#ifndef PACMAN_HOSTED_DANGER_TEST
#define PACMAN_HOSTED_DANGER_TEST 0
#endif

#ifndef PACMAN_HOSTED_RED_MOVEMENT_TEST
#define PACMAN_HOSTED_RED_MOVEMENT_TEST 0
#endif

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
static uint64_t g_frameSequence = 0;

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

#if PACMAN_HOSTED_DANGER_TEST
static void append_frame_text(char* message, uint32_t* index, uint32_t capacity, const char* text) {
    if (!message || !index || !text) return;
    for (uint32_t i = 0; text[i] && *index + 1u < capacity; ++i) message[(*index)++] = text[i];
}

static void append_frame_number(char* message, uint32_t* index, uint32_t capacity, uint64_t value) {
    char digits[20];
    uint32_t length = 0;
    do {
        digits[length++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && length < sizeof(digits));
    while (length > 0 && *index + 1u < capacity) message[(*index)++] = digits[--length];
}

static const char* validation_state_name(PlayState state) {
    switch (state) {
    case PlayState::Playing: return "Playing";
    case PlayState::Dying: return "Dying";
    case PlayState::ReadyAfterDeath: return "Ready";
    case PlayState::LevelComplete: return "LevelComplete";
    case PlayState::GameOver: return "GameOver";
    }
    return "Unknown";
}

static void log_validation_frame(gx_app_context* ctx, gx_handle window, const GameState& game,
                                 uint64_t frameSequence, gx_result result) {
    if (!ctx || !ctx->host || !ctx->host->log) return;
    char message[320];
    uint32_t index = 0;
    append_frame_text(message, &index, sizeof(message), "PacMan frame seq=");
    append_frame_number(message, &index, sizeof(message), frameSequence);
    append_frame_text(message, &index, sizeof(message), " window=");
    append_frame_number(message, &index, sizeof(message), window);
    append_frame_text(message, &index, sizeof(message), " state=");
    append_frame_text(message, &index, sizeof(message), validation_state_name(game.playState));
    append_frame_text(message, &index, sizeof(message), " step=");
    append_frame_number(message, &index, sizeof(message), game.simulationSteps);
    append_frame_text(message, &index, sizeof(message), " score=");
    append_frame_number(message, &index, sizeof(message), game.score);
    append_frame_text(message, &index, sizeof(message), " lives=");
    append_frame_number(message, &index, sizeof(message), game.lives);
    append_frame_text(message, &index, sizeof(message), " level=");
    append_frame_number(message, &index, sizeof(message), game.levelNumber);
    append_frame_text(message, &index, sizeof(message), " pacman=");
    append_frame_number(message, &index, sizeof(message), static_cast<uint64_t>(game.pacman.x));
    append_frame_text(message, &index, sizeof(message), ",");
    append_frame_number(message, &index, sizeof(message), static_cast<uint64_t>(game.pacman.y));
    append_frame_text(message, &index, sizeof(message), " size=448x553 stride=1792 bytes=990976 result=");
    append_frame_number(message, &index, sizeof(message), static_cast<uint64_t>(static_cast<uint32_t>(result)));
    append_frame_text(message, &index, sizeof(message), " red=");
    append_frame_number(message, &index, sizeof(message), static_cast<uint64_t>(game.ghosts[0].x));
    append_frame_text(message, &index, sizeof(message), ",");
    append_frame_number(message, &index, sizeof(message), static_cast<uint64_t>(game.ghosts[0].y));
    append_frame_text(message, &index, sizeof(message), " dir=");
    append_frame_text(message, &index, sizeof(message), game_direction_name(game.ghosts[0].direction));
    append_frame_text(message, &index, sizeof(message), " target=");
    append_frame_number(message, &index, sizeof(message), static_cast<uint64_t>(game.ghosts[0].targetX));
    append_frame_text(message, &index, sizeof(message), ",");
    append_frame_number(message, &index, sizeof(message), static_cast<uint64_t>(game.ghosts[0].targetY));
    append_frame_text(message, &index, sizeof(message), " anim=");
    append_frame_number(message, &index, sizeof(message), game.ghosts[0].animationFrame);
    message[index] = '\0';
    ctx->host->log(ctx, message);
}
#endif

static bool render_and_present(gx_app_context* ctx, gx_handle window, const PacImage& sprites,
                              const GameState& game) {
    const uint64_t frameSequence = ++g_frameSequence;
    if (!render_game_scene(&sprites, &game, g_backgroundPixels, g_framePixels,
                           kPacManWidth * kPacManFrameHeight, frameSequence)) return false;
    const gx_result result = present(ctx, window);
#if PACMAN_HOSTED_DANGER_TEST
    log_validation_frame(ctx, window, game, frameSequence, result);
#endif
    return result == GX_OK;
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
    char message[192];
    uint32_t index = 0;
    const char* prefix = "PacMan Red state x=";
    while (prefix[index] && index + 1u < sizeof(message)) message[index++] = prefix[index];
    auto append_number = [&message, &index](uint32_t value) {
        char digits[10];
        uint32_t length = 0;
        do {
            digits[length++] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        } while (value != 0 && length < sizeof(digits));
        while (length > 0 && index + 1u < sizeof(message)) message[index++] = digits[--length];
    };
    auto append_text = [&message, &index](const char* text) {
        if (!text) return;
        for (uint32_t i = 0; text[i] && index + 1u < sizeof(message); ++i) message[index++] = text[i];
    };
    append_number(static_cast<uint32_t>(game.ghosts[0].x));
    append_text(" y=");
    append_number(static_cast<uint32_t>(game.ghosts[0].y));
    append_text(" dir=");
    append_text(game_direction_name(game.ghosts[0].direction));
    append_text(" target=");
    append_number(static_cast<uint32_t>(game.ghosts[0].targetX));
    append_text(",");
    append_number(static_cast<uint32_t>(game.ghosts[0].targetY));
    append_text(" anim=");
    append_number(game.ghosts[0].animationFrame);
    message[index] = '\0';
    ctx->host->log(ctx, message);
#else
    (void)ctx;
    (void)game;
#endif
}

#if PACMAN_HOSTED_DANGER_TEST && !PACMAN_HOSTED_RED_MOVEMENT_TEST
static bool apply_hosted_danger_test_placement(GameState* game, uint32_t* placementCount) {
    if (!game || !placementCount || game->playState != PlayState::Playing || *placementCount >= 3u) return false;
    const uint32_t stepsBetweenPlacements = kPacManDeathDurationSteps +
        kPacManReadyAfterDeathSteps + 30u;
    const uint32_t nextPlacementStep = 100u + *placementCount * stepsBetweenPlacements;
    if (game->simulationSteps < nextPlacementStep) return false;
    game->ghosts[0].x = game->pacman.x;
    game->ghosts[0].y = game->pacman.y;
    game->ghosts[0].offset = 0;
    game->ghosts[0].speed = 0;
    game->ghosts[0].active = true;
    game->visualDirty = true;
    ++*placementCount;
    return true;
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
    ctx->host->log(ctx, "PacMan ghosts initialized: Red moving; 3 stationary entities");
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        const GhostState& ghost = game.ghosts[index];
        ctx->host->log(ctx, ghost_kind_name(ghost.kind));
    }
#endif
#if PACMAN_HOSTED_DANGER_TEST
    ctx->host->log(ctx, "PacMan hosted danger test placement enabled");
#endif
#if PACMAN_HOSTED_RED_MOVEMENT_TEST
    game.pacman.x = 64;
    game.pacman.y = 264;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 0;
    game.pacman.speed = 0;
    game.ghosts[0].targetX = game.pacman.x;
    game.ghosts[0].targetY = game.pacman.y;
    game.suppressGhostCollisionsForValidation = true;
    game.visualDirty = true;
    ctx->host->log(ctx, "PacMan hosted Red movement validation enabled");
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
#if PACMAN_HOSTED_DANGER_TEST && !PACMAN_HOSTED_RED_MOVEMENT_TEST
    uint32_t hostedDangerPlacementCount = 0;
#endif

    while (running) {
        gx_event event;
        clear_event(&event);
        gx_result eventResult = ctx->host->poll_event(ctx, &event, 10);
        if (eventResult == GX_OK && event.window == window) {
            if (gx_event_is_paint(&event)) {
                if (game.visualDirty) {
                    if (!render_and_present(ctx, window, sprites, game)) running = false;
                    game.visualDirty = false;
                    lastPresentedTicks = gx_get_ticks_ms(ctx);
                }
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
#if PACMAN_HOSTED_DANGER_TEST && !PACMAN_HOSTED_RED_MOVEMENT_TEST
            if (apply_hosted_danger_test_placement(&game, &hostedDangerPlacementCount)) {
                log_game_value(ctx, "PacMan hosted danger overlap placed: ", hostedDangerPlacementCount);
            }
#endif
#if PACMAN_HOSTED_RED_MOVEMENT_TEST
            if (game.playState == PlayState::Playing && game.suppressGhostCollisionsForValidation) {
                game.pacman.x = 64;
                game.pacman.y = 264;
                game.pacman.direction = Direction::None;
                game.pacman.facingDirection = Direction::None;
                game.pacman.requestedDirection = Direction::None;
                game.pacman.offset = 0;
                game.pacman.speed = 0;
                game.ghosts[0].targetX = game.pacman.x;
                game.ghosts[0].targetY = game.pacman.y;
            }
            if (game.suppressGhostCollisionsForValidation && game.simulationSteps >= 140u &&
                game.ghosts[0].y == 232 &&
                (game.ghosts[0].x <= 32 || game.ghosts[0].x >= 416)) {
                int collisionTargetX = game.ghosts[0].x;
                int collisionTargetY = game.ghosts[0].y;
                if (game.ghosts[0].direction == Direction::Left) collisionTargetX -= 32;
                if (game.ghosts[0].direction == Direction::Right) collisionTargetX += 32;
                if (game.ghosts[0].direction == Direction::Up) collisionTargetY -= 32;
                if (game.ghosts[0].direction == Direction::Down) collisionTargetY += 32;
                if (collisionTargetX < 16) collisionTargetX += 416;
                if (collisionTargetX > 431) collisionTargetX -= 416;
                game.pacman.x = collisionTargetX;
                game.pacman.y = collisionTargetY;
                game.pacman.direction = Direction::None;
                game.pacman.facingDirection = Direction::None;
                game.pacman.requestedDirection = Direction::None;
                game.pacman.offset = 0;
                game.pacman.speed = 0;
                game.ghosts[0].targetX = collisionTargetX;
                game.ghosts[0].targetY = collisionTargetY;
                game.suppressGhostCollisionsForValidation = false;
                ctx->host->log(ctx, "PacMan hosted Red movement collision window enabled");
            }
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
            if (game.redTunnelWrapped) ctx->host->log(ctx, "PacMan Red ghost tunnel wrap");
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
