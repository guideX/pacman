# Isolated guideXOS/QEMU Pac-Man soak

`tools/pacman_hosted_soak.ps1` builds the Pac-Man regression suites, a diagnostic
Native ELF package for live observation, and a separate production Native ELF.
It stages only the diagnostic package into a private copy of an existing
guideXOS Server ESP, boots that copy under QEMU, launches the normal Pac-Man app
with the desktop's F12 shortcut, and injects directional key presses through
the guest's QMP endpoint.

## Run it

From PowerShell on the development host:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\dev\pacman\guidexos\tools\pacman_hosted_soak.ps1 -DurationMinutes 30
```

The default target is 30 minutes. Use `-DurationMinutes 15` for the minimum
qualification window or `-DurationMinutes 60` for an extended run. The runner
accepts 15 through 60 minutes. Optional host/resource paths and guest memory/
vCPU values are script parameters; the defaults target the local development
layout (`D:\dev\guideXOSServer`, QEMU under `C:\Program Files\qemu`, and LLVM
under `C:\Program Files\LLVM`). QEMU uses WHPX acceleration by default when
the installed QEMU and host hypervisor expose it; pass `-Acceleration tcg` to
select software translation explicitly.

The QMP endpoint has a bounded two-minute startup wait, and the guest kernel
main-loop startup bound defaults to five minutes. Initial
creation of the writable desktop directory can require a long FAT free-cluster
scan on a host-backed QEMU disk; the serial PIT heartbeat remains observable
while that scan runs.

Required inputs are the existing Server ESP's `EFI\BOOT\BOOTX64.EFI`,
`kernel.elf`, and `ramdisk.img`; the x86-64 UEFI code image; Python 3; CMake,
Ninja, LLVM clang++/ld.lld; and enough available host memory and CPU capacity.
The runner copies these inputs and records sizes, SHA-256 hashes, branch/HEAD,
process details, and the preexisting QEMU process snapshot. It does not rebuild
the Server repository.

## Isolation and ownership

Every invocation makes a new directory under
`%LOCALAPPDATA%\PacManHostedSoak\<run-id>`. Both CMake build trees and their
package roots are inside that directory. The Server ESP and firmware are copied
there before use; QEMU's writable FAT backing refers to the copied ESP, and
UEFI code is attached read-only. The removable-media boot path does not require
a writable firmware-variable store. QEMU networking and display output are
disabled; QMP binds to loopback on a dynamically allocated port, and the
serial/debug streams and screenshots go to the run directory. Before each QMP
command, the runner verifies that the listening endpoint belongs to its exact
QEMU PID. Capability negotiation has a five-second timeout and retries a
timed-out handshake up to three times before sending a guest command. Shutdown
uses QMP `quit`; forced termination is limited to that PID after its executable
path and command line are checked against the run's unique ownership identifier.

The runner runs with below-normal QEMU priority, two vCPUs, and a 1.5 GiB guest
by default. It checks host CPU and free-memory headroom before launch and again
once per minute. Existing unrelated QEMU processes are recorded and left alone.

## Gameplay and measurements

The guest launches the regular `PacMan` Native ELF with all gameplay test hooks
off. Diagnostics are enabled only in this soak package and emit key transitions
and bounded one-minute health snapshots. After each observed game-over state,
the runner presses Space through the ordinary gameplay input path.

Input follows a repeating 24-direction route with varied 180–280 ms taps,
200–370 ms gaps, rapid four-direction bursts, and periodic 1.5-second corridor
sweeps. This exercises buffered turns, corner changes, corridor traversal, and
tunnels without changing collision rules. Guest serial logs record key down/up,
turn acceptance, Pac-Man progress, simulation timing, and each ghost's mode,
release state, movement expectation, position, and progress age. Ghosts paused
in the house, eaten/returning, or outside active gameplay are marked as not
expected to move; an active ghost without coordinate progress for five seconds
is flagged and captured.

The runner records host QEMU CPU and memory, free host memory, key-delivery
latency, guest FPS, average/p95/p99 frame duration, simulation backlog/drops,
turn latency, and minute-spaced game/ghost observations. It continues after an
isolated missing key event, but stops after three consecutive missing down/up
events; any missing event prevents Outcome A. Captures are made at
guest boot, game launch, and 5, 10, 15, and 30 minutes when those points are
reached, plus the final frame and any possible ghost-stall fault. Each capture
keeps QEMU's original PPM and a PNG conversion made with Python's standard
library.

Each injected direction waits up to 12 seconds for the guest's serial down/up
echo. The recorded host delivery interval ends when that log line is observed,
so it is an upper bound that includes guest handling and serial observation.
The raw QEMU serial log is retained unchanged; the event parser removes
interleaved PIT-heartbeat fragments from its working buffer before matching.
CSV appends tolerate brief Windows sharing locks from concurrent readers.

## Outputs

The run directory contains:

- `run-summary.json`: outcome, elapsed gameplay wall time, evidence gates, first
  and final five-minute performance averages, and artifact paths.
- `inputs.json`, `process.json`, and `logs/qemu-command-line.txt`: image
  provenance and the isolated process/endpoint ownership record.
- `logs/qemu-serial.log`, `logs/pacman-events.log`,
  `logs/validation.log`, and QEMU debug/output logs.
- `host-metrics.csv`, `input-events.csv`, `ghost-observations.csv`, and JSONL
  health samples.
- `regression/`: individual gameplay, audio, and endurance suite output.
- `screenshots/`: authentic guest framebuffer captures (`.ppm` and `.png`).
- Separate diagnostic and production CMake builds, packages, and ELF outputs.

Outcome A requires at least 15 minutes of measured gameplay, successful
production/soak builds, all three established regression suites passing, guest
keyboard and legal-turn evidence, Pac-Man movement between periodic samples,
observed ghost movement, and no possible-stall captures. Otherwise the run
summary is Outcome B and includes the missing evidence or failure. Inspect the
serial log and captures before treating a possible-stall report as a confirmed
game defect.

The runner never edits phase metadata. If `.phase` is missing, it remains
missing. It is safe to rerun; each run uses a fresh runtime directory and does
not overwrite another run's artifacts.
