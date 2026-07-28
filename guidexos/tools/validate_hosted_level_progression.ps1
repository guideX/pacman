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
public static class PacManLevelProgressionCapture {
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
$rawLog = Join-Path $serverRoot "hosted-pacman-level-$runId-raw.log"
$summaryPath = Join-Path $serverRoot "hosted-pacman-level-$runId-validation.txt"
$captureDirectory = Join-Path $pacmanRoot 'captures'
$results = [System.Collections.Generic.List[string]]::new()
$ownedPids = [System.Collections.Generic.List[int]]::new()
$process = $null
$compositor = [IntPtr]::Zero
$failed = $false
$oldFrameDiagnostics = $env:GXOS_PACMAN_FRAME_DIAGNOSTICS
$oldFreezeDiagnostics = $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS

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

function Get-Frames {
    $pattern = 'PacMan frame seq=(?<seq>\d+) window=(?<window>\d+) state=(?<state>\w+).*?score=(?<score>\d+).*?lives=(?<lives>\d+).*?level=(?<level>\d+).*?remaining=(?<remaining>\d+)'
    $frames = [System.Collections.Generic.List[object]]::new()
    foreach ($match in [regex]::Matches((Read-RawLog), $pattern)) {
        [void]$frames.Add([pscustomobject]@{
            Sequence = [uint64]$match.Groups['seq'].Value
            WindowId = [uint64]$match.Groups['window'].Value
            State = $match.Groups['state'].Value
            Score = [uint32]$match.Groups['score'].Value
            Lives = [uint32]$match.Groups['lives'].Value
            Level = [uint32]$match.Groups['level'].Value
            Remaining = [uint32]$match.Groups['remaining'].Value
        })
    }
    return $frames
}

function Wait-ForFrame([uint64]$minimumSequence, [scriptblock]$predicate, [int]$timeoutMs = 15000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        foreach ($frame in @(Get-Frames | Sort-Object Sequence -Descending)) {
            if ($frame.Sequence -le $minimumSequence) { continue }
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
            $sync = [pscustomobject]@{
                Sequence = [uint64]$m.Groups[1].Value
                FrameGeneration = [uint64]$m.Groups[2].Value
                PaintGeneration = [uint64]$m.Groups[3].Value
                CaptureGeneration = [uint64]$m.Groups[4].Value
            }
        }
    } while ($null -eq $sync -and [DateTime]::UtcNow -lt $deadline)
    if ($null -eq $sync) { throw "No synchronized frame for $label." }
    if ($sync.FrameGeneration -ne $sync.PaintGeneration) {
        throw "Frame/paint generation mismatch for $label."
    }
    $boundary = [regex]::Matches((Read-RawLog), "Native ELF frame boundary runtimeId=(\d+) windowId=$($frame.WindowId) incomingFrameSeq=$($sync.Sequence)")
    if ($boundary.Count -eq 0) { throw "No runtime boundary for $label." }
    $runtimeId = [uint64]$boundary[$boundary.Count - 1].Groups[1].Value
    $path = Join-Path $captureDirectory "pacman-level-$runId-$label-seq$($frame.Sequence)-gen$($sync.FrameGeneration).png"
    try {
        if (-not [PacManLevelProgressionCapture]::Capture($path, $compositor)) { throw "Capture failed for $label." }
    } finally { Send-ServerCommand "gui.unfreeze $($frame.WindowId)" }
    $results.Add("capture=$path")
    $results.Add("capture-target runtimeId=$runtimeId windowId=$($frame.WindowId) applicationSequence=$($frame.Sequence) frameSequence=$($sync.Sequence) frameGeneration=$($sync.FrameGeneration) paintGeneration=$($sync.PaintGeneration) captureGeneration=$($sync.CaptureGeneration) level=$($frame.Level) score=$($frame.Score) lives=$($frame.Lives) remaining=$($frame.Remaining) state=$($frame.State)")
    return $frame
}

function Escape-And-Wait {
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        [PacManLevelProgressionCapture]::Key($compositor, 39, $true); Start-Sleep -Milliseconds 80
        [PacManLevelProgressionCapture]::Key($compositor, 39, $false)
        [PacManLevelProgressionCapture]::Key($compositor, 27, $true); [PacManLevelProgressionCapture]::Key($compositor, 27, $false)
        if (Wait-ForLog 'Cleanup complete app=com.guidexos.pacman.danger-validation.*remainingWindows=0' 3000) { return }
    }
    throw 'Escape did not clean up the validation window.'
}

try {
    if (@(Get-ServerProcesses).Count -gt 0) { throw 'A guideXOS Server process is already running; refusing to overlap it.' }
    if ([PacManLevelProgressionCapture]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor') -ne [IntPtr]::Zero) { throw 'Another compositor owns the compositor window.' }
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
    for ($i = 0; $i -lt 50; ++$i) { $compositor = [PacManLevelProgressionCapture]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor'); if ($compositor -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 200 }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Hosted compositor was not created.' }
    $results.Add("compositor-window=$compositor")
    Send-ServerCommand 'desktop.launch Nexgen PacMan Danger Validation'
    if (-not (Wait-ForLog 'PacMan hosted level transition trigger prepared' 30000)) { throw 'Level validation ELF did not launch or prepare.' }

    $initial = Wait-ForFrame 0 { param($f) $f.State -eq 'Playing' -and $f.Level -eq 1 -and $f.Remaining -eq 244 } 12000
    if ($null -eq $initial) { throw 'Initial level-1 Playing frame was not observed.' }
    [void](Sync-Capture $initial 'level1-initial-playing')
    $final1 = Wait-ForFrame $initial.Sequence { param($f) $f.State -eq 'Playing' -and $f.Level -eq 1 -and $f.Remaining -eq 1 } 12000
    if ($null -eq $final1) { throw 'Final active level-1 frame was not observed.' }
    [void](Sync-Capture $final1 'level1-final-active')
    $complete1 = Wait-ForFrame $final1.Sequence { param($f) $f.State -eq 'LevelComplete' -and $f.Level -eq 1 -and $f.Remaining -eq 0 } 12000
    if ($null -eq $complete1) { throw 'Level Complete frame for level 1 was not observed.' }
    [void](Sync-Capture $complete1 'level1-complete')
    $ready2 = Wait-ForFrame $complete1.Sequence { param($f) $f.State -eq 'Ready' -and $f.Level -eq 2 -and $f.Remaining -eq 244 } 12000
    if ($null -eq $ready2) { throw 'Level 2 Ready frame was not observed.' }
    [void](Sync-Capture $ready2 'level2-ready')
    $playing2 = Wait-ForFrame $ready2.Sequence { param($f) $f.State -eq 'Playing' -and $f.Level -eq 2 -and $f.Remaining -gt 0 } 12000
    if ($null -eq $playing2) { throw 'Level 2 Playing frame was not observed.' }
    [void](Sync-Capture $playing2 'level2-playing')
    $complete2 = Wait-ForFrame $playing2.Sequence { param($f) $f.State -eq 'LevelComplete' -and $f.Level -eq 2 } 12000
    if ($null -eq $complete2) { throw 'Level 2 completion was not observed.' }
    $ready3 = Wait-ForFrame $complete2.Sequence { param($f) $f.State -eq 'Ready' -and $f.Level -eq 3 } 12000
    if ($null -eq $ready3) { throw 'Level 3 Ready frame was not observed.' }
    $playing3 = Wait-ForFrame $ready3.Sequence { param($f) $f.State -eq 'Playing' -and $f.Level -eq 3 -and $f.Remaining -gt 0 } 12000
    if ($null -eq $playing3) { throw 'Level 3 Playing frame was not observed.' }
    [void](Sync-Capture $playing3 'level3-rule-change')
    $complete3 = Wait-ForFrame $playing3.Sequence { param($f) $f.State -eq 'LevelComplete' -and $f.Level -eq 3 } 12000
    if ($null -eq $complete3) { throw 'Level 3 completion was not observed.' }
    $ready8 = Wait-ForFrame $complete3.Sequence { param($f) $f.State -eq 'Ready' -and $f.Level -eq 4 } 12000
    if ($null -eq $ready8) { throw 'Post-level-3 Ready frame was not observed.' }
    $playing8 = Wait-ForFrame $ready8.Sequence { param($f) $f.State -eq 'Playing' -and $f.Level -eq 8 -and $f.Remaining -eq 1 } 12000
    if ($null -eq $playing8) { throw 'High-level short-duration frame was not observed.' }
    [void](Sync-Capture $playing8 'level8-short-frightened-rule')
    $complete8 = Wait-ForFrame $playing8.Sequence { param($f) $f.State -eq 'LevelComplete' -and $f.Level -eq 8 -and $f.Remaining -eq 0 } 12000
    if ($null -eq $complete8) { throw 'Level 8 power-pill completion was not observed.' }
    [void](Sync-Capture $complete8 'level8-power-pill-complete')
    $ready8Final = Wait-ForFrame $complete8.Sequence { param($f) $f.State -eq 'Ready' -and $f.Level -eq 8 -and $f.Remaining -eq 244 } 12000
    if ($null -eq $ready8Final) { throw 'Level 8 post-transition Ready frame was not observed.' }
    [void](Sync-Capture $ready8Final 'level8-ready')
    $laterDying = Wait-ForFrame $ready8Final.Sequence { param($f) $f.State -eq 'Dying' -and $f.Level -eq 8 -and $f.Lives -eq 2 } 12000
    if ($null -eq $laterDying) { throw 'Later-level death frame was not observed.' }
    [void](Sync-Capture $laterDying 'level8-death')
    $laterGameOver = Wait-ForFrame $laterDying.Sequence { param($f) $f.State -eq 'GameOver' -and $f.Level -eq 8 -and $f.Lives -eq 0 } 30000
    if ($null -eq $laterGameOver) { throw 'Later-level Game Over frame was not observed.' }
    [void](Sync-Capture $laterGameOver 'level8-game-over')
    $restartReady = Wait-ForFrame $laterGameOver.Sequence { param($f) $f.State -eq 'Ready' -and $f.Level -eq 1 -and $f.Lives -eq 3 -and $f.Score -eq 0 -and $f.Remaining -eq 244 } 12000
    if ($null -eq $restartReady) { throw 'Restarted level-1 Ready frame was not observed.' }
    [void](Sync-Capture $restartReady 'restart-level1-ready')

    $raw = Read-RawLog
    $results.Add("level1-to-level2=$([bool]($raw -match 'level reset: 2'))")
    $results.Add("level2-to-level3=$([bool]($raw -match 'level reset: 3'))")
    $results.Add("level3-to-level8-validation=$([bool]($raw -match 'level rules frightened duration: 700') -and [bool]($raw -match 'level=8'))")
    $results.Add("level8-frightened-duration-200=$([bool]($raw -match 'level rules frightened duration: 200'))")
    $results.Add("level-completion-bonus=$([bool]($raw -match 'score updated: 1010') -and [bool]($raw -match 'score updated: 2020'))")
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
    $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = $oldFrameDiagnostics
    $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = $oldFreezeDiagnostics
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}

if ($failed) { exit 1 }
