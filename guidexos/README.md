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

## Native platform additions

The existing `guidexos-c-abi-v1` table keeps its original member order and appends these general-purpose calls:

- `request_window_ex(..., flags, ...)`, with `GX_WINDOW_FLAG_FIXED_SIZE` for non-resizable windows.
- `file_read(..., offset, ...)`, which reads sequential chunks up to 64 KiB so package resources larger than `file_read_all` can be streamed safely.
- `present_frame(...)`, which copies a complete XRGB8888 frame into compositor-owned storage.

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

## Current limitations and next milestone

This milestone is static only: no movement, timers, ghost AI, collision handling, scoring, credits, two-player mode, sound, high scores, gamepads, fullscreen, or bare-metal Native ELF execution. Only the hosted amd64 experimental executor is supported. In the current live server worktree, manifest discovery, ELF validation, image loading, and runtime preparation pass, but valid Native ELF package-file calls abort before the app can present its frame; the hosted Navigator worktree also currently prevents rebuilding the experimental server because `CssDiagnostics::runtimeFocusRecomputations` is referenced but absent. Therefore this cycle is classified as outcome B, not a successful scene launch. The next recommended cycle is to fix that host file-call/runtime bookkeeping boundary, rerun the retained-frame smoke test, and then add a small app-side retained game state with monotonic timing and keyboard direction events. Audio should remain a separate later platform slice.
