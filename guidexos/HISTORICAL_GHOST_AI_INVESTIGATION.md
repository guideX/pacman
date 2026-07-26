# Historical Nexgen Ghost AI Investigation

Investigation date: 2026-07-25

Conclusion: **C. No coordinated ghost mode system exists.**

The authoritative local Nexgen VB6 project is the source at `D:\dev\pacman`.
The archived Nexgen 1.0.70 source at
`D:\dev\team-nexgen\bkup\sourcearchive\2003\full\src(pacman_10_70)\Pacman\source`
was also inspected as corroboration. It contains the same individual ghost
routine in both `ghostai.txt` and `basGhostAI.bas`; its additional networking
forms do not add a shared ghost-mode schedule.

## Reachable source boundary

`PacMan.vbp` wires the gameplay together with `frmPacMan.frm`,
`basPacSetUp.bas`, `basPacman.bas`, and `basGhostAI.bas`. No other gameplay
module is in the project file.

The reachable timer path is:

1. `frmPacMan.frm:539-608`, `tmrKeyBoard_Timer`, runs every 10 ms.
2. It calls `PacmanMovement`, then `AIGhostMonsters`, then `TestCollisions`.
3. `PlayIntro` at `frmPacMan.frm:674-680` disables `tmrKeyBoard`; the
   `tmrReady_Timer` handler at `frmPacMan.frm:682-714` re-enables it after its
   Ready counter expires.
4. Level completion at `frmPacMan.frm:571-584` calls `ResetLevel` and
   `DefaultPositions`, so all actors are reset before play resumes.
5. Death handling at `frmPacMan.frm:611-665` calls `DefaultPositions` for a
   remaining life and disables the keyboard timer on Game Over.

There is no reachable timer callback, form field, module variable, or UDT
member representing a shared ghost mode, phase index, phase duration, scatter
target, or target-mode transition.

## Direct evidence

### Per-ghost state only

`basPacSetUp.bas:44-56` defines `UDTGhost` with `InGame`, `Eyesonly`, position,
direction, movement fields, `Ycounter`, `PPTimer`, and `DelayTime`. These are
individual ghost fields. The public state at `basPacSetUp.bas:101-114` contains
the four-element `Ghost(1 To 4)` array and movement lookup arrays, but no shared
mode or phase variable.

`basPacSetUp.bas:297-346`, `DefaultPositions`, initializes each ghost
individually. Red starts outside the house; Pink, Cyan, and Orange start in
the house. Every ghost's `PPTimer` is initialized to zero.

### Individual target selection

`basGhostAI.bas:18-19` loops over the four ghosts, and the target calculation
at `basGhostAI.bas:80-110` is selected solely by `nLoop`:

- Ghost 1 uses Pac-Man's current integer tile.
- Ghost 2 projects four tiles when its own separation is greater than two.
- Ghost 3 projects eight tiles when its own separation is greater than three.
- Ghost 4 projects twelve tiles when its own separation is greater than four.

The projection uses `XD(Pacman.Direction)` for both `Px` and `Py`. The source
does not read a shared mode or a fixed corner target in this path.

At a junction, `basGhostAI.bas:112-159` derives `Xs2`/`Ys2` from that one
ghost's selected target, optionally negates those signs for that same ghost's
`PPTimer`, excludes its immediate reverse, and chooses a legal direction.
The routine is called independently for each ghost; no target is switched for
multiple ghosts by a common event.

The native implementation preserves these four independent helpers and their
validated movement/release behavior. It does not add a mode-aware wrapper or
scatter targets.

### Timers and timing units

The only gameplay timer that invokes ghost AI is `tmrKeyBoard` at
`frmPacMan.frm:50-54`, with `Interval = 10`. That is one AI/movement update per
10 ms while the keyboard timer is enabled. It is not a phase timer.

The other form timers are unrelated: `tmrReady` is 500 ms,
`tmrPookie` is 100 ms, and `tmrFlash` is 500 ms (`frmPacMan.frm:19-34`).
`tmrReady_Timer` uses a local static `Counter` only for the Ready display
(`frmPacMan.frm:682-714`); it never changes ghost targets or state.

`Game.Speed` changes movement speed and the per-ghost power-pill duration; it
is not a level-dependent mode schedule. `Pacman.Level` is used in the
power-pill duration and is capped at eight in the level-completion path, but
no level-specific ghost phase schedule exists.

Therefore there is no historical initial mode, phase schedule, duration list,
indefinite final phase, shared timing unit, or level-dependent mode difference
to convert to the native 10 ms fixed-step simulation.

### Reversal and power pills

There are reversals, but they are not shared mode-transition reversals:

- House release reverses a house ghost at the top/bottom of the box in
  `basGhostAI.bas:33-40`.
- Consuming a power pill in `basPacman.bas:73-88` assigns each eligible ghost
  its own `PPTimer = (1000 - 100 * Pacman.Level) / Game.Speed` and immediately
  reverses that ghost with `Rev(Ghost(nLoop).Direction)`.
- While a ghost's own `PPTimer > 0`, `basGhostAI.bas:119-124` negates its own
  target signs and `basGhostAI.bas:163-181` applies half-speed delay and
  decrements that timer.

These are the source-supported frightened/eaten hooks. They do not establish
Chase, Scatter, a shared phase transition, or a shared reversal event. The
native milestone therefore leaves power pills scoring and disappearing with
normal ghost collision behavior, as already validated, and does not implement
frightened mode in this investigation.

## State/reset implications

Because the source has no shared mode state, the questions about pausing or
resetting a shared schedule during Playing, Ready, Dying, Level Complete, or
Game Over have no historical value: there is no schedule to pause or reset.
The reachable source behavior is instead:

- Ready disables the 10 ms gameplay timer until the Ready counter completes.
- Ghost movement occurs only through `AIGhostMonsters` while that timer runs.
- Level completion calls `ResetLevel` and `DefaultPositions`.
- A non-final death calls `DefaultPositions` after the death path.
- Game Over disables `tmrKeyBoard`.

The native update order remains the source-supported order already validated:
Pac-Man input and next-tile pill/completion handling, Pac-Man movement and
animation, Red, Pink, Cyan, Orange, one collision sample, then state-timer and
dirty-frame handling. No mode-boundary insertion is required.

## Classification and decision

This investigation is **C**, not A, B, or D. The source boundary is complete,
the ghost routine is reachable, and its state is explicit enough to show that
the four ghosts continuously use their own target behavior without a shared
schedule.

No speculative Chase/Scatter implementation, scatter target, forced
mode-transition reversal, timing schedule, validation indicator, or hosted mode
harness was added. Existing Red, Pink, Cyan, and Orange target formulas and
release paths remain unchanged.

The next source-supported milestone is the power-pill behavior already exposed
by the VB6 source: per-ghost frightened state and timer, ghost color changes,
ghost eating, and the source's score progression. Those behaviors are kept
separate from this investigation so no unverified shared mode semantics are
introduced.
