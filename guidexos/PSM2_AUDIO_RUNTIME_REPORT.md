# Pac-Man Audio Runtime Verification — PSM2

## Result

The native Pac-Man package now uses `startmusicold.wav` as its normal new-session and Game Over restart cue. The earlier PSM1 choice of `StartMusic.wav` was changed before this runtime run, as authorized. `StartMusic.wav` remains staged and is used only if the preferred WAV cannot be loaded or decoded.

The runtime source selection and App Model submission were verified in an isolated guideXOS QEMU session with HDA output enabled. The Pac-Man log `PacMan start cue submitted: startmusicold.wav` is emitted only after `gx_play_pcm` returns `GX_OK`. In the same serial log, the runtime reports `backend=hda-48000-stereo-s16`, reads `/Apps/PacMan/resources/audio/startmusicold.wav` (46,592 bytes) through `file_read`, and reports the audio resources loaded. The QEMU HDA capture from that session has a nonzero PCM16 stereo 44.1 kHz payload.

The run therefore ties the `startmusicold.wav` submission to the active HDA output and its capture. The capture also includes guideXOS boot self-test audio and backend resampling/mixing. A byte-exact or sample-level fingerprint of the Pac-Man cue was not recovered from that mixed capture, so source identity is verified by the app's accepted-submission log and runtime file-read evidence rather than by matching the output waveform back to the WAV.

## Start-cue asset and duration

`startmusicold.wav` is unsigned 8-bit mono PCM at 11,025 Hz. Its WAV data contains 46,490 frames (4.216780 seconds). The per-voice App Model limit remains four seconds, so Pac-Man submits its first 44,100 samples and omits the final 2,390 samples (0.216780 seconds). The cap was not changed for PSM2.

The cut falls while the waveform is still active: the final 10 ms before the cut has about 49 PCM counts RMS from the unsigned midpoint, while the omitted tail averages about 36.5 counts RMS and fades to near silence only at the original file's end. This shortens the natural ending and is likely audible as a shortened tail. The opening and the full four-second body remain intact, so the cue's start identity is not materially compromised. This is a waveform-based assessment; no separate human listening check was performed.

## Current PSM1 event mapping

| Gameplay event | Submitted sound |
|---|---|
| New session; Enter/Space restart after Game Over | `startmusicold.wav`; `StartMusic.wav` only if the preferred cue fails to load or decode |
| Ordinary pellets | Alternating `EatPill.wav` and the original synthesized approximately 60 ms PCM effect |
| Power pellet consumed | `fruiteat.wav` |
| Frightened ghost eaten | `ghosteat.wav` |
| Fruit collected | `fruiteat.wav` |
| Extra life | `extralife.wav` |
| Pac-Man death | `killed.wav` |

Both WAVs remain staged in the Native ELF package. The global guideXOS voice-duration setting was not changed.

## Verification and capture artifacts

- `cmake --build build --target pacman-audio-tests pacman-native`: passed.
- `pacman-audio-tests.exe`: passed. The test compares the complete runtime submission hash with the first 44,100 decoded bytes of `startmusicold.wav`, checks 11,025 Hz mono PCM8 and the four-second size, and exercises `StartMusic.wav` only as a forced-load-failure fallback.
- The isolated QEMU serial evidence is at `D:\temp\pacman-psm2-startmusic-audio-final-20260927\qemu-serial.log`.
- The corresponding HDA capture is at `D:\temp\pacman-psm2-startmusic-audio-final-20260927\hda-capture.wav` (2,155,396 bytes; approximately 12.219 seconds of captured output). QEMU left the RIFF/data size fields unset, but the captured payload is present and nonzero.
- A copy with corrected RIFF/data length fields for playback is at `D:\temp\pacman-psm2-startmusic-audio-final-20260927\hda-capture-playable.wav`; the original capture was left intact.
- Both original assets remain packaged. The staged `startmusicold.wav` SHA-256 matches the repository asset: `A6E1C14F6CBC59CB5244C0119C728D412252370EEA48BC574C1307A774C83CAF`.
