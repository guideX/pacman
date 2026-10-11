#pragma once

#include <stdint.h>

static const uint64_t kPacManFixedStepMilliseconds = 10u;
static const uint64_t kPacManMaximumBacklogMilliseconds = 5000u;

// The native loop limits work per event-loop pass and carries remaining
// fixed-step debt forward. A five-second cap bounds recovery after suspension;
// any excess is counted explicitly for the once-per-minute diagnostic sample.
struct PacManFixedStepClock {
    uint64_t previousTicksMs;
    uint64_t accumulatedMs;
    uint64_t totalElapsedMs;
    uint64_t maximumBacklogMs;
    uint64_t droppedMilliseconds;
    uint64_t catchUpFrames;
    uint64_t totalSteps;
    uint32_t maximumStepsPerFrame;
};

static inline uint64_t pacman_timing_saturating_add(uint64_t left, uint64_t right,
                                                   uint64_t* dropped) {
    const uint64_t maximum = ~static_cast<uint64_t>(0);
    if (right > maximum - left) {
        if (dropped) {
            const uint64_t excess = right - (maximum - left);
            *dropped = excess > maximum - *dropped ? maximum : *dropped + excess;
        }
        return maximum;
    }
    return left + right;
}

static inline void pacman_fixed_step_clock_initialize(PacManFixedStepClock* clock,
                                                       uint64_t currentTicksMs) {
    if (!clock) return;
    clock->previousTicksMs = currentTicksMs;
    clock->accumulatedMs = 0;
    clock->totalElapsedMs = 0;
    clock->maximumBacklogMs = 0;
    clock->droppedMilliseconds = 0;
    clock->catchUpFrames = 0;
    clock->totalSteps = 0;
    clock->maximumStepsPerFrame = 0;
}

// Unsigned subtraction is intentional: the host tick counter is monotonic and
// wraps modulo 2^64. A correctly wrapped counter therefore yields its true
// elapsed interval instead of a false regression or a zero-duration frame.
static inline uint64_t pacman_fixed_step_clock_sample(PacManFixedStepClock* clock,
                                                       uint64_t currentTicksMs) {
    if (!clock) return 0;
    const uint64_t elapsedMs = currentTicksMs - clock->previousTicksMs;
    clock->previousTicksMs = currentTicksMs;
    clock->totalElapsedMs = pacman_timing_saturating_add(clock->totalElapsedMs,
                                                         elapsedMs, 0);
    clock->accumulatedMs = pacman_timing_saturating_add(clock->accumulatedMs,
                                                        elapsedMs,
                                                        &clock->droppedMilliseconds);
    if (clock->accumulatedMs > kPacManMaximumBacklogMilliseconds) {
        const uint64_t excessMs = clock->accumulatedMs - kPacManMaximumBacklogMilliseconds;
        clock->accumulatedMs = kPacManMaximumBacklogMilliseconds;
        clock->droppedMilliseconds = pacman_timing_saturating_add(
            clock->droppedMilliseconds, excessMs, 0);
    }
    if (clock->accumulatedMs > clock->maximumBacklogMs) {
        clock->maximumBacklogMs = clock->accumulatedMs;
    }
    return elapsedMs;
}

static inline bool pacman_fixed_step_clock_has_step(const PacManFixedStepClock& clock) {
    return clock.accumulatedMs >= kPacManFixedStepMilliseconds;
}

static inline void pacman_fixed_step_clock_consume_step(PacManFixedStepClock* clock) {
    if (!clock || !pacman_fixed_step_clock_has_step(*clock)) return;
    clock->accumulatedMs -= kPacManFixedStepMilliseconds;
    ++clock->totalSteps;
}

static inline void pacman_fixed_step_clock_finish_frame(PacManFixedStepClock* clock,
                                                        uint32_t stepsThisFrame,
                                                        uint32_t maximumStepsPerFrame) {
    if (!clock) return;
    if (stepsThisFrame > clock->maximumStepsPerFrame) {
        clock->maximumStepsPerFrame = stepsThisFrame;
    }
    if (stepsThisFrame == maximumStepsPerFrame && pacman_fixed_step_clock_has_step(*clock)) {
        ++clock->catchUpFrames;
    }
}
