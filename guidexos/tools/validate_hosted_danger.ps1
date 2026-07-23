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
            IntPtr hdc = graphics.GetHdc();
            bool printed;
            try { printed = PrintWindow(hwnd, hdc, 2); }
            finally { graphics.ReleaseHdc(hdc); }
            if (!printed) return false;
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

function Capture-Hosted([string]$label) {
    $path = Join-Path $captureDirectory "pacman-danger-$runId-$label.png"
    if (-not [PacManDangerCapture5]::CaptureCompositor($path, $compositor)) {
        throw "Failed to capture the owned guideXOS compositor window for $label."
    }
    $results.Add("capture=$path")
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
    Launch-DangerPacMan
    Capture-Hosted 'playing'
    $noEarlyCollision = Wait-ForNoAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 350
    $results.Add("cycle=1 no-automatic-collision-before-schedule=$noEarlyCollision")

    $collision1 = Wait-ForAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 1 5000
    $results.Add("cycle=1 collision=$collision1")
    if (-not $collision1) { throw 'Danger validation did not detect the first deterministic collision.' }
    Start-Sleep -Milliseconds 350
    Capture-Hosted 'dying'
    $death = (Get-LogCount 'PacMan death state entered') -ge 1
    $life = (Get-LogCount 'PacMan life decremented; lives remaining: 2') -eq ($lifeTwoBaseline + 1)
    $duplicateSuppressed = Wait-ForNoAdditionalLogCount 'PacMan ghost collision detected' ($collisionBaseline + 1) 350
    $results.Add("cycle=1 dying=$death lives-decremented-once=$life duplicate-death-suppressed=$duplicateSuppressed")

    $resetBaseline = Get-LogCount 'PacMan actors reset after death'
    $actorReset = Wait-ForAdditionalLogCount 'PacMan actors reset after death' $resetBaseline 1 3000
    Start-Sleep -Milliseconds 350
    Capture-Hosted 'ready'
    $readyCollisionBaseline = Get-LogCount 'PacMan ghost collision detected'
    $readySafe = Wait-ForNoAdditionalLogCount 'PacMan ghost collision detected' $readyCollisionBaseline 350
    $results.Add("cycle=1 actor-reset=$actorReset ready-collision-free-window=$readySafe")
    if (-not ($death -and $life -and $duplicateSuppressed -and $actorReset -and $readySafe)) {
        throw 'Danger validation did not preserve the bounded death and Ready-after-death transition.'
    }

    $collisionB = Wait-ForAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 2 8000
    $collisionC = Wait-ForAdditionalLogCount 'PacMan ghost collision detected' $collisionBaseline 3 8000
    $gameOverBaseline = Get-LogCount 'PacMan Game Over entered'
    $lifeZero = (Get-LogCount 'PacMan life decremented; lives remaining: 0') -ge 1
    $gameOver = Wait-ForAdditionalLogCount 'PacMan Game Over entered' $gameOverBaseline 1 8000
    $results.Add("cycle=1 collisions=$($collision1 -and $collisionB -and $collisionC) game-over=$gameOver lives-zero=$lifeZero")
    if (-not ($collision1 -and $collisionB -and $collisionC -and $gameOver -and $lifeZero)) {
        throw 'Danger validation did not reach Game Over after three bounded collisions.'
    }
    Start-Sleep -Milliseconds 500
    Capture-Hosted 'game-over'
    $restart = $false
    for ($attempt = 1; $attempt -le 3 -and -not $restart; ++$attempt) {
        Send-Key 39 80
        Send-Key 13
        $restart = Wait-ForLog 'PacMan session restarted' 2000
    }
    $results.Add("cycle=1 restart=$restart")
    if (-not $restart) { throw 'Enter did not restart the Game Over session.' }
    Start-Sleep -Milliseconds 700
    Capture-Hosted 'restart'
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
    [File]::WriteAllLines($summaryPath, [string[]]$results)
    $results | ForEach-Object { $_ }
}

if ($failed) { exit 1 }
