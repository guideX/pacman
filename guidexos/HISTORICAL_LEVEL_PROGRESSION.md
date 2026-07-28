# Nexgen PacMan level progression

Classification: **B — partial historical progression**.

The historical VB6 project has one logical maze and one enhanced maze bitmap.
`basPacSetUp.bas:156-241` contains the complete 28×31 `strData` maze in
`ResetLevel`; every call rebuilds the same walls, 240 normal pills, and four
power pills. `basPacSetUp.bas:513-542` always loads `level1.bmp`. The comment
there says additional backgrounds were a future change, and no additional
level bitmap, logical level array, or level-specific pill layout exists in the
project. Native therefore reuses `level1.gximg` and resets the logical
consumables independently on every level.

## Numbering and bounds

`frmPacMan.frm:492-511` initializes `Pacman.Level = 1`. The completion branch
at `frmPacMan.frm:571-583` increments it and immediately clamps values above 8
back to 8. Native uses the same one-based range and saturates malformed or
very-high `uint32_t` values at 8. It never wraps to zero.

## Rules supported by the source

Pac-Man and all four ghosts receive `Game.Speed` in
`basPacSetUp.bas:286-345`; `Game.Speed` starts at 1 in
`frmPacMan.frm:386` and the `S` key only cycles 1, 2, and 4
(`frmPacMan.frm:418-430`). No movement speed read depends on `Pacman.Level`.
The native fixed-step equivalent is one movement update each 10 ms, with the
shared speed used as pixels per update. Frightened ghosts skip every second
AI movement pass in `basGhostAI.bas:163-182`, so their effective interval is 2
fixed steps.

The only level-dependent difficulty formula is in
`basPacman.bas:80-88`:

```text
PPTimer = (1000 - 100 * Pacman.Level) / Game.Speed
```

At the normal speed this gives levels 1–8: `900, 800, 700, 600, 500, 400,
300, 200` fixed steps. `basPacSetUp.bas:386-400` uses the separate,
level-independent flash threshold `200 / Game.Speed` and the existing 16-step
flash phase. There is no level-dependent flash count, cadence, ghost speed,
Pac-Man speed, targeting mode, release delay, pill score, or power-pill
placement table. Because Nexgen clamps the level to 8, a zero-duration level
is unreachable in the historical game; native still handles high values
without unsigned underflow or division by zero.

Ghost release code in `basGhostAI.bas:30-68` and the ghost-specific branches
below it do not read `Pacman.Level`. Pink, Cyan, and Orange keep their fixed
source release paths on every level. Targeting branches likewise have no
level read.

## Transition and reset evidence

The source completion path adds 1000 points, redraws the same maze, resets
actor positions, starts the intro, and enters the Ready timer
(`frmPacMan.frm:571-583`, `frmPacMan.frm:674-714`). Native presents a bounded
Level Complete state, adds the same one-time 1000-point bonus, then enters
Ready before gameplay. Score and lives are retained. Death preserves the
current level, current rules, score, lives, and consumed cells; Game Over
preserves the displayed score and level, while restart restores level 1 and
all 244 consumables.

On a level reset, native clears Pac-Man buffered input, frightened/eaten/
returning conditions, timers, release state, ghost-eating chain, and movement
schedules, then restores the four source actor positions and the exact
`240 + 4 = 244` consumable count.

## Fruit mapping for the later milestone

Fruit is not spawned or drawn by the native milestone. The historical source
does expose the future mapping in `basPacSetUp.bas:140-149` and
`frmPacMan.frm:595-601`: the sprite sheet `pacpics.bmp` uses
`x = ((Level - 1) Mod 4) * 32` and `y = ((Level - 1) \ 4) * 32 + 256`.
Thus levels 1–4 select the first four 32×32 cells on the row beginning at
y=256, and levels 5–8 select the first four cells on the row beginning at
y=288. The source does not name those bitmap cells in code; spawning,
collision, and scoring remain deferred as requested.
