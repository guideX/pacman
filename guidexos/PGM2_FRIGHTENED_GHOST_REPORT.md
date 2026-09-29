# Pac-Man PGM2 Frightened-Ghost Report

## Outcome

**Outcome B.** Frightened mode, combo scoring, reset boundaries, tests, and both
Native ELF builds are coherent. The isolated guideXOS host run registered the
PGM2 validation app but rejected `gui.start` with `Unknown command`, so no
compositor or Pac-Man window was created. No visual gameplay proof is claimed.

## Starting Git state

- Repository: `D:\dev\pacman`
- Branch: `master`; upstream: `origin/master`, ahead/behind `0/0`
- Starting HEAD: `cd73dd8bca016c61f3c7e31c0afbd04b44a480ab`
- The request named `ba21310543ff58c9c1e2f74e38ea5aa1921325c7`; that PGM1
  commit was the parent of the actual starting HEAD.
- Worktree, index, and untracked-file list were clean at task start.
- The PGM1 report described audio edits as uncommitted. In this checkout those
  edits were already included in starting commit `cd73dd8`; PGM2 left audio
  runtime code unchanged and only added focused ghost-eat event tests.

PGM1's tracked generated artifacts remain: `build-pgm1-native/`,
`build-pgm1-runtime/`, and the two `captures/pacman-pgm1-ready-input-*-validation.txt`
summaries. The older tracked `diagnostic-power/` and `diagnostic-stage/`
outputs are from an earlier milestone. I left these tracked artifacts intact.
The guideXOS Server checkout also had the untracked PGM1 package
`Apps/PacManPGM1ReadyInputValidation/` and two PGM1 raw logs; the PGM1 package
stages under `D:\Apps` remain external artifacts. None were edited or deleted.

Two task-created temporary build roots remain because recursive cleanup was
rejected by tool policy:

- `C:\Users\guideX\AppData\Local\Temp\pacman-pgm2-tests-271a6ede512f45d9bf3b1f50d994d7fa`
- `C:\Users\guideX\AppData\Local\Temp\pacman-pgm2-validation-27b9169bc4164481a072a3fb73d34b16`

The second contains the copied source/build trees, staged production and
validation packages, isolated server copy, and runtime log/summary. No PGM2
build output was placed in the Pac-Man repository.

## Reference and native starting behavior

I read `PGM1_GAMEPLAY_PARITY_REPORT.md`,
`HISTORICAL_POWER_PILL_INVESTIGATION.md`, `HISTORICAL_GHOST_AI_INVESTIGATION.md`,
and the reachable VB6 paths in `basPacman.bas`, `basGhostAI.bas`,
`basPacSetUp.bas`, and `frmPacMan.frm`.

| Behavior before PGM2 | Classification | Finding |
| --- | --- | --- |
| Power-pellet activation | Correct | Consuming the cell removes it and triggers one state entry; only active ghosts outside their house are eligible. |
| Frightened duration | Correct | Fixed 10 ms simulation updates; level 1 through 8 uses 900 down to 200 updates at speed 1, divided by the historical game-speed setting. Rendering cadence does not control it. |
| Direction reversal | Correct | Each eligible ghost reverses immediately on an energizer. |
| Frightened movement | Correct | Legal deterministic turns use the source's reversed target signs; the source itself has no random frightened chooser. |
| Frightened speed | Correct | Ghost movement skips every second AI update, matching the source's half-speed delay. |
| Appearance and expiration warning | Correct | Existing PacPics graphics provide the blue body and eyes. Ghosts return to normal after expiry and flash near the source threshold. |
| Collision | Correct | Center distance uses the strict `<16` rule. Frightened ghosts are eaten; normal ghosts are lethal; eaten/returning ghosts are non-lethal. Ghost-index order makes overlap resolution deterministic. |
| Combo values | Correct | Successive eaten ghosts score 200, 400, 800, and 1600, capped at the fourth value. |
| Combo reset on another energizer | Correct | A new pellet restarts eligible timers at full duration, reverses them again, and resets the chain. It does not add time to the old timers. House ghosts and eaten/returning ghosts are excluded, matching the reference. |
| Eaten, return, and revival | Correct, with a documented deterministic difference | The eyes state returns toward the house gate, performs a renewal bounce, then becomes normal and collision-active. The native route is wall-safe and uses deterministic Left where VB6 uses unseeded `Rnd`. |
| Combo reset on frightened expiry | Partially implemented | Ghost timers expired correctly, but the chain could remain set after the last frightened timer ended. |
| Death/reset | Partially implemented | Death cleared ghost conditions and timers, but the combo and frightened flash phase remained set until later reset. New game and level reset already cleared them. |
| Ghost-eat score popup | Absent, also absent in the reference | The VB6 source has no temporary score popup; PGM2 did not add a new rendering system. |
| Ghost-eaten audio | Correct | The one-shot `ghostEaten` event maps to `ghosteat.wav`; PGM2 preserved the mapping. |

The reference sets `PPTimer = (1000 - 100 * level) / Game.Speed`, reverses
eligible ghosts on each pellet, and resets `GhostsEaten` on each new pellet.
Its flash threshold is `PPTimer < 200 / Game.Speed`, with the existing 16-step
phase. Eaten ghosts clear their timer, double speed, return to `(224,184)`, and
renew through the house. The reference leaves its combo counter unchanged on
death and level reset; PGM2 follows the requested native reset behavior instead.

## PGM2 changes

- When a frightened timer expires, the combo now resets only after no other
  ghost remains frightened. This preserves the chain while any eligible ghost
  can still be eaten.
- Entering Dying clears the chain and flash phase immediately. The flash phase
  also stops advancing once the update leaves `Playing`.
- The hosted power-pill harness now expects the chain to be zero after the last
  frightened timer expires.
- Deterministic tests cover exact per-ghost score progression, second-pellet
  timer restart and reversal, combo retention until the last timer expires,
  expiry/death/new-game resets, death during frightened mode, death after an
  eaten ghost, warning-threshold edges, return/revival, and one-shot
  `ghosteat.wav` audio. Existing PGM1 Ready/held-input tests remain in the game
  suite.

## Tests and builds

The PGM1 baseline was game `35 groups / 313 checks` and audio
`6 groups / 27 checks`. Final totals are:

- Game: **35 groups / 318 checks — passed**
- Audio: **6 groups / 29 checks — passed**

Both test targets were freshly built and run from an external temporary CMake
directory. The production `pacman-native` target built as a static ELF64
x86-64 executable. `llvm-readobj` reported `EM_X86_64`; the isolated production
package contained `app.json`, the correctly referenced
`bin/amd64/pacman.elf`, both graphics resources, and all seven WAV assets. The
separate `pacman-danger-validation` target also built and staged successfully.
All package/build output was outside the repository; no generated build output
was staged.

## Runtime approach and evidence

The existing supported hosted validation path is the PowerShell harness:
start a server, issue `gui.start`, launch the app with `desktop.launch`, then
use `gui.sync`/`gui.unfreeze` and keyboard input for synchronized captures.
`AppRegistry::DefaultSources()` uses an `Apps` folder under the server's current
directory when present. I used a copied stable `guideXOSServer.exe` and the PGM2
validation package in a unique temporary server root; the registry scanned one
manifest and registered `Nexgen PacMan Danger Validation`.

The server printed `gui.start` in its command list, then returned
`Unknown command` when the harness sent it. No compositor window appeared, so
`desktop.launch` was never reached. The harness cleanup reported zero remaining
owned server processes. The older experimental server had the same command
failure in the PGM1 report. This is a guideXOS command/runtime boundary, not a
Pac-Man gameplay failure.

**Evidence separation:** game-state transitions and audio mapping were observed
in unit tests; Native ELF and package correctness were observed in builds and
package inspection; no Pac-Man frame, input, frightened sprite, score, or return
animation was observed in a live GUI session.

## Remaining gaps and PGM3 candidates

- PGM3 completed the source-backed fruit lifecycle and added a bounded,
  native-only fruit score popup. This presentation choice does not change the
  VB6 parity finding that the reference has no score popups.
- Repeat the visual PGM2 scenario after the guideXOS host provides an
  operational GUI command path.
- Consider persistent high-score storage as a separate design.
- Treat ghost score popups, title/coin/credit flow, attract mode, and two-player
  play as explicit product choices; the inspected source does not define them.
- Preserve the deterministic eyes-return route and the lack of a shared
  Chase/Scatter schedule unless a later phase chooses to change those
  source-backed boundaries.
