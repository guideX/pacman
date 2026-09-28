#include "game.h"
#include "pacman_audio_runtime.h"

#include <stdio.h>
#include <string.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct Recorder {
    PacManSoundId sounds[256];
    uint32_t count;
    PacManAudioSubmitResult result;
};

struct PcmCapture {
    struct Request {
        uint32_t bytes;
        uint32_t rate;
        uint32_t channels;
        uint32_t bits;
        uint32_t hash;
        unsigned char firstBytes[4];
    } requests[32];
    uint32_t count;
    gx_result result;
    bool failStartMusicOldRead;
    uint32_t logCount;
    char lastLog[128];
};

static gx_result GX_CALL read_asset(gx_app_context* context, const char* path, uint64_t offset,
                                    void* buffer, uint32_t capacity, uint32_t* outBytes) {
    if (!path || !buffer || !outBytes) return GX_ERROR_INVALID_ARGUMENT;
    const char* basename = path;
    for (const char* cursor = path; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') basename = cursor + 1;
    }
    PcmCapture* capture = static_cast<PcmCapture*>(context->userData);
    if (capture && capture->failStartMusicOldRead &&
        strcmp(basename, "startmusicold.wav") == 0) return GX_ERROR_FAILED;
    const std::string fullPath = std::string(PACMAN_AUDIO_ASSET_ROOT) + "/" + basename;
    std::ifstream input(fullPath.c_str(), std::ios::binary);
    if (!input) return GX_ERROR_FAILED;
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    input.read(static_cast<char*>(buffer), static_cast<std::streamsize>(capacity));
    const std::streamsize received = input.gcount();
    *outBytes = received > 0 ? static_cast<uint32_t>(received) : 0u;
    return GX_OK;
}

static gx_result GX_CALL capture_pcm(gx_app_context* context, const void* pcm,
                                     uint32_t bytes, uint32_t rate,
                                     uint32_t channels, uint32_t bits) {
    PcmCapture* capture = static_cast<PcmCapture*>(context->userData);
    if (!capture || !pcm || bytes == 0u) return GX_ERROR_INVALID_ARGUMENT;
    if (capture->count < sizeof(capture->requests) / sizeof(capture->requests[0])) {
        PcmCapture::Request& request = capture->requests[capture->count];
        request.bytes = bytes;
        request.rate = rate;
        request.channels = channels;
        request.bits = bits;
        uint32_t hash = 2166136261u;
        const unsigned char* samples = static_cast<const unsigned char*>(pcm);
        for (uint32_t index = 0; index < bytes; ++index) {
            hash = (hash ^ samples[index]) * 16777619u;
        }
        request.hash = hash;
        const uint32_t copyBytes = bytes < sizeof(request.firstBytes) ? bytes : sizeof(request.firstBytes);
        memcpy(request.firstBytes, pcm, copyBytes);
    }
    ++capture->count;
    return capture->result;
}

static gx_result GX_CALL record_app_log(gx_app_context* context, const char* message) {
    PcmCapture* capture = context ? static_cast<PcmCapture*>(context->userData) : 0;
    if (!capture || !message) return GX_ERROR_INVALID_ARGUMENT;
    ++capture->logCount;
    uint32_t index = 0;
    while (index + 1u < sizeof(capture->lastLog) && message[index]) {
        capture->lastLog[index] = message[index];
        ++index;
    }
    capture->lastLog[index] = '\0';
    return GX_OK;
}

static uint32_t hash_pcm(const unsigned char* samples, uint32_t bytes) {
    uint32_t hash = 2166136261u;
    for (uint32_t index = 0; index < bytes; ++index) {
        hash = (hash ^ samples[index]) * 16777619u;
    }
    return hash;
}

static uint32_t asset_pcm_hash(const char* name, uint32_t capacityFrames,
                               uint32_t* outBytes, uint32_t* outRate) {
    const std::string path = std::string(PACMAN_AUDIO_ASSET_ROOT) + "/" + name;
    std::ifstream input(path.c_str(), std::ios::binary);
    std::vector<unsigned char> wav((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
    std::vector<unsigned char> pcm(capacityFrames * 2u);
    PacManWavPcm info{};
    if (wav.empty() || !pacman_audio_decode_wav(wav.data(), static_cast<uint32_t>(wav.size()),
            pcm.data(), capacityFrames, &info, true)) return 0u;
    if (outBytes) *outBytes = info.frameCount * (info.bitsPerSample / 8u);
    if (outRate) *outRate = info.sampleRateHz;
    return hash_pcm(pcm.data(), info.frameCount * (info.bitsPerSample / 8u));
}

static PacManAudioSubmitResult record_sound(void* userData, PacManSoundId sound) {
    Recorder* recorder = static_cast<Recorder*>(userData);
    if (recorder->count < sizeof(recorder->sounds) / sizeof(recorder->sounds[0])) {
        recorder->sounds[recorder->count] = sound;
    }
    ++recorder->count;
    return recorder->result;
}

static bool expect(bool condition, const char* message) {
    if (condition) return true;
    printf("FAIL: %s\n", message);
    return false;
}

static void append_u16(std::vector<unsigned char>& bytes, uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8u));
}

static void append_u32(std::vector<unsigned char>& bytes, uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8u));
    bytes.push_back(static_cast<unsigned char>(value >> 16u));
    bytes.push_back(static_cast<unsigned char>(value >> 24u));
}

static void append_chunk(std::vector<unsigned char>& bytes, const char id[4],
                         const unsigned char* data, uint32_t size) {
    bytes.insert(bytes.end(), id, id + 4);
    append_u32(bytes, size);
    bytes.insert(bytes.end(), data, data + size);
    if (size & 1u) bytes.push_back(0);
}

static std::vector<unsigned char> make_wav(const unsigned char* samples, uint32_t frameCount) {
    std::vector<unsigned char> bytes;
    const unsigned char listChunk[] = { 'I', 'N', 'F' };
    const unsigned char junkChunk[] = { 1, 2, 3 };
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    append_u32(bytes, 0u);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E'});
    append_chunk(bytes, "LIST", listChunk, sizeof(listChunk));

    std::vector<unsigned char> fmt;
    append_u16(fmt, 1u);
    append_u16(fmt, 1u);
    append_u32(fmt, 16000u);
    append_u32(fmt, 16000u);
    append_u16(fmt, 1u);
    append_u16(fmt, 8u);
    append_chunk(bytes, "fmt ", fmt.data(), static_cast<uint32_t>(fmt.size()));
    append_chunk(bytes, "JUNK", junkChunk, sizeof(junkChunk));
    append_chunk(bytes, "data", samples, frameCount);
    const uint32_t riffSize = static_cast<uint32_t>(bytes.size() - 8u);
    bytes[4] = static_cast<unsigned char>(riffSize);
    bytes[5] = static_cast<unsigned char>(riffSize >> 8u);
    bytes[6] = static_cast<unsigned char>(riffSize >> 16u);
    bytes[7] = static_cast<unsigned char>(riffSize >> 24u);
    return bytes;
}

static bool test_wav_parser() {
    bool ok = true;
    const unsigned char source[] = { 0u, 128u, 255u, 64u, 192u };
    std::vector<unsigned char> wav = make_wav(source, sizeof(source));
    unsigned char output[8] = {};
    PacManWavPcm info{};
    ok &= expect(pacman_audio_decode_wav(wav.data(), static_cast<uint32_t>(wav.size()),
        output, sizeof(output), &info, false), "WAV chunks may precede fmt and have padding");
    ok &= expect(info.sampleRateHz == 16000u && info.bitsPerSample == 8u &&
        info.frameCount == sizeof(source) && !info.truncated,
        "WAV metadata decoded from PCM fmt/data chunks");
    ok &= expect(memcmp(output, source, sizeof(source)) == 0,
        "unsigned PCM8 payload is preserved for play_pcm");

    PacManWavPcm tooSmall{};
    ok &= expect(!pacman_audio_decode_wav(wav.data(), static_cast<uint32_t>(wav.size()),
        output, 3u, &tooSmall, false), "oversized WAV rejected unless truncation is explicit");
    ok &= expect(pacman_audio_decode_wav(wav.data(), static_cast<uint32_t>(wav.size()),
        output, 3u, &tooSmall, true) && tooSmall.frameCount == 3u && tooSmall.truncated,
        "bounded start-music truncation is explicit and reported");

    std::vector<unsigned char> malformed = wav;
    malformed[52] = 0xFFu;
    malformed[53] = 0xFFu;
    malformed[54] = 0xFFu;
    malformed[55] = 0x7Fu;
    ok &= expect(!pacman_audio_decode_wav(malformed.data(), static_cast<uint32_t>(malformed.size()),
        output, sizeof(output), &info, false), "truncated RIFF chunk bounds are rejected");
    return ok;
}

static bool test_supplied_wav_assets() {
    struct ExpectedAsset {
        const char* name;
        uint32_t rate;
        uint32_t frames;
    };
    static const ExpectedAsset assets[] = {
        { "ghosteat.wav", 16000u, 9046u },
        { "EatPill.wav", 22050u, 7063u },
        { "startmusicold.wav", 11025u, 46490u },
        { "StartMusic.wav", 16000u, 68226u },
        { "extralife.wav", 11025u, 20940u },
        { "killed.wav", 11025u, 16916u },
        { "fruiteat.wav", 11025u, 4845u },
    };
    bool ok = true;
    unsigned char decoded[70000] = {};
    for (uint32_t index = 0; index < sizeof(assets) / sizeof(assets[0]); ++index) {
        const std::string path = std::string(PACMAN_AUDIO_ASSET_ROOT) + "/" + assets[index].name;
        std::ifstream input(path.c_str(), std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                         std::istreambuf_iterator<char>());
        PacManWavPcm info{};
        const bool loaded = input.good() || input.eof();
        const bool decodedOk = loaded && !bytes.empty() &&
            pacman_audio_decode_wav(bytes.data(), static_cast<uint32_t>(bytes.size()),
                                    decoded, sizeof(decoded), &info, false);
        ok &= expect(decodedOk, "supplied WAV parses with the runtime RIFF decoder");
        if (decodedOk) {
            ok &= expect(info.sampleRateHz == assets[index].rate && info.bitsPerSample == 8u &&
                         info.frameCount == assets[index].frames && !info.truncated,
                         "supplied WAV format matches inspected PCM8 mono metadata");
        }
    }
    return ok;
}

static bool test_runtime_resource_load_and_play_pcm() {
    bool ok = true;
    PcmCapture capture{};
    capture.result = GX_OK;
    gx_host_calls host{};
    host.size = sizeof(host);
    host.file_read = read_asset;
    host.play_pcm = capture_pcm;
    host.log = record_app_log;
    gx_app_context context{};
    context.size = sizeof(context);
    context.host = &host;
    context.userData = &capture;

    const uint32_t loaded = pacman_audio_load_resources(&context);
    ok &= expect(loaded == 6u, "runtime loads all six selected original WAV resources once");
    PacManAudioState audio{};
    pacman_audio_initialize(&audio);
    pacman_audio_start_session(&audio, pacman_audio_submit, &context);
    uint32_t preferredBytes = 0u;
    uint32_t preferredRate = 0u;
    const uint32_t preferredHash = asset_pcm_hash("startmusicold.wav", 44100u,
                                                   &preferredBytes, &preferredRate);
    ok &= expect(capture.count == 1u && capture.requests[0].bytes == 44100u &&
        capture.requests[0].bytes == preferredBytes && capture.requests[0].hash == preferredHash &&
        capture.requests[0].rate == 11025u && capture.requests[0].rate == preferredRate &&
        capture.requests[0].channels == 1u &&
        capture.requests[0].bits == 8u && memcmp(capture.requests[0].firstBytes, "RIFF", 4u) != 0 &&
        capture.logCount == 1u &&
        strcmp(capture.lastLog, "PacMan start cue submitted: startmusicold.wav") == 0,
        "start music submits startmusicold.wav raw PCM8 capped at four seconds, not the WAV container");

    GameState game{};
    game.normalPillConsumed = true;
    pacman_audio_process_game_events(&audio, &game, pacman_audio_submit, &context);
    game.normalPillConsumed = true;
    pacman_audio_process_game_events(&audio, &game, pacman_audio_submit, &context);
    ok &= expect(capture.count == 3u && capture.requests[1].bytes == 7063u &&
        capture.requests[1].rate == 22050u && capture.requests[1].bits == 8u &&
        capture.requests[2].bytes == 1323u * 2u && capture.requests[2].rate == 22050u &&
        capture.requests[2].bits == 16u,
        "alternating pellet cues submit the original asset and generated PCM tone");

    memset(&game, 0, sizeof(game));
    game.powerPillConsumed = true;
    pacman_audio_process_game_events(&audio, &game, pacman_audio_submit, &context);
    ok &= expect(capture.requests[3].bytes == 4845u && capture.requests[3].rate == 11025u,
        "power-pill event submits source-mapped fruiteat PCM");
    memset(&game, 0, sizeof(game));
    game.ghostEaten[0] = true;
    pacman_audio_process_game_events(&audio, &game, pacman_audio_submit, &context);
    ok &= expect(capture.requests[4].bytes == 9046u && capture.requests[4].rate == 16000u,
        "ghost-eaten event submits ghosteat PCM");
    memset(&game, 0, sizeof(game));
    game.fruitConsumed = true;
    pacman_audio_process_game_events(&audio, &game, pacman_audio_submit, &context);
    ok &= expect(capture.requests[5].bytes == 4845u && capture.requests[5].rate == 11025u,
        "fruit event submits the historical fruiteat PCM cue");
    memset(&game, 0, sizeof(game));
    game.extraLifeAwarded = true;
    pacman_audio_process_game_events(&audio, &game, pacman_audio_submit, &context);
    ok &= expect(capture.requests[6].bytes == 20940u && capture.requests[6].rate == 11025u,
        "extra-life event submits extralife PCM");
    memset(&game, 0, sizeof(game));
    game.deathEntered = true;
    pacman_audio_process_game_events(&audio, &game, pacman_audio_submit, &context);
    ok &= expect(capture.requests[7].bytes == 16916u && capture.requests[7].rate == 11025u,
        "death event submits killed PCM");

    capture.result = GX_ERROR_UNSUPPORTED;
    PacManAudioState failedAudio{};
    pacman_audio_initialize(&failedAudio);
    const uint32_t beforeFailure = capture.count;
    pacman_audio_start_session(&failedAudio, pacman_audio_submit, &context);
    memset(&game, 0, sizeof(game));
    game.normalPillConsumed = true;
    pacman_audio_process_game_events(&failedAudio, &game, pacman_audio_submit, &context);
    ok &= expect(failedAudio.disabled && capture.count == beforeFailure + 1u,
        "runtime play_pcm failure is logged once and later requests are not retried");
    return ok;
}

static bool test_runtime_start_music_legacy_fallback() {
    bool ok = true;
    PcmCapture capture{};
    capture.result = GX_OK;
    capture.failStartMusicOldRead = true;
    gx_host_calls host{};
    host.size = sizeof(host);
    host.file_read = read_asset;
    host.play_pcm = capture_pcm;
    host.log = record_app_log;
    gx_app_context context{};
    context.size = sizeof(context);
    context.host = &host;
    context.userData = &capture;

    const uint32_t loaded = pacman_audio_load_resources(&context);
    PacManAudioState audio{};
    pacman_audio_initialize(&audio);
    pacman_audio_start_session(&audio, pacman_audio_submit, &context);

    uint32_t legacyBytes = 0u;
    uint32_t legacyRate = 0u;
    const uint32_t legacyHash = asset_pcm_hash("StartMusic.wav", 64000u,
                                                &legacyBytes, &legacyRate);
    ok &= expect(loaded == 6u && capture.count == 1u &&
        capture.requests[0].bytes == 64000u && capture.requests[0].bytes == legacyBytes &&
        capture.requests[0].rate == 16000u && capture.requests[0].rate == legacyRate &&
        capture.requests[0].bits == 8u && capture.requests[0].hash == legacyHash &&
        capture.logCount == 1u && strcmp(capture.lastLog,
            "PacMan start cue submitted: StartMusic.wav (fallback)") == 0,
        "StartMusic.wav is submitted only when startmusicold.wav cannot be read");
    return ok;
}

static bool test_event_mapping_and_alternation() {
    bool ok = true;
    PacManAudioState audio{};
    pacman_audio_initialize(&audio);
    Recorder recorder{};
    recorder.result = kPacManAudioAccepted;
    pacman_audio_start_session(&audio, record_sound, &recorder);
    pacman_audio_start_session(&audio, record_sound, &recorder);
    ok &= expect(recorder.count == 1u && recorder.sounds[0] == kPacManSoundStartMusic,
        "start music is submitted once for a session");

    GameState game{};
    game.normalPillConsumed = true;
    pacman_audio_process_game_events(&audio, &game, record_sound, &recorder);
    game.normalPillConsumed = false;
    game.normalPillConsumed = true;
    pacman_audio_process_game_events(&audio, &game, record_sound, &recorder);
    ok &= expect(recorder.sounds[1] == kPacManSoundWakaA &&
        recorder.sounds[2] == kPacManSoundWakaB,
        "normal pellet events alternate waka A and B");

    game.normalPillConsumed = false;
    game.powerPillConsumed = true;
    game.ghostEaten[2] = true;
    game.fruitConsumed = true;
    game.extraLifeAwarded = true;
    game.extraLifeAwardsThisUpdate = 2u;
    pacman_audio_process_game_events(&audio, &game, record_sound, &recorder);
    ok &= expect(recorder.count == 8u && recorder.sounds[3] == kPacManSoundPowerPill &&
        recorder.sounds[4] == kPacManSoundGhostEaten &&
        recorder.sounds[5] == kPacManSoundFruitEaten &&
        recorder.sounds[6] == kPacManSoundExtraLife &&
        recorder.sounds[7] == kPacManSoundExtraLife,
        "power pill, ghost, fruit, and each extra-life event are mapped");

    memset(&game, 0, sizeof(game));
    game.normalPillConsumed = true;
    game.deathEntered = true;
    pacman_audio_process_game_events(&audio, &game, record_sound, &recorder);
    ok &= expect(recorder.sounds[8] == kPacManSoundKilled && recorder.count == 9u,
        "death cue suppresses a same-tick pellet sound");

    pacman_audio_reset_waka(&audio);
    memset(&game, 0, sizeof(game));
    game.normalPillConsumed = true;
    pacman_audio_process_game_events(&audio, &game, record_sound, &recorder);
    ok &= expect(recorder.sounds[9] == kPacManSoundWakaA,
        "level reset returns waka alternation to A");

    pacman_audio_reset_for_new_session(&audio);
    pacman_audio_start_session(&audio, record_sound, &recorder);
    ok &= expect(recorder.sounds[10] == kPacManSoundStartMusic && recorder.count == 11u,
        "new session resets one-shot start-music state");
    return ok;
}

static bool test_failure_is_silent_and_bounded() {
    bool ok = true;
    PacManAudioState audio{};
    pacman_audio_initialize(&audio);
    Recorder recorder{};
    recorder.result = kPacManAudioUnavailable;
    pacman_audio_start_session(&audio, record_sound, &recorder);
    GameState game{};
    game.normalPillConsumed = true;
    pacman_audio_process_game_events(&audio, &game, record_sound, &recorder);
    ok &= expect(audio.disabled && recorder.count == 1u,
        "an unavailable audio backend disables later requests without retrying");

    const uint32_t countBefore = recorder.count;
    for (uint32_t index = 0; index < 10000u; ++index) {
        game.normalPillConsumed = true;
        pacman_audio_process_game_events(&audio, &game, record_sound, &recorder);
    }
    ok &= expect(recorder.count == countBefore && sizeof(audio) <= 8u,
        "repeated pellet events keep fixed audio state and make no backend queue");

    // Compare a real game update with and without a failed audio callback.
    GameState withAudio{};
    GameState withoutAudio{};
    game_initialize(&withAudio);
    game_initialize(&withoutAudio);
    game_begin_initial_ready(&withAudio);
    game_begin_initial_ready(&withoutAudio);
    for (uint32_t step = 0; step < 100u; ++step) {
        game_update(&withAudio);
        pacman_audio_process_game_events(&audio, &withAudio, record_sound, &recorder);
        game_update(&withoutAudio);
    }
    ok &= expect(withAudio.simulationSteps == withoutAudio.simulationSteps &&
        withAudio.playState == withoutAudio.playState && withAudio.score == withoutAudio.score &&
        withAudio.lives == withoutAudio.lives && withAudio.pacman.x == withoutAudio.pacman.x &&
        withAudio.pacman.y == withoutAudio.pacman.y,
        "audio failure does not change gameplay state or timing");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= test_wav_parser();
    ok &= test_supplied_wav_assets();
    ok &= test_runtime_resource_load_and_play_pcm();
    ok &= test_runtime_start_music_legacy_fallback();
    ok &= test_event_mapping_and_alternation();
    ok &= test_failure_is_silent_and_bounded();
    if (!ok) return 1;
    printf("PacMan audio tests passed.\n");
    return 0;
}
