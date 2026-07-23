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
public static class PacManDangerCapture4 {
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
        RECT rect;
        if (!GetWindowRect(hwnd, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top) {
            Rectangle bounds = Screen.PrimaryScreen.Bounds;
            using (Bitmap fallback = new Bitmap(bounds.Width, bounds.Height))
            using (Graphics fallbackGraphics = Graphics.FromImage(fallback)) {
                fallbackGraphics.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size);
                fallback.Save(path, ImageFormat.Png);
            }
            return;
        }
        using (Bitmap bitmap = new Bitmap(rect.Right - rect.Left, rect.Bottom - rect.Top))
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            SetForegroundWindow(hwnd);
            IntPtr hdc = graphics.GetHdc();
            bool printed = PrintWindow(hwnd, hdc, 2u);
            graphics.ReleaseHdc(hdc);
            if (!printed) graphics.CopyFromScreen(new Point(rect.Left, rect.Top), Point.Empty, bitmap.Size);
            bitmap.Save(path, ImageFormat.Png);
        }
    }
    public static void CapturePrimary(string path) {
        Rectangle bounds = Screen.PrimaryScreen.Bounds;
        using (Bitmap bitmap = new Bitmap(bounds.Width, bounds.Height))
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            graphics.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size);
            bitmap.Save(path, ImageFormat.Png);
        }
    }
}
'@

$pacmanRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = 'D:\dev\guideXOSServer'
$rawLog = Join-Path $serverRoot 'hosted-pacman-danger-raw.log'
$summaryPath = Join-Path $serverRoot 'hosted-pacman-danger-validation.txt'
$captureDirectory = Join-Path $pacmanRoot 'captures'

if (Get-Process -Name 'guideXOSServer.experimental' -ErrorAction SilentlyContinue) {
    throw 'guideXOSServer.experimental.exe is already running; close it before this isolated validation.'
}

New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
if (Test-Path -LiteralPath $rawLog) { Clear-Content -LiteralPath $rawLog }

$psi = [ProcessStartInfo]::new()
$psi.FileName = $env:ComSpec
$psi.Arguments = '/d /c ".\guideXOSServer.experimental.exe > hosted-pacman-danger-raw.log 2>&1"'
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
        try { return [File]::ReadAllText($rawLog) } catch { return '' }
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

function Send-Key([int]$key, [int]$durationMs = 100) {
    [PacManDangerCapture4]::Key($compositor, [byte]$key, $true)
    Start-Sleep -Milliseconds $durationMs
    [PacManDangerCapture4]::Key($compositor, [byte]$key, $false)
    Start-Sleep -Milliseconds 80
}

function Run-RedRoute {
    # Historical-coordinate route from Pac-Man's start to reachable Red:
    # The initial automatic right travel reaches logical column 14 first;
    # from there the route is left two, up three, left three, up nine, right four.
    Send-Key 37 260
    Send-Key 37 260
    Send-Key 38 650
    Send-Key 37 650
    Send-Key 38 1550
    Send-Key 39 800
}

function Capture-Hosted([string]$fileName) {
    Save-FullScreen $fileName
}

function Save-FullScreen([string]$fileName) {
    $path = Join-Path $captureDirectory $fileName
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    $results.Add("capture=$path")
}

function Launch-PacMan([int]$cycle) {
    Send-ServerCommand 'desktop.launch Nexgen PacMan'
    $launched = Wait-ForLogCount 'PacMan interactive frame presented' $cycle 30000
    $results.Add("cycle=$cycle launch=$launched")
    Start-Sleep -Milliseconds 1400
}

try {
    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50 -and $compositor -eq [IntPtr]::Zero; ++$i) {
        Start-Sleep -Milliseconds 200
        $compositor = [PacManDangerCapture4]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
    }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Hosted compositor window was not found.' }
    $results.Add('compositor=found')

    Launch-PacMan 1
    Write-Output 'danger-harness: launched cycle 1'
    Capture-Hosted 'pacman-four-ghosts.png'
    Write-Output 'danger-harness: captured ghosts'
    Run-RedRoute
    Write-Output 'danger-harness: route complete'
    $collision1 = Wait-ForLogCount 'PacMan ghost collision detected' 1 8000
    $results.Add("cycle=1 collision=$collision1")
    if ($collision1) {
        Start-Sleep -Milliseconds 150
        Save-FullScreen 'pacman-death-danger.png'
    }
    Send-Key 27 100
    $cleanup1 = Wait-ForLogCount 'Cleanup complete app=com.guidexos.pacman.*remainingWindows=0' 1 30000
    $results.Add("cycle=1 escape-during-death-cleanup=$cleanup1")

    Launch-PacMan 2
    for ($death = 1; $death -le 3; ++$death) {
        Run-RedRoute
        $collision = Wait-ForLogCount 'PacMan ghost collision detected' $death 9000
        $results.Add("cycle=2 collision-$death=$collision")
        if ($death -lt 3) { Start-Sleep -Milliseconds 2400 }
    }
    $gameOver = Wait-ForLog 'PacMan Game Over entered' 5000
    $results.Add("cycle=2 game-over=$gameOver")
    if ($gameOver) {
        Start-Sleep -Milliseconds 150
        Save-FullScreen 'pacman-game-over-danger.png'
    }
    Send-Key 13 100
    $restart = Wait-ForLog 'PacMan session restarted' 5000
    $results.Add("cycle=2 restart=$restart")
    if ($restart) {
        Start-Sleep -Milliseconds 250
        Save-FullScreen 'pacman-restart-danger.png'
    }
    Send-Key 27 100
    $cleanup2 = Wait-ForLogCount 'Cleanup complete app=com.guidexos.pacman.*remainingWindows=0' 2 30000
    $results.Add("cycle=2 escape-after-restart-cleanup=$cleanup2")

    Launch-PacMan 3
    Run-RedRoute
    $collision3 = Wait-ForLogCount 'PacMan ghost collision detected' 4 9000
    $results.Add("cycle=3 collision=$collision3")
    Send-Key 27 100
    $cleanup3 = Wait-ForLogCount 'Cleanup complete app=com.guidexos.pacman.*remainingWindows=0' 3 30000
    $results.Add("cycle=3 escape-cleanup=$cleanup3")

    Send-ServerCommand 'nativeapp.processes'
    Start-Sleep -Milliseconds 500
    $finalLog = Read-RawLog
    $results.Add("zero-owned-windows=$([bool]($finalLog -match 'remainingWindows=0'))")
    $results.Add("retained-frame-present-diagnostics=$([bool]($finalLog -match 'present_frame call count'))")
}
catch {
    $results.Add("error=$($_.Exception.Message)")
}
finally {
    try {
    if ($process -and -not $process.HasExited) {
        Send-ServerCommand 'exit'
        $process.StandardInput.Close()
        if (-not $process.WaitForExit(20000)) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
    if ($process) { $results.Add("server-exit-code=$($process.ExitCode)") }

    # The server is started through cmd.exe for log redirection; clean the
    # child runtime if cmd reports success before its child has exited.
    Get-Process -Name 'guideXOSServer.experimental' -ErrorAction SilentlyContinue | Stop-Process -Force
    } catch {
        $results.Add("cleanup-error=$($_.Exception.Message)")
    }
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}
