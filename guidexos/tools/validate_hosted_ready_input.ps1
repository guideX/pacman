using namespace System
using namespace System.Diagnostics
using namespace System.Drawing
using namespace System.Drawing.Imaging
using namespace System.IO
using namespace System.Runtime.InteropServices

$ErrorActionPreference = 'Stop'

Add-Type -ReferencedAssemblies @('System.Drawing') @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class PacManReadyInputCapture {
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

$repoRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = 'D:\dev\guideXOSServer'
$serverRootFull = [IO.Path]::GetFullPath($serverRoot).TrimEnd('\')
$serverExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.experimental.exe'))
$normalServerExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.exe'))
$packageDirectory = 'D:\dev\guideXOSServer\Apps\PacManPGM1ReadyInputValidation'
$appId = 'com.guidexos.pacman.pgm1.ready-input-validation'
$displayName = 'PacMan PGM1 Ready Input Validation'
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$rawLog = Join-Path $serverRoot "hosted-pacman-pgm1-ready-input-$runId-raw.log"
$summaryPath = Join-Path $repoRoot "captures\pacman-pgm1-ready-input-$runId-validation.txt"
$captureDirectory = Join-Path $repoRoot 'captures'
$results = [System.Collections.Generic.List[string]]::new()
$ownedPids = [System.Collections.Generic.List[int]]::new()
$process = $null
$compositor = [IntPtr]::Zero
$rightIsDown = $false
$failed = $false
$oldFrameDiagnostics = $env:GXOS_PACMAN_FRAME_DIAGNOSTICS
$oldFreezeDiagnostics = $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS

function Get-ServerProcesses {
    $paths = @($serverExe, $normalServerExe)
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -and ($paths -contains [IO.Path]::GetFullPath($_.ExecutablePath))
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
        $stream = [File]::Open($rawLog, [FileMode]::Open, [FileAccess]::Read, [FileShare]::ReadWrite)
        try {
            $offset = [Math]::Max([int64]0, $stream.Length - 8MB)
            [void]$stream.Seek($offset, [SeekOrigin]::Begin)
            $reader = [StreamReader]::new($stream)
            try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    } catch { return '' }
}

function Get-Frames {
    $pattern = 'PacMan frame seq=(?<seq>\d+).*? state=(?<state>\w+).*? pacman=(?<x>-?\d+),(?<y>-?\d+)'
    $frames = [System.Collections.Generic.List[object]]::new()
    foreach ($match in [regex]::Matches((Read-RawLog), $pattern)) {
        [void]$frames.Add([pscustomobject]@{
            Sequence = [uint64]$match.Groups['seq'].Value
            State = $match.Groups['state'].Value
            X = [int]$match.Groups['x'].Value
            Y = [int]$match.Groups['y'].Value
        })
    }
    return $frames
}

function Wait-ForFrame([scriptblock]$predicate, [int]$timeoutMs = 12000, [uint64]$afterSequence = 0) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        foreach ($frame in @(Get-Frames | Sort-Object Sequence -Descending)) {
            if ($frame.Sequence -le $afterSequence) { continue }
            if (& $predicate $frame) { return $frame }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Wait-ForLog([string]$pattern, [int]$timeoutMs = 10000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        if ((Read-RawLog) -match $pattern) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Send-ServerCommand([string]$command) {
    if (-not $process -or $process.HasExited) { throw "Cannot send '$command': the owned server wrapper is not running." }
    $process.StandardInput.WriteLine($command)
    $process.StandardInput.Flush()
}

function Send-Right([bool]$down) {
    [PacManReadyInputCapture]::Key($compositor, 39, $down)
    $script:rightIsDown = $down
}

try {
    if (-not (Test-Path -LiteralPath $packageDirectory -PathType Container)) {
        throw "Isolated runtime package is missing: $packageDirectory"
    }
    if (-not (Test-Path -LiteralPath $serverExe -PathType Leaf)) {
        throw "Experimental hosted runtime is missing: $serverExe"
    }
    if (@(Get-ServerProcesses).Count -gt 0) {
        throw 'A guideXOS Server process is already running; refusing to overlap it.'
    }
    if ([PacManReadyInputCapture]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor') -ne [IntPtr]::Zero) {
        throw 'Another hosted compositor owns the compositor window; refusing to overlap it.'
    }

    New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
    $psi = [ProcessStartInfo]::new()
    $psi.FileName = $env:ComSpec
    $psi.Arguments = "/d /c `".\guideXOSServer.experimental.exe > $(Split-Path -Leaf $rawLog) 2>&1`""
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
    if (-not $process.Start()) { throw 'Failed to start the owned experimental server wrapper.' }
    [void]$ownedPids.Add($process.Id)
    $results.Add("run=$runId")
    $results.Add("server-wrapper-pid=$($process.Id)")

    for ($i = 0; $i -lt 100; ++$i) {
        Add-OwnedDescendants
        if (@(Get-ServerProcesses | Where-Object {
            $_.ParentProcessId -eq $process.Id -and [IO.Path]::GetFullPath($_.ExecutablePath) -ieq $serverExe
        }).Count -gt 0) { break }
        if ($i -eq 99) { throw "Experimental server child did not start; inspect $rawLog" }
        Start-Sleep -Milliseconds 100
    }

    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50; ++$i) {
        $compositor = [PacManReadyInputCapture]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
        if ($compositor -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Hosted compositor was not created.' }
    $results.Add("compositor-window=$compositor")

    Send-ServerCommand 'desktop.apps.verbose'
    Send-ServerCommand "desktop.launch $displayName"
    $initialReady = Wait-ForFrame { param($frame) $frame.State -eq 'InitialReady' }
    if ($null -eq $initialReady) { throw 'The initial Ready frame was not observed.' }
    $readyCapture = Join-Path $captureDirectory "pacman-pgm1-ready-input-$runId-initial-ready.png"
    if (-not [PacManReadyInputCapture]::Capture($readyCapture, $compositor)) {
        throw 'Initial Ready screen capture failed.'
    }
    $results.Add("initial-ready-frame=seq:$($initialReady.Sequence),pacman:$($initialReady.X),$($initialReady.Y)")
    $results.Add("initial-ready-capture=$readyCapture")

    Send-Right $true
    $results.Add('right-held-during-initial-ready=pass')
    $moving = Wait-ForFrame { param($frame) $frame.State -eq 'Playing' -and $frame.X -ge ($initialReady.X + 16) } 9000 $initialReady.Sequence
    if ($null -eq $moving) { throw 'Pac-Man did not move right after the held Ready input resumed gameplay.' }
    Send-Right $false
    $movingCapture = Join-Path $captureDirectory "pacman-pgm1-ready-input-$runId-playing.png"
    if (-not [PacManReadyInputCapture]::Capture($movingCapture, $compositor)) {
        throw 'Playing screen capture failed.'
    }
    $results.Add("playing-frame=seq:$($moving.Sequence),pacman:$($moving.X),$($moving.Y)")
    $results.Add("playing-capture=$movingCapture")
    $results.Add('held-ready-direction-started-movement=pass')

    Send-Right $false
    [PacManReadyInputCapture]::Key($compositor, 27, $true)
    [PacManReadyInputCapture]::Key($compositor, 27, $false)
    if (-not (Wait-ForLog "Cleanup complete app=$([regex]::Escape($appId)).*remainingWindows=0" 5000)) {
        throw 'Ready-input validation app did not clean up after Escape.'
    }
    $results.Add('escape-requested-app-close=pass')
}
catch {
    $failed = $true
    $results.Add("error=$($_.Exception.Message)")
    Write-Error $_
}
finally {
    try {
        if ($rightIsDown -and $compositor -ne [IntPtr]::Zero) { Send-Right $false }
        if ($process -and -not $process.HasExited) {
            try { Send-ServerCommand 'exit' } catch {}
            try { $process.StandardInput.Close() } catch {}
            if (-not $process.WaitForExit(20000)) { try { $process.Kill() } catch {} }
        }
        Add-OwnedDescendants
        foreach ($processId in @($ownedPids | Sort-Object -Descending -Unique)) {
            try { Stop-Process -Id $processId -Force -ErrorAction Stop } catch {}
        }
        Start-Sleep -Milliseconds 500
        $remaining = @(Get-ServerProcesses | Where-Object { $ownedPids.Contains([int]$_.ProcessId) })
        $results.Add("remaining-owned-server-processes=$($remaining.Count)")
        if ($process) { $results.Add("server-wrapper-exit-code=$($process.ExitCode)") }
    }
    catch { $failed = $true; $results.Add("cleanup-error=$($_.Exception.Message)") }
    $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = $oldFrameDiagnostics
    $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = $oldFreezeDiagnostics
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
    Write-Output "validation-summary=$summaryPath"
    Write-Output "runtime-log=$rawLog"
}

if ($failed) { exit 1 }
