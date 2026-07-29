# Historical fruit and extra-life investigation

## Classification

Both systems are source-supported and functional in the original VB6 project.
Fruit is classification **A: complete fruit system exists**. The source has
appearance timing, level selection, artwork, collision, score, and expiration.
Extra lives are classification **A: explicit extra-life behavior exists**.
The source checks three explicit score crossings and calls `ExtraLife`.

Inspected source: `PacMan.vbp`, `frmPacMan.frm`/`.frx`, `basPacSetUp.bas`,
`basPacman.bas`, `basGhostAI.bas`, `PacPics.bmp`, `Level1.bmp`,
`fruiteat.wav`, and `extralife.wav`. The BMPs are the authoritative artwork;
the WAVs are evidence of source-side events only and are not ported here.

## Fruit source trace

`basPacSetUp.bas:67-88` declares `FruitHere`, `FruitGone`, `TimeCount`,
`Level`, `Score`, and `Lives` in `UDTPacman`. `frmPacMan.frm:50-54` sets
`tmrKeyBoard.Interval = 10`, so one source fruit timer unit is one 10 ms
keyboard/game callback at normal speed.

`frmPacMan.frm:539-608` contains the complete fruit path. It runs movement,
ghosts, and `TestCollisions`, then checks expiration, increments `TimeCount`,
handles death and level completion, and finally draws fruit.

At the default `Game.Speed = 1`, fruit appears when `TimeCount` reaches 4000,
is drawn at sprite top-left `(216,264)`, is logically centered at `(232,280)`,
and expires when a later callback begins with `TimeCount = 5000`. Its visible
interval is 1000 deterministic 10 ms steps. The source uses integer
`4000 / Game.Speed` and `5000 / Game.Speed`; the native port preserves that
bounded division. The timer is independent of pill and power-pill counts,
non-random, and allows one appearance per level because `FruitGone` prevents a
second one.

The source keyboard timer is disabled during Ready/death handling and Game
Over, so native fruit timing pauses outside `Playing`. No render-frequency or
wall-clock timer is used.

`basPacSetUp.bas:246-267` shows the transparency operation. Fruit uses
`SRCAND` from `XP + 128` at the same `YP`, followed by `SRCPAINT` from `XP,YP`.
Each cell is 32×32. `PacPics.bmp` is 256×352 and supplies the real fruit and
mask artwork.

## Exact level mapping

`frmPacMan.frm:595-601` computes `XP = ((Level - 1) Mod 4) * 32` and
`YP = ((Level - 1) \ 4) * 32 + 256`; the mask is `XP + 128`, `YP`.

| Level | Type | Artwork | Sprite | Mask | Score |
| ---: | ---: | --- | --- | --- | ---: |
| 1 | 0 | Cherry | `(0,256)` | `(128,256)` | 500 |
| 2 | 1 | Strawberry | `(32,256)` | `(160,256)` | 1000 |
| 3 | 2 | Orange | `(64,256)` | `(192,256)` | 1500 |
| 4 | 3 | Apple | `(96,256)` | `(224,256)` | 2000 |
| 5 | 4 | Melon | `(0,288)` | `(128,288)` | 2500 |
| 6 | 5 | Galaxian | `(32,288)` | `(160,288)` | 3000 |
| 7 | 6 | Bell | `(64,288)` | `(192,288)` | 3500 |
| 8 | 7 | Key | `(96,288)` | `(224,288)` | 4000 |

`basPacSetUp.bas:455-465` explicitly scores `500 * Pacman.Level`; these
values are not inferred from common arcade tables. The existing source level
path saturates above 8, and native `calculate_fruit_rules` safely returns the
Level 8 Key mapping for untrusted levels.

`basPacSetUp.bas:405-467` scans ghosts first with the strict ghost threshold,
then checks fruit with `Abs(Pacman.Xpos - 232) < 16` and exact
`Pacman.Ypos = 280`. Fruit cannot kill Pac-Man, does not affect the frightened
ghost chain, and scores once because the source clears `FruitHere` before
calling `AddScore`. Native preserves this strict-X/exact-Y quirk and makes the
fruit phase non-collidable before centralized scoring.

There is no fruit score popup in the source. Native therefore does not add a
familiar but unsupported popup phase to gameplay.

## Death, level, and update behavior

`frmPacMan.frm:611-665` does not assign `FruitHere`, `FruitGone`, or
`TimeCount` in `PacDied`. A visible fruit therefore remains logically present
and its timer pauses while the keyboard timer is disabled for death and Ready.
Native preserves that ordinary-death behavior. Fruit is non-collidable during
Game Over.

`frmPacMan.frm:571-584` resets `TimeCount` and rebuilds the clean maze at level
completion, but does not explicitly clear every fruit flag. The clean
background removes the artwork while stale logical flags could otherwise
allow an invisible collision. Native clears explicit fruit state at the
LevelComplete boundary, records the source omission, and selects the new
level's mapping deterministically.

The prior native level milestone's final-consumable look-ahead returns before
movement/collision. That preserved ordering means a final-pill step can
suppress a fruit collision or trigger. On ordinary Playing steps the native
order is pill look-ahead, Pac-Man movement, ghosts, ghost collision, fruit
collision, fruit timer/trigger, state timers, and dirty render. This matches the
source's ghost-loop-before-fruit branch for same-step ghost/fruit encounters;
the source-supported final-pill boundary remains the existing native quirk.

## Extra-life source trace

`basPacSetUp.bas:469-503` is the complete scoring/life path:

```text
if Score >= 10000 and Score - addTo < 10000: ExtraLife
if Score >= 50000 and Score - addTo < 50000: ExtraLife
if Score >= 100000 and Score - addTo < 100000: ExtraLife
```

The checks are independent, so one large score addition can award all three.
`AddScore 0` cannot award a life. There are no later/scaled thresholds, source
award flags, maximum-life check, or source suppression at a life maximum;
`ExtraLife` simply increments `Pacman.Lives`. In practical source gameplay the
three fixed crossings mean at most three awards per session. Native adds a
uint8 bound at 255 to prevent overflow and records suppression if that bound is
reached.

`frmPacMan.frm:492-500` initializes three lives and zero score. `ShowLives` in
`basPacSetUp.bas:128-154` draws `Lives - 1` reserve Pac-Man icons from source
rectangle `(96,224)` and level-history fruit icons from the fruit cells. Native
keeps total current lives in `GameState::lives` but shows reserve lives in its
compact numeric HUD. Native does not play `extralife.wav`.

Score, lives, and implicit threshold progress survive ordinary death and level
completion. New-game initialization/restart clears score, lives, fruit
trigger progress, and life-award progress. Game Over cannot award a new life.

## Native implementation and tests

`FruitState` is fixed-size and explicit: phase, type, appearance count,
deterministic timer, score, and logical center. `LifeAwardState` tracks crossed
thresholds, grants, and the next threshold. `award_score` is the common
bounded scoring path for pills, power pills, frightened ghosts, fruit, and the
level-clear bonus.

Gated diagnostics report fruit trigger, spawn/type/score/timer, expiration,
collection, score award, reset boundaries, threshold crossing, award, and
maximum-life suppression. Validation-only frames carry fruit phase, type,
coordinates, timer, appearance count, threshold mask, and award count.

`tests/game_logic_test.cpp` covers initial/early/exact timer boundaries, all
eight mappings and safe saturation, collision and single scoring,
death/level/Game Over/restart interactions, centralized scoring, saturation,
threshold equality/jumps/repetition, maximum-life behavior, and regression
coverage for pills, ghosts, frightened mode, and ten level transitions.
