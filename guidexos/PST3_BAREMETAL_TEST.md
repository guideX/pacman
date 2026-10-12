# PST3 physical bare-metal test procedure

## Status

The UEFI x86-64 image is prepared for a user-run physical test. No computer was
booted and no USB device or disk was written during PST3. QEMU results are
virtualized evidence and must be recorded separately from this procedure's
physical results.

The local development computer was identified as an HP ProDesk 600 G6
Microtower with a 6-core Intel Core i5-10500 and Intel UHD 630 graphics. It is
a candidate test machine, not a completed physical test target. Physical USB
keyboard behavior remains unverified. The guideXOS amd64 source path initializes
the PS/2 keyboard; its USB host-controller header describes UHCI/EHCI, while the
USB HID path is guarded by `KERNEL_HAS_USB_HID`, which is not defined in the
checked-in Server sources. A legacy PS/2 keyboard/controller path may therefore
be needed if the firmware does not provide compatibility for a USB keyboard.
HP's ProDesk 600 G6 QuickSpecs list an optional PS/2/serial card, so first test
the actual machine's standard USB ports and record whether that guideXOS build
recognizes its keyboard. The test assumes that optional card is not installed.

## Build and package

Use PowerShell from the Pac-Man checkout. The production build keeps gameplay
diagnostics and all hosted/bare-metal test hooks disabled (these CMake options
default to `OFF`):

```powershell
$RepoRoot = 'D:\dev\pacman'
$ServerRoot = 'D:\dev\guideXOSServer'
$ArtifactRoot = Join-Path $RepoRoot 'guidexos\artifacts\pst3-baremetal\build-reproduction'
$PackageRoot = Join-Path $ArtifactRoot 'packages'
$BuildRoot = Join-Path $ArtifactRoot 'build-production'
$NativeRoot = Join-Path $ArtifactRoot 'native-production'

cmake -S (Join-Path $RepoRoot 'guidexos') -B $BuildRoot -G Ninja `
  -DGUIDEXOS_SERVER_ROOT=$ServerRoot `
  -DGUIDEXOS_PACKAGE_ROOT=$PackageRoot `
  -DGUIDEXOS_LOCAL_BIN_DIR=$NativeRoot `
  -DPACMAN_ENABLE_DIAGNOSTICS=OFF
cmake --build $BuildRoot --target pacman-native

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File (Join-Path $RepoRoot 'guidexos\tools\prepare_pst3_baremetal.ps1') `
  -ServerRoot $ServerRoot `
  -ProductionPackagePath (Join-Path $PackageRoot 'PacMan') `
  -ArtifactName 'PST3-GuideXOS-PacMan-UEFI-x64-reproduction'
```

The packaging script copies the existing Server ESP into a new Pac-Man-owned
artifact directory, replaces the copied `/Apps/PacMan` package, removes the
ESP's stale Pac-Man build marker from that copy, and calls the existing Server
FAT32/ISO helpers in read-only mode. It writes a FAT32 boot image, a UEFI El
Torito ISO, a GPT disk image, independent verification reports, and hashes. The
Server ESP, kernel, bootloader, and release tooling are inputs only. The release
helper Python environment must already exist at
`D:\dev\guideXOSServer\out\release-tools\venv`; the packaging command does not
bootstrap or modify that other repository.

The packaged media contains `EFI/BOOT/BOOTX64.EFI`, the current guideXOS
`kernel.elf` and `ramdisk.img`, and the production package at
`Apps/PacMan/app.json`, `Apps/PacMan/bin/amd64/pacman.elf`, and its resources.
The expected loader is 64-bit x86 UEFI and the Native ELF is x86-64. The
bootloader is unsigned; if firmware Secure Boot blocks it, temporarily disable
Secure Boot for this live test and restore the previous setting afterwards.

## Prepared artifacts

The verified artifacts currently available are in:

**D:\dev\pacman\guidexos\artifacts\pst3-baremetal\PST3-GuideXOS-PacMan-UEFI-x64-validated**

| File | Size | SHA-256 |
| --- | ---: | --- |
| PST3-GuideXOS-PacMan-UEFI-x64.iso | 134,285,312 bytes | 1E1421F0770E519AB90176BACAB7710FBBBEED4A7A8ABE782B4457692736E2A2 |
| PST3-GuideXOS-PacMan-UEFI-x64.img (GPT disk image) | 135,283,200 bytes | C39D3A8DD34D3AF92AFD1276D22CDA94BF6955235D6439A4A6B0E9BC5FD95D35 |
| PST3-GuideXOS-PacMan-UEFI-BOOT.img (FAT32 boot image) | 134,217,728 bytes | 0A1F607FD58F6D3098C0016286D828EBD0B32FF4139927A760C8D0D8420DAD85 |

The production Pac-Man ELF inside the package has SHA-256
EEB544BBB2E2574EEAED7BB7F0BAD61EA24F6945231B7757223A225B30B8E1CC.
Independent FAT32, UEFI El Torito, and GPT verification passed. The ISO has
not been boot-smoked separately in QEMU or on a physical machine. Use the
packaging command above with a fresh ArtifactName to reproduce it without
overwriting these files.

## Non-destructive boot

1. Prefer the ISO on UEFI optical media, or use the GPT `.img` with a spare USB
   device through a raw disk-image writer. The task does not write either image
   to physical media.
2. Check the artifact's `provenance.json` SHA-256 against the downloaded/copied
   file before booting. Keep the generated ISO/GPT image hashes with the test
   record.
3. Use the firmware's one-time boot menu and select the UEFI optical or USB
   entry. On this HP, press `Esc` during startup, then `F9` for Boot Device
   Options. Menu labels can vary by firmware. Do not change the persistent boot
   order.
4. This is a live gameplay test. Do not run an installer, partition editor, disk
   manager, format command, or storage application. If Windows offers to format
   the test USB after imaging it, cancel the prompt.
5. Record the computer make/model, CPU, graphics device, firmware version and
   settings relevant to Secure Boot and legacy USB/PS/2 input. Record which
   physical keyboard port was used.
6. Photograph or video the bootloader/kernel startup and desktop. A display
   connected to the computer is the physical evidence; QEMU screenshots do not
   substitute for it.
7. Launch **Nexgen PacMan** from the guideXOS application menu. Record a photo
   or short video that shows actual gameplay, all four ghosts, and the game
   screen. The default window is 480 by 640 pixels.

The guideXOS UEFI boot path is expected to use the firmware graphics output.
The image and WAV files are packaged, and the kernel has an audio stream path,
but physical display and audio output have not been verified. Treat silence as
an observation to record, not as proof that Pac-Man input or movement failed.

## Keyboard and gameplay checks

At startup and again near 5, 10, 15 and 30 minutes, record the wall-clock time
and run the same short sequence:

1. Press and release Up, Right, Down, and Left separately. Record whether each
   direction is accepted once Pac-Man reaches a legal maze position.
2. Tap the next direction before each intersection to exercise buffered turns.
   Distinguish the intentional wait for the next legal intersection from a
   direction that was never accepted.
3. Hold a direction through several corners, release it in a corridor, and
   verify movement stops when the maze or game state says it should.
4. Rapidly alternate two directions and then tap three or four directions
   quickly. Confirm Pac-Man takes legal turns and does not remain steered by a
   direction that has already been released.
5. Test turns immediately after tunnel travel, then continue playing for the
   full 15-minute minimum (30 minutes preferred). Repeat the input sequence at
   each observation time and compare it with startup.
6. Observe Red, Pink, Cyan, and Orange separately. Record movement during
   chase/scatter and frightened periods, after a ghost is eaten, and after
   death/respawn. Record Game Over and restart behavior.
7. Complete a maze naturally if practical. Record the level number shown by
   the game; restarting after Game Over begins a new session at level 1.

For a video, retain one uninterrupted clip when practical or record the exact
start/end times of each clip. Time key presses and visible turns from the video
timeline. A legal turn can wait for an intersection, so record both key time and
the first visible direction change. Do not report this as instrumented guest
latency unless a synchronized guest timestamp was collected.

The release image has diagnostics and automated test hooks disabled. It does
not emit per-key, frame-rate, or per-ghost Pac-Man telemetry. Use external
photos/video and contemporaneous notes for this production build. If serial
output can be captured from the actual machine, preserve the raw bytes and note
the physical port and capture device. Record missing telemetry as unavailable;
do not infer a numeric FPS, memory value, or latency from appearance alone.

## Evidence record

For each physical run, retain:

- Exact image name, SHA-256, and `provenance.json`.
- Date/time and local timezone for startup and each 5/10/15/30-minute sample.
- Computer model, CPU, memory, graphics, firmware version/settings, keyboard
  type/port, display connection and resolution, and audio output device.
- Whether UEFI, kernel, keyboard, display, app discovery, and Pac-Man launch
  succeeded; include visible error text or a photograph for any failure.
- Keyboard sequence results, any turns that waited for intersections, ghost
  movement, power-pill/frightened behavior, death/respawn, Game Over/restart,
  natural level transitions, and any loss of responsiveness over time.
- Unedited original photos/video and raw serial logs, with file hashes if
  available. Label user-observed timings separately from instrumented values.
- Total active gameplay time and whether the 15-minute or 30-minute point was
  reached.

If the PS/2 keyboard path works while a USB keyboard does not, record the exact
USB port/controller and firmware legacy-input settings. That would be a
guideXOS hardware-input issue for its Server repository; PST3 must not change
that external repository as part of Pac-Man qualification.

## Hardware references

- [HP ProDesk 600 G6 Commercial Desktops QuickSpecs](https://h30434.www3.hp.com/psg/attachments/psg/Business-PC-Workstation-POS/39001/1/c06640111.pdf)
- [HP Business PCs: one-time boot from USB](https://support.hp.com/rs-en/document/ish_4374279-4216685-16)
- [HP Secure Boot guidance](https://support.hp.com/in-en/document/ish_6930187-6931079-16)
