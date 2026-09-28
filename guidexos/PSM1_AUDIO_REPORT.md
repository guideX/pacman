# Pac-Man Audio Integration — PSM1

## Outcome

**Outcome B — substantially complete.** Pac-Man loads the original WAV resources, dispatches sounds from real gameplay event flags through the guideXOS App Model, and passes application-side tests. The package builds and stages correctly. A Pac-Man-specific runtime capture proving that its PCM reached the HDA backend was not obtained in this phase; the available QEMU audio evidence is from the generic guideXOS AudioBeep proof, not Pac-Man. The desktop already had a QEMU process running, so this phase did not take over its VM or shared firmware state.

## Starting repository state

- Repository: `D:\dev\pacman`
- Branch: `master`, upstream `origin/master`
- Starting HEAD: `196aaea97a6ed5a9d1f78643973d8c84a8e8ed50`
- Starting worktree: clean; ahead/behind `0/0`
- Native app: `guidexos/`, built with CMake/Ninja into a freestanding amd64 Native ELF and staged under `/Apps/PacMan`.
- The parent directory holds the historical VB6 project and all seven original WAV files.

## Original audio state and API

The native C++ port had no audio playback code or WAV loader. The historical VB6 project used Windows `sndPlaySound`; Pac-Man does not call that Windows API. guideXOS Server already exposes the application capability `audio.output` and the appended `play_pcm` host call. Its audio contract accepts mono 8-bit unsigned or 16-bit signed PCM at 8–48 kHz, mixes bounded voices, and allows voices from separate applications to overlap.

The PSM1 app-side WAV reader walks RIFF chunks and optional padding, validates PCM format fields, and strips the WAV container before playback. It does not assume a 44-byte header. Original 8-bit samples are passed through without sample-format conversion. Static application-owned buffers keep the decoded PCM valid; the App Model copies accepted PCM into its bounded mixer voices. Completed voices are reclaimed by the host. The guideXOS runtime tags voices with the application owner and stops that owner's voices during application cleanup, so Pac-Man does not stop another app's audio.

## WAV inventory

All seven files are PCM format tag 1, unsigned 8-bit, mono. The data sizes below are also frame counts because each frame is one byte.

| WAV | File bytes | PCM data bytes | Rate | Bits | Channels | Duration | App Model input |
|---|---:|---:|---:|---:|---:|---:|---|
| `ghosteat.wav` | 9,090 | 9,046 | 16,000 Hz | 8 | 1 | 0.565375 s | Direct PCM8 |
| `EatPill.wav` | 7,156 | 7,063 | 22,050 Hz | 8 | 1 | 0.320317 s | Direct PCM8 |
| `startmusicold.wav` | 46,592 | 46,490 | 11,025 Hz | 8 | 1 | 4.216780 s | Direct PCM8; unused |
| `StartMusic.wav` | 68,318 | 68,226 | 16,000 Hz | 8 | 1 | 4.264125 s | First 4.000 s submitted |
| `extralife.wav` | 21,248 | 20,940 | 11,025 Hz | 8 | 1 | 1.899320 s | Direct PCM8 |
| `killed.wav` | 17,008 | 16,916 | 11,025 Hz | 8 | 1 | 1.534331 s | Direct PCM8 |
| `fruiteat.wav` | 5,120 | 4,845 | 11,025 Hz | 8 | 1 | 0.439456 s | Direct PCM8 |

`StartMusic.wav` exceeds the App Model's four-second per-voice limit, so the application submits its first 64,000 samples (4 seconds at 16 kHz); the final 0.264125 seconds are omitted. `startmusicold.wav` remains staged but unused. The original VB6 `StartMusic` chooses `StartMusic.wav` when `Game.Enhanced` is true and `startmusicold.wav` otherwise. The native app has no legacy/non-Enhanced mode, so it uses `StartMusic.wav` only.

## Event mapping

The original source changes two expected mappings: `basPacman.bas` plays `EatPill.wav` for an ordinary pellet and `fruiteat.wav` for a power pellet. `basPacSetUp.bas` also plays `fruiteat.wav` for fruit collection. PSM1 follows those verified gameplay mappings.

| Gameplay event | Submitted sound |
|---|---|
| New game start; Enter/Space restart after Game Over | `StartMusic.wav`, once per session start |
| Ordinary pellet | Alternating waka A/B: supplied `EatPill.wav` for A; original synthesized 60 ms triangle PCM for B |
| Power pellet consumed | `fruiteat.wav` (the historical source cue) |
| Frightened ghost eaten | `ghosteat.wav` |
| Fruit collected | `fruiteat.wav` |
| Each extra life awarded | `extralife.wav` |
| Pac-Man death begins | `killed.wav` |

Waka alternation advances only on actual normal-pellet events and resets on a new session or level reset. A death suppresses a same-update waka request. Start music begins at the existing `InitialReady` transition. Gameplay begins after the existing 450 fixed steps (9 seconds at 20 ms per step); audio never delays that timer. No loop or continuous frightened-mode track was added.

## Failure, lifetime, and queue bounds

WAVs are loaded once before the game loop. PCM buffers and the generated waka B buffer have static lifetime; there are no per-pellet allocations and no app-side pending queue. The guideXOS mixer has 16 bounded voices; when it returns `GX_ERROR_BUSY`, the event is dropped without retry. A missing resource stays silent. If the host reports audio unavailable, unsupported, permission denied, or another non-busy error, Pac-Man disables further audio requests and logs one concise diagnostic. No audio result changes simulation state or timing.

`play_pcm` is fire-and-forget. Gameplay audio is requested after fixed-step simulation updates, not from rendering, and no code sleeps or waits for a sound. The supplied cues are one-shots; there is no continuing Pac-Man-owned gameplay loop to stop at death. Application shutdown relies on the App Model's per-owner voice cleanup.

## Build, tests, and runtime evidence

- `cmake --build build`: passed; produced a static `elf64-x86-64` Pac-Man executable and staged the manifest, images, and all seven WAV files under `D:\Apps\PacMan`.
- `pacman-audio-tests.exe`: passed, 5 groups / 26 checks. It parses all seven real WAVs, exercises the runtime loader with a file-read test host, captures submitted raw PCM with a fake `play_pcm` host, checks sound/event sizes and formats, verifies waka alternation and one-shot start music, and checks backend failure behavior.
- `pacman-game-tests.exe`: passed, 34 groups / 300 existing checks.
- All production and validation manifests request `audio.output`.
- Existing guideXOS proof artifacts dated 2026-09-22 report an HDA backend (`hda-48000-stereo-s16`), an App Model audio self-test with `dma=PASS`, and an AudioBeep app proof. The QEMU capture contains a nonzero PCM16 stereo 44.1 kHz payload (4,612,052 bytes; 542,188 nonzero samples). This verifies the generic guideXOS backend path, not Pac-Man's sound requests.
- No Pac-Man launch, real gameplay input, Pac-Man `play_pcm` acceptance, or Pac-Man-to-HDA capture was completed. Human audibility was not tested. Therefore the generic server proof and fake-host tests are not presented as Pac-Man backend playback proof.

## Remaining audio work

The main remaining proof is an isolated guideXOS runtime run of this Pac-Man package with a fresh QEMU HDA capture while moving through actual gameplay events. A human listening check can follow that machine proof. A later polish pass could revisit the clipped tail of `StartMusic.wav` if the App Model offers a longer per-voice limit or a reliable sequenced-playback facility.
