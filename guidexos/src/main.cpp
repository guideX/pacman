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

#ifndef PACMAN_HOSTED_PINK_MOVEMENT_TEST
#define PACMAN_HOSTED_PINK_MOVEMENT_TEST 0
#endif

#ifndef PACMAN_HOSTED_CYAN_MOVEMENT_TEST
#define PACMAN_HOSTED_CYAN_MOVEMENT_TEST 0
#endif

#ifndef PACMAN_HOSTED_ORANGE_MOVEMENT_TEST
#define PACMAN_HOSTED_ORANGE_MOVEMENT_TEST 0
#endif

#ifndef PACMAN_HOSTED_POWER_PILL_TEST
#define PACMAN_HOSTED_POWER_PILL_TEST 0
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

#if PACMAN_ENABLE_DIAGNOSTICS
static uint32_t g_pacbmTickMarkers = 0;
static uint32_t g_pacbmPollMarkers = 0;
static uint32_t g_pacbmUpdateMarkers = 0;
static uint32_t g_pacbmFrameMarkers = 0;

static void pacbm_marker(gx_app_context* ctx, const char* marker) {
    if (!ctx || !ctx->host || !ctx->host->log || !marker) return;
    // Pass the read-only literal directly through the ABI.  Keeping the
    // breadcrumb out of a temporary application-stack buffer makes the
    // diagnostic path independent of the Native ELF stack boundary it is
    // measuring.
    ctx->host->log(ctx, marker);
}

static uint64_t pacbm_get_ticks_ms(gx_app_context* ctx) {
    const bool trace = g_pacbmTickMarkers < 10u;
    if (trace) pacbm_marker(ctx, "PACBM 12 GET_TICKS_BEGIN");
    const uint64_t value = gx_get_ticks_ms(ctx);
    if (trace) {
        pacbm_marker(ctx, "PACBM 13 GET_TICKS_END");
        ++g_pacbmTickMarkers;
    }
    return value;
}

static gx_result pacbm_poll_event(gx_app_context* ctx, gx_event* event, int timeoutMs) {
    const bool trace = g_pacbmPollMarkers < 5u;
    if (trace) pacbm_marker(ctx, "PACBM 14 POLL_EVENT_BEGIN");
    const gx_result result = ctx->host->poll_event(ctx, event, timeoutMs);
    if (trace) {
        pacbm_marker(ctx, "PACBM 15 POLL_EVENT_END");
        ++g_pacbmPollMarkers;
    }
    return result;
}
#else
static void pacbm_marker(gx_app_context*, const char*) { }
static uint64_t pacbm_get_ticks_ms(gx_app_context* ctx) { return gx_get_ticks_ms(ctx); }
static gx_result pacbm_poll_event(gx_app_context* ctx, gx_event* event, int timeoutMs) {
    return ctx->host->poll_event(ctx, event, timeoutMs);
}
#endif

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

static void append_frame_signed_number(char* message, uint32_t* index, uint32_t capacity,
                                       int64_t value) {
    if (value < 0) {
        append_frame_text(message, index, capacity, "-");
        value = -(value + 1) + 1;
    }
    append_frame_number(message, index, capacity, static_cast<uint64_t>(value));
}

static void append_validation_ghost(char* message, uint32_t* index, uint32_t capacity,
                                    const char* label, const GhostState& ghost) {
    append_frame_text(message, index, capacity, label);
    append_frame_signed_number(message, index, capacity, ghost.x);
    append_frame_text(message, index, capacity, ",");
    append_frame_signed_number(message, index, capacity, ghost.y);
    append_frame_text(message, index, capacity, " dir=");
    append_frame_text(message, index, capacity, game_direction_name(ghost.direction));
    append_frame_text(message, index, capacity, " target=");
    append_frame_signed_number(message, index, capacity, ghost.targetX);
    append_frame_text(message, index, capacity, ",");
    append_frame_signed_number(message, index, capacity, ghost.targetY);
    append_frame_text(message, index, capacity, " release=");
    append_frame_text(message, index, capacity, ghost_release_state_name(ghost.releaseState));
    append_frame_text(message, index, capacity, " condition=");
    append_frame_text(message, index, capacity, ghost_condition_name(ghost.condition));
    append_frame_text(message, index, capacity, " pp=");
    append_frame_number(message, index, capacity, ghost.powerPillStepsRemaining);
    append_frame_text(message, index, capacity, " anim=");
    append_frame_number(message, index, capacity, ghost.animationFrame);
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
    char message[1024];
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
    append_frame_text(message, &index, sizeof(message), " chain=");
    append_frame_number(message, &index, sizeof(message), game.ghostEatChain);
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
    append_validation_ghost(message, &index, sizeof(message), " red=", game.ghosts[0]);
    append_validation_ghost(message, &index, sizeof(message), " pink=", game.ghosts[1]);
    append_validation_ghost(message, &index, sizeof(message), " cyan=", game.ghosts[2]);
    append_validation_ghost(message, &index, sizeof(message), " orange=", game.ghosts[3]);
    append_frame_text(message, &index, sizeof(message), " orangeDistanceTiles=");
    append_frame_number(message, &index, sizeof(message),
                        static_cast<uint64_t>(orange_distance_tiles(game.pacman, game.ghosts[3])));
    append_frame_text(message, &index, sizeof(message), " orangeTargetMode=");
    append_frame_text(message, &index, sizeof(message),
                      orange_uses_far_target(game.pacman, game.ghosts[3]) ? "far" : "near");
    message[index] = '\0';
    ctx->host->log(ctx, message);
}
#endif

static bool render_and_present(gx_app_context* ctx, gx_handle window, const PacImage& sprites,
                              const GameState& game) {
    const uint64_t frameSequence = ++g_frameSequence;
#if PACMAN_ENABLE_DIAGNOSTICS
    const bool traceFrame = g_pacbmFrameMarkers < 3u;
    if (traceFrame) pacbm_marker(ctx, "PACBM 18 NEXT_FRAME_BEGIN");
#endif
    if (!render_game_scene(&sprites, &game, g_backgroundPixels, g_framePixels,
                           kPacManWidth * kPacManFrameHeight, frameSequence)) return false;
#if PACMAN_ENABLE_DIAGNOSTICS
    if (frameSequence == 1u) pacbm_marker(ctx, "PACBM 08 FIRST_FRAME_COMPOSED");
    if (frameSequence == 1u) pacbm_marker(ctx, "PACBM 09 FIRST_FRAME_CALL_BEGIN");
#endif
    const gx_result result = present(ctx, window);
#if PACMAN_ENABLE_DIAGNOSTICS
    if (frameSequence == 1u) pacbm_marker(ctx, "PACBM 10 FIRST_FRAME_CALL_END");
    if (traceFrame) {
        pacbm_marker(ctx, "PACBM 19 NEXT_FRAME_END");
        ++g_pacbmFrameMarkers;
    }
#endif
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

#if PACMAN_ENABLE_DIAGNOSTICS
static void log_diagnostic_ghost_state(gx_app_context* ctx, const char* label,
                                       const GhostState& ghost) {
    if (!ctx || !ctx->host || !ctx->host->log || !label) return;
    char message[256];
    uint32_t index = 0;
    auto append_text = [&message, &index](const char* text) {
        if (!text) return;
        for (uint32_t i = 0; text[i] && index + 1u < sizeof(message); ++i) message[index++] = text[i];
    };
    auto append_number = [&message, &index](int64_t value) {
        char digits[20];
        uint32_t length = 0;
        bool negative = value < 0;
        uint64_t magnitude = negative ? static_cast<uint64_t>(-(value + 1)) + 1u : static_cast<uint64_t>(value);
        do {
            digits[length++] = static_cast<char>('0' + (magnitude % 10u));
            magnitude /= 10u;
        } while (magnitude != 0 && length < sizeof(digits));
        if (negative && index + 1u < sizeof(message)) message[index++] = '-';
        while (length > 0 && index + 1u < sizeof(message)) message[index++] = digits[--length];
    };
    append_text("PacMan ");
    append_text(label);
    append_text(" state x=");
    append_number(ghost.x);
    append_text(" y=");
    append_number(ghost.y);
    append_text(" dir=");
    append_text(game_direction_name(ghost.direction));
    append_text(" target=");
    append_number(ghost.targetX);
    append_text(",");
    append_number(ghost.targetY);
    append_text(" release=");
    append_text(ghost_release_state_name(ghost.releaseState));
    append_text(" condition=");
    append_text(ghost_condition_name(ghost.condition));
    append_text(" pp=");
    append_number(ghost.powerPillStepsRemaining);
    append_text(" anim=");
    append_number(ghost.animationFrame);
    message[index] = '\0';
    ctx->host->log(ctx, message);
}
#endif

static void log_game_events(gx_app_context* ctx, const GameState& game) {
#if PACMAN_ENABLE_DIAGNOSTICS
    if (!ctx || !ctx->host || !ctx->host->log) return;
    if (game.normalPillConsumed) ctx->host->log(ctx, "PacMan normal pill consumed");
    if (game.powerPillConsumed) ctx->host->log(ctx, "PacMan power pill consumed");
    if (game.powerPillEncounterReset) ctx->host->log(ctx, "PacMan power-pill encounter reset");
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
    static const char* labels[] = {"Red", "Pink", "Cyan", "Orange"};
    for (uint32_t ghostIndex = 0; ghostIndex < kPacManGhostCount; ++ghostIndex) {
        if (game.ghostTimerInitialized[ghostIndex]) {
            log_game_value(ctx, "PacMan frightened timer initialized: ",
                           game_frightened_duration_steps(game));
        }
        if (game.ghostReversalRequested[ghostIndex]) ctx->host->log(ctx, "PacMan ghost reversal requested");
        if (game.ghostReversalApplied[ghostIndex]) ctx->host->log(ctx, "PacMan ghost reversal applied");
        if (game.ghostEnteredFrightened[ghostIndex]) ctx->host->log(ctx, "PacMan ghost entered frightened");
        if (game.frightenedFlashingBegan[ghostIndex]) ctx->host->log(ctx, "PacMan frightened flashing began");
        if (game.ghostTimerExpired[ghostIndex]) ctx->host->log(ctx, "PacMan ghost timer expired");
        if (game.ghostEaten[ghostIndex]) ctx->host->log(ctx, "PacMan ghost eaten");
        if (game.ghostEatScoreAwarded[ghostIndex]) {
            log_game_value(ctx, "PacMan ghost-eating score awarded: ", game.ghostEatScore[ghostIndex]);
            log_game_value(ctx, "PacMan score-chain index: ", game.ghostEatChain);
        }
        if (game.ghostEnteredReturning[ghostIndex]) {
            ctx->host->log(ctx, "PacMan ghost entered returning/reset state");
        }
        if (game.ghostReturned[ghostIndex]) ctx->host->log(ctx, "PacMan ghost returned to normal play");
        log_diagnostic_ghost_state(ctx, labels[ghostIndex], game.ghosts[ghostIndex]);
    }
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

#if PACMAN_HOSTED_POWER_PILL_TEST
static void configure_hosted_power_pill_test(GameState* game) {
    if (!game) return;
    // Row 23, column 1 is the historical lower-left power pill. Pac-Man is
    // held one tile above it so the first fixed update consumes it.
    game->pacman.x = 24;
    game->pacman.y = 360;
    game->pacman.direction = Direction::Down;
    game->pacman.facingDirection = Direction::Down;
    game->pacman.requestedDirection = Direction::Down;
    game->pacman.offset = 0;
    game->pacman.speed = 0;
    const int positions[4][2] = {{160, 232}, {192, 232}, {224, 232}, {256, 232}};
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        GhostState& ghost = game->ghosts[index];
        ghost.x = positions[index][0];
        ghost.y = positions[index][1];
        ghost.offset = 1;
        ghost.direction = Direction::Right;
        ghost.requestedDirection = Direction::Right;
        ghost.condition = GhostCondition::Normal;
        ghost.releaseState = GhostReleaseState::Normal;
        ghost.powerPillStepsRemaining = 0;
        ghost.collisionActive = true;
        ghost.speed = 1;
        ghost.active = true;
    }
    game->suppressGhostCollisionsForValidation = true;
    game->visualDirty = true;
}

static void apply_hosted_power_pill_test_step(GameState* game, bool* activated,
                                               uint32_t* movementWarmupSteps,
                                               bool* expirationArmed) {
    if (!game || !activated || !movementWarmupSteps || !expirationArmed ||
        game->playState != PlayState::Playing) return;
    if (!*activated && game->ghosts[0].condition == GhostCondition::Frightened) {
        *activated = true;
    }
    if (!*activated) return;

    // Keep the first eight updates as a short movement warm-up, then let the
    // source timer run naturally until it enters its flashing window. Holding
    // collisions suppressed through that window makes the hosted proof
    // independent of repaint timing while leaving production behavior untouched.
    if (*movementWarmupSteps < 750u) {
        for (uint32_t index = 0; index < kPacManGhostCount; ++index) game->ghosts[index].speed = 1;
        game->pacman.x = 24;
        game->pacman.y = 360;
        game->pacman.direction = Direction::None;
        game->pacman.facingDirection = Direction::None;
        game->pacman.requestedDirection = Direction::None;
        game->pacman.offset = 1;
        game->pacman.speed = 0;
        game->suppressGhostCollisionsForValidation = true;
        ++*movementWarmupSteps;
        return;
    }

    if (game->ghostEatChain < 4u) {
        const GhostState& target = game->ghosts[game->ghostEatChain];
        game->pacman.x = target.x;
        game->pacman.y = target.y;
        game->pacman.direction = Direction::None;
        game->pacman.facingDirection = Direction::None;
        game->pacman.requestedDirection = Direction::None;
        game->pacman.offset = 1;
        game->pacman.speed = 0;
        game->suppressGhostCollisionsForValidation = false;
        return;
    }

    if (!*expirationArmed) {
        GhostState& ghost = game->ghosts[0];
        ghost.condition = GhostCondition::Frightened;
        ghost.powerPillStepsRemaining = 30;
        ghost.frightenedDelayToggle = false;
        ghost.collisionActive = false;
        ghost.speed = 0;
        game->frightenedFlashPhase = 0;
        game->suppressGhostCollisionsForValidation = true;
        *expirationArmed = true;
    }
}
#endif

}

extern "C" gx_result GX_CALL gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host || !ctx->host->log || !ctx->host->request_window || !ctx->host->poll_event ||
        !ctx->host->get_ticks_ms) return GX_ERROR_INVALID_ARGUMENT;
    ctx->host->log(ctx, "Nexgen PacMan Native ELF starting");

    PacImage level{};
    PacImage sprites{};
    if (!load_gximg(ctx, "resources/level1.gximg", g_levelPixels, kPacManWidth * kPacManMazeHeight, &level)) {
        ctx->host->log(ctx, "PacMan resource load failed");
        return GX_ERROR_FAILED;
    }
    pacbm_marker(ctx, "PACBM 05 LEVEL_RESOURCE_LOADED");
    if (!load_gximg(ctx, "resources/pacpics.gximg", g_spritePixels, 256u * 352u, &sprites)) {
        ctx->host->log(ctx, "PacMan resource load failed");
        return GX_ERROR_FAILED;
    }
    pacbm_marker(ctx, "PACBM 06 SPRITE_RESOURCE_LOADED");
    ctx->host->log(ctx, "PacMan resources loaded");
    if (!level.pixels) {
        ctx->host->log(ctx, "PacMan background invariant failed: level pixels null");
    } else if (level.width != kPacManWidth) {
        ctx->host->log(ctx, "PacMan background invariant failed: level width");
    } else if (level.height < kPacManMazeHeight) {
        ctx->host->log(ctx, "PacMan background invariant failed: level height");
    } else if (level.strideBytes < level.width * 4u) {
        ctx->host->log(ctx, "PacMan background invariant failed: level stride");
    }
    ctx->host->log(ctx, "PacMan background render begin");
    if (!build_background_frame(&level, g_backgroundPixels, kPacManWidth * kPacManFrameHeight)) {
        ctx->host->log(ctx, "PacMan background render failed");
        return GX_ERROR_FAILED;
    }
    ctx->host->log(ctx, "PacMan background render complete");

    ctx->host->log(ctx, "PacMan game initialization begin");
    GameState game;
    game_initialize(&game);
    ctx->host->log(ctx, "PacMan game initialization complete");
#if PACMAN_ENABLE_DIAGNOSTICS
    ctx->host->log(ctx, "PacMan ghosts initialized: Red, Pink, Cyan, and Orange moving");
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
#if PACMAN_HOSTED_PINK_MOVEMENT_TEST
    // Validation-only stable target: Pac-Man remains still while Pink's
    // house route and projected target are observed. This code is absent from
    // the production ELF and has no input/cheat-key path.
    game.pacman.x = 64;
    game.pacman.y = 232;
    game.pacman.direction = Direction::Right;
    game.pacman.facingDirection = Direction::Right;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    {
        const GhostTarget pinkTarget = calculate_pink_target(game, game.pacman);
        game.ghosts[1].targetX = pinkTarget.x;
        game.ghosts[1].targetY = pinkTarget.y;
    }
    game.suppressGhostCollisionsForValidation = true;
    game.visualDirty = true;
    ctx->host->log(ctx, "PacMan hosted Pink movement validation enabled");
#endif
#if PACMAN_HOSTED_CYAN_MOVEMENT_TEST
    // Validation-only stable Pac-Man position. Cyan's historical target is
    // still calculated by the production update path; unrelated collisions
    // are suppressed until the bounded moving-Cyan collision window below.
    game.pacman.x = 64;
    game.pacman.y = 232;
    game.pacman.direction = Direction::Right;
    game.pacman.facingDirection = Direction::Right;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game.suppressGhostCollisionsForValidation = true;
    game.visualDirty = true;
    ctx->host->log(ctx, "PacMan hosted Cyan movement validation enabled");
#endif
#if PACMAN_HOSTED_ORANGE_MOVEMENT_TEST
    // Validation-only target control. Orange's normal release, movement,
    // target selection, tunnel handling, collision, and reset code remain the
    // production path; this hook only holds Pac-Man still and later moves it
    // across Orange's historical threshold. It is absent from pacman.elf.
    game.pacman.x = 64;
    game.pacman.y = 232;
    game.pacman.direction = Direction::Right;
    game.pacman.facingDirection = Direction::Right;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game.suppressGhostCollisionsForValidation = true;
    game.visualDirty = true;
    ctx->host->log(ctx, "PacMan hosted Orange movement validation enabled");
#endif
#if PACMAN_HOSTED_POWER_PILL_TEST
    configure_hosted_power_pill_test(&game);
    ctx->host->log(ctx, "PacMan hosted power-pill validation enabled");
#endif

    gx_handle window = 0;
    gx_result windowResult = GX_ERROR_UNSUPPORTED;
    ctx->host->log(ctx, "PacMan window request begin");
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
    pacbm_marker(ctx, "PACBM 07 WINDOW_CREATED");
    ctx->host->log(ctx, "PacMan initial frame render begin");
    if (!render_and_present(ctx, window, sprites, game)) {
        ctx->host->log(ctx, "PacMan frame presentation failed");
        return GX_ERROR_FAILED;
    }
    ctx->host->log(ctx, "PacMan initial frame render complete");
    game.visualDirty = false;
    ctx->host->log(ctx, "PacMan interactive frame presented");

    pacbm_marker(ctx, "PACBM 11 MAIN_LOOP_ENTER");
    uint64_t previousTicks = pacbm_get_ticks_ms(ctx);
    uint64_t lastPresentedTicks = previousTicks;
    uint64_t accumulatorMs = 0;
    bool running = true;
    bool simulationStartedLogged = false;
#if PACMAN_HOSTED_DANGER_TEST && !PACMAN_HOSTED_RED_MOVEMENT_TEST
    uint32_t hostedDangerPlacementCount = 0;
#endif
#if PACMAN_HOSTED_ORANGE_MOVEMENT_TEST
    bool orangeThresholdSwitched = false;
    uint64_t orangeThresholdSwitchStep = 0;
#endif
#if PACMAN_HOSTED_POWER_PILL_TEST
    bool hostedPowerPillActivated = false;
    uint32_t hostedPowerPillMovementWarmupSteps = 0;
    bool hostedPowerPillExpirationArmed = false;
#endif

    while (running) {
        gx_event event;
        clear_event(&event);
        gx_result eventResult = pacbm_poll_event(ctx, &event, 10);
        if (eventResult == GX_OK && event.window == window) {
            if (gx_event_is_paint(&event)) {
                if (game.visualDirty) {
                    if (!render_and_present(ctx, window, sprites, game)) running = false;
                    game.visualDirty = false;
                    lastPresentedTicks = pacbm_get_ticks_ms(ctx);
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

        const uint64_t currentTicks = pacbm_get_ticks_ms(ctx);
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
#if PACMAN_HOSTED_PINK_MOVEMENT_TEST
            if (game.playState == PlayState::Playing && game.suppressGhostCollisionsForValidation &&
                game.ghosts[1].releaseState == GhostReleaseState::Normal &&
                game.simulationSteps >= 240u) {
                // Put Pac-Man on the moving Pink body for one deterministic
                // validation collision; normal production collision code does
                // the actual death transition on the following update.
                game.pacman.x = game.ghosts[1].x;
                game.pacman.y = game.ghosts[1].y;
                game.pacman.direction = Direction::None;
                game.pacman.facingDirection = Direction::None;
                game.pacman.requestedDirection = Direction::None;
                game.pacman.offset = 0;
                game.pacman.speed = 0;
                game.suppressGhostCollisionsForValidation = false;
                ctx->host->log(ctx, "PacMan hosted Pink movement collision window enabled");
            }
#endif
#if PACMAN_HOSTED_CYAN_MOVEMENT_TEST
            if (game.playState == PlayState::Playing && game.suppressGhostCollisionsForValidation &&
                game.ghosts[2].releaseState == GhostReleaseState::Normal &&
                game.simulationSteps >= 300u) {
                // Place Pac-Man on the moving Cyan body for one deterministic
                // validation collision. Production has no placement hook.
                game.pacman.x = game.ghosts[2].x;
                game.pacman.y = game.ghosts[2].y;
                game.pacman.direction = Direction::None;
                game.pacman.facingDirection = Direction::None;
                game.pacman.requestedDirection = Direction::None;
                game.pacman.offset = 0;
                game.pacman.speed = 0;
                game.suppressGhostCollisionsForValidation = false;
                ctx->host->log(ctx, "PacMan hosted Cyan movement collision window enabled");
            }
#endif
#if PACMAN_HOSTED_ORANGE_MOVEMENT_TEST
            if (game.playState == PlayState::Playing && game.suppressGhostCollisionsForValidation &&
                !orangeThresholdSwitched && game.ghosts[3].releaseState == GhostReleaseState::Normal &&
                game.ghosts[3].offset == 0 && game.simulationSteps >= 240u) {
                // Cross from the far side to exactly four historical tiles at
                // Orange's next legal decision point. Four is intentionally
                // the near side because VB6 projects only when distance > 4.
                game.pacman.x = game.ghosts[3].x + 64;
                game.pacman.y = game.ghosts[3].y;
                game.pacman.direction = Direction::Left;
                game.pacman.facingDirection = Direction::Left;
                game.pacman.requestedDirection = Direction::None;
                game.pacman.offset = 0;
                game.pacman.speed = 0;
                orangeThresholdSwitched = true;
                orangeThresholdSwitchStep = game.simulationSteps;
                ctx->host->log(ctx, "PacMan hosted Orange threshold switch to near target");
            }
            if (game.playState == PlayState::Playing && game.suppressGhostCollisionsForValidation &&
                orangeThresholdSwitched && game.ghosts[3].releaseState == GhostReleaseState::Normal &&
                game.simulationSteps >= orangeThresholdSwitchStep + 80u) {
                game.pacman.x = game.ghosts[3].x;
                game.pacman.y = game.ghosts[3].y;
                game.pacman.direction = Direction::None;
                game.pacman.facingDirection = Direction::None;
                game.pacman.requestedDirection = Direction::None;
                game.pacman.offset = 0;
                game.pacman.speed = 0;
                game.suppressGhostCollisionsForValidation = false;
                ctx->host->log(ctx, "PacMan hosted Orange movement collision window enabled");
            }
#endif
#if PACMAN_HOSTED_POWER_PILL_TEST
            apply_hosted_power_pill_test_step(&game, &hostedPowerPillActivated,
                                              &hostedPowerPillMovementWarmupSteps,
                                              &hostedPowerPillExpirationArmed);
#endif
#if PACMAN_ENABLE_DIAGNOSTICS
            const bool traceUpdate = g_pacbmUpdateMarkers < 5u;
            if (traceUpdate) pacbm_marker(ctx, "PACBM 16 UPDATE_BEGIN");
#endif
            game_update(&game);
#if PACMAN_ENABLE_DIAGNOSTICS
            if (traceUpdate) {
                pacbm_marker(ctx, "PACBM 17 UPDATE_END");
                ++g_pacbmUpdateMarkers;
            }
#endif
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
            if (game.cyanTunnelWrapped) ctx->host->log(ctx, "PacMan Cyan ghost tunnel wrap");
            if (game.orangeTunnelWrapped) ctx->host->log(ctx, "PacMan Orange ghost tunnel wrap");
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

    if (ctx->host->exit) {
        pacbm_marker(ctx, "PACBM 20 EXIT_REQUESTED");
        return ctx->host->exit(ctx, GX_OK);
    }
    return GX_OK;
}
