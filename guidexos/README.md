# guideXOS Native ELF Pac-Man milestone

This directory is the new guideXOS-native implementation. The historical Visual Basic 6 project remains untouched in the parent directory and is still the reference implementation.

## Historical reference

The original project is `PacMan.vbp`, with the form and logic split across:

- `frmPacMan.frm` / `frmPacMan.frx`: the 6720×8295-twip fixed VB6 form, 448×553 logical client, timers, labels, PictureBoxes, and Win32 event/render calls.
- `basPacSetUp.bas`: level data, sprite blits, default positions, life display, and `RefreshLevel`.
- `basPacman.bas`: keyboard polling and Pac-Man movement/collision logic.
- `basGhostAI.bas`: the four-ghost movement and targeting rules.
- `Level1.bmp`: uncompressed 24-bit, 448×496 maze background.
- `PacPics.bmp`: uncompressed 24-bit, 256×352 source/mask sprite sheet.
- WAV files: `EatPill.wav`, `extralife.wav`, `fruiteat.wav`, `ghosteat.wav`, `killed.wav`, `StartMusic.wav`, and `startmusicold.wav`.

The VB6 renderer uses a 16-pixel tile grid, 28×31 level cells, 32×32 sprites, and `BitBlt` with `SRCAND` followed by `SRCPAINT` for transparency. The screen PictureBox is logically 448×495 pixels; the form also has a 32-pixel stats strip. The native milestone preserves the 448-pixel width and presents a 448×553 XRGB8888 frame with the converted 496-pixel maze below the stats strip.

The port deliberately does not copy the VB6 gameplay loop, `GetAsyncKeyState`, `BitBlt`, `sndPlaySound`, GDI, or other Windows APIs.

## Native package layout

```text
guidexos/
  app.json
  CMakeLists.txt
  README.md
  src/
    main.cpp
    game_state.h
    game.cpp / game.h
    level.cpp / level.h
    renderer.cpp
    renderer.h
    bitmap_loader.cpp
    bitmap_loader.h
    game_types.h
  tools/
    convert_bmp_to_gximg.ps1
  resources/generated/
    level1.gximg
    pacpics.gximg
  bin/amd64/pacman.elf
```

`resources/generated/*.gximg` is a reproducible package format: a 28-byte little-endian `GXIM` header followed by XRGB8888 words in top-down row order. The conversion script reads the original BMP files and never modifies them.

## Rebuild and stage

LLVM clang++ and `ld.lld` are required because MinGW g++ produces PE/COFF, not Native ELF:

```powershell
cd D:\dev\pacman\guidexos
powershell -ExecutionPolicy Bypass -File tools\convert_bmp_to_gximg.ps1 -Source ..\Level1.bmp -Destination resources\generated\level1.gximg
powershell -ExecutionPolicy Bypass -File tools\convert_bmp_to_gximg.ps1 -Source ..\PacPics.bmp -Destination resources\generated\pacpics.gximg
cmake -S . -B build -G Ninja -DGUIDEXOS_SERVER_ROOT=D:\dev\guideXOSServer -DGUIDEXOS_PACKAGE_ROOT=D:\Apps
cmake --build build
```

The build writes the local ELF to `bin/amd64/pacman.elf` and stages the discovered package at `D:\Apps\PacMan`, which is the hosted runtime's `/Apps` package root. Override `GUIDEXOS_PACKAGE_ROOT` for another guideXOS target.

The hosted validation builds are explicitly isolated. They use the same `PACMAN_HOSTED_DANGER_TEST` compile-time hook that defaults to `OFF`, write `bin/amd64/pacman-danger-validation.elf`, stage a separate `D:\Apps\PacManDangerValidation` package, and use the test-only manifest `app-danger-validation.json`. They never write the production ELF or `D:\Apps\PacMan`:

```powershell
cmake -S . -B build-danger -G Ninja -DPACMAN_HOSTED_DANGER_TEST=ON -DGUIDEXOS_SERVER_ROOT=D:\dev\guideXOSServer -DGUIDEXOS_PACKAGE_ROOT=D:\Apps
cmake --build build-danger --target pacman-danger-validation
```

The ordinary danger build places Red over Pac-Man at bounded simulation steps for up to three deterministic hosted deaths. The separate Red movement validation build keeps Red moving, uses a staged moving-collision window, and is enabled only with `-DPACMAN_HOSTED_RED_MOVEMENT_TEST=ON` (which requires the danger hook):

```powershell
cmake -S . -B build-red -G Ninja -DPACMAN_HOSTED_DANGER_TEST=ON -DPACMAN_HOSTED_RED_MOVEMENT_TEST=ON -DPACMAN_ENABLE_DIAGNOSTICS=ON -DGUIDEXOS_SERVER_ROOT=D:\dev\guideXOSServer -DGUIDEXOS_PACKAGE_ROOT=D:\Apps
cmake --build build-red --target pacman-danger-validation
powershell -ExecutionPolicy Bypass -File tools\validate_hosted_red_movement.ps1
```

The bounded Pink movement validation uses the same danger package, adds only the `PACMAN_HOSTED_PINK_MOVEMENT_TEST=ON` hook, pins Pac-Man still, and captures Pink's house release, projected-target route, tunnel/distant movement, collision, and Ready reset:

```powershell
cmake -S . -B build-pink -G Ninja -DPACMAN_HOSTED_DANGER_TEST=ON -DPACMAN_HOSTED_PINK_MOVEMENT_TEST=ON -DPACMAN_ENABLE_DIAGNOSTICS=ON -DGUIDEXOS_SERVER_ROOT=D:\dev\guideXOSServer -DGUIDEXOS_PACKAGE_ROOT=D:\Apps
cmake --build build-pink --target pacman-danger-validation
powershell -ExecutionPolicy Bypass -File tools\validate_hosted_pink_movement.ps1
```

The bounded Orange movement validation adds only `PACMAN_HOSTED_ORANGE_MOVEMENT_TEST=ON`.
It captures Orange's historical house release, far target, threshold switch, near route,
tunnel-safe movement, moving collision, and Ready reset. Pac-Man is held and moved only
by this validation build; the hook is absent from production:

```powershell
cmake -S . -B build-orange-validation -G Ninja -DPACMAN_HOSTED_DANGER_TEST=ON -DPACMAN_HOSTED_ORANGE_MOVEMENT_TEST=ON -DPACMAN_ENABLE_DIAGNOSTICS=ON -DGUIDEXOS_SERVER_ROOT=D:\dev\guideXOSServer -DGUIDEXOS_PACKAGE_ROOT=D:\Apps
cmake --build build-orange-validation --target pacman-danger-validation
powershell -ExecutionPolicy Bypass -File tools\validate_hosted_orange_movement.ps1
```

All hooks are compile-time validation behavior, are off by default, are isolated from `D:\Apps\PacMan`, and are not exposed through a production key.

## Native platform additions

The existing `guidexos-c-abi-v1` table keeps its original member order and appends these general-purpose calls:

- `request_window_ex(..., flags, ...)`, with `GX_WINDOW_FLAG_FIXED_SIZE` for non-resizable windows.
- `file_read(..., offset, ...)`, which reads sequential chunks up to 64 KiB so package resources larger than `file_read_all` can be streamed safely.
- `present_frame(...)`, which copies a complete XRGB8888 frame into compositor-owned storage.
- `get_ticks_ms(...)`, which returns monotonic milliseconds from a hosted process-local epoch. The `uint64_t` value wraps after 2^64 milliseconds.

Frame ownership is explicit: the app owns the input buffer and may reuse it after `present_frame` returns; the host copies the bytes before returning. The current supported format is `GX_PIXEL_FORMAT_XRGB8888`, with a `0x00RRGGBB` word per pixel, a caller-provided byte stride, and a 16 MiB host frame cap. The compositor retains one surface per window and replaces it on later presentations, so repaint handling does not append unbounded draw objects.

## Hosted launch and validation

Build the normal hosted server for discovery/inspection:

```powershell
cd D:\dev\guideXOSServer
cmd /c build.bat
```

For actual trusted local Native ELF execution, build the explicitly experimental runtime:

```powershell
cmd /c build-native-experimental.bat
guideXOSServer.experimental.exe
```

Then use the server console:

```text
gui.start
desktop.apps.verbose
nativeapp.inspect com.guidexos.pacman
desktop.launch Nexgen PacMan
```

`nativeapp.smoketest com.guidexos.pacman` is the bounded diagnostic path; it requests a close after the smoke interval. Escape and the window close control are handled by the app's normal Native ELF event loop.

## Verified historical pill and level rules

The values below are taken from `basPacSetUp.bas` and `basPacman.bas`, not inferred from the bitmap:

- `Wall = -1`, blank/space = `0`, normal pill `.` = `Pac.Pill = 1`, and power pill `o` = `Pac.PowerPill = 2`.
- Level 1 contains 240 normal pills and 4 power pills, for 244 consumables.
- Both normal pills and power pills award 10 points in `basPacman.bas`.
- Pac-Man's `Xpos`/`Ypos` are the logical center of the 32×32 sprite; the sprite is drawn at `(Xpos - 16, Ypos - 16)`. The port preserves the historical `Offset = 0` test and consumes the tile one movement step ahead using `(Xpos + direction * 16 - 8) / 16` before the movement step.
- Space cells are blank floor. The port classifies the row-14 outer spaces as `Tunnel` and the central rows 13–15, columns 11–16 as `GhostHouse`; neither special region receives pills.
- Historical completion is `DotsLeft < 1`. The VB6 timer immediately increments the level, awards a 1000-point clear bonus, resets the level and positions, and starts the intro again. The native scoring milestone intentionally does not add that bonus or any other non-pill score; it presents `LevelComplete` for 100 fixed steps (1 second) before resetting.

## Ghost layout and lives milestone

`Initialize` sets `Pacman.Lives = 3`. `DefaultPositions` places Red at `(224,184)`, Pink at `(192,224)`, Cyan at `(224,240)`, and Orange at `(256,224)`; their historical directions are Red `2 + Rnd`, Pink up, Cyan down, and Orange up. The native port uses deterministic Left for the source's random horizontal activation/reset choices.

`ShowBlit` uses ghost body source x positions `0`, `32`, `64`, and `96`, direction source y `Direction * 32`, and the normal ghost mask at source x `192`. The Native ELF retains this `SRCAND`/`SRCPAINT` equivalent composition and draws ghosts after pills, with ghosts after Pac-Man on overlap as in historical `ShowSprites`.

`TestCollisions` uses strict center thresholds `Abs(Pacman.Xpos - Ghost.Xpos) < 16` and `Abs(Pacman.Ypos - Ghost.Ypos) < 16`. The native collision helper applies the same rule to active ghosts and never samples rendered pixels; Pink is collision-inactive until it reaches its normal outside-house route.

Historical `PacDied` decrements lives immediately, resets the actors when lives remain, and stops the keyboard timer on the final life. The native port preserves score, consumed pills, remaining count, and level across ordinary death, adds a bounded 100-step death state plus a 60-step ready pause, and shows a native `DEATH` indicator because the historical sheet has no dedicated death frames. The status strip shows the total remaining lives as a number, including zero in Game Over.

Power pills score 10, disappear, and activate the source-supported per-ghost
frightened lifecycle. Each eligible ghost has its own level/speed-scaled
10 ms timer, immediate reversal, half-speed frightened movement, blue/flash
rendering, collision-safe ghost eating, 200/400/800/1600 score progression,
eyes return/reset behavior, and normal restoration. The exact VB6 evidence and
source quirks are recorded in
[HISTORICAL_POWER_PILL_INVESTIGATION.md](HISTORICAL_POWER_PILL_INVESTIGATION.md).

## Red ghost movement milestone

Historical `basGhostAI.bas` gives Ghost(1), the logical Red ghost, Pac-Man's
current tile directly. Red is assigned `Game.Speed`, which is initialized to
1 logical pixel per 10 ms timer step. The native port makes the historical
random initial horizontal direction deterministic by using Left, starts Red
active at `(224,184)`, and keeps the other three ghosts at their historical
positions. Red is already outside the ghost house in the historical setup, so
the house-exit path used by later ghosts does not apply; its first AI tick
activates it immediately.

Red chooses only at an aligned tile center (`offset == 0`). It queries the
logical maze, enumerates candidates in the fixed order Up, Down, Left, Right,
and continues straight through a corridor. The VB6 source does not compute an
Euclidean, squared, or Manhattan distance; its direct-target rule is a
sign-priority sequence. The native bounded helper expresses that same direct
target preference as Manhattan distance in logical pixels, with the shortest
wrapped horizontal distance when both positions are on the tunnel row.
Immediate reverse is excluded when any other legal direction exists; at a dead
end it is the only remaining legal choice. Equal scores use the fixed
enumeration order, so selection is deterministic and independent of container
order. The historical source investigation found no shared scatter/chase
 schedule; see [HISTORICAL_GHOST_AI_INVESTIGATION.md](HISTORICAL_GHOST_AI_INVESTIGATION.md).

The logical tunnel row wraps Red from the left edge to the right edge and back
using the historical 416-pixel span. Red's animation frame toggles on fixed
simulation steps, while the renderer selects the historical directional sprite
row (`direction * 32`) and uses the existing mask composition. The source
sprites provide one normal body frame per direction, so no render-rate-based
animation sequence is invented. The update order is input, aligned Pac-Man
turn/wall handling, next-tile pill consumption and completion check, Pac-Man
movement/tunnel wrap, mouth animation, Red movement, collision, then the
simulation-step/state-timer advance and dirty-frame marking. Existing
completion-before-Red/collision behavior is preserved.

## Pink ghost movement milestone

`basGhostAI.bas` identifies Pink as Ghost(2). `DefaultPositions` places it at
`(192,224)`, facing Up, with `InGame=False`, inside the central ghost house.
The historical source bounces Pink vertically, then moves it right to
`(224,224)` and up to the outside-house activation point at `y=184`. The native
port preserves the source's gate through Cyan's normal release state; the fixed
door lane is the only house exception
to the normal logical maze check. Pink becomes collision-active once it reaches
that outside-house state and its release state is reset after death, level
completion, and Game Over restart.

Pink's target helper preserves the exact historical formula. It computes the
integer tile separation and projects only when it is greater than two tiles.
The projection is four tiles (64 logical pixels) and uses the Pac-Man
horizontal delta for both axes: `targetX = pacmanX + XD(direction)*64` and
`targetY = pacmanY + XD(direction)*64`. Thus Right gives `(x+64,y+64)`, Left
gives `(x-64,y-64)`, and Up/Down give no projection. This is an observable
historical coordinate quirk, not a correction; the helper clamps/wraps targets
at logical bounds without changing Red's direct target.

Pink chooses only at aligned tile centers, excludes the immediate reverse when
another legal direction exists, reverses at dead ends, and uses the VB6
sign-priority order. A target-directed horizontal choice overwrites a
target-directed vertical choice, with Right over Left and Down over Up; the
fallback order is Up, Down, Left, Right. The shared mover advances at one
logical pixel per 10 ms step, wraps only on row 14, and selects the historical
directional sprite row with the existing mask composition. Cyan is covered by
the separate Cyan movement milestone below; Orange is covered by the Orange
movement milestone below.

## Cyan ghost movement milestone

`basGhostAI.bas` identifies Cyan as Ghost(3). `DefaultPositions` places it at
`(224,240)`, facing Down, inside the ghost house, with the normal historical
speed of 1 logical pixel per 10 ms timer step. The source reverses Cyan between
`y=240` and `y=224`, counts two aligned visits to the top of the box, then sends
it up the fixed `x=224` doorway to `y=184`. The native port keeps that release
path, makes collision active only at the outside-house boundary, and chooses a
deterministic Left where the VB6 source uses `2 + Rnd` for its first horizontal
normal direction.

Cyan's target helper preserves the exact local VB6 Ghost(3) formula. Using
integer `Xpos \ 16`/`Ypos \ 16` coordinates, it projects only when the
Pac-Man/Cyan Manhattan tile separation is greater than three tiles. The
projected point is `targetX = pacmanX + XD(direction)*128` and
`targetY = pacmanY + XD(direction)*128`; the `XD`-for-`Y` expression is an
observable source quirk, not a correction. The helper accepts Red explicitly,
but the authoritative VB6 Ghost(3) code never reads Red or forms a Red-to-
projection vector, so Cyan's target is intentionally unchanged when only Red
moves. This differs from arcade Inky and is documented as a source inspection
result. Targets are kept in logical coordinates without premature maze clamping.

Cyan shares the validated aligned decision machinery: the historical
sign-priority order is Up, Down, Left, Right, immediate reverse is excluded
when another legal direction exists, reverse is allowed at a dead end, and
choices are deterministic. It wraps only on the row-14 tunnel, advances and
animates at the fixed 10 ms rate, renders from the cyan sprite column 64 with
directional rows, collides with the existing strict center threshold, and
resets to its house state after death, level completion, and Game Over restart.
The shared update order is Pac-Man input, pill look-ahead/completion, Pac-Man
movement and animation, Red, Pink, Cyan, Orange, one collision sample, then
timers and dirty state. Cyan sees Red's post-move position even though the
source-faithful target helper does not use it. Orange sees the post-move
Pac-Man position and follows the source loop's fourth-ghost order.

The bounded hosted harness is `tools/validate_hosted_cyan_movement.ps1`; build
the validation package with `PACMAN_HOSTED_DANGER_TEST=ON` and
`PACMAN_HOSTED_CYAN_MOVEMENT_TEST=ON`. It uses the same synchronized
`gui.sync`/freeze capture path as the Red and Pink harnesses and is absent from
the production ELF.

## Orange ghost movement milestone

`basGhostAI.bas` identifies Orange as Ghost(4). `DefaultPositions` places it at
`(256,224)`, facing Up, inside the ghost house, with `Offset=0`, `InGame=False`,
and `Speed=Game.Speed=1` logical pixel per 10 ms update. The shared house routine
first bounces it vertically between `y=224` and `y=240`. Once Pink's historical
`InGame` equivalent becomes true, Orange moves left on `y=224` to `x=224`, turns
Up through the fixed door lane, and becomes normal/collision-active at `y=184`.
The native port chooses deterministic Left at that activation point where the
VB6 source uses `2 + Rnd`; the initial Up direction and release route remain
source-faithful. Pink, Cyan, and Orange release states are independent and reset
to their house states after death, level completion, and restart.

Orange's exact source target rule is:

```text
distanceTiles = Abs(Pacman.Xpos \ 16 - Orange.Xpos \ 16)
              + Abs(Pacman.Ypos \ 16 - Orange.Ypos \ 16)

if distanceTiles > 4:
    targetX = PacManX + XD(PacMan.Direction) * 12 * 16
    targetY = PacManY + XD(PacMan.Direction) * 12 * 16
else:
    targetX = PacManX
    targetY = PacManY
```

The comparison is strictly `> 4`, so exactly four tiles is near-targeting.
Near targeting is Pac-Man's current logical position, not a fixed corner,
relative coordinate, random point, projected point, or Red-dependent point.
The same `XD(Direction)`-for-Y quirk is preserved: Left/Right change both axes;
Up/Down use the zero-valued `XD` slots and do not project. Off-maze logical
targets remain valid target values, with only integer conversion bounded.

Orange reuses the validated sign-priority chooser: directions are considered at
aligned tile centers only, target-directed Up/Down/Left/Right tests use the
historical overwrite order (horizontal wins; Right wins over Left and Down over
Up), immediate reverse is excluded when another legal route exists, and reverse
is allowed at a dead end. The shared mover blocks walls, commits a direction for
the whole tile crossing, wraps only on row 14, toggles the two historical normal
animation frames, and applies the strict `<16` collision rule after all four
ghosts update.

The source inspection and deterministic/hosted evidence are recorded in
`VALIDATION.md`. The retained Orange harness is
`tools/validate_hosted_orange_movement.ps1`.

## Power-pill hosted validation

The deterministic power-pill proof uses a separate validation package and the
same synchronized `gui.sync`/`gui.unfreeze` capture path described in
`VALIDATION.md`. It holds Pac-Man over the historical lower-left power pill,
captures normal and frightened states, verifies frightened movement and
near-expiration flashing, eats all four ghosts for the 200/400/800/1600 chain,
then verifies timer expiration and normal restoration. The hook is absent from
the production ELF and does not write `D:\Apps\PacMan`:

```powershell
cmake -S . -B build-power-validation -G Ninja -DPACMAN_HOSTED_DANGER_TEST=ON -DPACMAN_HOSTED_POWER_PILL_TEST=ON -DPACMAN_ENABLE_DIAGNOSTICS=ON -DGUIDEXOS_SERVER_ROOT=D:\dev\guideXOSServer -DGUIDEXOS_PACKAGE_ROOT=D:\Apps
cmake --build build-power-validation --target pacman-danger-validation
powershell -ExecutionPolicy Bypass -File tools\validate_hosted_power_pill.ps1
```

## Current limitations and next milestone

The interactive Native ELF now supports Pac-Man movement under the arrow keys, deterministic Red, Pink, Cyan, and Orange movement, fixed-step simulation, buffered turns, wall blocking, tunnel wrapping, mutable normal/power pills, bounded score, level progress, per-ghost frightened timers and conditions, blue/flashing frightened rendering, ghost eating and score progression, eyes return/reset, center-based collision, a one-life-per-overlap death state, actor reset, Game Over, and Enter/Space session restart. Status text shows total remaining lives; the VB6 display showed spare-life Pac-Man icons, so this is an intentional text simplification. Collision-versus-pill ordering is input, pill look-ahead/completion, Pac-Man movement/animation, Red, Pink, Cyan, Orange, each timer decrement, then one collision sample; level completion wins over a same-step collision. Focus loss clears held directions and stops movement; new input is required after focus returns. The historical source investigation found no shared Chase/Scatter or equivalent coordinated ghost-mode schedule; the evidence and exact source boundary are recorded in [HISTORICAL_GHOST_AI_INVESTIGATION.md](HISTORICAL_GHOST_AI_INVESTIGATION.md). The source-backed power-pill evidence is recorded in [HISTORICAL_POWER_PILL_INVESTIGATION.md](HISTORICAL_POWER_PILL_INVESTIGATION.md). Sounds, fruit, extra lives, and two-player behavior remain out of scope. The hosted amd64 experimental executor remains the supported runtime; bare-metal Native ELF execution is not claimed. The next recommended milestone is historically supported fruit/sound/UI behavior only if those source-backed systems are explicitly brought into scope.
