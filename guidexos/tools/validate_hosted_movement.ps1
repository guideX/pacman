using namespace System;
using namespace System.Diagnostics;
using namespace System.Drawing;
using namespace System.Drawing.Imaging;
using namespace System.IO;
using namespace System.Runtime.InteropServices;
using namespace System.Threading;
using namespace System.Windows.Forms;

Add-Type -ReferencedAssemblies @('System.Drawing', 'System.Windows.Forms') @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;
public static class PacManHostedInput {
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public const uint KEYUP = 0x0002;
    public static void Key(IntPtr hwnd, byte key, bool down) {
        SetForegroundWindow(hwnd);
        keybd_event(key, 0, down ? 0u : KEYUP, UIntPtr.Zero);
    }
    public static void Capture(string path) {
        RECT rect;
        if (!GetWindowRect(_captureWindow, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top) {
            Rectangle bounds = Screen.PrimaryScreen.Bounds;
            using (Bitmap bitmap = new Bitmap(bounds.Width, bounds.Height))
            using (Graphics graphics = Graphics.FromImage(bitmap)) {
                graphics.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size);
                bitmap.Save(path, ImageFormat.Png);
            }
            return;
        }
        using (Bitmap bitmap = new Bitmap(rect.Right - rect.Left, rect.Bottom - rect.Top))
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            IntPtr hdc = graphics.GetHdc();
            bool printed = PrintWindow(_captureWindow, hdc, 2u);
            graphics.ReleaseHdc(hdc);
            if (!printed) graphics.CopyFromScreen(new Point(rect.Left, rect.Top), Point.Empty, bitmap.Size);
            bitmap.Save(path, ImageFormat.Png);
        }
    }
    private static IntPtr _captureWindow;
    public static void SetCaptureWindow(IntPtr hwnd) { _captureWindow = hwnd; }
}
'@

$pacmanRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = 'D:\dev\guideXOSServer'
$rawLog = Join-Path $serverRoot 'hosted-pacman-movement-raw.log'
$summaryPath = Join-Path $serverRoot 'hosted-pacman-movement-validation.txt'
$captureDirectory = Join-Path $pacmanRoot 'captures'
$movedCapture = Join-Path $captureDirectory 'pacman-moved.png'

if (Get-Process -Name 'guideXOSServer.experimental' -ErrorAction SilentlyContinue) {
    throw 'guideXOSServer.experimental.exe is already running; close it before this isolated validation.'
}

New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null
$psi = [ProcessStartInfo]::new()
$psi.FileName = $env:ComSpec
$psi.Arguments = '/d /c ".\guideXOSServer.experimental.exe > hosted-pacman-movement-raw.log 2>&1"'
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

function Wait-ForLogCount([string]$pattern, [int]$minimumCount, [int]$timeoutMs = 12000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        $count = [regex]::Matches((Read-RawLog), $pattern).Count
        if ($count -ge $minimumCount) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Last-CreatedWindowId {
    $matches = [regex]::Matches((Read-RawLog), 'Compositor created window id=(\d+)')
    if ($matches.Count -eq 0) { return 0 }
    return [uint64]$matches[$matches.Count - 1].Groups[1].Value
}

function Wait-ForNewWindowId([uint64]$previousId, [int]$timeoutMs = 30000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    do {
        $currentId = Last-CreatedWindowId
        if ($currentId -gt $previousId) { return $currentId }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return 0
}

function Send-Key([int]$key, [int]$durationMs = 250) {
    [PacManHostedInput]::Key($compositor, [byte]$key, $true)
    Start-Sleep -Milliseconds $durationMs
    [PacManHostedInput]::Key($compositor, [byte]$key, $false)
}

function Press-KeyDown([int]$key) {
    [PacManHostedInput]::Key($compositor, [byte]$key, $true)
}

function Press-KeyUp([int]$key) {
    [PacManHostedInput]::Key($compositor, [byte]$key, $false)
}

try {
    Send-ServerCommand 'gui.start'
    for ($i = 0; $i -lt 50 -and $compositor -eq [IntPtr]::Zero; ++$i) {
        Start-Sleep -Milliseconds 200
        $compositor = [PacManHostedInput]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
    }
    if ($compositor -eq [IntPtr]::Zero) { throw 'Hosted compositor window was not found.' }
    $results.Add('compositor=found')

    $previousWindowId = [uint64]0
    for ($cycle = 1; $cycle -le 3; ++$cycle) {
        Send-ServerCommand 'desktop.launch Nexgen PacMan'
        $pacmanWindowId = Wait-ForNewWindowId $previousWindowId
        if ($pacmanWindowId -eq 0) { throw "PacMan launch failed in cycle $($cycle): no new window." }
        $previousWindowId = $pacmanWindowId
        if (-not (Wait-ForLogCount 'PacMan interactive frame presented' $cycle)) { throw "PacMan launch failed in cycle $cycle." }
        Start-Sleep -Milliseconds 250
        if ($cycle -eq 1) {
            [PacManHostedInput]::SetCaptureWindow($compositor)
        }

        if ($cycle -eq 2) {
            Send-Key 39 1300
            Press-KeyDown 38
            Start-Sleep -Milliseconds 700
            Press-KeyUp 38
            Press-KeyDown 39
            Start-Sleep -Milliseconds 800
            Press-KeyUp 39
            Press-KeyDown 37
            Start-Sleep -Milliseconds 600
            Press-KeyUp 37
        } else {
            Send-Key 37 350
            Send-Key 39 350
            Send-Key 38 350
            Send-Key 40 350
            Send-Key 39 900
        }
        if ($cycle -eq 1) {
            [PacManHostedInput]::SetForegroundWindow($compositor)
            Start-Sleep -Milliseconds 100
            [PacManHostedInput]::Capture($movedCapture)
            $results.Add("cycle=$cycle moved-capture=$movedCapture")
        }
        Send-Key 27 100
        if (-not (Wait-ForLogCount 'Cleanup complete app=com.guidexos.pacman.*remainingWindows=0' $cycle 30000)) {
            throw "PacMan cleanup did not report zero remaining windows in cycle $cycle."
        }
        $results.Add("cycle=$cycle launch-move-escape=pass")
        if ($cycle -lt 3) { Start-Sleep -Milliseconds 6000 }
    }

    Send-ServerCommand 'nativeapp.processes'
    Start-Sleep -Milliseconds 500
    $raw = Read-RawLog
    $results.Add("discovery-and-elf-validation=$([bool]($raw -match 'Native ELF validated'))")
    $results.Add("focus-loss-log=$([bool]($raw -match 'focus lost; directional state cleared'))")
    $results.Add("focus-return-log=$([bool]($raw -match 'focus gained; waiting for new direction'))")
    $results.Add("tunnel-diagnostic=$([bool]($raw -match 'PacMan tunnel wrap'))")
    $results.Add("escape-diagnostic=$([bool]($raw -match 'PacMan Escape pressed'))")
    $results.Add("zero-window-cleanup=$([bool]($raw -match 'remainingWindows=0'))")
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
    if ($process) {
        $results.Add("server-exit-code=$($process.ExitCode)")
    }
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}
