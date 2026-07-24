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

After the synchronized stationary-ghost milestone was proven, Red received the
next bounded movement step: legal direction selection at aligned intersections,
maze wall checks, tunnel wrapping, and a deterministic Pac-Man-position target.
Pink, Cyan, and Orange remain stationary. The hosted danger placement is
test-only and temporarily pins Red to Pac-Man so the existing three-collision
life sequence remains deterministic.
