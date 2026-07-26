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
public static class PacManPowerPillCapture1 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    public const uint KEYUP = 0x0002;
    public static void Key(IntPtr hwnd, byte key, bool down) {
        if (hwnd == IntPtr.Zero) throw new InvalidOperationException("The compositor handle is invalid.");
        SetForegroundWindow(hwnd);
        keybd_event(key, 0, down ? 0u : KEYUP, UIntPtr.Zero);
    }
    public static bool Capture(string path, IntPtr hwnd) {
        RECT rect;
        if (hwnd == IntPtr.Zero || !GetWindowRect(hwnd, out rect)) return false;
        int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
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
$rawLog = Join-Path $serverRoot "hosted-pacman-power-pill-$runId-raw.log"
$summaryPath = Join-Path $serverRoot "hosted-pacman-power-pill-$runId-validation.txt"
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
                -not $ownedPids.Contains([int]$item.ProcessId)) { [void]$ownedPids.Add([int]$item.ProcessId) }
        }
    }
}

function Read-RawLog {
    if (-not (Test-Path -LiteralPath $rawLog)) { return '' }
    try {
        $stream = [FileStream]::new($rawLog, [FileMode]::Open, [FileAccess]::Read, [FileShare]::ReadWrite)
        $reader = [StreamReader]::new($stream)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose(); $stream.Dispose() }
    } catch { return '' }
}

function Wait-ForLog([string]$pattern, [int]$timeoutMs = 15000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do { if ((Read-RawLog) -match $pattern) { return $true }; Start-Sleep -Milliseconds 100 }
    while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Send-ServerCommand([string]$command) {
    if (-not $process -or $process.HasExited) { throw "Cannot send '$command': server wrapper is not running." }
    $process.StandardInput.WriteLine($command)
    $process.StandardInput.Flush()
}

$framePattern = 'PacMan frame seq=(?<seq>\d+) window=(?<window>\d+) state=(?<state>\w+) step=(?<step>\d+) score=(?<score>\d+) chain=(?<chain>\d+) lives=(?<lives>\d+) level=(?<level>\d+) pacman=(?<px>-?\d+),(?<py>-?\d+) size=\S+ stride=\S+ bytes=\S+ result=(?<result>\d+) red=(?<rx>-?\d+),(?<ry>-?\d+) dir=(?<rd>\w+) target=-?\d+,-?\d+ release=\S+ condition=(?<rc>\w+) pp=(?<rp>\d+) anim=\d+ pink=(?<pinkx>-?\d+),(?<pinky>-?\d+) dir=\w+ target=-?\d+,-?\d+ release=\S+ condition=(?<pinkc>\w+) pp=(?<pinkp>\d+) anim=\d+ cyan=(?<cyanx>-?\d+),(?<cyany>-?\d+) dir=\w+ target=-?\d+,-?\d+ release=\S+ condition=(?<cyanc>\w+) pp=(?<cyanp>\d+) anim=\d+ orange=(?<ox>-?\d+),(?<oy>-?\d+) dir=\w+ target=-?\d+,-?\d+ release=\S+ condition=(?<oc>\w+) pp=(?<op>\d+) anim=\d+'

function Get-Frames {
    $frames = [System.Collections.Generic.List[object]]::new()
    foreach ($match in [regex]::Matches((Read-RawLog), $framePattern)) {
        [void]$frames.Add([pscustomobject]@{
            Sequence = [uint64]$match.Groups['seq'].Value; WindowId = [uint64]$match.Groups['window'].Value
            State = $match.Groups['state'].Value; Step = [uint64]$match.Groups['step'].Value
            Score = [uint32]$match.Groups['score'].Value; Chain = [uint32]$match.Groups['chain'].Value
            PacmanX = [int]$match.Groups['px'].Value; PacmanY = [int]$match.Groups['py'].Value
            Result = [uint32]$match.Groups['result'].Value
            RedX = [int]$match.Groups['rx'].Value; RedY = [int]$match.Groups['ry'].Value
            RedCondition = $match.Groups['rc'].Value; RedTimer = [uint32]$match.Groups['rp'].Value
            PinkX = [int]$match.Groups['pinkx'].Value; PinkY = [int]$match.Groups['pinky'].Value
            PinkCondition = $match.Groups['pinkc'].Value; PinkTimer = [uint32]$match.Groups['pinkp'].Value
            CyanX = [int]$match.Groups['cyanx'].Value; CyanY = [int]$match.Groups['cyany'].Value
            CyanCondition = $match.Groups['cyanc'].Value; CyanTimer = [uint32]$match.Groups['cyanp'].Value
            OrangeX = [int]$match.Groups['ox'].Value; OrangeY = [int]$match.Groups['oy'].Value
            OrangeCondition = $match.Groups['oc'].Value; OrangeTimer = [uint32]$match.Groups['op'].Value
        })
    }
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

function Sync-Capture([pscustomobject]$frame, [string]$label) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds(10000)
    $sync = $null
    do {
        Send-ServerCommand "gui.sync $($frame.WindowId) 0 $($frame.Sequence) 1"
        Start-Sleep -Milliseconds 100
        $pattern = "Compositor frame sync windowId=$($frame.WindowId) expectedFrameGeneration=0 expectedFrameSequence=$($frame.Sequence) frameSequence=(\d+) frameGeneration=(\d+) paintGeneration=(\d+) captureGeneration=(\d+).*result=PASS"
        $matches = [regex]::Matches((Read-RawLog), $pattern)
        if ($matches.Count -gt 0) {
            $m = $matches[$matches.Count - 1]
            $sync = [pscustomobject]@{ Sequence = [uint64]$m.Groups[1].Value; FrameGeneration = [uint64]$m.Groups[2].Value; PaintGeneration = [uint64]$m.Groups[3].Value; CaptureGeneration = [uint64]$m.Groups[4].Value }
        }
    } while ($null -eq $sync -and [DateTime]::UtcNow -lt $deadline)
    if ($null -eq $sync) { throw "No synchronized frame for $label." }
    $boundary = [regex]::Matches((Read-RawLog), "Native ELF frame boundary runtimeId=(\d+) windowId=$($frame.WindowId) incomingFrameSeq=$($sync.Sequence)")
    if ($boundary.Count -eq 0) { throw "No runtime boundary for $label." }
    $runtimeId = [uint64]$boundary[$boundary.Count - 1].Groups[1].Value
    $path = Join-Path $captureDirectory "pacman-power-pill-$runId-$label-seq$($frame.Sequence)-gen$($sync.FrameGeneration).png"
    try {
        if (-not [PacManPowerPillCapture1]::Capture($path, $compositor)) { throw "Capture failed for $label." }
    } finally { Send-ServerCommand "gui.unfreeze $($frame.WindowId)" }
    $results.Add("capture=$path")
    $results.Add("capture-target runtimeId=$runtimeId windowId=$($frame.WindowId) frameSequence=$($frame.Sequence) frameGeneration=$($sync.FrameGeneration) paintGeneration=$($sync.PaintGeneration) captureGeneration=$($sync.CaptureGeneration) state=$($frame.State) step=$($frame.Step) pacman=$($frame.PacmanX),$($frame.PacmanY) score=$($frame.Score) chain=$($frame.Chain) red=$($frame.RedX),$($frame.RedY),$($frame.RedCondition),$($frame.RedTimer) pink=$($frame.PinkX),$($frame.PinkY),$($frame.PinkCondition),$($frame.PinkTimer) cyan=$($frame.CyanX),$($frame.CyanY),$($frame.CyanCondition),$($frame.CyanTimer) orange=$($frame.OrangeX),$($frame.OrangeY),$($frame.OrangeCondition),$($frame.OrangeTimer)")
    return $frame
}

function Escape-And-Wait {
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        [PacManPowerPillCapture1]::Key($compositor, 39, $true); Start-Sleep -Milliseconds 80
        [PacManPowerPillCapture1]::Key($compositor, 39, $false)
        [PacManPowerPillCapture1]::Key($compositor, 27, $true); [PacManPowerPillCapture1]::Key($compositor, 27, $false)
        if (Wait-ForLog 'Cleanup complete app=com.guidexos.pacman.danger-validation.*remainingWindows=0' 3000) { return }
    }
    throw 'Escape did not clean up the validation window.'
}

try {
    if (@(Get-ServerProcesses).Count -gt 0) { throw 'A guideXOS Server process is already running; refusing to overlap it.' }
    if ([PacManPowerPillCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor') -ne [IntPtr]::Zero) { throw 'Another compositor owns the compositor window.' }
    New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
    $psi = [ProcessStartInfo]::new(); $psi.FileName = $env:ComSpec
    $psi.Arguments = "/d /c `".\guideXOSServer.experimental.exe > $(Split-Path -Leaf $rawLog) 2>&1`""
    $psi.WorkingDirectory = $serverRoot; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = '1'; $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = '1'
    $process = [Process]::new(); $process.StartInfo = $psi
    if (-not $process.Start()) { throw 'Failed to start the owned experimental server wrapper.' }
    [void]$ownedPids.Add($process.Id); $results.Add("run=$runId"); $results.Add("server-wrapper-pid=$($process.Id)")
    for ($i = 0; $i -lt 100; ++$i) {
        Add-OwnedDescendants
        $child = @(Get-ServerProcesses | Where-Object { $_.ParentProcessId -eq $process.Id -and $_.ExecutablePath -ieq $serverExe })
        if ($child.Count -gt 0) { $results.Add("server-pid=$($child[0].ProcessId)"); break }
        if ($i -eq 99) { throw "Experimental server child did not start; inspect $rawLog" }
        Start-Sleep -Milliseconds 100
    }
    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50; ++$i) { $compositor = [PacManPowerPillCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor'); if ($compositor -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 200 }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Hosted compositor was not created.' }
    $results.Add("compositor-window=$compositor")
    Send-ServerCommand 'desktop.launch Nexgen PacMan Danger Validation'
    if (-not (Wait-ForLog 'PacMan hosted power-pill validation enabled' 30000)) { throw 'Power-pill validation ELF did not launch.' }

    $initial = Wait-ForFrame 0 { param($f) $f.Chain -eq 0 -and $f.RedCondition -eq 'normal' -and $f.RedTimer -eq 0 } 12000
    if ($null -eq $initial) { throw 'Initial normal ghost frame was not observed.' }
    [void](Sync-Capture $initial 'before-power-pill')
    $fright = Wait-ForFrame $initial.Sequence { param($f) $f.RedCondition -eq 'frightened' -and $f.RedTimer -gt 0 -and $f.Chain -eq 0 } 12000
    if ($null -eq $fright) { throw 'First frightened frame was not observed.' }
    [void](Sync-Capture $fright 'first-frightened')
    $movement = Wait-ForFrame $fright.Sequence { param($f) $f.RedCondition -eq 'frightened' -and (($f.RedX -ne $fright.RedX) -or ($f.RedY -ne $fright.RedY)) } 12000
    if ($null -eq $movement) { throw 'Frightened movement was not observed.' }
    [void](Sync-Capture $movement 'frightened-movement')
    $flash = Wait-ForFrame $movement.Sequence { param($f) $f.RedCondition -eq 'frightened' -and $f.RedTimer -lt 200 -and $f.RedTimer -gt 0 } 12000
    if ($null -eq $flash) { throw 'Near-expiration frightened frame was not observed.' }
    [void](Sync-Capture $flash 'flashing-near-expiration')
    $first = Wait-ForFrame $flash.Sequence { param($f) $f.Chain -ge 1 -and $f.Score -ge 210 } 12000
    if ($null -eq $first) { throw 'First frightened ghost eating was not observed.' }
    [void](Sync-Capture $first 'first-ghost-eaten')
    $second = Wait-ForFrame $first.Sequence { param($f) $f.Chain -ge 2 } 12000
    if ($null -eq $second) { throw 'Second ghost score-chain step was not observed.' }
    [void](Sync-Capture $second 'second-ghost-eaten')
    $fourth = Wait-ForFrame $second.Sequence { param($f) $f.Chain -eq 4 } 12000
    if ($null -eq $fourth) { throw 'Fourth score-chain step was not observed.' }
    [void](Sync-Capture $fourth 'fourth-ghost-eaten')
    $expired = Wait-ForFrame $fourth.Sequence { param($f) $f.Chain -eq 4 -and $f.RedCondition -eq 'normal' -and $f.RedTimer -eq 0 } 12000
    if ($null -eq $expired) { throw 'Frightened expiration/normal restoration was not observed.' }
    [void](Sync-Capture $expired 'frightened-expired-normal')

    $raw = Read-RawLog
    $results.Add("power-pill-log=$([bool]($raw -match 'PacMan power pill consumed'))")
    $results.Add("timer-init-log=$([bool]($raw -match 'PacMan frightened timer initialized'))")
    $results.Add("reversal-log=$([bool]($raw -match 'PacMan ghost reversal applied'))")
    $results.Add("flashing-log=$([bool]($raw -match 'PacMan frightened flashing began'))")
    $results.Add("eaten-log=$([bool]($raw -match 'PacMan ghost eaten'))")
    $results.Add("score-sequence=$([bool]($raw -match 'ghost-eating score awarded: 200') -and [bool]($raw -match 'ghost-eating score awarded: 400') -and [bool]($raw -match 'ghost-eating score awarded: 800') -and [bool]($raw -match 'ghost-eating score awarded: 1600'))")
    $results.Add("expiration-log=$([bool]($raw -match 'PacMan ghost timer expired'))")
    Escape-And-Wait
    $results.Add('escape-and-zero-window-cleanup=pass')
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
            if (-not $process.WaitForExit(20000)) { try { $process.Kill() } catch {}; try { $process.WaitForExit(5000) } catch {} }
        }
        Add-OwnedDescendants
        foreach ($processId in @($ownedPids | Sort-Object -Descending -Unique)) { try { Stop-Process -Id $processId -Force -ErrorAction Stop } catch {} }
        Start-Sleep -Milliseconds 500
        $remaining = @(Get-ServerProcesses)
        $results.Add("remaining-owned-server-processes=$($remaining.Count)")
        if ($process) { $results.Add("server-wrapper-exit-code=$($process.ExitCode)") }
    } catch { $failed = $true; $results.Add("cleanup-error=$($_.Exception.Message)") }
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}

if ($failed) { exit 1 }
