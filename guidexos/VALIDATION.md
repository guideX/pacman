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

Red is now the only moving ghost. The historical Red rule targets Pac-Man's
current logical position directly; projected targets in the original VB6 code
belong to later ghosts and are not copied into Red's helper. Red starts at
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
tunnel wrap, mouth animation, Red movement, Pink movement, one collision
sample, then the simulation step/state-timer advance and dirty-frame marking.
If the final pill is consumed on an update, LevelComplete returns before either
moving ghost or collision on that update. All four non-Playing states gate both
moving ghosts, and one collision sample suppresses duplicate Red/Pink overlap
life loss.

Pink starts at `(192,224)`, facing Up, in `PinkHouseBounce` with collision
inactive. The historical VB6 source bounces Ghost(2), moves it right to
`(224,224)`, and sends it up the fixed house door lane to `(224,184)`. Because
Cyan remains stationary in this milestone, the port uses a deterministic two-
alignment bounce wait before the same route. Pink becomes collision-active only
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

Cyan and Orange remain at `(224,240)` and `(256,224)`. The ordinary danger
placement remains test-only and temporarily overlaps Red with Pac-Man for the
existing three-life regression. The Red movement mode keeps Red moving, and the
Pink movement mode pins Pac-Man still, suppresses unrelated collisions, and
arms one deterministic moving-Pink collision after route captures.

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
