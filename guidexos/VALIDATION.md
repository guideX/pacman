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
tunnel wrap, mouth animation, Red movement, collision, then the simulation
step/state-timer advance and dirty-frame marking. If the final pill is consumed
on an update, LevelComplete returns before Red movement or collision on that
update. All four non-Playing states gate Red movement.

Pink, Cyan, and Orange remain stationary at `(192,224)`, `(224,240)`, and
`(256,224)`. Their positions are asserted by host-independent tests and by the
hosted captures while Red moves. The ordinary danger placement remains
test-only and temporarily overlaps Red with Pac-Man for the existing three-life
regression. The Red movement mode instead pins a reachable Pac-Man target for
observation and arms a collision by placing Pac-Man 32 pixels ahead of moving
Red at the tunnel edge; it does not force an initial overlap.

The reusable Red harness is `tools/validate_hosted_red_movement.ps1`. It builds
and launches only the validation package, uses one owned experimental server,
records runtime/window/frame/compositor/paint/capture generations, captures
initial, corridor, intersection, tunnel, Dying, and Ready frames, and cleans up
the exact server process tree in `try/finally`. It is off by default and does
not change the production package.
