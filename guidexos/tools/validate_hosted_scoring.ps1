using namespace System;
using namespace System.Diagnostics;
using namespace System.Drawing;
using namespace System.Drawing.Imaging;
using namespace System.IO;
using namespace System.Runtime.InteropServices;
using namespace System.Threading;

$ErrorActionPreference = 'Stop'

Add-Type -ReferencedAssemblies @('System.Drawing') @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class PacManScoringCapture {
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public const uint KEYUP = 0x0002;
    public static void Key(IntPtr hwnd, byte key, bool down) {
        SetForegroundWindow(hwnd);
        keybd_event(key, 0, down ? 0u : KEYUP, UIntPtr.Zero);
    }
    public static void Capture(IntPtr hwnd, string path) {
        SetForegroundWindow(hwnd);
        RECT rect;
        if (!GetWindowRect(hwnd, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top) return;
        using (Bitmap bitmap = new Bitmap(rect.Right - rect.Left, rect.Bottom - rect.Top))
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            graphics.CopyFromScreen(new Point(rect.Left, rect.Top), Point.Empty, bitmap.Size);
            bitmap.Save(path, ImageFormat.Png);
        }
    }
}
'@

$pacmanRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = 'D:\dev\guideXOSServer'
$rawLog = Join-Path $serverRoot 'hosted-pacman-scoring-raw.log'
$summaryPath = Join-Path $serverRoot 'hosted-pacman-scoring-validation.txt'
$captureDirectory = Join-Path $pacmanRoot 'captures'
$powerCapture = Join-Path $captureDirectory 'pacman-power-pill.png'

if (Get-Process -Name 'guideXOSServer.experimental' -ErrorAction SilentlyContinue) {
    throw 'guideXOSServer.experimental.exe is already running; close it before this isolated validation.'
}

New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
if (Test-Path -LiteralPath $rawLog) { Clear-Content -LiteralPath $rawLog }
$psi = [ProcessStartInfo]::new()
$psi.FileName = $env:ComSpec
$psi.Arguments = '/d /c ".\guideXOSServer.experimental.exe > hosted-pacman-scoring-raw.log 2>&1"'
$psi.WorkingDirectory = $serverRoot
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$process = [Process]::new()
$process.StartInfo = $psi
[void]$process.Start()

$results = [System.Collections.Generic.List[string]]::new()
$compositor = [IntPtr]::Zero

function Send-ServerCommand([string]$command) {
    $process.StandardInput.WriteLine($command)
    $process.StandardInput.Flush()
}

function Read-RawLog {
    if (Test-Path -LiteralPath $rawLog) {
        try { return (Get-Content -Raw -LiteralPath $rawLog -ErrorAction Stop) } catch { return '' }
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

function Send-Key([int]$key, [int]$durationMs) {
    [PacManScoringCapture]::Key($compositor, [byte]$key, $true)
    Start-Sleep -Milliseconds $durationMs
    [PacManScoringCapture]::Key($compositor, [byte]$key, $false)
}

try {
    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50 -and $compositor -eq [IntPtr]::Zero; ++$i) {
        Start-Sleep -Milliseconds 200
        $compositor = [PacManScoringCapture]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
    }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Hosted compositor window was not found.' }
    $results.Add('compositor=found')

    Send-ServerCommand 'desktop.launch Nexgen PacMan'
    if (-not (Wait-ForLog 'PacMan interactive frame presented')) { throw 'PacMan launch failed.' }
    Start-Sleep -Milliseconds 1200

    # From the historical start corridor, travel left to column 6, up column 6,
    # left along row 1, then down column 1 to the row-23 power pill.
    Send-Key 39 250
    Send-Key 37 2200
    Send-Key 38 4200
    Send-Key 37 1100
    Send-Key 40 4200
    if (-not (Wait-ForLog 'PacMan power pill consumed' 12000)) { throw 'Power pill was not consumed on the deterministic route.' }
    Start-Sleep -Milliseconds 3000
    [PacManScoringCapture]::Capture($compositor, $powerCapture)
    $results.Add("power-capture=$powerCapture")

    $raw = Read-RawLog
    $results.Add("normal-pill-log=$([bool]($raw -match 'PacMan normal pill consumed'))")
    $results.Add("power-pill-log=$([bool]($raw -match 'PacMan power pill consumed'))")
    $results.Add("score-log=$([bool]($raw -match 'PacMan score updated:'))")
    $results.Add("remaining-count-log=$([bool]($raw -match 'PacMan remaining consumables:'))")

    Send-Key 27 100
    if (-not (Wait-ForLog 'Cleanup complete app=com.guidexos.pacman.*remainingWindows=0' 30000)) {
        throw 'PacMan cleanup did not report zero remaining windows.'
    }
    $results.Add('escape-and-zero-window-cleanup=pass')
}
finally {
    if ($process -and -not $process.HasExited) {
        Send-ServerCommand 'exit'
        $process.StandardInput.Close()
        if (-not $process.WaitForExit(20000)) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
    if ($process) { $results.Add("server-exit-code=$($process.ExitCode)") }
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}
