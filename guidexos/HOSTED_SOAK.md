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

QMP socket connection, capability negotiation, and sendkey-command times are
recorded separately, along with the host wrapper residual. That residual also
contains the per-input PowerShell QEMU-endpoint ownership query,
serialization/stream setup, and other client overhead; it is not QMP wire time
and is not individually decomposed. The runner records
when the guest key event appears in serial, guest down/up ticks, and later legal
turn acceptance. Serial observation is an upper bound on actual guest delivery;
host and guest clocks are never subtracted from each other.

The runner records host QEMU CPU and memory, free host memory, staged input
timings, render/present calls per guest-clock second and per host-measured second,
the guest-clock/host-wall ratio, frame-call cost and start intervals,
scene-composition and presentation call costs, fixed-step `game_update` cost,
the cost of Pac-Man's direction-state calls, legal-turn-to-presentation-call
latency, simulation backlog/drops, and minute-spaced game/ghost observations.
The diagnostic durations use the guest's 1 ms clock, so values below that
resolution appear as zero. A completed presentation ABI call is not a scanout
measurement. `host-metrics.csv` is sampled on the host clock and keeps the most
recent guest timing record; when a guest timing line arrives less often than
the host sample interval, adjacent rows can repeat the same `frame_sequence`
and `guest_elapsed_ms`. Use those fields to identify fresh guest observations.
`host_frame_calls_fps` and `guest_clock_pct_host_wall` use the recorded
`timing_observation_span_s` between fresh timing lines; first/final window
averages are descriptive host-sample summaries, not a matched-state benchmark.

The runner continues after an isolated missing key event, but stops after three
consecutive missing down/up events; any missing event prevents a QEMU pass.
Captures are made at
guest boot, game launch, and 5, 10, 15, and 30 minutes when those points are
reached, plus the final frame and any possible ghost-stall fault. Each capture
keeps QEMU's original PPM and a PNG conversion made with Python's standard
library.

Each injected direction waits up to 12 seconds for the guest's serial down/up
echo. The input CSV splits the QMP API round trip from the subsequent wait for
the guest key-event log. A QMP round trip opens a new connection, negotiates
capabilities, sends the HMP `sendkey` request, and waits for the response. The
host-to-log interval starts just before that sequence and ends when the guest
event is read from the serial file; it is an upper bound on guest delivery and
includes event handling, serial output, and host polling. Guest key-down/up
ticks and their difference stay in the guest clock domain; host Stopwatch
durations are not subtracted from guest ticks. The raw QEMU serial log is
retained unchanged; the event parser removes interleaved PIT-heartbeat
fragments from its working buffer before matching. CSV appends tolerate brief
Windows sharing locks from concurrent readers.

`guest_fps` is the number of Pac-Man `render_and_present` calls per second of
the guest's fixed-step clock. The frame sequence advances when the app begins
that call. It is not QEMU display refresh, monitor scanout, or a screen-capture
rate; this runner uses `-display none`. `frame_avg_us` measures from just before
scene composition through return from `gx_present_frame`, using the guest's
integer-millisecond tick API and converting the mean to microseconds. The
reported p95/p99 are millisecond histogram buckets. These costs omit idle time
between calls. `frame_interval_avg_ms` and its p95/p99 measure start-to-start
intervals between those calls, again using guest ticks. The renderer clips
sprite and pill bounds once before their inner loops to avoid per-pixel helper
calls. Native ELF compiler flags remain at their established mode: broad and
renderer-only `-O2` builds both faulted before the first frame under the current
loader/runtime. Diagnostic logging remains compiled out of production.

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

`QEMU_PASS` requires at least 15 minutes of measured gameplay, successful
production/soak builds, all three established regression suites passing, guest
keyboard and legal-turn evidence, Pac-Man movement between periodic samples,
observed ghost movement, and no possible-stall captures. Otherwise the run
summary is `QEMU_PARTIAL` and includes the missing evidence or failure. These
labels describe only the isolated virtualized run; they do not qualify physical
bare-metal execution. Inspect the serial log and captures before treating a
possible-stall report as a confirmed game defect.

## PST3 responsiveness findings

The PST3 result and measurement interpretation are recorded in
PST3_RESPONSIVENESS_REPORT.md. Its 15-minute QEMU run completed with
QEMU_PASS: 814 direction attempts, no delivery failures, zero dropped
simulation time, and recent movement from all four ghosts. The reported guest
frame rate remains a Native ELF present-call rate, not a physical display
refresh measurement.

For run 20261012-143504-162-d715b8be1e, the host input-start to guest serial
observation upper bound averaged 537 ms. The QMP sendkey command averaged 16
ms, the full QMP API round trip averaged 456 ms, and the wait after QMP until
the guest serial observation averaged 81 ms. The large approximately 429 ms
host wrapper residual includes the PowerShell endpoint-owner query and other
host-side overhead; the current runner does not measure the ownership query
separately. Guest request-to-legal-turn averaged 102 ms and includes waiting
for a legal maze alignment.

The guideXOS Server present path was inspected read-only. It allocates and
copies the full framebuffer and emits synchronous serial records on each
presentation call. That stage accounts for nearly all measured Pac-Man
present-call duration in QEMU. Fixing it requires a separate guideXOS Server
change. Physical display timing and keyboard behavior are not represented by
the QEMU measurements.

The runner never edits phase metadata. If `.phase` is missing, it remains
missing. It is safe to rerun; each run uses a fresh runtime directory and does
not overwrite another run's artifacts.
