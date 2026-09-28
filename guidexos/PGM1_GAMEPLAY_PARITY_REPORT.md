# Pac-Man PGM1 Gameplay Parity Report

## Outcome

**Outcome B.** The source audit is complete, held-direction continuity through
Ready/death/level transitions is implemented, the deterministic game and
audio suites pass, and both production and isolated validation Native ELF
packages build. I could not exercise the new input behavior in a running app:
the installed experimental guideXOS server printed `Unknown command` for its
advertised `gui.start` command and did not create a compositor. The app never
reached runtime launch. No QEMU process or image was stopped or overwritten.

## Starting state

- Repository: `D:\dev\pacman`
- Branch: `master`
- Starting HEAD: `24a805112d538ac26c1c10633b0b71519339e72b`
- Upstream: `origin/master`; ahead/behind `0/0`
- Starting worktree: pre-existing audio work was present in
  `guidexos/CMakeLists.txt`, `guidexos/PSM1_AUDIO_REPORT.md`,
  `guidexos/README.md`, `guidexos/src/pacman_audio.cpp`,
  `guidexos/src/pacman_audio.h`, and `guidexos/tests/audio_test.cpp`, plus the
  untracked `guidexos/PSM2_AUDIO_RUNTIME_REPORT.md`. PGM1 did not edit or stage
  those files.
- Recent Pac-Man commits included `24a8051` (“Pac-Man: add complete game
  audio”), `838b2ff` (“Pacman stable in bare metal”), `0065f05` (four-ghost
  movement milestone), `cde6202` (Cyan ghost movement milestone), and
  `8ebfc5b` (stationary ghosts and lives milestone).
- Standard configure/build commands documented by the repository:

  ```powershell
  cmake -S . -B build -G Ninja -DGUIDEXOS_SERVER_ROOT=D:/dev/guideXOSServer -DGUIDEXOS_PACKAGE_ROOT=D:/Apps
  cmake --build build
  .\build\pacman-game-tests.exe
  .\build\pacman-audio-tests.exe
  ```

- Confirmed pre-PGM1 test baseline: game `34 groups / 300 checks`; audio
  `6 groups / 27 checks` with the phase-start audio work in the worktree.

## Reference implementation inspected

The project file `PacMan.vbp` names the reachable VB6 gameplay modules. I
reviewed `frmPacMan.frm`, `basPacSetUp.bas`, `basPacman.bas`, and
`basGhostAI.bas`, along with the existing source investigations:

- `HISTORICAL_GHOST_AI_INVESTIGATION.md`
- `HISTORICAL_POWER_PILL_INVESTIGATION.md`
- `HISTORICAL_FRUIT_AND_EXTRA_LIFE_INVESTIGATION.md`
- `HISTORICAL_LEVEL_PROGRESSION.md`
- `HISTORICAL_SESSION_AND_HUD_INVESTIGATION.md`
- `PSM1_AUDIO_REPORT.md` and `PSM2_AUDIO_RUNTIME_REPORT.md`

The source-backed findings below agree with the current native code and
existing deterministic tests; they are not inferred from arcade conventions.

## Parity inventory

| System | Classification | Findings |
| --- | --- | --- |
| Title, credits, and new game | Intentionally omitted / behaviorally different | The VB6 title and coin/credit flow, one/two-player selection, and attract path are not part of the standalone native app. Native starts with a bounded `InitialReady` screen. |
| Ready, death, respawn, Game Over, restart | Present and working, with scoped differences | Death retains score, consumed pellets, and level; a remaining life respawns through Ready. Game Over and Enter/Space restart are native single-player substitutes for the source's return-to-title flow. |
| Pause | Intentionally omitted | No pause/resume key or pause state was found in the reachable VB6 source. Focus loss is handled separately and clears input. |
| Pac-Man movement | Present; previously incomplete at state boundaries | Native uses fixed 10 ms steps, legal tile turns, buffered requests, wall checks, and tunnel wrapping. VB6 samples `GetAsyncKeyState` after Ready resumes. Native previously discarded keydown outside `Playing`, losing a held key; PGM1 fixes this for initial, post-death, and post-level Ready. |
| Normal and power pellets | Present and working | The logical level has 240 normal and four power pellets. Both score 10, are removed once, and survive ordinary death; level reset restores all 244. Completion is a one-shot transition. |
| Ghost house and AI | Present; deterministic difference | All four source ghosts and their distinct targeting/release routines are implemented. The native version replaces unseeded VB6 `Rnd` choices with deterministic choices and uses bounded wall-safe eyes return. The source has no shared Chase/Scatter schedule. |
| Frightened mode and ghost combos | Present and working | Per-ghost eligibility/timers, reversal, half-rate movement, blue/flashing appearance, ghost eating, return, and 200/400/800/1600 scoring are present. Energizers reset the combo. The native level reset also clears it; the source leaves the counter untouched, but no ghost can score again before another energizer resets it. |
| Fruit | Present and working | Source timing, level mapping, lifetime, collision/scoring, death pause, and one-time collection are implemented. Levels above eight safely use the Level 8 mapping, matching the source's level cap. |
| Score and HUD | Present; scoped visual differences | Pill/ghost/fruit/bonus scoring is centralized and bounded. High score is in-memory, initialized to 10,000, and survives a Game Over restart. Reserve lives and fruit history are displayed. The source has no score popups, persistent high-score file, or textual level label. |
| Level progression | Present and working; intentionally limited content | Levels advance and cap at eight, apply the source frightened-duration rule, and reset pellets/actors. The legacy project itself rebuilds the same maze each level and has no second maze asset. |
| Ready/power-pellet visuals | Present; one intentional approximation | Ready timing/text is represented. Native adds a shared 500 ms power-pill visibility blink; the source draws pills once and removes them only on consumption. |
| Game loop and performance | Present and deterministic | Simulation advances on monotonic time in fixed 10 ms steps with bounded catch-up. Rendering and audio submission are outside game logic; no per-update resource load or unbounded gameplay queue was found. |
| Diagnostics and validation hooks | Present and isolated | Hosted/bare-metal test behavior is behind compile-time options, off in production, and staged under separate package names. Existing smoke/validation helpers remain available. |
| Audio | Present; not changed in PGM1 | Existing App Model routing still uses `startmusicold.wav` first, `StartMusic.wav` as fallback, alternating waka, and event-backed power/ghost/fruit/death/life cues. The four-second host voice limit remains platform behavior. |

## Important gap and selected PGM1 change

The VB6 form disables its 10 ms keyboard timer during `Ready!`, then reenables
it. `PacmanMovement` polls held arrow keys when the actor is aligned. A key
still held as Ready ends is therefore seen on the first gameplay update. The
native event handler previously rejected all directional input outside
`Playing`, so the same held key was lost at startup, after respawn, and after a
level reset.

PGM1 now records arrow key state during `InitialReady`, `Dying`,
`ReadyAfterDeath`, and `LevelComplete` without moving Pac-Man in those states.
Only directions still held at the resume boundary carry forward; releasing a
key during Ready removes that temporary request. Death and level reset retain
live held-key state across actor reset. Game Over still ignores arrows, and a
new session starts with clean input. If initial Ready has a held direction,
Pac-Man resumes facing right and advances to the first aligned point where the
buffered request can be checked against the maze.

Focused tests cover initial Ready movement blocking and resume, a released
Ready tap, held input through death and respawn, fallback when one of multiple
held keys is released, Game Over rejection, restart Ready, and held input
through level reset. Existing intersection/wall tests continue to cover legal
buffered turning.

## Tests and builds

- `cmake --build build-baremetal --target pacman-game-tests
  pacman-audio-tests`: passed.
- `pacman-game-tests.exe`: passed, `35 groups / 313 checks`.
- `pacman-audio-tests.exe`: passed, `6 groups / 27 checks`.
- Production Native ELF: configured in `build-pgm1-native` and
  `pacman-native` built successfully. The production package was staged at the
  isolated `D:\Apps\PacManPGM1NativeValidation` path; the normal
  `D:\Apps\PacMan` package was not overwritten.
- Hosted input-validation ELF: `pacman-danger-validation` built successfully
  with diagnostics and the existing isolated danger-test hook. Its package
  was staged separately for a runtime attempt; that hook is absent from the
  production ELF.
- `git diff --check`: passed; Git emitted only the repository's existing
  LF/CRLF working-copy warnings.

## Runtime proof and regressions

The isolated hosted input probe started `guideXOSServer.experimental.exe` and
the registry recognized the PGM1 validation package. The server executable on
this machine was last written on September 21, 2026. It then printed
`Unknown command (help for list)` after `gui.start`; no compositor window was
created, so no Pac-Man window, input, frame, or visual result was exercised.
The validation script cleaned up only its own server process tree. Runtime
gameplay proof remains incomplete until a compatible experimental server is
available.

| Evidence type | Result |
| --- | --- |
| Unit/focused logic | Held-input transitions and existing game rules passed in 35/313 checks. |
| Audio regression | 6/27 audio checks passed; PGM1 did not edit audio code or audio tests. The seven original WAV assets were staged in the production package. |
| Native package build | Production and isolated hosted validation ELFs linked and staged successfully. |
| Runtime behavior | Not exercised; compositor startup failed before app launch. |
| Visual observation | None for this PGM1 change. |
| QEMU | No existing QEMU process or disk image was modified. |

The existing PSM2 report remains the source for the earlier start-cue/HDA
runtime proof. PGM1's attempted hosted run does not add runtime audio evidence.

## Deferred PGM2+ candidates

- Run the held-Ready input scenario on a current experimental guideXOS server
  and record a compositor frame showing movement after Ready.
- Keep the single-player standalone scope unless a later phase explicitly
  chooses to add the legacy coin/credit title flow, two-player state, or
  attract/demo behavior.
- Consider persistent high score only as a separate storage design; the VB6
  reference keeps it in memory only.
- Continue using the Nexgen ghost behavior as the target. Do not add an
  arcade-style shared Chase/Scatter schedule absent from the reference.
- Treat pause, numeric level text, score popups, multiple maze content, and
  enhanced transition effects as new product choices rather than parity gaps;
  the inspected source does not define those behaviors.

## Final result

The audit found that most high-value gameplay systems already exist and are
source-backed. PGM1 fixes the remaining held-input gap across the gameplay
Ready transitions, with deterministic regression coverage and a successful
production package build. Outcome B reflects only the uncompleted hosted
runtime demonstration; the specific boundary is the installed server's
`gui.start` command failure before Pac-Man launch.
