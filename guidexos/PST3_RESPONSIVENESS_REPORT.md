# PST3 responsiveness, performance, and bare-metal qualification

## Repository safety and starting point

- Repository: `D:\dev\pacman`
- Starting branch: `master`
- Starting HEAD: `faf63394f10a4f4b9f9789d4849da5d2174cd1a1`
- Starting worktree: clean (`master...origin/master`)
- `.phase`: absent
- Branch/worktree operations: none
- Other guideXOS repository: inspected read-only

## PST2 evidence review

The accepted bundle is `%LOCALAPPDATA%\PacManHostedSoak\20261012-013337-291-3338fe76ba`.
It contains the run summary, input event CSV, host metrics CSV, ghost observations,
serial and validation logs, screenshots, build trees, and regression output.
The run completed 1,800.868 seconds of QEMU gameplay. All 1,753 injected
directions had down/up serial echoes, all four ghosts had movement evidence,
there were no possible-stall captures, and simulation time dropped was zero.

The bundle records Pac-Man HEAD `c9507b25ccfd6e4580b4fcad6e3372770ea2ea01`
with modified and untracked Pac-Man files, and does not hash the Pac-Man source
tree. It is useful historical evidence, but it does not prove that those exact
sources equal the clean PST3 starting HEAD. The Server boot artifacts are
individually hashed in `inputs.json`.

### What the frame values measure

The diagnostic app increments `frame_seq` on entry to `render_and_present`.
Every 60 guest-clock seconds it reports
`(frame_seq_delta * 1,000,000) / guest_elapsed_ms` as `fps_milli`. This is the
Pac-Man Native ELF's frame composition/presentation-call rate per guest clock
second. QEMU ran with `-display none`, so the result is not monitor refresh,
physical scanout, or screenshot frequency.

For each call, `frame_avg_us` is the mean of the integer-millisecond guest tick
difference from immediately before `render_game_scene` through return from
`gx_present_frame`, multiplied by 1,000. It includes scene composition and the
synchronous presentation ABI call, but excludes time between calls. The p95 and
p99 values use millisecond buckets (with values at or above 128 ms clamped to
the final bucket). Thus a 29 ms average call cost can coexist with a 14 Hz call
rate: the call costs about 29 ms, while the average start-to-start interval is
about 71 ms. The unmeasured remainder is event-loop waiting and other work.

The run summary's first and final five minute averages were:

| Measure | First five minutes | Final five minutes |
| --- | ---: | ---: |
| Frame compose/present calls per guest second | 14.072 | 13.820 |
| Compose plus present call cost | 29.795 ms | 29.286 ms |
| Call cost p95 / p99 | 32.5 / 50 ms | 36 / 44 ms |
| Simulation time dropped | 0 ms | 0 ms |
| QEMU CPU, normalized to two vCPUs | 37.809% | 49.577% |
| QEMU working set / private bytes | 1,601.42 / 1,847.40 MB | 1,601.60 / 1,847.40 MB |

These first/last results show no progressive frame-rate or memory decline in
that QEMU run. The QEMU process memory includes the preallocated 1.5 GiB guest;
it is not a Pac-Man heap measurement. Frame-call cost averaged above a 16.7 ms
60 Hz budget. Source review found that the Native ELF compile command supplied
no optimization level, leaving clang at its `-O0` default. PST3 tried broad
compiler optimization first, but `-O2` output faults in the current Native ELF
loader/runtime before the first game frame. The retained optimization removes
repeated per-pixel bounds checks from sprite and pill drawing while preserving
the established compiler flags.

### What the input values measure

PST2's `delivery_ms` starts before `Send-GuestKey` and stops after the runner
reads the matching guest key-down record from the QEMU serial log. Every key
uses a new QMP TCP connection and capability negotiation before the HMP
`sendkey` command. The measurement also includes guest event dispatch, guest
serial logging, host file observation, and up to one 20 ms polling interval.
It is therefore an upper bound from host automation start to observed guest
log, not an isolated hardware or guest input latency.

PST2 recorded 476.917 ms average, 584.340 ms p95, 1,232.949 ms p99, and a
3,375.176 ms maximum for that composite interval. It did not time the QMP
round trip separately, so the data cannot attribute those milliseconds to
QMP scheduling versus guest/serial observation. Host timestamps and guest
`tick_ms` were not synchronized, so they must not be subtracted.

The guest-side turn timer starts when the app dequeues a direction key event
and measures until a later 10 ms simulation update accepts a legal turn. It
includes intentional travel to the next aligned maze point. PST2 reports a
cumulative 151 ms average and 960 ms maximum among timed pending requests, but
each new key-down can replace the single pending request timestamp. Those
figures are not one independent latency sample for every host input.

Code review confirms that `game_press_direction` immediately stores the held
and requested direction. A released tap remains buffered until its first legal
junction. Once a held turn is accepted, key-up clears that persistent steering
preference; focus loss clears all held directions. The QEMU evidence does not
confirm a Pac-Man input-state defect. The current runner now records QMP time,
post-QMP serial observation time, guest down/up clock values, and one-minute
turn-latency windows separately.

The 53 Game Over/restart cycles do not establish a level-progression defect:
the normal restart resets a new session to level 1, and the deterministic route
did not complete a maze. The run did not exercise a naturally completed level.

## PST3 implementation and qualification

### Changes

- Pac-Man sprite and pill rendering now clips bounds once before entering the
  pixel loops. This removes repeated per-pixel bounds helper work without
  changing the 10 ms simulation step, movement speed, or maze rules. A partial
  sprite clipping regression test was added.
- The live diagnostics now report call interval, scene composition, present
  call, fixed-step update, direction-state call, and legal-turn-to-next-present
  timings. The health-log buffer was enlarged after run 2 showed that its
  trailing dropped-time field could be truncated. These diagnostics are
  compiled out of the production ELF.
- The hosted runner now reports QMP connection, capability negotiation, HMP
  sendkey, host-wrapper residual, post-QMP serial observation, guest held
  duration, and turn latency separately. Its serial reader uses bounded reopen
  and retry reads. The host frame-call rate and guest-clock/host-wall ratio are
  also recorded.
- Source review found no Pac-Man input-state defect to fix. Direction presses
  set the requested direction immediately; a released tap remains buffered
  until a legal maze alignment, key-up clears held steering, and focus loss
  clears held directions. The virtual run delivered down/up observations for
  every attempted direction and finished with no held direction. The original
  “hard to steer” symptom was not reproduced as a Pac-Man state bug.
- Broad and renderer-only Native ELF -O2 builds faulted in the current loader
  before the first game frame. Production compiler settings were left at their
  existing mode.

### PST3 input measurements

The corrected run was isolated QEMU gameplay, run ID
20261012-143504-162-d715b8be1e. It recorded 814 direction attempts, zero
delivery failures, and down/up serial observations for every attempt. Host
latencies use Stopwatch. Guest ticks remain in the guest clock domain.

| Boundary | Whole run | First five minutes | Final five minutes |
| --- | ---: | ---: | ---: |
| QMP API round trip | avg 456.427 ms; p95 592.735; p99 644.733; max 741.930 | avg 445.142 ms; p95 596.224; p99 626.561 | avg 458.230 ms; p95 571.344; p99 645.091 |
| QMP TCP connect | avg 0.277 ms | avg 0.280 ms | avg 0.273 ms |
| QMP capabilities | avg 11.313 ms | avg 12.524 ms | avg 9.234 ms |
| QMP sendkey command and response | avg 16.006 ms | avg 15.255 ms | avg 16.135 ms |
| Host wrapper residual | avg 428.831 ms | avg 417.082 ms | avg 432.588 ms |
| After QMP to guest serial observation | avg 81.024 ms; p95 352.147; p99 766.847; max 1,481.460 | avg 89.830 ms; p95 377.096; p99 671.604 | avg 44.400 ms; p95 87.026; p99 604.744 |
| Host input start to guest serial observation | avg 537.452 ms; p95 789.874; p99 1,234.093 | avg 534.973 ms; p95 826.851; p99 1,136.688 | avg 502.629 ms; p95 630.645; p99 1,023.207 |
| Guest key held duration | avg 181.069 ms; p95 250; p99 380; max 1,320 | avg 180.659 ms | avg 186.844 ms |

The QMP API includes a new TCP connection, capability negotiation, sendkey
request/response, and a host ownership check. The roughly 429 ms wrapper
residual is not QMP transmission time. Source review shows that it includes the
per-input PowerShell QEMU-endpoint ownership query and other unseparated client
overhead. The ownership query was not timed independently, so the runner cannot
assign that residual precisely. The additional 81 ms average is the wait after
the QMP API returns until the guest key event is read from serial; it includes
guest dispatch/logging and host polling and is an upper bound, not a direct
keyboard-device latency. The measurements explain why the original host
automation figure near 477 ms must not be interpreted as Pac-Man taking that
long to process an input.

Guest request-to-legal-turn time averaged 102 ms over the run, with an 840 ms
maximum. In the first five-minute host window the one-minute guest windows
averaged 120.667 ms; in the final five minutes they averaged 96 ms. The latest
guest window contained 24 accepted requests and averaged 51 ms, with a 310 ms
maximum. These intervals start when Pac-Man processes a direction request and
end when a later fixed-step update reaches a legal turn. They include travel to
the next maze intersection; a newer key-down can replace the pending request.
The runner therefore does not count them as pure input-dispatch latency.

The next successful presentation ABI call followed legal turn acceptance by
an average 28 ms in the first window and 26 ms in the final window. This is a
guest-side response proxy only. The QEMU run used display-none mode and neither
it nor the available timestamps measured monitor scanout or synchronized
input-to-visible-pixel latency. Physical key delivery and visible response
remain unmeasured.

### PST3 frame and resource measurements

The corrected runner reported per-window host-sampled summaries. Some adjacent
host rows repeat a guest timing record; the run documentation identifies fresh
records by frame_sequence, guest_elapsed_ms, and
timing_observation_span_s. These windows are descriptive comparisons rather
than matched game-state benchmarks.

| Measure | First five minutes | Final five minutes |
| --- | ---: | ---: |
| Pac-Man present-call rate per guest fixed-step second | 15.046 calls/s | 17.182 calls/s |
| Present-call rate per host Stopwatch second | 12.672 calls/s | 14.057 calls/s |
| Guest elapsed-clock rate over host timing span | 80.430% | 81.798% |
| Composition plus present-call cost | 26.071 ms | 23.744 ms |
| Present API stage | 25.333 ms | 23.200 ms |
| Guest-clock call start interval | 66.333 ms | 57.800 ms |
| Call interval p95 / p99 | 160 / 500 ms | 102 / 490 ms |
| Fixed-step dropped simulation time | 0 ms | 0 ms |
| QEMU CPU, normalized to two vCPUs | 36.042% | 49.426% |
| QEMU working set / private bytes | 1,602.780 / 1,848.060 MB | 1,602.900 / 1,847.940 MB |

The original 14 FPS and 29 ms values are compatible: FPS is calls per guest
simulation-clock second, while 29 ms is time spent inside each composition and
presentation call. Calls were about 71 ms apart, so roughly 42 ms per interval
was outside that measured call. It was not presentation frequency or scanout.
The same distinction applies to PST3. Present calls remained below 60 per
second, and call cost remained above a 16.7 ms 60 Hz budget. Within PST3,
average call cost fell and call rate rose late in the run, but the windows were
not controlled same-state measurements; do not attribute the entire difference
to the renderer change.

Composition, fixed-step update, and direction-state-call measurements mostly
quantize to zero at the guest timer's 1 ms resolution; zero means below that
resolution, not literally free. The present API stage accounts for nearly all
of measured call duration. Read-only inspection of the guideXOS Server path
found that NativeWindowOwner::present allocates a new full-frame pixel buffer
and copies the full 480 by 640 image byte by byte on every present. Its caller
also emits synchronous serial records and pumps the desktop for each frame.
That is the leading measured bottleneck and resides in the separate
guideXOSServer repository; PST3 did not modify it. QEMU display-none execution
does not measure the cost or refresh rate of physical display hardware.

Audio processing was not timed as a separate stage. The diagnostics bracket
game_update, while pacman_audio_process_game_events runs after the update; the
audio regression suite passed 42 checks, but that does not isolate audio cost
or qualify a physical audio backend.

At the end of the 900.949-second host gameplay interval, guest ticks measured
700.330 seconds and fixed-step simulation measured 660.180 seconds (66,018
10 ms steps). The latter excludes non-playing game states; the runner recorded
zero dropped simulation milliseconds, zero current backlog, a 460 ms maximum
backlog, and at most eight catch-up steps per frame. This guest-clock/host-wall
difference is a QEMU timing observation to compare against physical hardware,
not proof of a Pac-Man slowdown.

### Isolated QEMU qualification

The run completed 900.949 seconds of active wall-clock gameplay in a private
two-vCPU, 1.5 GiB QEMU x86-64 UEFI guest. The QEMU process used WHPX, no
network, and no display output. The runner returned QEMU_PASS. It observed:

- 814 attempted direction inputs, 814 down/up pairs, and no delivery failures.
- 58 deaths and 19 ordinary Game Over restarts; each new session began on
  level 1. The route did not naturally complete a maze or advance a level.
- 10 power pills, 40 frightened transitions, and 15 tunnel wraps. No ghost was
  eaten by this route.
- Recent movement from all four ghosts while movement was expected, with zero
  possible-stall captures.
- Zero dropped simulation time in both the first and final five-minute
  windows. Frame calls and memory did not show a progressive decline.
- QEMU memory remained effectively flat. These process totals include the
  preallocated guest memory and are not a Pac-Man heap measurement.

This is virtualized qualification only. It is separate from the deterministic
hosted suites and from physical bare-metal testing.

### Regression and production build

The three established suites passed 1,675 checks across 47 groups:

| Suite | Groups | Checks |
| --- | ---: | ---: |
| Gameplay | 36 | 1,548 |
| Audio | 6 | 42 |
| Endurance | 5 | 85 |

The production Native ELF built with diagnostics and test hooks disabled. Its
SHA-256 is
EEB544BBB2E2574EEAED7BB7F0BAD61EA24F6945231B7757223A225B30B8E1CC. The
diagnostic soak package used for live QEMU observation had diagnostics enabled
and all gameplay test hooks disabled.

The deterministic endurance suite's seven level states come from a test-only
maze setup that removes all but the final pill. It is not evidence that the
ordinary virtual route or a physical player naturally completed seven levels.

### Prepared physical boot image

The generated artifacts are in the Pac-Man-owned, Git-ignored directory
guidexos/artifacts/pst3-baremetal/PST3-GuideXOS-PacMan-UEFI-x64-validated:

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| PST3-GuideXOS-PacMan-UEFI-x64.iso | 134,285,312 bytes | 1E1421F0770E519AB90176BACAB7710FBBBEED4A7A8ABE782B4457692736E2A2 |
| PST3-GuideXOS-PacMan-UEFI-x64.img (GPT disk image) | 135,283,200 bytes | C39D3A8DD34D3AF92AFD1276D22CDA94BF6955235D6439A4A6B0E9BC5FD95D35 |
| PST3-GuideXOS-PacMan-UEFI-BOOT.img (FAT32 boot image) | 134,217,728 bytes | 0A1F607FD58F6D3098C0016286D828EBD0B32FF4139927A760C8D0D8420DAD85 |

The package contains the current production Native ELF, matching the hash
above, with the existing x86-64 guideXOS UEFI loader, kernel, ramdisk, app
manifest, and Pac-Man resources. FAT32, UEFI El Torito, and GPT structure
verification passed. The ISO was format-verified but was not separately
boot-smoked in QEMU or on physical hardware. Reproduction commands, image
checksums, and the non-destructive live-boot procedure are in
PST3_BAREMETAL_TEST.md.

The local HP ProDesk 600 G6 Microtower with i5-10500 and UHD 630 is only a
candidate system. No physical computer was booted, no USB device was written,
and no physical Pac-Man gameplay was observed. The checked-in guideXOS kernel
path initializes PS/2 keyboard support; the USB HID path is guarded by a macro
not defined in the inspected Server sources. USB keyboard operation on the
candidate machine is therefore a specific compatibility item for the physical
test.

## Outcome

**Outcome B — implementation, regression suites, production build, image
preparation, and 15-minute isolated QEMU qualification are complete;
physical bare-metal execution is pending.** No physical photos, video,
latency, frame-rate, ghost, or endurance observations are claimed.

The virtualized evidence and prepared physical procedure do not qualify Outcome
A until an actual computer boots this image, launches Pac-Man, and completes at
least 15 minutes of responsive physical gameplay. Physical measurements must
be added as their own evidence, without combining them with QEMU or hosted
simulation figures.

## Evidence locations

- QEMU run summary: %LOCALAPPDATA%\PacManHostedSoak\20261012-143504-162-d715b8be1e\run-summary.json
- QEMU input events: %LOCALAPPDATA%\PacManHostedSoak\20261012-143504-162-d715b8be1e\input-events.csv
- QEMU host metrics: %LOCALAPPDATA%\PacManHostedSoak\20261012-143504-162-d715b8be1e\host-metrics.csv
- QEMU ghost observations: %LOCALAPPDATA%\PacManHostedSoak\20261012-143504-162-d715b8be1e\ghost-observations.csv
- QEMU serial and validation logs: %LOCALAPPDATA%\PacManHostedSoak\20261012-143504-162-d715b8be1e\logs
- Authentic QEMU guest screenshot: %LOCALAPPDATA%\PacManHostedSoak\20261012-143504-162-d715b8be1e\screenshots\gameplay-15min-final.png
- Bare-metal image and provenance: D:\dev\pacman\guidexos\artifacts\pst3-baremetal\PST3-GuideXOS-PacMan-UEFI-x64-validated

No independently reproducible Pac-Man input-state defect remains from the
evidence collected. The main outstanding performance issue is the measured
guideXOS Server presentation path described above. It should be handled in
that repository as a separate task.
