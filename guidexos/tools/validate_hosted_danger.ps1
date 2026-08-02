using namespace System
using namespace System.Diagnostics
using namespace System.Drawing
using namespace System.Drawing.Imaging
using namespace System.IO
using namespace System.Runtime.InteropServices

$ErrorActionPreference = 'Stop'

Add-Type -ReferencedAssemblies @('System.Drawing', 'System.Windows.Forms') @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;
public static class PacManDangerCapture5 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    public const uint KEYUP = 0x0002;
    public static void Key(IntPtr hwnd, byte key, bool down) {
        if (hwnd == IntPtr.Zero) throw new InvalidOperationException("The hosted compositor handle is invalid.");
        SetForegroundWindow(hwnd);
        keybd_event(key, 0, down ? 0u : KEYUP, UIntPtr.Zero);
    }
    public static void CapturePrimary(string path) {
        Rectangle bounds = Screen.PrimaryScreen.Bounds;
        using (Bitmap bitmap = new Bitmap(bounds.Width, bounds.Height))
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            graphics.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size);
            bitmap.Save(path, ImageFormat.Png);
        }
    }
    public static bool CaptureCompositor(string path, IntPtr hwnd) {
        RECT rect;
        if (hwnd == IntPtr.Zero || !GetWindowRect(hwnd, out rect)) return false;
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;
        if (width <= 0 || height <= 0) return false;
        using (Bitmap bitmap = new Bitmap(width, height))
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            SetForegroundWindow(hwnd);
            // gui.sync has already completed the compositor WM_PAINT. Capture
            // the visible hosted HWND surface so this helper cannot reuse
            // PrintWindow's older backing result.
            graphics.CopyFromScreen(new Point(rect.Left, rect.Top), Point.Empty, bitmap.Size);
            bitmap.Save(path, ImageFormat.Png);
        }
        return true;
    }
}
'@

$pacmanRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = 'D:\dev\guideXOSServer'
$serverRootFull = [IO.Path]::GetFullPath($serverRoot).TrimEnd('\')
$serverExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.experimental.exe'))
$normalServerExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.exe'))
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$rawLog = Join-Path $serverRoot "hosted-pacman-danger-$runId-raw.log"
$summaryPath = Join-Path $serverRoot "hosted-pacman-danger-$runId-validation.txt"
$captureDirectory = Join-Path $pacmanRoot 'captures'
$results = [System.Collections.Generic.List[string]]::new()
$ownedPids = [System.Collections.Generic.List[int]]::new()
$process = $null
$compositor = [IntPtr]::Zero
$launchCount = 0
$failed = $false
$previousFrameDiagnostics = [Environment]::GetEnvironmentVariable('GXOS_PACMAN_FRAME_DIAGNOSTICS', 'Process')
$previousFreezeDiagnostics = [Environment]::GetEnvironmentVariable('GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS', 'Process')

function Get-ServerProcesses {
    $paths = @($serverExe, $normalServerExe)
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -and ($paths -contains ([IO.Path]::GetFullPath($_.ExecutablePath)))
    })
}

function Add-OwnedDescendants {
    for ($pass = 0; $pass -lt 5; ++$pass) {
        $snapshot = @(Get-CimInstance Win32_Process)
        foreach ($item in $snapshot) {
            if (-not $item.ExecutablePath -or -not $ownedPids.Contains([int]$item.ParentProcessId)) { continue }
            $fullPath = [IO.Path]::GetFullPath($item.ExecutablePath)
            if ($fullPath.StartsWith($serverRootFull + '\', [StringComparison]::OrdinalIgnoreCase) -and
                -not $ownedPids.Contains([int]$item.ProcessId)) {
                [void]$ownedPids.Add([int]$item.ProcessId)
            }
        }
    }
}

function Read-RawLog {
    if (Test-Path -LiteralPath $rawLog) {
        try {
            $stream = [FileStream]::new($rawLog, [FileMode]::Open, [FileAccess]::Read, [FileShare]::ReadWrite)
            $reader = [StreamReader]::new($stream)
            try { return $reader.ReadToEnd() }
            finally { $reader.Dispose(); $stream.Dispose() }
        } catch { return '' }
    }
    return ''
}

function Wait-ForLog([string]$pattern, [int]$timeoutMs = 12000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        if ((Read-RawLog) -match $pattern) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Wait-ForLogCount([string]$pattern, [int]$minimumCount, [int]$timeoutMs = 12000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        if ([regex]::Matches((Read-RawLog), $pattern).Count -ge $minimumCount) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Get-LogCount([string]$pattern) {
    return [regex]::Matches((Read-RawLog), $pattern).Count
}

function Convert-FrameMatch([System.Text.RegularExpressions.Match]$match) {
    if (-not $match.Success) { return $null }
    return [pscustomobject]@{
        Sequence = [uint64]$match.Groups[1].Value
        WindowId = [uint64]$match.Groups[2].Value
        State = $match.Groups[3].Value
        Step = [uint64]$match.Groups[4].Value
        Score = [uint32]$match.Groups[5].Value
        Lives = [uint32]$match.Groups[6].Value
        Level = [uint32]$match.Groups[7].Value
        X = [int]$match.Groups[8].Value
        Y = [int]$match.Groups[9].Value
        Width = [int]$match.Groups[10].Value
        Height = [int]$match.Groups[11].Value
        Stride = [uint32]$match.Groups[12].Value
        Bytes = [uint32]$match.Groups[13].Value
        Result = [uint32]$match.Groups[14].Value
        ObservedAtUtc = [DateTime]::UtcNow
    }
}

function Get-LatestCompositorFrameSequence {
    $matches = [regex]::Matches((Read-RawLog), 'Compositor frame replaced windowId=(\d+) incomingFrameSeq=(\d+) frameGeneration=(\d+)')
    if ($matches.Count -eq 0) { return [uint64]0 }
    return [uint64]$matches[$matches.Count - 1].Groups[2].Value
}

function Get-LatestValidationFrame([string]$state, [uint64]$minimumSequence = 0, [int]$score = -1, [int]$lives = -1, [uint64]$maximumSequence = 0) {
    $pattern = 'PacMan frame seq=(\d+) window=(\d+) state=(\w+) step=(\d+) score=(\d+) chain=\d+ lives=(\d+) level=(\d+) pacman=(-?\d+),(-?\d+) size=(\d+)x(\d+) stride=(\d+) bytes=(\d+) result=(\d+)'
    $matches = [regex]::Matches((Read-RawLog), $pattern)
    for ($i = $matches.Count - 1; $i -ge 0; --$i) {
        $frame = Convert-FrameMatch $matches[$i]
        if ($frame.State -ne $state -or $frame.Sequence -le $minimumSequence -or $frame.Result -ne 0) { continue }
        if ($maximumSequence -gt 0 -and $frame.Sequence -gt $maximumSequence) { continue }
        if ($score -ge 0 -and $frame.Score -ne $score) { continue }
        if ($lives -ge 0 -and $frame.Lives -ne $lives) { continue }
        return $frame
    }
    return $null
}

function Wait-ForValidationFrame([string]$state, [uint64]$minimumSequence = 0, [int]$score = -1, [int]$lives = -1, [int]$timeoutMs = 12000, [bool]$requireCompositorBound = $true) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        # A new logical session can legitimately be ahead of the previous
        # compositor generation. Register the sync barrier for that future
        # frame instead of filtering it out using the old generation.
        $maximumSequence = if ($requireCompositorBound) { Get-LatestCompositorFrameSequence } else { [uint64]0 }
        $frame = Get-LatestValidationFrame $state $minimumSequence $score $lives $maximumSequence
        if ($null -ne $frame) { return $frame }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Get-ValidationFrameBySequence([uint64]$sequence) {
    $pattern = 'PacMan frame seq=(\d+) window=(\d+) state=(\w+) step=(\d+) score=(\d+) chain=\d+ lives=(\d+) level=(\d+) pacman=(-?\d+),(-?\d+) size=(\d+)x(\d+) stride=(\d+) bytes=(\d+) result=(\d+)'
    $matches = [regex]::Matches((Read-RawLog), $pattern)
    for ($i = $matches.Count - 1; $i -ge 0; --$i) {
        $frame = Convert-FrameMatch $matches[$i]
        if ($frame.Sequence -eq $sequence -and $frame.Result -eq 0) { return $frame }
    }
    return $null
}

function Get-FrameBoundary([uint64]$windowId, [uint64]$sequence) {
    $pattern = "Native ELF frame boundary runtimeId=(\d+) windowId=$windowId incomingFrameSeq=$sequence width=(\d+) height=(\d+) stride=(\d+) format=(\d+) bytes=(\d+) validation=PASS presentation=PASS result=0"
    $matches = [regex]::Matches((Read-RawLog), $pattern)
    if ($matches.Count -eq 0) { return $null }
    $match = $matches[$matches.Count - 1]
    return [pscustomobject]@{
        RuntimeId = [uint64]$match.Groups[1].Value
        Width = [int]$match.Groups[2].Value
        Height = [int]$match.Groups[3].Value
        Stride = [uint32]$match.Groups[4].Value
        Format = [uint32]$match.Groups[5].Value
        Bytes = [uint32]$match.Groups[6].Value
    }
}

function Wait-ForFrameLifecycle([pscustomobject]$frame, [int]$timeoutMs = 12000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        $boundary = Get-FrameBoundary $frame.WindowId $frame.Sequence
        $replacementPattern = "Compositor frame replaced windowId=$($frame.WindowId) incomingFrameSeq=$($frame.Sequence) frameGeneration=(\d+) storedBytes=(\d+) width=(\d+) height=(\d+) stride=(\d+) format=(\d+) validation=PASS"
        $replacement = [regex]::Matches((Read-RawLog), $replacementPattern)
        if ($null -ne $boundary -and $replacement.Count -gt 0) {
            $match = $replacement[$replacement.Count - 1]
            return [pscustomobject]@{ Frame = $frame; Boundary = $boundary; FrameGeneration = [uint64]$match.Groups[1].Value }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Sync-HostedFrame([pscustomobject]$frame, [int]$timeoutMs = 8000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    $pattern = "Compositor frame sync windowId=$($frame.WindowId) expectedFrameGeneration=0 expectedFrameSequence=$($frame.Sequence) frameSequence=(\d+) frameGeneration=(\d+) paintGeneration=(\d+) captureGeneration=(\d+).* result=PASS"
    $nextRetry = [DateTime]::UtcNow
    do {
        if ([DateTime]::UtcNow -ge $nextRetry) {
            Send-ServerCommand "gui.sync $($frame.WindowId) 0 $($frame.Sequence) 1"
            $nextRetry = [DateTime]::UtcNow.AddMilliseconds(1000)
        }
        Start-Sleep -Milliseconds 100
        $raw = Read-RawLog
        $syncMatches = [regex]::Matches($raw, $pattern)
        if ($syncMatches.Count -gt 0) {
            $match = $syncMatches[$syncMatches.Count - 1]
            return [pscustomobject]@{
                FrameSequence = [uint64]$match.Groups[1].Value
                FrameGeneration = [uint64]$match.Groups[2].Value
                PaintGeneration = [uint64]$match.Groups[3].Value
                CaptureGeneration = [uint64]$match.Groups[4].Value
            }
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Wait-ForAdditionalLogCount([string]$pattern, [int]$baselineCount, [int]$additionalCount, [int]$timeoutMs = 12000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    $targetCount = $baselineCount + $additionalCount
    do {
        if ((Get-LogCount $pattern) -ge $targetCount) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Wait-ForNoAdditionalLogCount([string]$pattern, [int]$baselineCount, [int]$timeoutMs = 300) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        if ([regex]::Matches((Read-RawLog), $pattern).Count -gt $baselineCount) { return $false }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $true
}

function Send-ServerCommand([string]$command) {
    if (-not $process -or $process.HasExited) { throw "Cannot send '$command': server wrapper is not running." }
    $process.StandardInput.WriteLine($command)
    $process.StandardInput.Flush()
}

function Last-CreatedWindowId {
    $matches = [regex]::Matches((Read-RawLog), 'Compositor created window id=(\d+)')
    if ($matches.Count -eq 0) { return [uint64]0 }
    return [uint64]$matches[$matches.Count - 1].Groups[1].Value
}

function Wait-ForNewWindowId([uint64]$previousId, [int]$timeoutMs = 30000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        $currentId = Last-CreatedWindowId
        if ($currentId -gt $previousId) { return $currentId }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return [uint64]0
}

function Send-Key([int]$key, [int]$durationMs = 100) {
    [PacManDangerCapture5]::Key($compositor, [byte]$key, $true)
    Start-Sleep -Milliseconds $durationMs
    [PacManDangerCapture5]::Key($compositor, [byte]$key, $false)
    Start-Sleep -Milliseconds 80
}

function Capture-Hosted([string]$label, [string]$state, [uint64]$minimumSequence = 0, [int]$score = -1, [int]$lives = -1, [bool]$allowFrameAheadOfCompositor = $false) {
    $frame = Wait-ForValidationFrame $state $minimumSequence $score $lives 12000 (-not $allowFrameAheadOfCompositor)
    if ($null -eq $frame) { throw "No fresh PacMan $state frame was logged for capture $label." }
    $freezeRequested = $true
    try {
        $sync = Sync-HostedFrame $frame 8000
        if ($null -eq $sync) { throw "Frame lifecycle did not reach compositor paint for $label seq=$($frame.Sequence)." }
        $lifecycle = Wait-ForFrameLifecycle $frame 12000
        if ($null -eq $lifecycle) { throw "Frame lifecycle did not reach host/compositor replacement for $label seq=$($frame.Sequence)." }
        $paintedFrame = Get-ValidationFrameBySequence $sync.FrameSequence
        if ($null -eq $paintedFrame) { throw "No application frame record matched painted generation $($sync.FrameGeneration) for $label." }
        if ($paintedFrame.State -ne $state -or ($score -ge 0 -and $paintedFrame.Score -ne $score) -or ($lives -ge 0 -and $paintedFrame.Lives -ne $lives)) {
            throw "Painted generation $($sync.FrameGeneration) belongs to state=$($paintedFrame.State) score=$($paintedFrame.Score) lives=$($paintedFrame.Lives), not requested state=$state score=$score lives=$lives for $label."
        }

        $path = Join-Path $captureDirectory "pacman-danger-$runId-$label-seq$($paintedFrame.Sequence)-gen$($sync.FrameGeneration).png"
        if (Test-Path -LiteralPath $path) { throw "Capture output already exists and was not intentionally replaced: $path" }
        $captureTimestamp = [DateTime]::UtcNow
        if ($captureTimestamp -lt $frame.ObservedAtUtc) { throw "Capture timestamp predates the expected state frame for $label." }
        if (-not [PacManDangerCapture5]::CaptureCompositor($path, $compositor)) {
            throw "Failed to capture the owned guideXOS compositor window for $label."
        }
        $rect = [PacManDangerCapture5+RECT]::new()
        if (-not [PacManDangerCapture5]::GetWindowRect($compositor, [ref]$rect)) { throw "Hosted compositor HWND disappeared during $label capture." }
        $windowWidth = $rect.Right - $rect.Left
        $windowHeight = $rect.Bottom - $rect.Top
        $hwndText = '0x' + $compositor.ToInt64().ToString('X')
        $frameRaw = Read-RawLog
        $stateValues = [regex]::Match($frameRaw, "PacMan frame seq=$($paintedFrame.Sequence) .*? powerVisible=(\d+) powerBlink=(\d+).*? high=(\d+)")
        $powerVisible = if ($stateValues.Success) { $stateValues.Groups[1].Value } else { 'unknown' }
        $powerBlink = if ($stateValues.Success) { $stateValues.Groups[2].Value } else { 'unknown' }
        $highScore = if ($stateValues.Success) { $stateValues.Groups[3].Value } else { 'unknown' }
        $results.Add("capture=$path")
        $results.Add("capture-target appId=com.guidexos.pacman.danger-validation runtimeId=$($lifecycle.Boundary.RuntimeId) windowId=$($paintedFrame.WindowId) hostedHwnd=$hwndText title=guideXOSCpp Compositor dimensions=$($windowWidth)x$($windowHeight) frameGeneration=$($sync.FrameGeneration) paintGeneration=$($sync.PaintGeneration) captureGeneration=$($sync.CaptureGeneration) frameSequence=$($paintedFrame.Sequence) state=$($paintedFrame.State) score=$($paintedFrame.Score) highScore=$highScore lives=$($paintedFrame.Lives) level=$($paintedFrame.Level) powerVisible=$powerVisible powerBlinkSteps=$powerBlink filename=$path timestampUtc=$($captureTimestamp.ToString('o'))")
    }
    finally {
        if ($freezeRequested) {
            try { Send-ServerCommand "gui.unfreeze $($frame.WindowId)" } catch {}
        }
    }
}

function Launch-DangerPacMan {
    $script:launchCount++
    Send-ServerCommand 'desktop.launch Nexgen PacMan Danger Validation'
    if (-not (Wait-ForLogCount 'PacMan interactive frame presented' $script:launchCount 30000)) {
        throw "Danger validation launch $($script:launchCount) did not present an interactive frame or compositor window."
    }
    $windowId = Last-CreatedWindowId
    if ($windowId -eq 0) { throw "Danger validation launch $($script:launchCount) presented no compositor window." }
    $results.Add("cycle=$($script:launchCount) launch=True windowId=$windowId")
    Start-Sleep -Milliseconds 250
}

function Escape-And-Wait([int]$cycle) {
    # Prime the compositor's focused-input path before Escape. This matches the
    # existing hosted input contract without adding any Pac-Man production key.
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        Send-Key 39 80
        Send-Key 27
        if (Wait-ForLogCount 'Cleanup complete app=com.guidexos.pacman.danger-validation.*remainingWindows=0' $cycle 2000) {
            $results.Add("cycle=$cycle escape-cleanup=True attempt=$attempt")
            return
        }
    }
    throw "Danger validation cleanup did not report zero owned windows in cycle $cycle after three Escape attempts."
}

try {
    $existing = @(Get-ServerProcesses)
    if ($existing.Count -gt 0) {
        $details = ($existing | ForEach-Object { "PID=$($_.ProcessId) path=$($_.ExecutablePath)" }) -join '; '
        throw "A guideXOS Server process is already running; refusing to overlap it: $details"
    }
    $existingCompositor = [PacManDangerCapture5]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
    if ($existingCompositor -ne [IntPtr]::Zero) {
        throw 'Another guideXOS compositor instance already owns the compositor window; close that session before validation.'
    }

    New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
    $logName = Split-Path -Leaf $rawLog
    $psi = [ProcessStartInfo]::new()
    $psi.FileName = $env:ComSpec
    $psi.Arguments = "/d /c `".\guideXOSServer.experimental.exe > $logName 2>&1`""
    $psi.WorkingDirectory = $serverRoot
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = '1'
    $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = '1'
    $process = [Process]::new()
    $process.StartInfo = $psi
    if (-not $process.Start()) { throw 'Failed to start the experimental guideXOS Server wrapper.' }
    [void]$ownedPids.Add($process.Id)
    $results.Add("run=$runId")
    $results.Add("server-wrapper-pid=$($process.Id)")

    $serverPid = 0
    for ($i = 0; $i -lt 100 -and $serverPid -eq 0; ++$i) {
        Add-OwnedDescendants
        $child = @(Get-ServerProcesses | Where-Object { $_.ParentProcessId -eq $process.Id -and $_.ExecutablePath -ieq $serverExe })
        if ($child.Count -gt 0) { $serverPid = [int]$child[0].ProcessId; break }
        Start-Sleep -Milliseconds 100
    }
    if ($serverPid -eq 0) { throw "Experimental guideXOS Server child did not start; inspect $rawLog" }
    $results.Add("server-pid=$serverPid")

    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50 -and $compositor -eq [IntPtr]::Zero; ++$i) {
        Start-Sleep -Milliseconds 200
        $compositor = [PacManDangerCapture5]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
    }
    if ($compositor -eq [IntPtr]::Zero) {
        throw 'Hosted compositor window was not created. Another compositor instance may be blocking window creation.'
    }
    $results.Add('compositor=found')

    $collisionBaseline = Get-LogCount 'PacMan ghost collision detected'
    $lifeTwoBaseline = Get-LogCount 'PacMan life decremented; lives remaining: 2'
    $deathBaseline = Get-LogCount 'PacMan death state entered'
    Launch-DangerPacMan
Capture-Hosted 'initial-ready' 'InitialReady' 0 -1 3 $true
    Capture-Hosted 'playing' 'Playing' 0 -1 3
    $noEarlyCollision = Wait-ForNoAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 350
    $results.Add("cycle=1 no-automatic-collision-before-schedule=$noEarlyCollision")

    $collision1 = Wait-ForAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 1 5000
    $results.Add("cycle=1 collision=$collision1")
    if (-not $collision1) { throw 'Danger validation did not detect the first deterministic collision.' }
    $death = (Get-LogCount 'PacMan death state entered') -ge ($deathBaseline + 1)
    $life = (Get-LogCount 'PacMan life decremented; lives remaining: 2') -eq ($lifeTwoBaseline + 1)
    $duplicateSuppressed = Wait-ForNoAdditionalLogCount 'PacMan death state entered' ($deathBaseline + 1) 350
    Capture-Hosted 'dying' 'Dying' 0 -1 2
    $results.Add("cycle=1 dying=$death lives-decremented-once=$life duplicate-death-suppressed=$duplicateSuppressed")

    $resetBaseline = Get-LogCount 'PacMan actors reset after death'
    $actorReset = Wait-ForAdditionalLogCount 'PacMan actors reset after death' $resetBaseline 1 3000
    $readyCollisionBaseline = Get-LogCount 'PacMan ghost collision detected'
    $readySafe = Wait-ForNoAdditionalLogCount 'PacMan ghost collision detected' $readyCollisionBaseline 350
    Capture-Hosted 'ready' 'Ready' 0 -1 2
    $results.Add("cycle=1 actor-reset=$actorReset ready-collision-free-window=$readySafe")
    if (-not ($death -and $life -and $duplicateSuppressed -and $actorReset -and $readySafe)) {
        throw 'Danger validation did not preserve the bounded death and Ready-after-death transition.'
    }

    $collisionB = Wait-ForAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 2 8000
    $collisionC = Wait-ForAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 3 8000
    $lifeZero = (Get-LogCount 'PacMan life decremented; lives remaining: 0') -ge 1
    # The final 100-step death animation is compositor-paced; allow the
    # existing fixed-step lifecycle to emit Game Over before asserting it.
    $gameOver = Wait-ForLog 'PacMan Game Over entered' 5000
    $results.Add("cycle=1 collisions=$($collision1 -and $collisionB -and $collisionC) game-over=$gameOver lives-zero=$lifeZero")
    if (-not ($collision1 -and $collisionB -and $collisionC -and $gameOver -and $lifeZero)) {
        throw 'Danger validation did not reach Game Over after three bounded collisions.'
    }
    Capture-Hosted 'game-over' 'GameOver' 0 -1 0
    $restartFrameBaseline = [uint64]0
    $latestBeforeRestart = Get-LatestValidationFrame 'GameOver' 0 -1 0
    if ($null -ne $latestBeforeRestart) { $restartFrameBaseline = $latestBeforeRestart.Sequence }
    $restart = $false
    for ($attempt = 1; $attempt -le 3 -and -not $restart; ++$attempt) {
        Send-Key 39 80
        Send-Key 13
        $restart = Wait-ForLog 'PacMan session restarted' 2000
    }
    $results.Add("cycle=1 restart=$restart")
    if (-not $restart) { throw 'Enter did not restart the Game Over session.' }
    $restartScoreResetFrame = Wait-ForValidationFrame 'Playing' $restartFrameBaseline 0 3 3000 $false
    $restartScoreReset = $null -ne $restartScoreResetFrame
    $results.Add("cycle=1 restart-score-reset=$restartScoreReset")
    if (-not $restartScoreReset) { throw 'Restart did not emit a fresh Playing frame with score=0 and lives=3.' }
    # The compositor can advance several native frames while the sync barrier
    # is serviced. The raw frame above is the exact reset assertion; the
    # capture itself only requires the restarted Playing/lives presentation.
    Capture-Hosted 'restart' 'Playing' $restartFrameBaseline -1 3 $true
    Escape-And-Wait 1

    Send-ServerCommand 'nativeapp.processes'
    Start-Sleep -Milliseconds 500
    Add-OwnedDescendants
    $finalLog = Read-RawLog
    $results.Add("zero-owned-windows=$([bool]($finalLog -match 'remainingWindows=0'))")
    $results.Add("retained-frame-present-diagnostics=$([bool]($finalLog -match 'present_frame call count'))")
    $results.Add("danger-placement-count=$([regex]::Matches($finalLog, 'PacMan hosted danger overlap placed').Count)")
}
catch {
    $failed = $true
    $results.Add("error=$($_.Exception.Message)")
    Write-Error $_
}
finally {
    try {
        if ($process -and -not $process.HasExited) {
            try { Send-ServerCommand 'exit' } catch {}
            try { $process.StandardInput.Close() } catch {}
            if (-not $process.WaitForExit(20000)) {
                try { $process.Kill() } catch {}
                try { $process.WaitForExit(5000) } catch {}
            }
        }
        Add-OwnedDescendants
        foreach ($processId in @($ownedPids | Sort-Object -Descending -Unique)) {
            try { Stop-Process -Id $processId -Force -ErrorAction Stop } catch {}
        }
        Start-Sleep -Milliseconds 500
        $remaining = @(Get-ServerProcesses)
        $results.Add("remaining-owned-server-processes=$($remaining.Count)")
        if ($process) { $results.Add("server-wrapper-exit-code=$($process.ExitCode)") }
    }
    catch {
        $failed = $true
        $results.Add("cleanup-error=$($_.Exception.Message)")
    }
    if ($null -eq $previousFrameDiagnostics) { Remove-Item Env:GXOS_PACMAN_FRAME_DIAGNOSTICS -ErrorAction SilentlyContinue } else { $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = $previousFrameDiagnostics }
    if ($null -eq $previousFreezeDiagnostics) { Remove-Item Env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS -ErrorAction SilentlyContinue } else { $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = $previousFreezeDiagnostics }
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}

if ($failed) { exit 1 }
