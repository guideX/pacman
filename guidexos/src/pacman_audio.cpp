#include "pacman_audio_runtime.h"

#include <guidexos/audio.h>

namespace {

static const uint32_t kWavReadBufferBytes = 70000u;
static const uint32_t kStartMusicFramesAtOldRate = 44100u; // 4.000 s at 11,025 Hz
static const uint32_t kStartMusicFramesAtLegacyRate = 64000u; // 4.000 s at 16 kHz
static const uint32_t kWakaBRate = 22050u;
static const uint32_t kWakaBFrames = 1323u; // 60 ms

struct PacManVoice {
    const unsigned char* pcm;
    uint32_t pcmBytes;
    uint32_t sampleRateHz;
    uint32_t channels;
    uint32_t bitsPerSample;
    bool valid;
};

static unsigned char g_wavReadBuffer[kWavReadBufferBytes];
static unsigned char g_startMusicPcm[kStartMusicFramesAtLegacyRate * 2u];
static unsigned char g_eatPillPcm[7063u * 2u];
static unsigned char g_powerPillPcm[4845u * 2u];
static unsigned char g_ghostEatenPcm[9046u * 2u];
static unsigned char g_killedPcm[16916u * 2u];
static unsigned char g_extraLifePcm[20940u * 2u];
static int16_t g_wakaBPcm[kWakaBFrames];
static PacManVoice g_voices[kPacManSoundCount];
static uint32_t g_audioFailureLogs;
static bool g_startMusicLegacyFallback;

static void clear_voice(PacManVoice* voice) {
    if (!voice) return;
    voice->pcm = 0;
    voice->pcmBytes = 0;
    voice->sampleRateHz = 0;
    voice->channels = 1u;
    voice->bitsPerSample = 0;
    voice->valid = false;
}

static bool read_wav(gx_app_context* ctx, const char* path, uint32_t* outBytes) {
    if (!ctx || !ctx->host || !ctx->host->file_read || !path || !outBytes) return false;
    uint32_t total = 0;
    while (total < sizeof(g_wavReadBuffer)) {
        const uint32_t request = sizeof(g_wavReadBuffer) - total > 8192u
            ? 8192u : static_cast<uint32_t>(sizeof(g_wavReadBuffer) - total);
        uint32_t received = 0;
        const gx_result result = ctx->host->file_read(ctx, path, total,
            g_wavReadBuffer + total, request, &received);
        if (result != GX_OK || received > request) return false;
        if (received == 0u) break;
        total += received;
        if (received < request) break;
    }
    *outBytes = total;
    return total > 0u;
}

static bool load_wav(gx_app_context* ctx, const char* path, unsigned char* pcm,
                     uint32_t capacityFrames, PacManVoice* voice, bool allowTruncate) {
    if (!voice) return false;
    clear_voice(voice);
    uint32_t wavBytes = 0;
    if (!read_wav(ctx, path, &wavBytes)) return false;
    PacManWavPcm info{};
    if (!pacman_audio_decode_wav(g_wavReadBuffer, wavBytes, pcm, capacityFrames,
                                 &info, allowTruncate)) return false;
    voice->pcm = pcm;
    voice->pcmBytes = info.frameCount * (info.bitsPerSample / 8u);
    voice->sampleRateHz = info.sampleRateHz;
    voice->channels = 1u;
    voice->bitsPerSample = info.bitsPerSample;
    voice->valid = true;
    return true;
}

static void synthesize_waka_b() {
    uint32_t phase = 0;
    const uint32_t step = 540u * 65536u / kWakaBRate;
    for (uint32_t index = 0; index < kWakaBFrames; ++index) {
        const int32_t triangle = phase < 32768u
            ? -32767 + static_cast<int32_t>(phase * 2u)
            : 98303 - static_cast<int32_t>(phase * 2u);
        const uint32_t attack = index < 24u ? (index + 1u) * 32768u / 24u : 32768u;
        const uint32_t decay = (kWakaBFrames - index) * 32768u / kWakaBFrames;
        const int32_t envelope = static_cast<int32_t>(attack < decay ? attack : decay);
        g_wakaBPcm[index] = static_cast<int16_t>(triangle * 9000 / 32768 * envelope / 32768);
        phase += step;
        if (phase >= 65536u) phase -= 65536u;
    }
}

static void set_voice(PacManSoundId id, const unsigned char* pcm, uint32_t bytes,
                      uint32_t rate, uint32_t bits) {
    PacManVoice& voice = g_voices[static_cast<uint32_t>(id)];
    voice.pcm = pcm;
    voice.pcmBytes = bytes;
    voice.sampleRateHz = rate;
    voice.channels = 1u;
    voice.bitsPerSample = bits;
    voice.valid = pcm != 0 && bytes != 0u;
}

static const PacManVoice* find_voice(PacManSoundId id) {
    if (static_cast<uint32_t>(id) >= kPacManSoundCount) return 0;
    return &g_voices[static_cast<uint32_t>(id)];
}

} // namespace

uint32_t pacman_audio_load_resources(gx_app_context* ctx) {
    for (uint32_t index = 0; index < kPacManSoundCount; ++index) clear_voice(&g_voices[index]);
    g_audioFailureLogs = 0;
    g_startMusicLegacyFallback = false;
    synthesize_waka_b();

    uint32_t loaded = 0;
    PacManVoice startMusic{};
    if (load_wav(ctx, "resources/audio/startmusicold.wav", g_startMusicPcm,
                 kStartMusicFramesAtOldRate, &startMusic, true)) {
        set_voice(kPacManSoundStartMusic, startMusic.pcm, startMusic.pcmBytes,
                  startMusic.sampleRateHz, startMusic.bitsPerSample);
        ++loaded;
    } else if (load_wav(ctx, "resources/audio/StartMusic.wav", g_startMusicPcm,
                        kStartMusicFramesAtLegacyRate, &startMusic, true)) {
        // Keep the former cue as a recovery path only when the preferred
        // startmusicold.wav cannot be loaded or decoded.
        set_voice(kPacManSoundStartMusic, startMusic.pcm, startMusic.pcmBytes,
                  startMusic.sampleRateHz, startMusic.bitsPerSample);
        g_startMusicLegacyFallback = true;
        ++loaded;
    }
    PacManVoice eatPill{};
    if (load_wav(ctx, "resources/audio/EatPill.wav", g_eatPillPcm,
                 7063u, &eatPill, false)) {
        // The legacy source calls EatPill for ordinary pellets. It is the
        // supplied A half of the native alternating waka pair.
        set_voice(kPacManSoundWakaA, eatPill.pcm, eatPill.pcmBytes,
                  eatPill.sampleRateHz, eatPill.bitsPerSample);
        ++loaded;
    }
    set_voice(kPacManSoundWakaB, reinterpret_cast<const unsigned char*>(g_wakaBPcm),
              sizeof(g_wakaBPcm), kWakaBRate, 16u);
    PacManVoice powerPill{};
    if (load_wav(ctx, "resources/audio/fruiteat.wav", g_powerPillPcm,
                 4845u, &powerPill, false)) {
        // VB6 plays this cue for both PowerPill and fruit collection.
        set_voice(kPacManSoundPowerPill, powerPill.pcm, powerPill.pcmBytes,
                  powerPill.sampleRateHz, powerPill.bitsPerSample);
        set_voice(kPacManSoundFruitEaten, powerPill.pcm, powerPill.pcmBytes,
                  powerPill.sampleRateHz, powerPill.bitsPerSample);
        ++loaded;
    }
    PacManVoice ghostEaten{};
    if (load_wav(ctx, "resources/audio/ghosteat.wav", g_ghostEatenPcm,
                 9046u, &ghostEaten, false)) {
        set_voice(kPacManSoundGhostEaten, ghostEaten.pcm, ghostEaten.pcmBytes,
                  ghostEaten.sampleRateHz, ghostEaten.bitsPerSample);
        ++loaded;
    }
    PacManVoice killed{};
    if (load_wav(ctx, "resources/audio/killed.wav", g_killedPcm,
                 16916u, &killed, false)) {
        set_voice(kPacManSoundKilled, killed.pcm, killed.pcmBytes,
                  killed.sampleRateHz, killed.bitsPerSample);
        ++loaded;
    }
    PacManVoice extraLife{};
    if (load_wav(ctx, "resources/audio/extralife.wav", g_extraLifePcm,
                 20940u, &extraLife, false)) {
        set_voice(kPacManSoundExtraLife, extraLife.pcm, extraLife.pcmBytes,
                  extraLife.sampleRateHz, extraLife.bitsPerSample);
        ++loaded;
    }
    return loaded;
}

PacManAudioSubmitResult pacman_audio_submit(void* userData, PacManSoundId sound) {
    gx_app_context* ctx = static_cast<gx_app_context*>(userData);
    const PacManVoice* voice = find_voice(sound);
    if (!ctx || !voice || !voice->valid) return kPacManAudioSilent;
    const gx_result result = gx_play_pcm(ctx, voice->pcm, voice->pcmBytes,
        voice->sampleRateHz, voice->channels, voice->bitsPerSample);
    if (result == GX_OK) {
        if (sound == kPacManSoundStartMusic && ctx->host && ctx->host->log) {
            ctx->host->log(ctx, g_startMusicLegacyFallback
                ? "PacMan start cue submitted: StartMusic.wav (fallback)"
                : "PacMan start cue submitted: startmusicold.wav");
        }
        return kPacManAudioAccepted;
    }
    if (result == GX_ERROR_BUSY) return kPacManAudioBusy;
    if (g_audioFailureLogs == 0u) {
        ++g_audioFailureLogs;
        if (ctx->host && ctx->host->log) {
            ctx->host->log(ctx, "PacMan audio unavailable; gameplay remains enabled");
        }
    }
    return kPacManAudioUnavailable;
}
