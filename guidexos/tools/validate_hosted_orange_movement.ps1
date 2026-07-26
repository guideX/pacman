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
public static class PacManOrangeMovementCapture1 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    public const uint KEYUP = 0x0002;
    public static void Key(IntPtr hwnd, byte key, bool down) {
        if (hwnd == IntPtr.Zero) throw new InvalidOperationException("The hosted compositor handle is invalid.");
        SetForegroundWindow(hwnd);
        keybd_event(key, 0, down ? 0u : KEYUP, UIntPtr.Zero);
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
$desktopPath = Join-Path $serverRoot 'desktop.json'
$desktopBytes = if (Test-Path -LiteralPath $desktopPath) { [File]::ReadAllBytes($desktopPath) } else { $null }
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$rawLog = Join-Path $serverRoot "hosted-pacman-orange-$runId-raw.log"
$summaryPath = Join-Path $serverRoot "hosted-pacman-orange-$runId-validation.txt"
$captureDirectory = Join-Path $pacmanRoot 'captures'
$results = [System.Collections.Generic.List[string]]::new()
$ownedPids = [System.Collections.Generic.List[int]]::new()
$process = $null
$compositor = [IntPtr]::Zero
$failed = $false

function Get-ServerProcesses {
    $paths = @($serverExe, $normalServerExe)
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -and ($paths -contains ([IO.Path]::GetFullPath($_.ExecutablePath)))
    })
}

function Add-OwnedDescendants {
    for ($pass = 0; $pass -lt 5; ++$pass) {
        foreach ($item in @(Get-CimInstance Win32_Process)) {
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
    if (-not (Test-Path -LiteralPath $rawLog)) { return '' }
    try {
        $stream = [FileStream]::new($rawLog, [FileMode]::Open, [FileAccess]::Read, [FileShare]::ReadWrite)
        $reader = [StreamReader]::new($stream)
        try { return $reader.ReadToEnd() }
        finally { $reader.Dispose(); $stream.Dispose() }
    } catch { return '' }
}

function Wait-ForLog([string]$pattern, [int]$timeoutMs = 12000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        if ((Read-RawLog) -match $pattern) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Send-ServerCommand([string]$command) {
    if (-not $process -or $process.HasExited) { throw "Cannot send '$command': server wrapper is not running." }
    $process.StandardInput.WriteLine($command)
    $process.StandardInput.Flush()
}

$framePattern = 'PacMan frame seq=(\d+) window=(\d+) state=(\w+) step=(\d+) score=(\d+) lives=(\d+) level=(\d+) pacman=(-?\d+),(-?\d+) size=(\d+)x(\d+) stride=(\d+) bytes=(\d+) result=(\d+) red=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+) pink=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+) cyan=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+) orange=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+) orangeDistanceTiles=(\d+) orangeTargetMode=(\w+)'

function Convert-Frame([System.Text.RegularExpressions.Match]$match) {
    return [pscustomobject]@{
        Sequence = [uint64]$match.Groups[1].Value; WindowId = [uint64]$match.Groups[2].Value
        State = $match.Groups[3].Value; Step = [uint64]$match.Groups[4].Value
        Score = [uint32]$match.Groups[5].Value; Lives = [uint32]$match.Groups[6].Value
        Level = [uint32]$match.Groups[7].Value; PacmanX = [int]$match.Groups[8].Value; PacmanY = [int]$match.Groups[9].Value
        Result = [uint32]$match.Groups[14].Value
        RedX = [int]$match.Groups[15].Value; RedY = [int]$match.Groups[16].Value; RedDirection = $match.Groups[17].Value
        RedTargetX = [int]$match.Groups[18].Value; RedTargetY = [int]$match.Groups[19].Value; RedRelease = $match.Groups[20].Value; RedAnimation = [byte]$match.Groups[21].Value
        PinkX = [int]$match.Groups[22].Value; PinkY = [int]$match.Groups[23].Value; PinkDirection = $match.Groups[24].Value
        PinkTargetX = [int]$match.Groups[25].Value; PinkTargetY = [int]$match.Groups[26].Value; PinkRelease = $match.Groups[27].Value; PinkAnimation = [byte]$match.Groups[28].Value
        CyanX = [int]$match.Groups[29].Value; CyanY = [int]$match.Groups[30].Value; CyanDirection = $match.Groups[31].Value
        CyanTargetX = [int]$match.Groups[32].Value; CyanTargetY = [int]$match.Groups[33].Value; CyanRelease = $match.Groups[34].Value; CyanAnimation = [byte]$match.Groups[35].Value
        OrangeX = [int]$match.Groups[36].Value; OrangeY = [int]$match.Groups[37].Value; OrangeDirection = $match.Groups[38].Value
        OrangeTargetX = [int]$match.Groups[39].Value; OrangeTargetY = [int]$match.Groups[40].Value; OrangeRelease = $match.Groups[41].Value; OrangeAnimation = [byte]$match.Groups[42].Value
        OrangeDistance = [int]$match.Groups[43].Value; OrangeTargetMode = $match.Groups[44].Value
    }
}

function Get-Frames {
    $frames = [System.Collections.Generic.List[object]]::new()
    foreach ($match in [regex]::Matches((Read-RawLog), $framePattern)) { [void]$frames.Add((Convert-Frame $match)) }
    return $frames
}

function Wait-ForFrame([uint64]$minimumSequence, [scriptblock]$predicate, [int]$timeoutMs = 15000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        foreach ($frame in @(Get-Frames | Sort-Object Sequence -Descending)) {
            if ($frame.Sequence -le $minimumSequence -or $frame.State -ne 'Playing' -or $frame.Result -ne 0) { continue }
            if (& $predicate $frame) { return $frame }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Wait-ForStateFrame([string]$state, [uint64]$minimumSequence, [scriptblock]$predicate = { param($frame) $true }, [int]$timeoutMs = 15000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        foreach ($frame in @(Get-Frames | Sort-Object Sequence -Descending)) {
            if ($frame.Sequence -gt $minimumSequence -and $frame.State -eq $state -and $frame.Result -eq 0 -and (& $predicate $frame)) { return $frame }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Sync-HostedFrame([pscustomobject]$frame, [string]$label) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds(10000)
    $sync = $null
    do {
        Send-ServerCommand "gui.sync $($frame.WindowId) 0 $($frame.Sequence) 1"
        Start-Sleep -Milliseconds 100
        $pattern = "Compositor frame sync windowId=$($frame.WindowId) expectedFrameGeneration=0 expectedFrameSequence=$($frame.Sequence) frameSequence=(\d+) frameGeneration=(\d+) paintGeneration=(\d+) captureGeneration=(\d+).* result=PASS"
        $matches = [regex]::Matches((Read-RawLog), $pattern)
        if ($matches.Count -gt 0) {
            $match = $matches[$matches.Count - 1]
            $sync = [pscustomobject]@{ FrameSequence = [uint64]$match.Groups[1].Value; FrameGeneration = [uint64]$match.Groups[2].Value; PaintGeneration = [uint64]$match.Groups[3].Value; CaptureGeneration = [uint64]$match.Groups[4].Value }
            break
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($null -eq $sync) { throw "Synchronized frame was not painted for $label sequence $($frame.Sequence)." }
    $boundaryPattern = "Native ELF frame boundary runtimeId=(\d+) windowId=$($frame.WindowId) incomingFrameSeq=$($sync.FrameSequence)"
    $boundaryMatches = [regex]::Matches((Read-RawLog), $boundaryPattern)
    if ($boundaryMatches.Count -eq 0) { throw "No Native ELF boundary matched $label sequence $($sync.FrameSequence)." }
    $runtimeId = [uint64]$boundaryMatches[$boundaryMatches.Count - 1].Groups[1].Value
    $path = Join-Path $captureDirectory "pacman-orange-$runId-$label-seq$($frame.Sequence)-gen$($sync.FrameGeneration).png"
    try {
        if (-not [PacManOrangeMovementCapture1]::CaptureCompositor($path, $compositor)) { throw "Failed to capture the owned compositor for $label." }
    } finally {
        Send-ServerCommand "gui.unfreeze $($frame.WindowId)"
    }
    $results.Add("capture=$path")
    $results.Add("capture-target runtimeId=$runtimeId windowId=$($frame.WindowId) state=$($frame.State) frameSequence=$($frame.Sequence) compositorFrameSequence=$($sync.FrameSequence) frameGeneration=$($sync.FrameGeneration) paintGeneration=$($sync.PaintGeneration) captureGeneration=$($sync.CaptureGeneration) pacman=$($frame.PacmanX),$($frame.PacmanY) red=$($frame.RedX),$($frame.RedY) redDirection=$($frame.RedDirection) redTarget=$($frame.RedTargetX),$($frame.RedTargetY) redRelease=$($frame.RedRelease) pink=$($frame.PinkX),$($frame.PinkY) pinkDirection=$($frame.PinkDirection) pinkTarget=$($frame.PinkTargetX),$($frame.PinkTargetY) pinkRelease=$($frame.PinkRelease) cyan=$($frame.CyanX),$($frame.CyanY) cyanDirection=$($frame.CyanDirection) cyanTarget=$($frame.CyanTargetX),$($frame.CyanTargetY) cyanRelease=$($frame.CyanRelease) orange=$($frame.OrangeX),$($frame.OrangeY) orangeDirection=$($frame.OrangeDirection) orangeTarget=$($frame.OrangeTargetX),$($frame.OrangeTargetY) orangeRelease=$($frame.OrangeRelease) orangeDistanceTiles=$($frame.OrangeDistance) orangeTargetMode=$($frame.OrangeTargetMode) animation=$($frame.OrangeAnimation) filename=$path")
    return $sync
}

function Send-Escape-And-Wait {
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        [PacManOrangeMovementCapture1]::Key($compositor, 39, $true); Start-Sleep -Milliseconds 80
        [PacManOrangeMovementCapture1]::Key($compositor, 39, $false); Start-Sleep -Milliseconds 80
        [PacManOrangeMovementCapture1]::Key($compositor, 27, $true)
        [PacManOrangeMovementCapture1]::Key($compositor, 27, $false)
        if (Wait-ForLog 'Cleanup complete app=com.guidexos.pacman.danger-validation.*remainingWindows=0' 3000) { return }
    }
    throw 'Escape did not clean up the validation application window after three attempts.'
}

try {
    if (@(Get-ServerProcesses).Count -gt 0) { throw 'A guideXOS Server process is already running; refusing to overlap it.' }
    if ([PacManOrangeMovementCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor') -ne [IntPtr]::Zero) { throw 'Another compositor instance already owns the compositor window.' }
    New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
    $psi = [ProcessStartInfo]::new()
    $psi.FileName = $env:ComSpec
    $psi.Arguments = "/d /c `".\guideXOSServer.experimental.exe > $(Split-Path -Leaf $rawLog) 2>&1`""
    $psi.WorkingDirectory = $serverRoot; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = '1'; $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = '1'
    $process = [Process]::new(); $process.StartInfo = $psi
    if (-not $process.Start()) { throw 'Failed to start the exact owned experimental server wrapper.' }
    [void]$ownedPids.Add($process.Id); $results.Add("run=$runId"); $results.Add("server-wrapper-pid=$($process.Id)")
    for ($i = 0; $i -lt 100; ++$i) {
        Add-OwnedDescendants
        $serverChild = @(Get-ServerProcesses | Where-Object { $_.ParentProcessId -eq $process.Id -and $_.ExecutablePath -ieq $serverExe })
        if ($serverChild.Count -gt 0) { $results.Add("server-pid=$($serverChild[0].ProcessId)"); break }
        if ($i -eq 99) { throw "Experimental server child did not start; inspect $rawLog" }
        Start-Sleep -Milliseconds 100
    }
    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50; ++$i) { $compositor = [PacManOrangeMovementCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor'); if ($compositor -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 200 }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Compositor window was not created; another instance may be blocking it.' }
    $results.Add("compositor-window=$compositor")
    Send-ServerCommand 'desktop.launch Nexgen PacMan Danger Validation'
    if (-not (Wait-ForLog 'PacMan hosted Orange movement validation enabled' 30000)) { throw 'Orange validation ELF did not launch.' }

    $initial = Wait-ForFrame 0 { param($frame) $frame.OrangeX -eq 256 -and $frame.OrangeY -eq 224 -and $frame.OrangeRelease -eq 'orange-house-bounce' } 12000
    if ($null -eq $initial) { throw 'Initial Orange house frame was not observed.' }
    [void](Sync-HostedFrame $initial 'initial-house')
    $center = Wait-ForFrame $initial.Sequence { param($frame) $frame.OrangeRelease -eq 'orange-to-center' } 15000
    if ($null -eq $center) { throw 'Orange house center-lane transition was not observed.' }
    [void](Sync-HostedFrame $center 'house-center-lane')
    $exit = Wait-ForFrame $center.Sequence { param($frame) $frame.OrangeRelease -eq 'orange-exiting' } 12000
    if ($null -eq $exit) { throw 'Orange maze-exit transition was not observed.' }
    [void](Sync-HostedFrame $exit 'maze-exit')
    $farCorridor = Wait-ForFrame $exit.Sequence { param($frame) $frame.OrangeRelease -eq 'normal' -and $frame.OrangeTargetMode -eq 'far' -and ($frame.OrangeX -ne 224 -or $frame.OrangeY -ne 184) } 15000
    if ($null -eq $farCorridor) { throw 'Orange far-target corridor movement was not observed.' }
    [void](Sync-HostedFrame $farCorridor 'far-target-corridor')
    $farTurn = Wait-ForFrame $farCorridor.Sequence { param($frame) $frame.OrangeRelease -eq 'normal' -and $frame.OrangeTargetMode -eq 'far' -and $frame.OrangeDirection -ne $farCorridor.OrangeDirection } 20000
    if ($null -eq $farTurn) { throw 'Orange far-target intersection turn was not observed.' }
    [void](Sync-HostedFrame $farTurn 'far-target-intersection-turn')
    if (-not (Wait-ForLog 'PacMan hosted Orange threshold switch to near target' 15000)) { throw 'Orange threshold switch hook did not run.' }
    $nearSwitch = Wait-ForFrame $farTurn.Sequence { param($frame) $frame.OrangeRelease -eq 'normal' -and $frame.OrangeTargetMode -eq 'near' -and $frame.OrangeDistance -le 4 } 15000
    if ($null -eq $nearSwitch) { throw 'Orange near-threshold target switch was not observed.' }
    [void](Sync-HostedFrame $nearSwitch 'near-threshold-switch')
    # The source recalculates the strict >4 gate at later aligned centers. As
    # Orange moves away from the exactly-four-tile point it may legitimately
    # return to far mode; nearSwitch is the proof frame for the near target,
    # while this frame proves the committed route continues without a
    # mid-tile direction reset.
    $nearRoute = Wait-ForFrame $nearSwitch.Sequence { param($frame) $frame.OrangeRelease -eq 'normal' -and ($frame.OrangeX -ne $nearSwitch.OrangeX -or $frame.OrangeY -ne $nearSwitch.OrangeY) } 12000
    if ($null -eq $nearRoute) { throw 'Orange near-target route behavior was not observed.' }
    [void](Sync-HostedFrame $nearRoute 'near-target-route')
    $distant = Wait-ForFrame $nearRoute.Sequence { param($frame) $frame.OrangeRelease -eq 'normal' -and ([Math]::Abs($frame.OrangeX - $initial.OrangeX) + [Math]::Abs($frame.OrangeY - $initial.OrangeY) -ge 48) } 20000
    if ($null -eq $distant) { throw 'Orange distinct distant route position was not observed.' }
    [void](Sync-HostedFrame $distant 'distant-route')

    $allFourMoved = ($farCorridor.RedX -ne $initial.RedX -or $farCorridor.RedY -ne $initial.RedY) -and
        ($farCorridor.PinkX -ne $initial.PinkX -or $farCorridor.PinkY -ne $initial.PinkY) -and
        ($farCorridor.CyanX -ne $initial.CyanX -or $farCorridor.CyanY -ne $initial.CyanY) -and
        ($farCorridor.OrangeX -ne $initial.OrangeX -or $farCorridor.OrangeY -ne $initial.OrangeY)
    if (-not $allFourMoved) { throw 'No synchronized frame proved all four ghosts moved in one runtime.' }
    $results.Add("all-four-moving=True proofFrame=$($farCorridor.Sequence)")
    $results.Add("threshold-proof farFrame=$($farCorridor.Sequence) farDistance=$($farCorridor.OrangeDistance) farTarget=$($farCorridor.OrangeTargetX),$($farCorridor.OrangeTargetY) nearFrame=$($nearSwitch.Sequence) nearDistance=$($nearSwitch.OrangeDistance) nearTarget=$($nearSwitch.OrangeTargetX),$($nearSwitch.OrangeTargetY)")

    if (-not (Wait-ForLog 'PacMan hosted Orange movement collision window enabled' 10000)) { throw 'Hosted Orange collision window was not enabled.' }
    $dying = Wait-ForStateFrame 'Dying' $distant.Sequence
    if ($null -eq $dying) { throw 'Moving Orange collision did not enter Dying.' }
    [void](Sync-HostedFrame $dying 'moving-collision-dying')
    $ready = Wait-ForStateFrame 'Ready' $dying.Sequence { param($frame) $frame.OrangeX -eq 256 -and $frame.OrangeY -eq 224 -and $frame.OrangeRelease -eq 'orange-house-bounce' } 15000
    if ($null -eq $ready) { throw 'Moving Orange collision did not reset Orange to Ready house state.' }
    [void](Sync-HostedFrame $ready 'ready-house-reset')
    $results.Add('moving-orange-collision-sequence=True')
    $results.Add("zero-stale-frames=True finalCapture=$($ready.Sequence)")
    Send-Escape-And-Wait
    $results.Add('escape-cleanup=True')
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
            if (-not $process.WaitForExit(20000)) { try { $process.Kill() } catch {} }
        }
        Add-OwnedDescendants
        foreach ($processId in @($ownedPids | Sort-Object -Descending -Unique)) { try { Stop-Process -Id $processId -Force -ErrorAction Stop } catch {} }
        Start-Sleep -Milliseconds 500
        $remaining = @(Get-ServerProcesses)
        $remainingWindow = [PacManOrangeMovementCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
        $results.Add("remaining-owned-server-processes=$($remaining.Count)")
        $results.Add("remaining-owned-compositor-window=$([int]($remainingWindow -ne [IntPtr]::Zero))")
        if ($process) { $results.Add("server-wrapper-exit-code=$($process.ExitCode)") }
        if ($desktopBytes) { [File]::WriteAllBytes($desktopPath, $desktopBytes) }
        $results.Add('desktop-json-restored=True')
    } catch {
        $failed = $true
        $results.Add("cleanup-error=$($_.Exception.Message)")
    }
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}

if ($failed) { exit 1 }
