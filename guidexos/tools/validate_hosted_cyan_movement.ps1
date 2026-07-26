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
public static class PacManCyanMovementCapture1 {
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
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$rawLog = Join-Path $serverRoot "hosted-pacman-cyan-$runId-raw.log"
$summaryPath = Join-Path $serverRoot "hosted-pacman-cyan-$runId-validation.txt"
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

$framePattern = 'PacMan frame seq=(\d+) window=(\d+) state=(\w+) step=(\d+) score=(\d+) lives=(\d+) level=(\d+) pacman=(-?\d+),(-?\d+) size=(\d+)x(\d+) stride=(\d+) bytes=(\d+) result=(\d+) red=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+) pink=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+) cyan=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+) orange=(-?\d+),(-?\d+) dir=(\w+) target=(-?\d+),(-?\d+) release=([\w-]+) anim=(\d+)'

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
    $path = Join-Path $captureDirectory "pacman-cyan-$runId-$label-seq$($frame.Sequence)-gen$($sync.FrameGeneration).png"
    if (-not [PacManCyanMovementCapture1]::CaptureCompositor($path, $compositor)) { throw "Failed to capture the owned compositor for $label." }
    $results.Add("capture=$path")
    $results.Add("capture-target runtimeId=$runtimeId windowId=$($frame.WindowId) state=$($frame.State) frameSequence=$($frame.Sequence) compositorFrameSequence=$($sync.FrameSequence) frameGeneration=$($sync.FrameGeneration) paintGeneration=$($sync.PaintGeneration) captureGeneration=$($sync.CaptureGeneration) pacman=$($frame.PacmanX),$($frame.PacmanY) red=$($frame.RedX),$($frame.RedY) redDirection=$($frame.RedDirection) pink=$($frame.PinkX),$($frame.PinkY) pinkDirection=$($frame.PinkDirection) cyan=$($frame.CyanX),$($frame.CyanY) cyanDirection=$($frame.CyanDirection) cyanTarget=$($frame.CyanTargetX),$($frame.CyanTargetY) cyanRelease=$($frame.CyanRelease) cyanAnimation=$($frame.CyanAnimation) orange=$($frame.OrangeX),$($frame.OrangeY) filename=$path")
    Send-ServerCommand "gui.unfreeze $($frame.WindowId)"
    return $sync
}

function Send-Escape-And-Wait {
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        [PacManCyanMovementCapture1]::Key($compositor, 39, $true); Start-Sleep -Milliseconds 80
        [PacManCyanMovementCapture1]::Key($compositor, 39, $false); Start-Sleep -Milliseconds 80
        [PacManCyanMovementCapture1]::Key($compositor, 27, $true)
        [PacManCyanMovementCapture1]::Key($compositor, 27, $false)
        if (Wait-ForLog 'Cleanup complete app=com.guidexos.pacman.danger-validation.*remainingWindows=0' 3000) { return }
    }
    throw 'Escape did not clean up the validation application window after three attempts.'
}

try {
    if (@(Get-ServerProcesses).Count -gt 0) { throw 'A guideXOS Server process is already running; refusing to overlap it.' }
    if ([PacManCyanMovementCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor') -ne [IntPtr]::Zero) { throw 'Another compositor instance already owns the compositor window.' }
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
    for ($i = 0; $i -lt 50; ++$i) { $compositor = [PacManCyanMovementCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor'); if ($compositor -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 200 }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Compositor window was not created; another instance may be blocking it.' }
    $results.Add('compositor=found')
    Send-ServerCommand 'desktop.launch Nexgen PacMan Danger Validation'
    if (-not (Wait-ForLog 'PacMan hosted Cyan movement validation enabled' 30000)) { throw 'Cyan validation ELF did not launch.' }

    $initial = Wait-ForFrame 0 { param($frame) $frame.CyanX -eq 224 -and $frame.CyanY -eq 240 -and $frame.CyanRelease -eq 'cyan-house-bounce' } 12000
    if ($null -eq $initial) { throw 'Initial Cyan house frame was not observed.' }
    [void](Sync-HostedFrame $initial 'initial-house')
    $exit = Wait-ForFrame $initial.Sequence { param($frame) $frame.CyanRelease -eq 'cyan-exiting' } 12000
    if ($null -eq $exit) { throw 'Cyan house-exit transition was not observed.' }
    [void](Sync-HostedFrame $exit 'house-exit')
    $corridor = Wait-ForFrame $exit.Sequence { param($frame) $frame.CyanRelease -eq 'normal' -and ($frame.CyanX -ne 224 -or $frame.CyanY -ne 184) } 12000
    if ($null -eq $corridor) { throw 'Cyan normal corridor movement was not observed.' }
    [void](Sync-HostedFrame $corridor 'corridor')
    $turn = Wait-ForFrame $corridor.Sequence { param($frame) $frame.CyanRelease -eq 'normal' -and $frame.CyanDirection -ne $corridor.CyanDirection } 20000
    if ($null -eq $turn) { throw 'Cyan target-directed intersection turn was not observed.' }
    [void](Sync-HostedFrame $turn 'targeted-intersection-turn')
    $distant = Wait-ForFrame $turn.Sequence { param($frame) $frame.CyanRelease -eq 'normal' -and ([Math]::Abs($frame.CyanX - $turn.CyanX) + [Math]::Abs($frame.CyanY - $turn.CyanY) -ge 48) } 20000
    if ($null -eq $distant) { throw 'Cyan distant route or tunnel position was not observed.' }
    [void](Sync-HostedFrame $distant 'distant-route')
    $results.Add("cyan-distinct-positions=True")
    # Compare two normal-route frames: the initial house target legitimately
    # changes when Cyan's own tile position changes, so it is not a Red-only
    # target comparison.
    $redMoved = $turn.RedX -ne $corridor.RedX -or $turn.RedY -ne $corridor.RedY
    $targetStable = $turn.CyanTargetX -eq $corridor.CyanTargetX -and $turn.CyanTargetY -eq $corridor.CyanTargetY
    $results.Add("red-moved=$redMoved")
    $results.Add("cyan-target-red-independence=$($redMoved -and $targetStable)")
    if (-not (Wait-ForLog 'PacMan hosted Cyan movement collision window enabled' 8000)) { throw 'Hosted Cyan collision window was not enabled.' }
    $dying = Wait-ForStateFrame 'Dying' $distant.Sequence
    if ($null -eq $dying) { throw 'Moving Cyan collision did not enter Dying.' }
    [void](Sync-HostedFrame $dying 'moving-collision-dying')
    $ready = Wait-ForStateFrame 'Ready' $dying.Sequence { param($frame) $frame.CyanX -eq 224 -and $frame.CyanY -eq 240 -and $frame.CyanRelease -eq 'cyan-house-bounce' } 15000
    if ($null -eq $ready) { throw 'Moving Cyan collision did not reset Cyan to Ready house state.' }
    [void](Sync-HostedFrame $ready 'ready-house-reset')
    $results.Add('moving-cyan-collision-sequence=True')
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
        $results.Add("remaining-owned-server-processes=$($remaining.Count)")
        if ($process) { $results.Add("server-wrapper-exit-code=$($process.ExitCode)") }
    } catch {
        $failed = $true
        $results.Add("cleanup-error=$($_.Exception.Message)")
    }
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}

if ($failed) { exit 1 }
