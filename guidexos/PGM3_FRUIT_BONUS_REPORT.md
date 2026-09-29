# PGM3 Fruit / Bonus Item Gameplay and Scoring Polish

## Outcome

**Outcome A.** The reference-backed fruit lifecycle remains deterministic, the
native score popup is bounded, focused tests pass, production and hosted Native
ELFs build, and the isolated GuideXOS hosted runtime displayed both the Level 1
cherry and the collected 500-point feedback. The hosted run also exercised the
Level 4, 5, and 8 item mappings and expiration, then closed the app window with
no owned server process left running.

## Starting Git state

- Repository: `D:\dev\pacman`
- Branch: `master`
- Starting HEAD: `7e6bd79053f0c63170b7368964b877cd7aa2d4d5`
- Worktree: clean; no staged, unstaged, or untracked files
- Upstream: `origin/master`; one commit ahead, zero behind
- No unrelated user changes were present or included.
- During the phase, the local `origin/master` tracking ref was updated to the
  PGM2 HEAD (`7e6bd79`) by a push recorded in its reflog at 20:10 local time.
  Before the PGM3 commit, `master` and that tracking ref were equal (0 ahead,
  0 behind); this phase had not pushed any PGM3 work at that point.

## Reference and starting implementation

Read `PGM1_GAMEPLAY_PARITY_REPORT.md`,
`PGM2_FRIGHTENED_GHOST_REPORT.md`, and
`HISTORICAL_FRUIT_AND_EXTRA_LIFE_INVESTIGATION.md`, then checked their cited VB6
fruit path in `frmPacMan.frm` and `basPacSetUp.bas`. The existing native game
already had the reference-backed trigger, single per-level appearance, fixed
maze location, timeout, one-time collection, level art/value mapping, death
pause, audio event, history icons, and reset on level/new-game. Fruit timing is
based on active gameplay callbacks, not pellet count. One lifecycle gap was
that fruit remained in a frozen state through Game Over. Temporary fruit score
feedback was absent, as it also is in the VB6 source.

| Area | Native state before PGM3 | PGM3 result |
| --- | --- | --- |
| Spawn | Correct: one trigger after 4,000 speed-adjusted active 10 ms updates per level | Preserved and hosted-validated |
| Location | Correct: logical center `(232,280)` | Preserved and visually observed |
| Lifetime | Correct: expires at source count 5,000, 1,000 active updates after the trigger at speed 1 | Preserved; Level 4/5/8 expiration observed |
| Collection and score | Correct: active fruit only, one award through centralized score handling | Preserved; repeat overlap and gating covered |
| Level item and artwork | Correct: eight source cells in existing `PacPics` sprite/mask asset | Preserved and hosted-validated at Levels 1/4/5/8 |
| Audio | Correct: existing fruit-eaten game event routes to `fruiteat.wav` | Preserved; full game-to-audio path tested once |
| Ordinary death | Correct: visible fruit and its timer pause through death/Ready; spawn opportunity stays consumed | Preserved; transient popup is cleared on death |
| Level transition | Correct: timer and spawn opportunity reset for the next level | Preserved and tested |
| Game Over | Partial: frozen fruit state remained after the final death | Now clears fruit, timers, and per-level spawn state |
| New game | Correct: fruit state resets | Preserved and tested |
| Score popup | Absent in native and reference | Added native-only exact-value popup for 100 gameplay steps |
| HUD | Correct: reserve lives and fruit history icons are present | Preserved; extra-life HUD state checked in runtime |

The reference fruit callback increments `TimeCount` while gameplay is active.
At speed 1, it triggers at 4,000 callbacks and the source removes it when a
later callback begins with count 5,000. This means one fruit opportunity per
level and a 1,000-step visible interval. Death/Ready disables that callback,
so visible fruit and its timer pause; `FruitGone` prevents the opportunity from
reopening. The source collision clears active fruit before awarding score.

The reference selects artwork with
`XP = ((Level - 1) Mod 4) * 32` and
`YP = ((Level - 1) \ 4) * 32 + 256`; its mask is 128 pixels to the right in the
same sprite sheet. It awards `500 * Level`, capped by the source's eight-level
content. Native uses the existing fixed-size sprite/mask cells and mapping:

| Level | Item | Type | Score |
| ---: | --- | ---: | ---: |
| 1 | Cherry | 0 | 500 |
| 2 | Strawberry | 1 | 1,000 |
| 3 | Orange | 2 | 1,500 |
| 4 | Apple | 3 | 2,000 |
| 5 | Melon | 4 | 2,500 |
| 6 | Galaxian | 5 | 3,000 |
| 7 | Bell | 6 | 3,500 |
| 8 and above | Key | 7 | 4,000 |

## Changes

- Added `FruitPhase::ScorePopup` feedback at the fruit's position using the
  exact awarded score. It advances only during Playing and expires after 100
  simulation steps. Death, level reset, Game Over, and new-game reset clear it.
- Preserved ordinary-death fruit behavior: a visible item and its active-time
  counter survive the death/Ready sequence, and the already-used per-level
  opportunity does not repeat.
- Clear active fruit, all fruit timers, and the spawn marker on Game Over.
- Extended validation-frame diagnostics with item value and popup time. The
  hosted harness now holds the actual spawned fruit visible long enough for a
  synchronized capture, then collects it through `game_update` and lets the
  popup expire before checking the higher-level cases.
- Made the local Native ELF output directory a CMake cache path so isolated
  validation builds can keep generated output outside the checkout.

## Tests and builds

- Game logic: **35 groups / 331 checks — passed**. The PGM2 baseline was 318;
  PGM3 adds 13 checks without removing coverage: all eight level mappings,
  pre-spawn/expired collision rejection, exact popup duration, and death-popup
  reset behavior.
- Audio: **6 groups / 30 checks — passed**. The PGM2 baseline was 29; the new
  integrated check runs the real fruit collision through `game_update` and the
  audio event processor, verifies 500 points, and confirms one `fruiteat`
  event across continued overlap.
- Production amd64 Native ELF: **passed**, staged in the isolated server-root
  package.
- Fruit validation amd64 Native ELF and package: **passed**, with test hooks
  kept in the separate `PacManFruitValidation` package.
- `git diff --check`: **passed**; only the repository's LF/CRLF working-copy
  notices appeared.
- PGM1 Ready/input and PGM2 frightened/ghost regressions are included in the
  complete passing game and audio test binaries.

## Hosted validation and live GUI evidence

The validation script used the isolated GuideXOS server root, hosted package,
and capture directory. Inspection of the current GuideXOS `server.cpp`
confirmed `gui.start` is a supported command. This server build consumes the
first redirected console line during startup, so the isolated runner primes
the input stream before sending it; no GuideXOS source or production binary was
changed. The compositor window's native class is `GXOS_COMPOSITOR` and its
window title is blank in this build, so the runner locates it by class.

All hosted assertions passed:

- Level 1 active fruit and coordinates `(232,280)`, score value 500;
- collection through the game logic, score 10,000, lives 4, and one extra-life
  award;
- exact 500-point popup, followed by popup expiration;
- Level 4 Apple / 2,000 points, Level 5 Melon / 2,500 points, and Level 8 Key /
  4,000 points;
- expiration at each tested level and native window cleanup with zero remaining
  windows;
- server wrapper exited with code 0 and zero owned server processes remained.

The isolated captures are under
`C:\Users\guideX\AppData\Local\Temp\pacman-pgm3-validation-047979cb9da94468b980503f9bb5d112\captures`.
The visible cherry and collected-score captures were inspected. They show the
actual GuideXOS compositor rendering the Pac-Man app, the cherry at the maze
center before collection, and the yellow `500` feedback with the updated score
and reserve-life HUD afterward. The run uses the repository's deterministic
hosted validation hook, which positions Pac-Man and advances the real game
logic; ordinary keyboard movement was not part of this scripted fruit run.

Evidence is separated as follows:

- **Implementation/tests:** unit assertions cover threshold and timer logic,
  active-only one-time collision, mapping, audio event, popup, and reset paths.
- **Hosted validation:** native frame diagnostics and fruit milestones confirm
  exact values and state transitions; synchronized compositor captures confirm
  visible rendering and popup presentation.
- **Live GuideXOS GUI:** the fruit-validation Native ELF ran in the real hosted
  compositor and displayed the captured cherry and score feedback. Pac-Man was
  pinned by the deterministic harness, so ordinary keyboard movement was not
  claimed as live evidence.

## Temporary artifacts and remaining work

The PGM3 validation root remains intact at
`C:\Users\guideX\AppData\Local\Temp\pacman-pgm3-validation-047979cb9da94468b980503f9bb5d112`.
It contains isolated build trees, staged production/validation packages, the
temporary experimental server, logs, and captures. The two PGM2 temporary roots
listed in the request were left untouched.

Fruit has no identified gameplay parity gap. The bounded score popup is a
native presentation enhancement because the VB6 source does not display score
popups. A later phase could separately examine ordinary keyboard movement in
the current hosted compositor, persistent high score, and broader Ready/level
presentation polish; none blocks PGM3 fruit behavior.

## Git result

The PGM3 commit hash and normal push result are reported in the task completion
summary.
