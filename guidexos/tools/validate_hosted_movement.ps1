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
public static class PacManProductionCapture1 {
    [DllImport("user32.dll", CharSet = CharSet.Ansi)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
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
}
'@

$pacmanRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = 'D:\dev\guideXOSServer'
$serverRootFull = [IO.Path]::GetFullPath($serverRoot).TrimEnd('\')
$serverExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.experimental.exe'))
$normalServerExe = [IO.Path]::GetFullPath((Join-Path $serverRoot 'guideXOSServer.exe'))
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$rawLog = Join-Path $serverRoot "hosted-pacman-production-$runId-raw.log"
$summaryPath = Join-Path $serverRoot "hosted-pacman-production-$runId-validation.txt"
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

function Wait-ForNoAdditionalLogCount([string]$pattern, [int]$baselineCount, [int]$timeoutMs = 500) {
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
    [PacManProductionCapture1]::Key($compositor, [byte]$key, $true)
    Start-Sleep -Milliseconds $durationMs
    [PacManProductionCapture1]::Key($compositor, [byte]$key, $false)
    Start-Sleep -Milliseconds 80
}

function Capture-Hosted([string]$label) {
    $path = Join-Path $captureDirectory "pacman-production-$runId-$label.png"
    [PacManProductionCapture1]::CapturePrimary($path)
    $results.Add("capture=$path")
}

function Launch-ProductionPacMan {
    $script:launchCount++
    Send-ServerCommand 'desktop.launch Nexgen PacMan'
    if (-not (Wait-ForLogCount 'PacMan interactive frame presented' $script:launchCount 30000)) {
        throw "Production validation launch $($script:launchCount) did not present an interactive frame or compositor window."
    }
    $windowId = Last-CreatedWindowId
    if ($windowId -eq 0) { throw "Production validation launch $($script:launchCount) presented no compositor window." }
    $results.Add("cycle=$($script:launchCount) launch=True windowId=$windowId")
    Start-Sleep -Milliseconds 250
}

function Escape-And-Wait([int]$cycle) {
    Send-Key 27
    if (-not (Wait-ForLogCount 'Cleanup complete app=com.guidexos.pacman.*remainingWindows=0' $cycle 30000)) {
        throw "Production validation cleanup did not report zero owned windows in cycle $cycle."
    }
    $results.Add("cycle=$cycle escape-cleanup=True")
}

try {
    $existing = @(Get-ServerProcesses)
    if ($existing.Count -gt 0) {
        $details = ($existing | ForEach-Object { "PID=$($_.ProcessId) path=$($_.ExecutablePath)" }) -join '; '
        throw "A guideXOS Server process is already running; refusing to overlap it: $details"
    }
    $existingCompositor = [PacManProductionCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
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
    Send-ServerCommand 'desktop.apps.verbose'
    for ($i = 0; $i -lt 50 -and $compositor -eq [IntPtr]::Zero; ++$i) {
        Start-Sleep -Milliseconds 200
        $compositor = [PacManProductionCapture1]::FindWindow('GXOS_COMPOSITOR', 'guideXOSCpp Compositor')
    }
    if ($compositor -eq [IntPtr]::Zero) {
        throw 'Hosted compositor window was not created. Another compositor instance may be blocking window creation.'
    }
    $results.Add('compositor=found')

    $cycleOneCollisionBaseline = Get-LogCount 'PacMan ghost collision detected'
    Launch-ProductionPacMan
    Capture-Hosted 'four-ghosts'
    $discovery = Wait-ForLog 'Native ELF validated' 5000
    $ghosts = @('red', 'pink', 'cyan', 'orange') | ForEach-Object { Wait-ForLog $_ 3000 } | Where-Object { $_ }
    $results.Add("discovery-and-elf-validation=$discovery")
    $results.Add("four-ghost-initialization=$($ghosts.Count -eq 4)")
    $noCollision = Wait-ForNoAdditionalLogCount 'PacMan ghost collision detected' $cycleOneCollisionBaseline 500
    $results.Add("no-automatic-launch-collision=$noCollision")

    Send-Key 39 1300
    Send-Key 38 700
    Send-Key 39 800
    Send-Key 37 600
    Capture-Hosted 'movement-and-score'
    $pill = Wait-ForLog 'PacMan normal pill consumed' 2000
    $score = Wait-ForLog 'PacMan score updated: 10' 2000
    $remaining = Wait-ForLog 'PacMan remaining consumables: 243' 2000
    $results.Add("movement-pills-score=$($pill -and $score -and $remaining)")
    Escape-And-Wait 1

    $cycleTwoCollisionBaseline = Get-LogCount 'PacMan ghost collision detected'
    Launch-ProductionPacMan
    $repeatNoCollision = Wait-ForNoAdditionalLogCount 'PacMan ghost collision detected' $cycleTwoCollisionBaseline 500
    $results.Add("repeat-launch-no-collision=$repeatNoCollision")
    $cycleBaselineSelfCheck = $cycleOneCollisionBaseline -ge 0 -and
        $cycleTwoCollisionBaseline -ge $cycleOneCollisionBaseline -and
        $noCollision -and $repeatNoCollision
    $results.Add("cycle-one-collision-baseline=$cycleOneCollisionBaseline")
    $results.Add("cycle-two-collision-baseline=$cycleTwoCollisionBaseline")
    $results.Add("cycle-baseline-self-check=$cycleBaselineSelfCheck")
    Escape-And-Wait 2

    Send-ServerCommand 'nativeapp.processes'
    Start-Sleep -Milliseconds 500
    Add-OwnedDescendants
    $finalLog = Read-RawLog
    $results.Add("focus-diagnostics=$([bool]($finalLog -match 'focus gained; waiting for new direction'))")
    $results.Add("escape-diagnostic=$([bool]($finalLog -match 'PacMan Escape pressed'))")
    $results.Add("zero-owned-windows=$([bool]($finalLog -match 'remainingWindows=0'))")
    $results.Add("retained-frame-present-diagnostics=$([bool]($finalLog -match 'present_frame call count'))")
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
