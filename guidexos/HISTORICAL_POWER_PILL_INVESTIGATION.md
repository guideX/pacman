# Historical Nexgen Power-Pill Investigation

Investigation date: 2026-07-26

The local VB6 source is authoritative. The reachable power-pill path is
`basPacman.bas:73-88`, `basGhostAI.bas:73-181`, and
`basPacSetUp.bas:44-55, 283-346, 372-453`; `frmPacMan.frm:539-584` supplies
the 10 ms update order and the level/death lifecycle.

## Exact source behavior

`UDTGhost.PPTimer` is a signed VB6 `Integer` containing remaining 10 ms AI
updates, not milliseconds. It starts at zero in every `DefaultPositions`
ghost initializer. When Pac-Man consumes a power pill, each ghost satisfying
`Eyesonly = False And InGame` receives:

```text
(1000 - 100 * Pacman.Level) / Game.Speed
```

The level path caps `Pacman.Level` at 8. With the default `Game.Speed = 1`,
the durations are 900, 800, 700, 600, 500, 400, 300, and 200 fixed updates
(9.0 through 2.0 seconds). The pre-game speed toggle supports 2 and 4 as
well; integer durations therefore become 450…100 and 225…50 updates.

`AIGhostMonsters` runs once from the 10 ms `tmrKeyBoard` callback. For every
ghost, movement is processed and then `PPTimer` is decremented once when it is
positive. Thus activation and the first decrement occur in the same callback.
Timers are independent and stop when the keyboard timer is disabled by Ready,
death, level completion, or Game Over.

Only ghosts already outside the house are activated. House-contained and
still-releasing ghosts receive neither a timer nor a frightened visual state.
Eyes-only ghosts are excluded. A later power pill resets every eligible
ghost's timer to the full duration and reverses it again; it does not extend or
add to the old timer. `Pacman.GhostsEaten` is reset to zero by each power pill.

The reversal is an immediate `Direction = Rev(Direction)` assignment. At an
aligned junction, the frightened branch negates the selected target signs,
then uses the same legal-direction order and reverse exclusion as the normal
branch. There is no random frightened movement, frightened target, or PRNG in
the source. The `DelayTime` toggle skips every second movement update while
`PPTimer > 0`, producing half the normal movement rate; it is not a separate
speed value.

The source classifies the following behaviors as explicit and directly
portable:

| Subsystem | Historical evidence | Classification |
| --- | --- | --- |
| Per-ghost timer and eligibility | `basPacman.bas:80-88`, `UDTGhost.PPTimer` | Explicit and directly portable |
| Timer duration and decrement | `basGhostAI.bas:163-181`, 10 ms form timer | Explicit and directly portable |
| Immediate reversal | `basPacman.bas:85-86` | Explicit and directly portable |
| Frightened movement | `basGhostAI.bas:119-124, 163-181` | Explicit and directly portable |
| Blue/fr flashing | `basPacSetUp.bas:372-400` | Explicit and directly portable |
| Ghost-eating score | `frmPacMan.frm:390-394` | Explicit and directly portable |
| Eyes-only recovery | `basPacSetUp.bas:416-451`, `basGhostAI.bas:73-78` | Explicit, with a source quirk |
| Death/level reset | `DefaultPositions`, `ResetLevel`, `PacDied` | Explicit, with a retained chain quirk |
| Sounds | `sndPlay` calls | Dependent on unimplemented sound behavior; excluded |

## Rendering

`PacPics.bmp` is 256x352. Normal ghost body columns are 0, 32, 64, and 96;
the frightened blue body is column 128; eyes are column 160. The normal mask
is column 192 and the eyes mask is column 224. The blue source has one body
frame per direction; there is no separate white frightened sheet. Near the end
of frightened mode the source simply stops selecting blue, revealing the
normal body color.

`FlashOkay = (FlashOkay + 1) Mod 16`. When `PPTimer < 200 / Game.Speed`, the
ghost flashes when `FlashOkay > 7`; otherwise it remains blue. The native port
advances the equivalent phase at the deterministic 10 ms simulation point,
uses the strict timer threshold, and derives the sprite only from logical
condition/timer state.

## Eating and recovery

`TestCollisions` uses the strict center threshold `Abs(dx) < 16 And Abs(dy) <
16`, scans Ghost(1) through Ghost(4), and eats a frightened ghost before the
normal lethal branch. The scores are 200, 400, 800, and 1600. The native chain
is bounded at four and score addition remains saturating. An eaten ghost sets
`Eyesonly = True`, clears `PPTimer`, doubles its speed, and aligns its position
for the faster step. It is collision-inactive while eyes-only.

Eyes target `(224,184)`. At that gate, the source halves the doubled speed,
sets `InGame = False`, and points down. The next house pass clears
`Eyesonly` at y=224 or y=240, and the ghost is made active again at y=184 with
an offset of 8 and a random horizontal direction. This is a short renewal
bounce, not a new shared ghost mode. The native port represents the logical
interval as `Eaten -> Returning`, uses a bounded wall-safe route to the gate,
then an explicit fixed door-lane renewal bounce and deterministic Left in
place of VB6's unseeded `Rnd`.

## Reset and ordering rules

The native update order is input and next-tile pill consumption, Pac-Man
movement, Red, Pink, Cyan, Orange, each timer decrement, then one collision
sample. A final consumable enters the existing `LevelComplete` state before
ghost movement/collision; level reset clears all ghost conditions and timers.
Activation on a non-final power pill occurs before the same-step ghost updates,
so the first timer decrement and reversal are observable on that update.

Collision is deterministic in ghost-index order. A frightened ghost is eaten
and becomes non-lethal immediately. A normal ghost enters Dying and stops
later collision processing, so a later frightened ghost cannot award a
post-death score. This is the native safety-preserving form of the historical
loop's index order.

Death clears all active frightened/eaten/returning conditions and timers, then
the existing actor reset restores the four spawn/release states. The source
does not assign `Pacman.GhostsEaten` in `DefaultPositions` or `ResetLevel`; it
is an observable historical quirk that the native score-chain field remains
unchanged across death and level reset, but the next power pill always resets
it before another ghost can score. A fresh Game Over restart clears the chain.

Ready, LevelComplete, Game Over, and Dying do not advance frightened timers or
ghost movement. Restart restores normal conditions, zero timers, the initial
chain, and all 244 consumables.

No Chase/Scatter system, sound, fruit, extra-life award, PRNG, or arcade-only
power-pill behavior was added.

## Power-pill visibility polish (2026-07-29)

The source search for `Blink`, `Flash`, `PowerPill`, `RefreshLevel`, timer
events, and visibility changes found no power-pill blink implementation.
`frmPacMan.frm:524-536` uses `tmrFlash` only to alternate the `lblInsert`
foreground color, and `basPacSetUp.bas:513-548` draws each power pill once
from the level cell. Power-pill visibility in the original is therefore
logical-cell driven, not timer driven.

The requested native polish uses the smallest deterministic approximation:

```text
initial phase: visible
shared cadence: 50 fixed 10 ms simulation steps (500 ms)
toggle boundary: the update that decrements the countdown from 1 to 0
scope: all remaining power pills share one phase
logical state: unchanged while hidden; CellType::PowerPill remains present
reset: game initialization, new level, and new-game restart
stopped states: no phase advancement outside Playing, matching the source's
            disabled keyboard timer during Ready, death, level completion,
            and Game Over
```

The renderer draws a power pill only when the shared `powerPillVisible` phase
is true. It always starts from the retained clean background and never writes
visibility into `LevelState::cells`, so a hidden pill remains consumable and a
consumed pill cannot be restored by a later visible phase. Normal pills are
not affected. The cadence is simulation-driven and independent of repaint or
capture frequency. The 500 ms value is derived from the only historical
presentation timer, but the source does not establish that this timer was
intended for power pills; it must not be described as a historical gameplay
timing fact.
