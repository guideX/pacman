#pragma once

#include <stdint.h>

#include "game_state.h"

enum PacManSoundId : uint8_t {
    kPacManSoundStartMusic = 0,
    kPacManSoundWakaA,
    kPacManSoundWakaB,
    kPacManSoundPowerPill,
    kPacManSoundGhostEaten,
    kPacManSoundKilled,
    kPacManSoundFruitEaten,
    kPacManSoundExtraLife,
    kPacManSoundCount
};

struct PacManWavPcm {
    uint32_t sampleRateHz;
    uint32_t bitsPerSample;
    uint32_t frameCount;
    bool truncated;
};

struct PacManAudioState {
    bool disabled;
    bool startMusicPlayed;
    bool nextWakaIsA;
};

enum PacManAudioSubmitResult : uint8_t {
    kPacManAudioAccepted = 0,
    kPacManAudioBusy,
    kPacManAudioUnavailable,
    kPacManAudioSilent
};

typedef PacManAudioSubmitResult (*PacManAudioSubmit)(void* userData, PacManSoundId sound);

inline uint16_t pacman_audio_u16le(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8u));
}

inline uint32_t pacman_audio_u32le(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

// Decode uncompressed, mono PCM from any valid RIFF chunk order. The caller
// owns the destination for the complete voice lifetime. Optional chunks and
// odd-size chunk padding are walked according to RIFF instead of assuming a
// 44-byte WAV header. When allowTruncate is true, the first capacityFrames
// are copied and reported as truncated; this is used only for StartMusic,
// whose 4.264 s source exceeds the App Model's four-second voice limit.
inline bool pacman_audio_decode_wav(const unsigned char* bytes, uint32_t size,
                                    unsigned char* outPcm, uint32_t capacityFrames,
                                    PacManWavPcm* outInfo, bool allowTruncate) {
    if (!bytes || !outPcm || !outInfo || size < 12u || capacityFrames == 0u) return false;
    if (bytes[0] != 'R' || bytes[1] != 'I' || bytes[2] != 'F' || bytes[3] != 'F' ||
        bytes[8] != 'W' || bytes[9] != 'A' || bytes[10] != 'V' || bytes[11] != 'E') return false;

    const uint32_t riffSize = pacman_audio_u32le(bytes + 4u);
    if (riffSize > 0xFFFFFFF7u) return false;
    const uint32_t riffEnd = riffSize + 8u;
    if (riffEnd < 12u || riffEnd > size) return false;

    bool haveFormat = false;
    uint16_t formatTag = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bits = 0;
    const unsigned char* data = 0;
    uint32_t dataBytes = 0;
    uint32_t position = 12u;
    while (position <= riffEnd && riffEnd - position >= 8u) {
        const unsigned char* chunk = bytes + position;
        const uint32_t chunkSize = pacman_audio_u32le(chunk + 4u);
        const uint32_t body = position + 8u;
        if (body > riffEnd || chunkSize > riffEnd - body) return false;

        if (chunk[0] == 'f' && chunk[1] == 'm' && chunk[2] == 't' && chunk[3] == ' ') {
            if (chunkSize < 16u) return false;
            formatTag = pacman_audio_u16le(bytes + body);
            channels = pacman_audio_u16le(bytes + body + 2u);
            sampleRate = pacman_audio_u32le(bytes + body + 4u);
            byteRate = pacman_audio_u32le(bytes + body + 8u);
            blockAlign = pacman_audio_u16le(bytes + body + 12u);
            bits = pacman_audio_u16le(bytes + body + 14u);
            haveFormat = true;
        } else if (chunk[0] == 'd' && chunk[1] == 'a' && chunk[2] == 't' && chunk[3] == 'a' && !data) {
            data = bytes + body;
            dataBytes = chunkSize;
        }

        const uint32_t paddedSize = chunkSize + (chunkSize & 1u);
        if (paddedSize < chunkSize || paddedSize > riffEnd - body) return false;
        position = body + paddedSize;
    }
    if (!haveFormat || !data || formatTag != 1u || channels != 1u ||
        sampleRate < 8000u || sampleRate > 48000u || (bits != 8u && bits != 16u)) return false;

    const uint32_t bytesPerFrame = bits / 8u;
    if (blockAlign != bytesPerFrame || byteRate != sampleRate * bytesPerFrame) return false;
    if (dataBytes == 0u || dataBytes % bytesPerFrame != 0u) return false;
    const uint32_t sourceFrames = dataBytes / bytesPerFrame;
    if (sourceFrames == 0u || (sourceFrames > capacityFrames && !allowTruncate)) return false;
    const uint32_t frames = sourceFrames > capacityFrames ? capacityFrames : sourceFrames;
    const uint32_t copyBytes = frames * bytesPerFrame;
    const uint32_t dataOffset = static_cast<uint32_t>(data - bytes);
    if (dataOffset > size || copyBytes > size - dataOffset) return false;
    for (uint32_t i = 0; i < copyBytes; ++i) outPcm[i] = data[i];

    outInfo->sampleRateHz = sampleRate;
    outInfo->bitsPerSample = bits;
    outInfo->frameCount = frames;
    outInfo->truncated = frames != sourceFrames;
    return true;
}

inline void pacman_audio_initialize(PacManAudioState* state) {
    if (!state) return;
    state->disabled = false;
    state->startMusicPlayed = false;
    state->nextWakaIsA = true;
}

inline void pacman_audio_emit(PacManAudioState* state, PacManSoundId sound,
                              PacManAudioSubmit submit, void* userData) {
    if (!state || state->disabled || !submit) return;
    const PacManAudioSubmitResult result = submit(userData, sound);
    if (result == kPacManAudioUnavailable) state->disabled = true;
}

inline void pacman_audio_start_session(PacManAudioState* state,
                                       PacManAudioSubmit submit, void* userData) {
    if (!state || state->startMusicPlayed) return;
    state->startMusicPlayed = true;
    state->nextWakaIsA = true;
    pacman_audio_emit(state, kPacManSoundStartMusic, submit, userData);
}

inline void pacman_audio_reset_for_new_session(PacManAudioState* state) {
    if (!state) return;
    const bool disabled = state->disabled;
    state->startMusicPlayed = false;
    state->nextWakaIsA = true;
    state->disabled = disabled;
}

inline void pacman_audio_reset_waka(PacManAudioState* state) {
    if (state) state->nextWakaIsA = true;
}

// Called once after each fixed simulation update. Only semantic event flags
// cause requests; a death suppresses a same-tick pellet sound. No playback
// result is fed back into GameState or gameplay timing.
inline void pacman_audio_process_game_events(PacManAudioState* state,
                                             const GameState* game,
                                             PacManAudioSubmit submit,
                                             void* userData) {
    if (!state || !game) return;
    if (game->levelReset) pacman_audio_reset_waka(state);

    if (game->normalPillConsumed) {
        const PacManSoundId waka = state->nextWakaIsA ? kPacManSoundWakaA : kPacManSoundWakaB;
        state->nextWakaIsA = !state->nextWakaIsA;
        if (!game->deathEntered) pacman_audio_emit(state, waka, submit, userData);
    }
    if (game->powerPillConsumed) {
        pacman_audio_emit(state, kPacManSoundPowerPill, submit, userData);
    }
    for (uint32_t index = 0; index < 4u; ++index) {
        if (game->ghostEaten[index]) pacman_audio_emit(state, kPacManSoundGhostEaten, submit, userData);
    }
    if (game->fruitConsumed) pacman_audio_emit(state, kPacManSoundFruitEaten, submit, userData);
    if (game->extraLifeAwarded) {
        const uint32_t awards = game->extraLifeAwardsThisUpdate ? game->extraLifeAwardsThisUpdate : 1u;
        for (uint32_t index = 0; index < awards && index < 3u; ++index) {
            pacman_audio_emit(state, kPacManSoundExtraLife, submit, userData);
        }
    }
    if (game->deathEntered) pacman_audio_emit(state, kPacManSoundKilled, submit, userData);
}
