# Historical Nexgen session and HUD investigation

Investigation date: 2026-07-29

The VB6 project in `PacMan.vbp`, `frmPacMan.frm`, `basPacSetUp.bas`, and
`basPacman.bas` is authoritative. The form is a 448x553 fixed single-window
layout. The native implementation intentionally excludes coins, credits,
two-player play, sound, and persistent storage as required by this milestone.

## Source trace

`frmPacMan.frm:388-398` initializes `Game.Started = False` and
`Game.HiScore = 10000`. The initial form is a title/coin screen with
`Nexgen Pacman`, `INSERT COINS`, credit/player labels, and the flashing insert
label timer. `frmPacMan.frm:434-461` accepts `I` for a credit, `1` for one
player after a credit, and `2` for two players after two credits. Because coin
and credit accounting are explicitly excluded, native launch enters a bounded
single-player `InitialReady` presentation instead of exposing a partial coin
screen or inventing a start key.

`frmPacMan.frm:481-520` initializes three lives, level one, score zero, the
in-memory high score, the clean level, actor positions, and the HUD. It then
starts the intro. `frmPacMan.frm:676-714` disables the keyboard timer while
`tmrReady` flashes `READY!`; the first-go path waits nine 500 ms callbacks and
the ordinary death path waits six callbacks. Native keeps the initial nine
callback duration (450 fixed 10 ms steps) and the ordinary bounded 300-step
Ready duration. The latter is also used for level/restart presentation because
the source's first-go wait is coupled to the omitted opening music.

`basPacSetUp.bas:128-154` is the complete bottom HUD rule: draw `Lives - 1`
reserve Pac-Man icons at the left and draw fruit cells from the current level
backward at the right. `frmPacMan.frm:270-386` places SCORE/value on the left
and the right-aligned HI SCORE/value on the top strip. `basPacSetUp.bas:469-503`
updates the high score immediately whenever the score exceeds it. No file read,
file write, or persistent high-score path exists in the inspected source.

`frmPacMan.frm:524-536` uses `tmrFlash` only to alternate the insert-coin label
color. There is no source power-pill visibility toggle. `basPacSetUp.bas:513-548`
draws every unconsumed power pill once during `RefreshLevel`; subsequent
visibility changes are caused only by `PacLevel(...).Block = 0` on consumption.
The native power-pill blink is therefore an explicit requested approximation,
not a claim about an existing VB6 cadence: one shared phase, initially visible,
50 fixed 10 ms steps per toggle, no logical maze mutation, and no render-time
dependency. It is stopped with gameplay in non-Playing states and resumes from
the same phase after focus returns.

`frmPacMan.frm:539-584` shows the historical update order. It has no explicit
Pause or Resume branch, and the only non-movement key behavior in the source is
Escape, speed/enhanced toggles before play, and the coin/player controls. Native
focus loss clears directional state and stops fixed-step advancement without
silently converting focus loss into a new pause feature.

## Historical remaining-feature inventory

| Feature | Historical evidence | Current native status | Action |
| --- | --- | --- | --- |
| Initial title/start screen | `frmPacMan.frm:1-386`, `Game.Started=False`, title/coin labels | Partial; coin/credit screen is intentionally excluded | Keep native package single-player launch bounded; defer cabinet title/coin flow |
| First Ready state | `frmPacMan.frm:481-520`, `676-714`, nine 500 ms callbacks | Explicit and complete as `InitialReady`, with source duration | Implemented and deterministic |
| Start input | `frmPacMan.frm:440-448`, key `1` only when credits > 0 | Present in source but unreachable in native scope | Intentionally deferred with coin/credit infrastructure |
| Pause | No Pause/Resume identifier or timer-toggle key branch found | Absent | Document intentional absence; do not add speculative pause |
| High-score display | `frmPacMan.frm:270-386`, `lblHiscore` | Explicit and complete; right-aligned in top HUD | Implemented |
| High-score initialization | `frmPacMan.frm:396`, `Game.HiScore=10000` | Explicit and complete | Implemented |
| High-score update | `basPacSetUp.bas:487-490` | Explicit and complete; immediate update | Implemented |
| High-score persistence | No file/API read or write for `HiScore` found | Absent from source | Intentionally deferred; runtime in-memory semantics only |
| Level indicator | `Pacman.Level`, fruit mapping, `ShowLives` | Source has no textual LEVEL label; current level is represented by fruit history and diagnostics | Implemented as historical fruit-history indicator; no modern LEVEL label |
| Fruit-history indicator | `basPacSetUp.bas:128-154`, right-to-left fruit cells | Explicit and complete for levels 1-8, clamped at 8 | Implemented |
| Spare-life icons | `basPacSetUp.bas:136-142`, loop `1 To Lives-1` | Explicit and complete; active life excluded, 14 visible slots | Implemented |
| Score popup behavior | No score-popup control or blit path; fruit clears then scores | Absent from source | Intentionally absent |
| Death animation | `PacDied` decrements/stops timers; no dedicated Pac-Man death sequence in `.bas` | Partial; bounded native death overlay/sprite presentation | Retain deliberate minimal substitute; no invented frames |
| Maze flashing | No maze inversion/color-flash branch in the inspected session code | Absent | Retain bounded native LevelComplete text/state only |
| Game Over text | Final `PacDied` hides game screen and restores title/coin labels; no in-maze GAME OVER label | Partial; native in-game `GAME OVER` is a scoped visual substitute | Keep for single-player visibility; document difference |
| Restart input | No restart key after Game Over; new game is key `1` after credits | Partial; Enter/Space restart native Game Over | Keep bounded native restart needed for standalone scope; cabinet restart remains deferred |
| Attract/demo behavior | No `Demo`/`Attract` state or AI path found | Absent | Intentionally deferred |
| Two-player state | `Game.Players`, `CurrentPlay`, `PacmanBackUp`, map swap in `PacDied` | Present in historical source, absent in native | Intentionally deferred |
| Coin/credit state | `Game.Coins`, `ShowCoins`, `I`, `1`, `2` handlers | Present in historical source, absent in native | Intentionally deferred |
| Sound-dependent timing | `StartMusic`, synchronous `killed`, async pill/fruit/ghost/life sounds | Sound calls are recorded; no playback added | Next milestone: audio-event extraction/integration planning |
| Ready blinking | `tmrReady` 500 ms callback, odd/even counter | Explicit and complete with fixed-step text phase | Implemented |
| Insert-label blinking | `tmrFlash_Timer`, `tmrFlash.Interval=500` | Not part of native gameplay HUD | Deferred with excluded title/coin screen |
| Power-pill blinking | No source toggle; `RefreshLevel` draws power pill once | Requested approximation: shared 500 ms/50-step phase | Implemented and documented as non-source visual polish |
| Focus loss | No explicit source focus event; keyboard timer continues unless UI changes | Native clears held/requested direction and stops simulation while unfocused | Implemented as safe source-compatible behavior; no catch-up burst |
| Hidden debug behavior | `E` enhanced toggle and `S` speed toggle before play; no gameplay cheat path | Not exposed in production | Intentionally deferred with excluded pre-game controls |
| Unused/unreachable historical UI | PictureBox buffers, `lblPookie`, title/player/credit labels when gameplay starts | Visual assets/source controls only in excluded flows | Preserve evidence; do not port dormant controls |

## Completion classification

This pass is **B. Historical session complete; some conventional features
absent** for the scoped standalone single-player runtime. The source-backed
score/high-score HUD, fruit history, reserve-life semantics, initial/ordinary
Ready presentation, focus behavior, Game Over/restart state, and requested
power-pill visual phase are implemented. Pause, persistence, coin/credit,
two-player, attract/demo, sound, and source title-screen cabinet flow remain
explicitly absent or deferred rather than inferred.

## Validation obligations

Host-independent coverage now includes initial Ready blocking and exit, high
score initialization/update/restart persistence, exact blink boundaries,
shared phase behavior, hidden-pill consumption, no reappearance after
consumption, and level/new-game phase resets. Existing pill, frightened,
fruit, life, four-ghost, level, death, Game Over, and restart regressions remain
in the same suite.

Validation-only frame diagnostics append high score, power-pill visibility, and
blink countdown without changing the production frame format or package. The
production ELF is built with all validation options off; hosted and QEMU capture
proof must be run through the existing synchronized compositor path before the
milestone can be classified A rather than B.

Recommended next milestone: extract the source sound-event table and design
integration points against guideXOS Server's future sound subsystem. Do not add
an application-specific bare-metal sound driver to Pac-Man.
