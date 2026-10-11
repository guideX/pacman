#include "game.h"
#include "level.h"
#include "pacman_timing.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <vector>

namespace {

uint32_t g_checks = 0;
uint32_t g_groups = 0;

bool check(bool condition, const char* label) {
    ++g_checks;
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", label);
    return condition;
}

void begin_group(const char* name) {
    ++g_groups;
    std::printf("[group %u] %s\n", g_groups, name);
}

static const uint32_t kMaximumCatchUpSteps = 8u;
static const uint64_t kStepsPerSimulatedMinute = 6000u;

void process_clock_frame(PacManFixedStepClock* clock, uint32_t* frameSteps) {
    *frameSteps = 0;
    while (*frameSteps < kMaximumCatchUpSteps && pacman_fixed_step_clock_has_step(*clock)) {
        pacman_fixed_step_clock_consume_step(clock);
        ++*frameSteps;
    }
    pacman_fixed_step_clock_finish_frame(clock, *frameSteps, kMaximumCatchUpSteps);
}

void drain_clock(PacManFixedStepClock* clock, uint64_t* drainedSteps) {
    uint32_t frameSteps = 0;
    while (pacman_fixed_step_clock_has_step(*clock)) {
        process_clock_frame(clock, &frameSteps);
        *drainedSteps += frameSteps;
    }
}

bool test_fixed_step_cadences_and_wrap() {
    begin_group("fixed-step cadence, catch-up debt, and uint64 tick wrap");
    bool ok = true;
    PacManFixedStepClock clock{};
    uint64_t now = 0;
    uint32_t frameSteps = 0;

    pacman_fixed_step_clock_initialize(&clock, now);
    for (uint32_t frame = 0; frame < 100u; ++frame) {
        now += 10u;
        pacman_fixed_step_clock_sample(&clock, now);
        process_clock_frame(&clock, &frameSteps);
    }
    ok &= check(clock.totalSteps == 100u && clock.accumulatedMs == 0u &&
                clock.droppedMilliseconds == 0u,
                "steady 10 ms cadence advances one exact fixed step per frame");

    pacman_fixed_step_clock_initialize(&clock, 0u);
    now = 0u;
    for (uint32_t frame = 0; frame < 120u; ++frame) {
        now += frame % 2u == 0u ? 16u : 17u;
        pacman_fixed_step_clock_sample(&clock, now);
        process_clock_frame(&clock, &frameSteps);
    }
    uint64_t drainedSteps = 0u;
    drain_clock(&clock, &drainedSteps);
    ok &= check(clock.totalElapsedMs == 1980u && clock.totalSteps == 198u &&
                clock.accumulatedMs == 0u,
                "16/17 ms pacing preserves every due fixed step without drift");

    pacman_fixed_step_clock_initialize(&clock, 0u);
    now = 0u;
    const uint32_t jitterMs[] = {1u, 2u, 36u, 5u, 130u, 3u, 250u, 17u, 11u, 83u};
    uint64_t elapsedTotal = 0u;
    uint64_t processedSteps = 0u;
    for (uint32_t frame = 0; frame < sizeof(jitterMs) / sizeof(jitterMs[0]); ++frame) {
        now += jitterMs[frame];
        elapsedTotal += jitterMs[frame];
        pacman_fixed_step_clock_sample(&clock, now);
        process_clock_frame(&clock, &frameSteps);
        processedSteps += frameSteps;
    }
    drain_clock(&clock, &processedSteps);
    ok &= check(clock.totalElapsedMs == elapsedTotal &&
                clock.totalSteps == elapsedTotal / kPacManFixedStepMilliseconds &&
                clock.accumulatedMs == elapsedTotal % kPacManFixedStepMilliseconds &&
                clock.droppedMilliseconds == 0u,
                "jitter and multi-second stalls recover by draining retained debt");

    pacman_fixed_step_clock_initialize(&clock, 0u);
    pacman_fixed_step_clock_sample(&clock, 500u);
    process_clock_frame(&clock, &frameSteps);
    ok &= check(frameSteps == kMaximumCatchUpSteps && clock.accumulatedMs == 420u,
                "500 ms stall is bounded to eight updates while retaining 420 ms debt");
    processedSteps = frameSteps;
    drain_clock(&clock, &processedSteps);
    ok &= check(processedSteps == 50u && clock.accumulatedMs == 0u &&
                clock.droppedMilliseconds == 0u,
                "bounded catch-up eventually simulates the complete 500 ms stall");

    pacman_fixed_step_clock_initialize(&clock, 0u);
    pacman_fixed_step_clock_sample(&clock, 60000u);
    ok &= check(clock.accumulatedMs == kPacManMaximumBacklogMilliseconds &&
                clock.maximumBacklogMs == kPacManMaximumBacklogMilliseconds &&
                clock.droppedMilliseconds == 55000u,
                "a one-minute suspension is bounded and the explicitly skipped interval is measured");
    processedSteps = 0u;
    drain_clock(&clock, &processedSteps);
    ok &= check(processedSteps == kPacManMaximumBacklogMilliseconds /
                    kPacManFixedStepMilliseconds && clock.accumulatedMs == 0u,
                "bounded suspension recovery drains all retained fixed-step debt");

    const uint64_t nearWrap = ~static_cast<uint64_t>(0) - 5u;
    pacman_fixed_step_clock_initialize(&clock, nearWrap);
    const uint64_t wrapElapsed = pacman_fixed_step_clock_sample(&clock, 4u);
    process_clock_frame(&clock, &frameSteps);
    ok &= check(wrapElapsed == 10u && clock.totalSteps == 1u &&
                clock.accumulatedMs == 0u,
                "monotonic tick wrap advances the exact elapsed duration");
    return ok;
}

bool test_simulation_timer_boundaries() {
    begin_group("simulation and gameplay timer boundaries");
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    game.suppressGhostCollisionsForValidation = true;
    game.pacman.speed = 0;
    game.pacman.direction = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.fruit.appearancesTriggered = 1u;
    game.fruit.timeCountSteps = ~static_cast<uint32_t>(0) - 1u;
    game.simulationSteps = ~static_cast<uint64_t>(0) - 1u;
    game_update(&game);
    ok &= check(game.simulationSteps == ~static_cast<uint64_t>(0) &&
                game.fruit.timeCountSteps == kPacManFruitExpirationTime,
                "near-maximum counters advance or clamp without signed wrap or runaway timers");
    game_update(&game);
    ok &= check(game.simulationSteps == 0u &&
                game.fruit.timeCountSteps == kPacManFruitExpirationTime,
                "the unsigned simulation-step wrap remains defined and bounded");
    return ok;
}

static Direction direction_for_index(uint32_t index) {
    static const Direction choices[] = {
        Direction::Up, Direction::Down, Direction::Left, Direction::Right
    };
    return choices[index % 4u];
}

static uint64_t trace_mix(uint64_t hash, uint64_t value) {
    hash ^= value;
    return hash * 1099511628211ull;
}

static void keep_only_final_pill(GameState* game) {
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            if (game->level.cells[row][column] == CellType::Pill ||
                game->level.cells[row][column] == CellType::PowerPill) {
                game->level.cells[row][column] = CellType::Empty;
            }
        }
    }
    game->level.cells[1][2] = CellType::Pill;
    game->level.normalPillsRemaining = 1u;
    game->level.powerPillsRemaining = 0u;
    game->level.totalConsumablesRemaining = 1u;
    game->pacman.x = 24;
    game->pacman.y = 24;
    game->pacman.offset = 0;
    game->pacman.speed = 1;
    game->pacman.direction = Direction::Right;
    game->pacman.facingDirection = Direction::Right;
    game->pacman.requestedDirection = Direction::Right;
    game->pacman.turnRequestPending = false;
}

static void prepare_forced_collision(GameState* game, uint8_t lives) {
    game->suppressGhostCollisionsForValidation = false;
    game->lives = lives;
    game->pacman.speed = 0;
    game->pacman.direction = Direction::None;
    game->pacman.requestedDirection = Direction::None;
    game->pacman.turnRequestPending = false;
    GhostState& red = game->ghosts[0];
    red.x = game->pacman.x;
    red.y = game->pacman.y;
    red.speed = 0;
    red.condition = GhostCondition::Normal;
    red.releaseState = GhostReleaseState::Normal;
    red.active = true;
    red.collisionActive = true;
}

struct EnduranceStats {
    uint64_t hash;
    uint64_t acceptedTurns;
    uint64_t pacmanTunnelWraps;
    uint64_t ghostTunnelWraps[4];
    uint64_t completedLevels;
    uint64_t deathTransitions;
    uint64_t respawns;
    uint64_t gameOvers;
    uint64_t sessionRestarts;
    uint64_t pacmanPixels;
    uint64_t ghostPixels[4];
    uint64_t longestPacmanStationarySteps;
    uint64_t pacmanStationarySteps;
    uint64_t longestGhostStationarySteps[4];
    uint64_t ghostStationarySteps[4];
    uint64_t maximumBufferedTurnLatency;
    uint64_t firstFiveMinuteSampleNs;
    uint64_t lastFiveMinuteSampleNs;
    uint64_t firstFiveMinuteSampleCount;
    uint64_t lastFiveMinuteSampleCount;
    uint32_t levelAtCheckpoint;
    uint32_t finalPacmanSpeed;
    uint32_t finalGhostSpeeds[4];
    bool movementSpeedDrifted;
    std::vector<uint64_t> updateSamplesNs;

    EnduranceStats() : hash(1469598103934665603ull), acceptedTurns(0), pacmanTunnelWraps(0),
        completedLevels(0),
        deathTransitions(0), respawns(0), gameOvers(0), sessionRestarts(0), pacmanPixels(0),
        longestPacmanStationarySteps(0), pacmanStationarySteps(0), maximumBufferedTurnLatency(0),
        firstFiveMinuteSampleNs(0), lastFiveMinuteSampleNs(0), firstFiveMinuteSampleCount(0),
        lastFiveMinuteSampleCount(0), levelAtCheckpoint(1u), finalPacmanSpeed(0u),
        movementSpeedDrifted(false) {
        for (uint32_t index = 0; index < 4u; ++index) {
            ghostPixels[index] = 0;
            ghostTunnelWraps[index] = 0;
            longestGhostStationarySteps[index] = 0;
            ghostStationarySteps[index] = 0;
            finalGhostSpeeds[index] = 0;
        }
        updateSamplesNs.reserve(6000u);
    }
};

static uint64_t actor_distance(const GhostState& before, const GhostState& after,
                              bool tunnelRow) {
    uint64_t dx = static_cast<uint64_t>(before.x > after.x
        ? before.x - after.x : after.x - before.x);
    if (tunnelRow && dx > 208u) dx = 416u - dx;
    const uint64_t dy = static_cast<uint64_t>(before.y > after.y
        ? before.y - after.y : after.y - before.y);
    return dx + dy;
}

static void apply_scripted_input(GameState* game, uint64_t step, uint32_t* randomState,
                                Direction* heldDirection, uint64_t* requestStartedAt) {
    if (!game || game->playState != PlayState::Playing) return;
    if (step % 11u == 0u) {
        if (*heldDirection != Direction::None) {
            game_release_direction(game, *heldDirection);
            *heldDirection = Direction::None;
        }
        *randomState = *randomState * 1664525u + 1013904223u;
        const Direction requested = direction_for_index(*randomState >> 29u);
        game_press_direction(game, requested);
        *heldDirection = requested;
        *requestStartedAt = step;
    } else if (step % 11u == 2u && *heldDirection != Direction::None) {
        game_release_direction(game, *heldDirection);
        *heldDirection = Direction::None;
    }

    if (game->pacman.direction == Direction::None && game->pacman.offset == 0) {
        for (uint32_t offset = 0; offset < 4u; ++offset) {
            const Direction candidate = direction_for_index((*randomState >> 30u) + offset);
            if (can_move(*game, game->pacman.x, game->pacman.y, candidate)) {
                game_press_direction(game, candidate);
                *heldDirection = candidate;
                *requestStartedAt = step;
                break;
            }
        }
    }
}

static void update_hash(EnduranceStats* stats, const GameState& game) {
    stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.pacman.x));
    stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.pacman.y));
    stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.pacman.direction));
    stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.pacman.requestedDirection));
    stats->hash = trace_mix(stats->hash, game.levelNumber);
    stats->hash = trace_mix(stats->hash, game.score);
    stats->hash = trace_mix(stats->hash, game.level.totalConsumablesRemaining);
    stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.playState));
    for (uint32_t index = 0; index < 4u; ++index) {
        stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.ghosts[index].x));
        stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.ghosts[index].y));
        stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.ghosts[index].direction));
        stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.ghosts[index].condition));
        stats->hash = trace_mix(stats->hash, static_cast<uint64_t>(game.ghosts[index].releaseState));
    }
}

static EnduranceStats run_endurance(uint64_t totalSteps, bool printCheckpoints) {
    GameState game{};
    game_initialize(&game);
    game.suppressGhostCollisionsForValidation = true;
    EnduranceStats stats;
    uint32_t randomState = 0xC0FFEEu;
    Direction heldDirection = Direction::None;
    uint64_t requestStartedAt = 0u;
    int previousPacmanX = game.pacman.x;
    int previousPacmanY = game.pacman.y;
    uint64_t checkpointHash = 0u;
    const auto wallStart = std::chrono::steady_clock::now();

    for (uint64_t step = 1u; step <= totalSteps; ++step) {
        if (step == 5u * kStepsPerSimulatedMinute ||
            step == 10u * kStepsPerSimulatedMinute ||
            step == 20u * kStepsPerSimulatedMinute ||
            step == 30u * kStepsPerSimulatedMinute ||
            step == 40u * kStepsPerSimulatedMinute ||
            step == 50u * kStepsPerSimulatedMinute ||
            step == 55u * kStepsPerSimulatedMinute) {
            keep_only_final_pill(&game);
            heldDirection = Direction::None;
        }
        if (step == 15u * kStepsPerSimulatedMinute ||
            step == 45u * kStepsPerSimulatedMinute) {
            prepare_forced_collision(&game, kPacManInitialLives);
        }
        if (step == 25u * kStepsPerSimulatedMinute) {
            prepare_forced_collision(&game, 1u);
        }

        apply_scripted_input(&game, step, &randomState, &heldDirection,
                             &requestStartedAt);
        previousPacmanX = game.pacman.x;
        previousPacmanY = game.pacman.y;
        GhostState ghostsBefore[4];
        for (uint32_t index = 0; index < 4u; ++index) ghostsBefore[index] = game.ghosts[index];

        const bool sampleUpdate = (step - 1u) % kStepsPerSimulatedMinute < 100u;
        std::chrono::steady_clock::time_point updateStart;
        if (sampleUpdate) updateStart = std::chrono::steady_clock::now();
        const PlayState stateBeforeUpdate = game.playState;
        game_update(&game);
        if (sampleUpdate) {
            const std::chrono::steady_clock::time_point updateEnd = std::chrono::steady_clock::now();
            const uint64_t sampleNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(updateEnd - updateStart).count());
            stats.updateSamplesNs.push_back(sampleNs);
            if (step <= 5u * kStepsPerSimulatedMinute) {
                stats.firstFiveMinuteSampleNs += sampleNs;
                ++stats.firstFiveMinuteSampleCount;
            }
            if (step > totalSteps - 5u * kStepsPerSimulatedMinute) {
                stats.lastFiveMinuteSampleNs += sampleNs;
                ++stats.lastFiveMinuteSampleCount;
            }
        }

        if (game.turnAccepted) {
            ++stats.acceptedTurns;
            const uint64_t latency = step - requestStartedAt;
            if (latency > stats.maximumBufferedTurnLatency) {
                stats.maximumBufferedTurnLatency = latency;
            }
        }
        if (game.levelCompleteEntered) ++stats.completedLevels;
        if (game.deathEntered) ++stats.deathTransitions;
        if (game.actorReset && stateBeforeUpdate == PlayState::Dying) ++stats.respawns;
        if (game.gameOverEntered) ++stats.gameOvers;
        if (game.tunnelWrapped) ++stats.pacmanTunnelWraps;
        if (game.redTunnelWrapped) ++stats.ghostTunnelWraps[0];
        if (game.pinkTunnelWrapped) ++stats.ghostTunnelWraps[1];
        if (game.cyanTunnelWrapped) ++stats.ghostTunnelWraps[2];
        if (game.orangeTunnelWrapped) ++stats.ghostTunnelWraps[3];
        if (game.playState == PlayState::Playing) {
            if (game.gameSpeed != 1u || game.pacman.speed != 1) stats.movementSpeedDrifted = true;
            for (uint32_t index = 0; index < 4u; ++index) {
                if (game.ghosts[index].speed != 1) stats.movementSpeedDrifted = true;
            }
        }
        if (game.deathEntered) game.suppressGhostCollisionsForValidation = true;
        if (game.playState == PlayState::GameOver && stats.sessionRestarts == 0u) {
            if (game_restart_session(&game)) ++stats.sessionRestarts;
        }

        if (game.playState == PlayState::Playing && game.pacman.speed > 0) {
            uint64_t dx = static_cast<uint64_t>(game.pacman.x > previousPacmanX
                ? game.pacman.x - previousPacmanX : previousPacmanX - game.pacman.x);
            if (level_row_from_position(game.pacman.y) == kPacManTunnelRow &&
                level_row_from_position(previousPacmanY) == kPacManTunnelRow && dx > 208u) {
                dx = 416u - dx;
            }
            const uint64_t dy = static_cast<uint64_t>(game.pacman.y > previousPacmanY
                ? game.pacman.y - previousPacmanY : previousPacmanY - game.pacman.y);
            const uint64_t distance = dx + dy;
            if (distance == 0u) {
                ++stats.pacmanStationarySteps;
                if (stats.pacmanStationarySteps > stats.longestPacmanStationarySteps) {
                    stats.longestPacmanStationarySteps = stats.pacmanStationarySteps;
                }
            } else {
                stats.pacmanPixels += distance;
                stats.pacmanStationarySteps = 0u;
            }
        } else {
            stats.pacmanStationarySteps = 0u;
        }
        previousPacmanX = game.pacman.x;
        previousPacmanY = game.pacman.y;

        for (uint32_t index = 0; index < 4u; ++index) {
            const GhostState& ghost = game.ghosts[index];
            if (game.playState != PlayState::Playing || !ghost.active || ghost.speed <= 0) {
                stats.ghostStationarySteps[index] = 0u;
                continue;
            }
            const uint64_t distance = actor_distance(ghostsBefore[index], ghost,
                level_row_from_position(ghost.y) == kPacManTunnelRow);
            if (distance != 0u) {
                stats.ghostPixels[index] += distance;
                stats.ghostStationarySteps[index] = 0u;
            } else {
                ++stats.ghostStationarySteps[index];
                if (stats.ghostStationarySteps[index] > stats.longestGhostStationarySteps[index]) {
                    stats.longestGhostStationarySteps[index] = stats.ghostStationarySteps[index];
                }
            }
        }

        update_hash(&stats, game);
        if (step == 5u * kStepsPerSimulatedMinute) {
            checkpointHash = stats.hash;
            stats.levelAtCheckpoint = game.levelNumber;
        }
        if (printCheckpoints && step % kStepsPerSimulatedMinute == 0u) {
            const uint64_t minute = step / kStepsPerSimulatedMinute;
            if (minute == 5u || minute == 15u || minute == 30u || minute == 60u) {
                const auto now = std::chrono::steady_clock::now();
                const uint64_t wallMs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - wallStart).count());
                std::printf("CHECKPOINT sim_min=%llu wall_ms=%llu pac_pixels=%llu turns=%llu "
                            "ghost_pixels=%llu,%llu,%llu,%llu max_stall=%llu,%llu,%llu,%llu "
                            "level=%u score=%u\n",
                    static_cast<unsigned long long>(minute),
                    static_cast<unsigned long long>(wallMs),
                    static_cast<unsigned long long>(stats.pacmanPixels),
                    static_cast<unsigned long long>(stats.acceptedTurns),
                    static_cast<unsigned long long>(stats.ghostPixels[0]),
                    static_cast<unsigned long long>(stats.ghostPixels[1]),
                    static_cast<unsigned long long>(stats.ghostPixels[2]),
                    static_cast<unsigned long long>(stats.ghostPixels[3]),
                    static_cast<unsigned long long>(stats.longestGhostStationarySteps[0]),
                    static_cast<unsigned long long>(stats.longestGhostStationarySteps[1]),
                    static_cast<unsigned long long>(stats.longestGhostStationarySteps[2]),
                    static_cast<unsigned long long>(stats.longestGhostStationarySteps[3]),
                    game.levelNumber, game.score);
            }
        }
    }

    stats.finalPacmanSpeed = static_cast<uint32_t>(game.pacman.speed);
    for (uint32_t index = 0; index < 4u; ++index) {
        stats.finalGhostSpeeds[index] = static_cast<uint32_t>(game.ghosts[index].speed);
    }
    if (printCheckpoints) {
        const auto wallEnd = std::chrono::steady_clock::now();
        const uint64_t wallMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(wallEnd - wallStart).count());
        std::printf("ENDURANCE total_sim_min=%.1f steps=%llu wall_ms=%llu replay_prefix_hash=%llu levels=%llu "
                    "deaths=%llu respawns=%llu gameovers=%llu restarts=%llu "
                    "update_avg_ns=%llu first5_avg_ns=%llu last5_avg_ns=%llu "
                    "max_turn_latency_steps=%llu\n",
            static_cast<double>(totalSteps) / 6000.0,
            static_cast<unsigned long long>(totalSteps),
            static_cast<unsigned long long>(wallMs),
            static_cast<unsigned long long>(stats.hash),
            static_cast<unsigned long long>(stats.completedLevels),
            static_cast<unsigned long long>(stats.deathTransitions),
            static_cast<unsigned long long>(stats.respawns),
            static_cast<unsigned long long>(stats.gameOvers),
            static_cast<unsigned long long>(stats.sessionRestarts),
            static_cast<unsigned long long>(stats.updateSamplesNs.empty() ? 0u :
                std::accumulate(stats.updateSamplesNs.begin(), stats.updateSamplesNs.end(),
                                static_cast<uint64_t>(0)) / stats.updateSamplesNs.size()),
            static_cast<unsigned long long>(stats.firstFiveMinuteSampleCount == 0u ? 0u :
                stats.firstFiveMinuteSampleNs / stats.firstFiveMinuteSampleCount),
            static_cast<unsigned long long>(stats.lastFiveMinuteSampleCount == 0u ? 0u :
                stats.lastFiveMinuteSampleNs / stats.lastFiveMinuteSampleCount),
            static_cast<unsigned long long>(stats.maximumBufferedTurnLatency));
    }
    // The caller compares this five-minute prefix with an independently
    // replayed run; future schedule entries cannot influence that prefix.
    stats.hash = checkpointHash;
    return stats;
}

static uint64_t percentile(std::vector<uint64_t> samples, uint32_t percentileValue) {
    if (samples.empty()) return 0u;
    std::sort(samples.begin(), samples.end());
    const uint64_t index = (static_cast<uint64_t>(samples.size()) * percentileValue + 99u) / 100u;
    return samples[static_cast<size_t>(index == 0u ? 0u : index - 1u)];
}

bool test_endurance_and_determinism() {
    begin_group("5/15/30/60 simulated-minute gameplay endurance");
    const uint64_t totalSteps = 60u * kStepsPerSimulatedMinute;
    EnduranceStats fullRun = run_endurance(totalSteps, true);
    EnduranceStats replay = run_endurance(5u * kStepsPerSimulatedMinute, false);
    bool ok = true;
    ok &= check(fullRun.hash != 0u && fullRun.hash == replay.hash,
                "the same scripted five-minute trace reproduces exactly");
    ok &= check(fullRun.acceptedTurns > 100u && fullRun.pacmanPixels > 10000u &&
                fullRun.maximumBufferedTurnLatency <= 16u &&
                fullRun.longestPacmanStationarySteps <= 16u,
                "Pac-Man continues moving and accepts buffered turns within one tile");
    ok &= check(fullRun.completedLevels >= 5u && fullRun.deathTransitions >= 3u &&
                fullRun.respawns >= 2u && fullRun.gameOvers == 1u &&
                fullRun.sessionRestarts == 1u,
                "the long run exercises repeated levels, death, respawn, Game Over, and restart");
    ok &= check(!fullRun.movementSpeedDrifted && fullRun.finalPacmanSpeed == 1u &&
                fullRun.finalGhostSpeeds[0] == 1u &&
                fullRun.finalGhostSpeeds[1] == 1u && fullRun.finalGhostSpeeds[2] == 1u &&
                fullRun.finalGhostSpeeds[3] == 1u,
                "actor speed remains tied to game difficulty and does not drift with session age");
    ok &= check(fullRun.pacmanTunnelWraps > 0u,
                "scripted input enters and exits the tunnel during the long session");
    for (uint32_t index = 0; index < 4u; ++index) {
        char label[96];
        std::snprintf(label, sizeof(label), "ghost %u makes long-run progress without a stall", index);
        ok &= check(fullRun.ghostPixels[index] > 10000u &&
                    fullRun.longestGhostStationarySteps[index] <= 4u, label);
        std::printf("GHOST_METRIC index=%u progress_pixels=%llu tunnel_wraps=%llu max_stationary_steps=%llu\n",
            index, static_cast<unsigned long long>(fullRun.ghostPixels[index]),
            static_cast<unsigned long long>(fullRun.ghostTunnelWraps[index]),
            static_cast<unsigned long long>(fullRun.longestGhostStationarySteps[index]));
    }
    ok &= check(fullRun.updateSamplesNs.size() == 6000u &&
                fullRun.firstFiveMinuteSampleCount > 0u && fullRun.lastFiveMinuteSampleCount > 0u,
                "per-update performance sampling stays bounded at ten samples per simulated minute");

    const uint64_t p95 = percentile(fullRun.updateSamplesNs, 95u);
    const uint64_t p99 = percentile(fullRun.updateSamplesNs, 99u);
    const uint64_t firstAverage = fullRun.firstFiveMinuteSampleNs /
        fullRun.firstFiveMinuteSampleCount;
    const uint64_t lastAverage = fullRun.lastFiveMinuteSampleNs /
        fullRun.lastFiveMinuteSampleCount;
    std::printf("METRICS simulated_game_ms=%llu sampled_updates=%llu update_avg_ns=%llu "
                "update_p95_ns=%llu update_p99_ns=%llu first5_avg_ns=%llu last5_avg_ns=%llu "
                "pacman_pixels=%llu buffered_turns=%llu longest_pacman_stall_steps=%llu\n",
        static_cast<unsigned long long>(totalSteps * kPacManFixedStepMilliseconds),
        static_cast<unsigned long long>(fullRun.updateSamplesNs.size()),
        static_cast<unsigned long long>(std::accumulate(fullRun.updateSamplesNs.begin(),
            fullRun.updateSamplesNs.end(), static_cast<uint64_t>(0)) /
            fullRun.updateSamplesNs.size()),
        static_cast<unsigned long long>(p95), static_cast<unsigned long long>(p99),
        static_cast<unsigned long long>(firstAverage), static_cast<unsigned long long>(lastAverage),
        static_cast<unsigned long long>(fullRun.pacmanPixels),
        static_cast<unsigned long long>(fullRun.acceptedTurns),
        static_cast<unsigned long long>(fullRun.longestPacmanStationarySteps));
    ok &= check(lastAverage <= firstAverage * 2u + 1000u,
                "sampled update cost does not progressively grow between session start and end");
    return ok;
}

bool test_power_pill_repetition_and_ghost_combo() {
    begin_group("repeated power-pellet effects and four-ghost combo lifecycle");
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    game.suppressGhostCollisionsForValidation = true;
    const int ghostX[] = {64, 144, 224, 304};
    for (uint32_t index = 0; index < 4u; ++index) {
        GhostState& ghost = game.ghosts[index];
        ghost.x = ghostX[index];
        ghost.y = 232;
        ghost.direction = Direction::Right;
        ghost.requestedDirection = Direction::Right;
        ghost.offset = 0;
        ghost.condition = GhostCondition::Normal;
        ghost.releaseState = GhostReleaseState::Normal;
        ghost.speed = 1;
        ghost.active = true;
        ghost.collisionActive = true;
    }
    game.pacman.x = 24;
    game.pacman.y = 40;
    game.pacman.offset = 0;
    game.pacman.speed = 0;
    game.pacman.direction = Direction::Down;
    game.pacman.requestedDirection = Direction::Down;
    game.pacman.turnRequestPending = false;
    game_update(&game);
    ok &= check(game.powerPillConsumed && game.score == kPacManPowerPillScore,
                "a real maze power pill activates the four-ghost frightened lifecycle");
    for (uint32_t index = 0; index < 4u; ++index) {
        ok &= check(game.ghosts[index].condition == GhostCondition::Frightened &&
                    game.ghosts[index].powerPillStepsRemaining ==
                        game_frightened_duration_steps(game) - 1u,
                    "all four released ghosts receive an independent power timer");
    }

    game.suppressGhostCollisionsForValidation = false;
    for (uint32_t index = 0; index < 4u; ++index) {
        game.pacman.x = game.ghosts[index].x;
        game.pacman.y = game.ghosts[index].y;
        game.pacman.offset = 0;
        game.pacman.speed = 0;
        game.pacman.direction = Direction::None;
        game.pacman.requestedDirection = Direction::None;
        game.pacman.turnRequestPending = false;
        game_update(&game);
        ok &= check(game.ghostEaten[index] && game.ghosts[index].condition == GhostCondition::Eaten,
                    "each frightened ghost can be eaten once during a combo");
    }
    ok &= check(game.score == kPacManPowerPillScore + 200u + 400u + 800u + 1600u &&
                game.ghostEatChain == 4u,
                "four-ghost combo awards source scores in order without duplicate overlap scoring");

    GameState repeated{};
    game_initialize(&repeated);
    repeated.suppressGhostCollisionsForValidation = true;
    for (uint32_t index = 0; index < 4u; ++index) {
        GhostState& ghost = repeated.ghosts[index];
        ghost.x = ghostX[index];
        ghost.y = 232;
        ghost.direction = Direction::Right;
        ghost.requestedDirection = Direction::Right;
        ghost.offset = 0;
        ghost.condition = GhostCondition::Normal;
        ghost.releaseState = GhostReleaseState::Normal;
        ghost.speed = 1;
    }
    repeated.pacman.x = 24;
    repeated.pacman.y = 40;
    repeated.pacman.offset = 0;
    repeated.pacman.speed = 0;
    repeated.pacman.direction = Direction::Down;
    repeated.pacman.requestedDirection = Direction::Down;
    repeated.pacman.turnRequestPending = false;
    game_update(&repeated);
    const uint32_t duration = game_frightened_duration_steps(repeated);
    uint32_t resets = 1u;
    for (uint32_t encounter = 1u; encounter < 6u; ++encounter) {
        for (uint32_t tick = 0; tick < 120u; ++tick) game_update(&repeated);
        repeated.level.cells[3][1] = CellType::PowerPill;
        ++repeated.level.powerPillsRemaining;
        ++repeated.level.totalConsumablesRemaining;
        repeated.pacman.x = 24;
        repeated.pacman.y = 40;
        repeated.pacman.offset = 0;
        repeated.pacman.direction = Direction::Down;
        repeated.pacman.requestedDirection = Direction::Down;
        game_update(&repeated);
        if (repeated.powerPillConsumed) ++resets;
        ok &= check(repeated.powerPillConsumed &&
                    repeated.ghosts[0].powerPillStepsRemaining == duration - 1u,
                    "repeated power pills refresh the active timer rather than accumulating debt");
    }
    for (uint32_t tick = 0; tick < duration; ++tick) game_update(&repeated);
    ok &= check(resets == 6u && repeated.ghosts[0].condition == GhostCondition::Normal &&
                repeated.ghosts[0].powerPillStepsRemaining == 0u,
                "repeated frightened effects expire once and restore normal movement");
    return ok;
}

bool test_repeated_game_initialization_and_restart() {
    begin_group("consecutive session initialization and Game Over restart cleanup");
    bool ok = true;
    for (uint32_t launch = 0; launch < 16u; ++launch) {
        GameState game{};
        game_initialize(&game);
        game_begin_initial_ready(&game);
        for (uint32_t step = 0; step < kPacManInitialReadyDurationSteps; ++step) game_update(&game);
        ok &= check(game.playState == PlayState::Playing && game.levelNumber == 1u &&
                    game.level.totalConsumablesRemaining == 244u &&
                    game.ghosts[0].speed == 1 && game.pacman.speed == 1,
                    "fresh game state initializes cleanly after the previous session exits");

        prepare_forced_collision(&game, 1u);
        game_update(&game);
        for (uint32_t step = 0; step < kPacManDeathDurationSteps; ++step) game_update(&game);
        ok &= check(game.playState == PlayState::GameOver && game.lives == 0u,
                    "final-life transition reaches Game Over without counter underflow");
        ok &= check(game_restart_session(&game) && game.playState == PlayState::ReadyAfterDeath &&
                    game.levelNumber == 1u && game.lives == kPacManInitialLives && game.score == 0u,
                    "session restart clears score, level, lives, and actor state");
    }
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= test_fixed_step_cadences_and_wrap();
    ok &= test_simulation_timer_boundaries();
    ok &= test_endurance_and_determinism();
    ok &= test_power_pill_repetition_and_ghost_combo();
    ok &= test_repeated_game_initialization_and_restart();
    std::printf("PacMan endurance tests %s: groups=%u checks=%u\n",
                ok ? "PASS" : "FAIL", g_groups, g_checks);
    return ok ? 0 : 1;
}
