# Hosted frame validation

The danger-validation ELF is built with `PACMAN_HOSTED_DANGER_TEST=1`. It draws a
test-only marker into the XRGB8888 game framebuffer: `D`, `R`, `G`, or `P` for
Dying, Ready, Game Over, or post-restart Playing, followed by the application
frame sequence. The marker is compiled out of the production ELF and is not a
gameplay control.

## Frame identity and synchronization

When `GXOS_PACMAN_FRAME_DIAGNOSTICS=1` is set, Pac-Man logs one monotonically
increasing application frame sequence for significant rendered frames. The
sequence is carried as diagnostic metadata at the Native ELF `present_frame`
boundary. The compositor records the retained frame generation, paint
generation, capture generation, window/runtime identity, and byte count.

The validation harness enables both `GXOS_PACMAN_FRAME_DIAGNOSTICS=1` and
`GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS=1`. It sends the general-purpose compositor
command:

```text
gui.sync <window-id> 0 <frame-sequence> 1
```

The compositor waits for the requested application sequence to be retained and
painted, then freezes a complete copy of that retained frame for the bounded
capture operation. The harness captures the matching hosted window and finally
sends:

```text
gui.unfreeze <window-id>
```

The freeze is validation-only, bounded by the harness timeout, and does not
block the production UI thread. It prevents later compositor repaint/request
traffic from changing the visible surface between the generation check and
`CopyFromScreen`.

The correlation chain is:

```text
Pac-Man render sequence -> Native present_frame -> compositor frame generation
-> compositor paint generation -> frozen capture generation -> PNG marker
```

The marker remains part of the normal game artwork in the validation build; it
does not replace death, Ready, Game Over, or restart visuals.

## Current ghost scope

Red, Pink, Cyan, and Orange are the moving ghosts in this milestone. The historical Red
rule targets Pac-Man's current logical position directly; projected targets in
the original VB6 code belong to later ghosts and are not copied into Red's
helper. Red starts at
`(224,184)`, uses deterministic Left in place of VB6's random initial
horizontal choice, and moves at 1 logical pixel per 10 ms simulation step. Red
starts outside the ghost house, so no house-exit timer or path is applicable;
the historical `InGame` check activates it on its first AI tick.

Red selects a direction only at an aligned tile center. Legal candidates are
enumerated in the fixed order Up, Down, Left, Right. The VB6 source uses
sign-priority tests rather than an explicit Euclidean, squared, or Manhattan
formula. The native bounded chooser keeps that direct-target behavior with
Manhattan distance in logical pixels, shortest wrapped tunnel distance on the
tunnel row, and the fixed candidate order for ties. Wall checks use the
logical maze, immediate reverse is excluded when another legal direction is
available, and reverse is allowed at a dead end. Red wraps between the two
valid horizontal tunnel edges and never uses an out-of-bounds maze cell.
Dying, Ready, LevelComplete, and GameOver gate movement. Its directional
sprite row is selected from the historical source sheet; the fixed-step
animation counter is diagnostic only because the normal source has one body
frame per direction.

The verified update order is input, aligned Pac-Man turn and wall handling,
next-tile pill consumption and level-completion check, Pac-Man movement and
tunnel wrap, mouth animation, Red movement, Pink movement, Cyan movement,
Orange movement, one collision sample, then the simulation step/state-timer
advance and dirty-frame marking.
If the final pill is consumed on an update, LevelComplete returns before any
moving ghost or collision on that update. All four non-Playing states gate all
moving ghosts, and one collision sample suppresses duplicate multi-ghost overlap
life loss.

Pink starts at `(192,224)`, facing Up, in `PinkHouseBounce` with collision
inactive. The historical VB6 source bounces Ghost(2), moves it right to
`(224,224)`, and sends it up the fixed house door lane to `(224,184)`. The port
keeps a deterministic two-alignment bounce wait and gates the source-faithful
center-lane release on Cyan's normal release state. Pink becomes collision-active only
after the release state becomes `normal`; death, level reset, and Game Over
restart restore the house state.

Pink's pure target helper keeps the historical four-tile projection gate:
project only when integer tile separation from Pink is greater than two, then
use `targetX = pacmanX + XD(direction)*64` and
`targetY = pacmanY + XD(direction)*64`. The use of `XD` for Y is the observable
historical coordinate quirk, so Right/Left project both axes and Up/Down project
neither. The helper uses logical coordinates, clamps normal bounds, wraps a
tunnel-row target safely, and does not change Red's direct target. Pink's
selection is the VB6 sign-priority sequence with Up, Down, Left, Right fallback,
reverse exclusion when alternatives exist, and reverse allowed at a dead end.

Cyan starts at `(224,240)`, facing Down, inside the house with collision
inactive. The authoritative `basGhostAI.bas` Ghost(3) branch reverses at the
top and bottom of the middle lane, counts two aligned visits to `(224,224)`,
then exits upward through `x=224` to `y=184`. The native fixed-step port keeps
that path, normalizes the historical `Offset=8` activation boundary, chooses a
deterministic Left in place of the source's `2 + Rnd` horizontal activation,
and enables collision only at the outside-house boundary. Cyan moves at one
logical pixel per 10 ms, chooses only at aligned decision points, uses the
same VB6 sign-priority order as Pink, excludes reverse directions when another
legal path exists, reverses at dead ends, wraps only on row 14, and uses sprite
column 64 with `Direction * 32` rows and the existing mask composition.

The exact Cyan target helper is source-faithful: if
`Abs(Pacman.Xpos \ 16 - Cyan.Xpos \ 16) + Abs(Pacman.Ypos \ 16 - Cyan.Ypos \ 16) > 3`,
then `targetX = Pacman.Xpos + XD(Pacman.Direction) * 128` and
`targetY = Pacman.Ypos + XD(Pacman.Direction) * 128`; otherwise the target is
Pac-Man's current logical point. The `XD` use on Y is the same observable
historical coordinate quirk preserved for Pink. The helper takes Red explicitly
for deterministic call-site/test isolation, but the local VB6 source contains
no Red position, vector, or doubled-vector term in Ghost(3), so Red movements
must not change Cyan's target. This is an inspected source difference from
arcade Inky behavior, not an implementation omission. Targets are not clamped
to the maze before direction scoring. Cyan observes Red's post-move position in
the update order, although the source-faithful helper does not use it.

Cyan and Orange reset to `(224,240)` and `(256,224)` respectively after death,
level completion, and Game Over restart. The ordinary danger placement remains
test-only and temporarily overlaps Red with Pac-Man for the existing three-life
regression. The Cyan movement mode pins Pac-Man still, suppresses unrelated
collisions, and arms one deterministic moving-Cyan collision after route
captures.

## Orange source and validation contract

Historical inspection of `basGhostAI.bas`, `basPacSetUp.bas`, and
`frmPacMan.frm` verified Orange as Ghost(4): spawn `(256,224)`, initial
direction Up, inside the ghost house, `Offset=0`, `InGame=False`, and speed 1
logical pixel per 10 ms. The shared source release branch bounces vertically
between `y=224` and `y=240`; once Pink's `InGame` condition is true, Orange
moves left from `(256,224)` to `x=224`, turns Up through the fixed doorway, and
becomes normal and collision-active at `y=184`. The native port represents that
sequence as `OrangeHouseBounce -> OrangeToCenter -> OrangeExiting -> Normal`.
The VB6 activation direction uses `2 + Rnd`; the native build chooses Left
deterministically at activation, while preserving the historical initial Up.

Orange's target is a pure, state-preserving helper. Its exact metric is raw
integer VB6 tile distance:

```text
distanceTiles = Abs(Pacman.Xpos \ 16 - Orange.Xpos \ 16)
              + Abs(Pacman.Ypos \ 16 - Orange.Ypos \ 16)
```

The projection comparison is strictly `distanceTiles > 4`. At 0–4 tiles the
target is Pac-Man's current logical coordinate. Above 4 tiles the target is
`targetX = PacManX + XD(Direction) * 12 * 16` and
`targetY = PacManY + XD(Direction) * 12 * 16`. The `XD` value is intentionally
used on Y as in the source: Left/Right project both axes; Up/Down project
neither. This is not a Red dependency, a fixed corner, random behavior, or
arcade Clyde substitution. Raw tunnel-adjacent coordinates are used for the
distance; tunnel wrapping is movement-only, and off-maze logical target values
are retained with bounded integer conversion.

Orange uses the validated Pink/Cyan sign-priority chooser. It decides only at
aligned centers, continues committed directions through a tile, enumerates
Up/Down/Left/Right with horizontal overwrite priority, excludes reverse when an
alternative exists, permits reverse at a dead end, and wraps only on row 14.
The renderer uses the historical Orange body column 96, directional rows, the
two bounded movement frames, and the existing mask composition. Collision is
the strict `<16` center test after Red, Pink, Cyan, and Orange each update once.

The reusable Red harness is `tools/validate_hosted_red_movement.ps1`. It builds
and launches only the validation package, uses one owned experimental server,
records runtime/window/frame/compositor/paint/capture generations, captures
initial, corridor, intersection, tunnel, Dying, and Ready frames, and cleans up
the exact server process tree in `try/finally`. It is off by default and does
not change the production package.

The reusable Pink harness is `tools/validate_hosted_pink_movement.ps1`. Build
the same validation package with `PACMAN_HOSTED_DANGER_TEST=ON` and
`PACMAN_HOSTED_PINK_MOVEMENT_TEST=ON`; it captures the initial house position,
house-exit transition, corridor movement, projected-target turn, distant route,
moving collision, and Ready house reset. It reports runtime/window identity,
application frame sequence, frame/paint/capture generations, actor coordinates,
directions, targets, release state, and screenshot paths. Its compile-time
marker and forced collision behavior are absent from the production ELF.

The reusable Cyan harness is `tools/validate_hosted_cyan_movement.ps1`. Build the
same validation package with `PACMAN_HOSTED_DANGER_TEST=ON` and
`PACMAN_HOSTED_CYAN_MOVEMENT_TEST=ON`; it captures the initial house position,
house-exit transition, normal corridor, target-directed turn, distant route,
moving collision, and Ready house reset with the same `gui.sync`/freeze
generation checks. It also records Red movement and the source-faithful fact
that Cyan's target is unchanged when only Red moves. Its compile-time marker
and forced collision behavior are absent from the production ELF.

The reusable Orange harness is `tools/validate_hosted_orange_movement.ps1`.
Build with `PACMAN_HOSTED_DANGER_TEST=ON` and
`PACMAN_HOSTED_ORANGE_MOVEMENT_TEST=ON`. It starts exactly one owned
experimental server, records wrapper/child PIDs and runtime/window identity,
captures initial house, center lane, maze exit, far corridor, far turn,
near-threshold switch, near route, distant route, moving-collision Dying, and
Ready reset, and reports application sequence plus matching frame/paint/capture
generations. The threshold proof records far distance/target and then a frame at
exactly four tiles using the near target. The validation-only hook holds
Pac-Man, changes it at Orange's next legal decision, arms one moving collision,
and is absent from production `pacman.elf`.
