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
public static class PacManFruitCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    public const uint KEYUP = 0x0002;
    public static void Key(IntPtr hwnd, byte key, bool down) {
        if (hwnd == IntPtr.Zero) throw new InvalidOperationException("The compositor handle is invalid.");
        SetForegroundWindow(hwnd); keybd_event(key, 0, down ? 0u : KEYUP, UIntPtr.Zero);
    }
    public static bool Capture(string path, IntPtr hwnd) {
        RECT rect; if (hwnd == IntPtr.Zero || !GetWindowRect(hwnd, out rect)) return false;
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
$serverRoot = if ($env:PACMAN_GUIDEXOS_SERVER_ROOT) { $env:PACMAN_GUIDEXOS_SERVER_ROOT } else { 'D:\dev\guideXOSServer' }
$serverRootFull = [IO.Path]::GetFullPath($serverRoot).TrimEnd('\')
$serverExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.experimental.exe'))
$normalServerExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.exe'))
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$rawLog = Join-Path $serverRoot "hosted-pacman-fruit-$runId-raw.log"
$summaryPath = Join-Path $serverRoot "hosted-pacman-fruit-$runId-validation.txt"
$captureDirectory = if ($env:PACMAN_FRUIT_CAPTURE_DIRECTORY) { $env:PACMAN_FRUIT_CAPTURE_DIRECTORY } else { Join-Path $pacmanRoot 'captures' }
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
                -not $ownedPids.Contains([int]$item.ProcessId)) { [void]$ownedPids.Add([int]$item.ProcessId) }
        }
    }
}
function Read-RawLog {
    if (-not (Test-Path -LiteralPath $rawLog)) { return '' }
    try {
        # Diagnostics can make a long run grow quickly.  The validation markers
        # and recent frame/synchronization records are all near the active tail;
        # avoid rereading the entire log on every 100 ms polling pass.
        $stream = [File]::Open($rawLog, [FileMode]::Open, [FileAccess]::Read, [FileShare]::ReadWrite)
        try {
            $tailBytes = 8MB
            $offset = [Math]::Max([int64]0, $stream.Length - $tailBytes)
            [void]$stream.Seek($offset, [SeekOrigin]::Begin)
            $reader = [StreamReader]::new($stream)
            try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
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
    $process.StandardInput.WriteLine($command); $process.StandardInput.Flush()
}
function Get-Frames {
    $pattern = 'PacMan frame seq=(?<seq>\d+) window=(?<window>\d+) state=(?<state>\w+).*?score=(?<score>\d+).*?lives=(?<lives>\d+).*?level=(?<level>\d+).*?remaining=(?<remaining>\d+).*?fruitPhase=(?<fruitPhase>[\w-]+).*?fruitType=(?<fruitType>\d+).*?fruitX=(?<fruitX>-?\d+).*?fruitY=(?<fruitY>-?\d+).*?fruitTimer=(?<fruitTimer>\d+).*?fruitScoreValue=(?<fruitScoreValue>\d+).*?fruitPopupTimer=(?<fruitPopupTimer>\d+).*?fruitTimeCount=(?<fruitTimeCount>\d+).*?fruitAppearance=(?<fruitAppearance>\d+).*?lifeAwardMask=(?<lifeAwardMask>\d+).*?lifeAwards=(?<lifeAwards>\d+)'
    $frames = [System.Collections.Generic.List[object]]::new()
    foreach ($match in [regex]::Matches((Read-RawLog), $pattern)) {
        [void]$frames.Add([pscustomobject]@{
            Sequence = [uint64]$match.Groups['seq'].Value; WindowId = [uint64]$match.Groups['window'].Value
            State = $match.Groups['state'].Value; Score = [uint32]$match.Groups['score'].Value
            Lives = [uint32]$match.Groups['lives'].Value; Level = [uint32]$match.Groups['level'].Value
            Remaining = [uint32]$match.Groups['remaining'].Value; FruitPhase = $match.Groups['fruitPhase'].Value
            FruitType = [uint32]$match.Groups['fruitType'].Value; FruitX = [int]$match.Groups['fruitX'].Value
            FruitY = [int]$match.Groups['fruitY'].Value; FruitTimer = [uint32]$match.Groups['fruitTimer'].Value
            FruitScoreValue = [uint32]$match.Groups['fruitScoreValue'].Value
            FruitPopupTimer = [uint32]$match.Groups['fruitPopupTimer'].Value
            FruitTimeCount = [uint32]$match.Groups['fruitTimeCount'].Value
            FruitAppearance = [uint32]$match.Groups['fruitAppearance'].Value
            LifeAwardMask = [uint32]$match.Groups['lifeAwardMask'].Value
            LifeAwards = [uint32]$match.Groups['lifeAwards'].Value
        })
    }
    return $frames
}
function Wait-ForFrame([uint64]$minimumSequence, [scriptblock]$predicate, [int]$timeoutMs = 20000) {
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
    $deadline = [DateTime]::UtcNow.AddMilliseconds(10000); $sync = $null
    do {
        Send-ServerCommand "gui.sync $($frame.WindowId) 0 $($frame.Sequence) 1"; Start-Sleep -Milliseconds 100
        $matches = [regex]::Matches((Read-RawLog), "Compositor frame sync windowId=$($frame.WindowId) expectedFrameGeneration=0 expectedFrameSequence=$($frame.Sequence) frameSequence=(\d+) frameGeneration=(\d+) paintGeneration=(\d+) captureGeneration=(\d+).*result=PASS")
        if ($matches.Count -gt 0) {
            $m = $matches[$matches.Count - 1]
            $sync = [pscustomobject]@{ Sequence=[uint64]$m.Groups[1].Value; FrameGeneration=[uint64]$m.Groups[2].Value; PaintGeneration=[uint64]$m.Groups[3].Value; CaptureGeneration=[uint64]$m.Groups[4].Value }
        }
    } while ($null -eq $sync -and [DateTime]::UtcNow -lt $deadline)
    if ($null -eq $sync) { throw "No synchronized frame for $label." }
    if ($sync.FrameGeneration -ne $sync.PaintGeneration) { throw "Frame/paint mismatch for $label." }
    $boundary = [regex]::Matches((Read-RawLog), "Native ELF frame boundary runtimeId=(\d+) windowId=$($frame.WindowId) incomingFrameSeq=$($sync.Sequence)")
    if ($boundary.Count -eq 0) { throw "No runtime boundary for $label." }
    $runtimeId = [uint64]$boundary[$boundary.Count - 1].Groups[1].Value
    $path = Join-Path $captureDirectory "pacman-fruit-$runId-$label-seq$($frame.Sequence)-gen$($sync.FrameGeneration).png"
    try { if (-not [PacManFruitCapture]::Capture($path, $compositor)) { throw "Capture failed for $label." } }
    finally { Send-ServerCommand "gui.unfreeze $($frame.WindowId)" }
    $results.Add("capture=$path")
    $results.Add("capture-target runtimeId=$runtimeId windowId=$($frame.WindowId) applicationSequence=$($frame.Sequence) frameSequence=$($sync.Sequence) frameGeneration=$($sync.FrameGeneration) paintGeneration=$($sync.PaintGeneration) captureGeneration=$($sync.CaptureGeneration) level=$($frame.Level) score=$($frame.Score) lives=$($frame.Lives) fruitPhase=$($frame.FruitPhase) fruitType=$($frame.FruitType) fruitScore=$($frame.FruitScoreValue) fruitPopupTimer=$($frame.FruitPopupTimer) fruitX=$($frame.FruitX) fruitY=$($frame.FruitY) fruitTimer=$($frame.FruitTimer) fruitAppearance=$($frame.FruitAppearance)")
    return $frame
}
function Escape-And-Wait {
    Send-ServerCommand 'gui.close 1000'
    if (-not (Wait-ForLog 'NativeAppRuntime.*Cleanup complete.*remainingWindows=0' 10000)) { throw 'Fruit validation window did not clean up.' }
}

try {
    if (@(Get-ServerProcesses).Count -gt 0) { throw 'A guideXOS Server process is already running; refusing to overlap it.' }
    if ([PacManFruitCapture]::FindWindow('GXOS_COMPOSITOR', $null) -ne [IntPtr]::Zero) { throw 'Another compositor owns the compositor window.' }
    New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
    $psi = [ProcessStartInfo]::new(); $psi.FileName = $env:ComSpec
    $psi.Arguments = "/d /c `".\guideXOSServer.experimental.exe > $(Split-Path -Leaf $rawLog) 2>&1`""
    $psi.WorkingDirectory = $serverRoot; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $false
    $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = '1'; $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = '1'
    $process = [Process]::new(); $process.StartInfo = $psi
    if (-not $process.Start()) { throw 'Failed to start the owned experimental server wrapper.' }
    [void]$ownedPids.Add($process.Id); $results.Add("run=$runId"); $results.Add("server-wrapper-pid=$($process.Id)")
    for ($i = 0; $i -lt 100; ++$i) { Add-OwnedDescendants; if (@(Get-ServerProcesses | Where-Object { $_.ParentProcessId -eq $process.Id -and $_.ExecutablePath -ieq $serverExe }).Count -gt 0) { break }; if ($i -eq 99) { throw "Experimental server child did not start; inspect $rawLog" }; Start-Sleep -Milliseconds 100 }
    if (-not (Wait-ForLog 'Commands:' 10000)) { throw 'Experimental server command loop did not become ready.' }
    # The first redirected command line is consumed during console startup.
    Send-ServerCommand ''
    if (-not (Wait-ForLog 'Unknown command \(help for list\)' 5000)) { throw 'Experimental server command input did not become ready.' }
    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50; ++$i) { $compositor = [PacManFruitCapture]::FindWindow('GXOS_COMPOSITOR', $null); if ($compositor -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 200 }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Hosted compositor was not created.' }
    $results.Add("compositor-window=$compositor"); Send-ServerCommand 'desktop.launch Nexgen PacMan Fruit Validation'
    if (-not (Wait-ForLog 'FRUITVAL PREPARE_SPAWN' 20000)) { throw 'Fruit validation ELF did not launch or prepare.' }
    $initial = Wait-ForFrame 0 { param($f) $f.Level -eq 1 -and $f.FruitPhase -eq 'inactive' } 12000
    if ($null -eq $initial) { throw 'Initial inactive fruit frame was not observed.' }
    [void](Sync-Capture $initial 'before-trigger')
    $spawn = Wait-ForFrame $initial.Sequence { param($f) $f.Level -eq 1 -and $f.FruitPhase -eq 'visible' -and $f.FruitType -eq 0 -and $f.FruitScoreValue -eq 500 -and $f.FruitX -eq 232 -and $f.FruitY -eq 280 } 15000
    if ($null -eq $spawn) { throw 'Level 1 visible fruit frame was not observed.' }
    if (-not (Wait-ForLog 'FRUITVAL SPAWN_OBSERVED' 10000)) { throw 'Fruit spawn milestone was not observed.' }
    [void](Sync-Capture $spawn 'level1-fruit-visible')
    $results.Add('fruit-trigger=pass')
    $popup = Wait-ForFrame $spawn.Sequence { param($f) $f.Level -eq 1 -and $f.FruitPhase -eq 'score-popup' -and $f.FruitScoreValue -eq 500 -and $f.FruitPopupTimer -gt 0 -and $f.Score -eq 10000 -and $f.Lives -eq 4 -and $f.LifeAwards -eq 1 } 12000
    if ($null -eq $popup) { throw 'Fruit collection/score-popup/extra-life frame was not observed.' }
    [void](Sync-Capture $popup 'fruit-collected-score-popup')
    if (-not (Wait-ForLog 'FRUITVAL SCORE_POPUP_OBSERVED' 10000)) { throw 'Fruit score popup milestone was not observed.' }
    if (-not (Wait-ForLog 'FRUITVAL SCORE_POPUP_EXPIRED' 10000)) { throw 'Fruit score popup expiration was not observed.' }
    if (-not (Wait-ForLog 'FRUITVAL EXTRA_LIFE_OBSERVED' 10000)) { throw 'Extra-life milestone was not observed.' }
    $results.Add('fruit-collection=pass')
    $results.Add('fruit-score-popup=pass')
    $results.Add('fruit-popup-expiration=pass')
    $results.Add('extra-life=pass')
    $level4 = Wait-ForFrame $popup.Sequence { param($f) $f.Level -eq 4 -and $f.FruitPhase -eq 'visible' -and $f.FruitType -eq 3 } 30000
    if ($null -eq $level4) { throw 'Level 4 fruit frame was not observed.' }
    [void](Sync-Capture $level4 'level4-fruit-visible')
    if (-not (Wait-ForLog 'FRUITVAL LEVEL4_EXPIRATION_OBSERVED' 30000)) { throw 'Level 4 fruit expiration was not observed.' }
    $results.Add('level4-expiration=pass')
    $level5 = Wait-ForFrame $level4.Sequence { param($f) $f.Level -eq 5 -and $f.FruitPhase -eq 'visible' -and $f.FruitType -eq 4 } 30000
    if ($null -eq $level5) { throw 'Level 5 fruit frame was not observed.' }
    [void](Sync-Capture $level5 'level5-fruit-visible')
    if (-not (Wait-ForLog 'FRUITVAL LEVEL5_EXPIRATION_OBSERVED' 30000)) { throw 'Level 5 fruit expiration was not observed.' }
    $results.Add('level5-expiration=pass')
    $level8 = Wait-ForFrame $level5.Sequence { param($f) $f.Level -eq 8 -and $f.FruitPhase -eq 'visible' -and $f.FruitType -eq 7 } 30000
    if ($null -eq $level8) { throw 'Level 8 fruit frame was not observed.' }
    [void](Sync-Capture $level8 'level8-fruit-visible')
    if (-not (Wait-ForLog 'FRUITVAL LEVEL8_EXPIRATION_OBSERVED' 30000)) { throw 'Level 8 fruit expiration was not observed.' }
    $results.Add('level8-expiration=pass')
    Escape-And-Wait; $results.Add('escape-and-zero-window-cleanup=pass')
}
catch { $failed = $true; $results.Add("error=$($_.Exception.Message)"); Write-Error $_ }
finally {
    try {
        if ($process -and -not $process.HasExited) { try { Send-ServerCommand 'exit' } catch {}; try { $process.StandardInput.Close() } catch {}; if (-not $process.WaitForExit(20000)) { try { $process.Kill() } catch {} } }
        Add-OwnedDescendants
        foreach ($processId in @($ownedPids | Sort-Object -Descending -Unique)) { try { Stop-Process -Id $processId -Force -ErrorAction Stop } catch {} }
        Start-Sleep -Milliseconds 500; $remaining = @(Get-ServerProcesses); $results.Add("remaining-owned-server-processes=$($remaining.Count)")
        if ($process) { $results.Add("server-wrapper-exit-code=$($process.ExitCode)") }
    } catch { $failed = $true; $results.Add("cleanup-error=$($_.Exception.Message)") }
    $env:GXOS_PACMAN_FRAME_DIAGNOSTICS = $oldFrameDiagnostics; $env:GXOS_COMPOSITOR_FREEZE_DIAGNOSTICS = $oldFreezeDiagnostics
    [File]::WriteAllLines($summaryPath, [string[]]$results); $results | ForEach-Object { $_ }
}
if ($failed) { exit 1 }
