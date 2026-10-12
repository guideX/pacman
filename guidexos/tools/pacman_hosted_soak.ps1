<#
.SYNOPSIS
    Run production-gameplay Pac-Man in a private guideXOS/QEMU instance.

.DESCRIPTION
    Copies an existing Server ESP and firmware into a unique Pac-Man-owned
    runtime directory, builds Pac-Man with diagnostics enabled and test hooks
    disabled, and launches the copied ESP under QEMU. The source Server tree
    and ESP are read-only inputs. QEMU receives only the run-owned FAT
    directory, read-only UEFI code, a private serial log, and a unique QMP
    endpoint. Arrow-key pulses are checked against guest-side input logs.
#>

[CmdletBinding()]
param(
    [ValidateRange(15, 60)]
    [int]$DurationMinutes = 30,
    [ValidateRange(1, 600)]
    [int]$StartupTimeoutSeconds = 300,
    [ValidateRange(1024, 4096)]
    [int]$MemoryMB = 1536,
    [ValidateRange(1, 4)]
    [int]$Vcpus = 2,
    [ValidateSet("whpx", "tcg")]
    [string]$Acceleration = "whpx",
    [string]$ServerRoot = "D:\dev\guideXOSServer",
    [string]$QemuPath = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$FirmwareCodePath = "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
    [string]$ClangPath = "C:\Program Files\LLVM\bin\clang++.exe",
    [string]$LldPath = "C:\Program Files\LLVM\bin\ld.lld.exe",
    [string]$CmakePath = "C:\mingw64\bin\cmake.exe"
)

$ErrorActionPreference = "Stop"
$PacmanRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$GuidexosRoot = Join-Path $PacmanRoot "guidexos"
$ServerEsp = Join-Path $ServerRoot "ESP"
$SdkInclude = Join-Path $ServerRoot "sdk\include"
$NinjaPath = "C:\mingw64\bin\ninja.exe"
$OwnershipId = [guid]::NewGuid().ToString("N")
$RunId = "{0}-{1}" -f [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss-fff"), $OwnershipId.Substring(0, 10)
$RunParent = Join-Path $env:LOCALAPPDATA "PacManHostedSoak"
$RunDir = Join-Path $RunParent $RunId
$EspDir = Join-Path $RunDir "esp"
$FirmwareDir = Join-Path $RunDir "firmware"
$BuildObserved = Join-Path $RunDir "build-diagnostics"
$BuildProduction = Join-Path $RunDir "build-production"
$ObservedPackageRoot = Join-Path $RunDir "packages-diagnostics"
$ProductionPackageRoot = Join-Path $RunDir "packages-production"
$ObservedBin = Join-Path $RunDir "native-diagnostics"
$ProductionBin = Join-Path $RunDir "native-production"
$LogDir = Join-Path $RunDir "logs"
$ScreenshotDir = Join-Path $RunDir "screenshots"
$TestDir = Join-Path $RunDir "regression"
$SerialPath = Join-Path $LogDir "qemu-serial.log"
$QemuDebugPath = Join-Path $LogDir "qemu-debug.log"
$QemuStdoutPath = Join-Path $LogDir "qemu-stdout.log"
$QemuStderrPath = Join-Path $LogDir "qemu-stderr.log"
$ValidationPath = Join-Path $LogDir "validation.log"
$ProcessPath = Join-Path $RunDir "process.json"
$InputsPath = Join-Path $RunDir "inputs.json"
$SummaryPath = Join-Path $RunDir "run-summary.json"
$InputCsvPath = Join-Path $RunDir "input-events.csv"
$MetricsCsvPath = Join-Path $RunDir "host-metrics.csv"
$GhostCsvPath = Join-Path $RunDir "ghost-observations.csv"
$GameEventsPath = Join-Path $LogDir "pacman-events.log"
$script:QemuProcess = $null
$script:QmpPort = 0
$script:SerialReader = $null
$script:SerialPending = ""
$script:SerialTail = ""
$script:LatestKeyDownSeq = 0L
$script:LatestKeyUpSeq = 0L
$script:LatestInputEvent = $null
$script:LatestTiming = @{}
$script:LatestGhosts = @{}
$script:LatestGameState = ""
$script:PendingFaultCaptures = [System.Collections.Generic.List[object]]::new()
$script:ReportedStalls = @{}
$script:InputLatencies = [System.Collections.Generic.List[double]]::new()
$script:InputEventRecords = [System.Collections.Generic.List[object]]::new()
$script:DeathCount = 0
$script:GameOverCount = 0
$script:SessionRestartCount = 0
$script:PowerPillCount = 0
$script:FrightenedCount = 0
$script:GhostEatenCount = 0
$script:TunnelWrapCount = 0
$script:QemuCpuPrevious = 0.0
$script:QemuSamplePreviousSeconds = 0.0
$script:RunWatch = $null
$script:ProcessStartUtc = $null
$script:GameplayStartUtc = $null
$script:InputActionCount = 0
$script:InputAttemptCount = 0
$script:InputDeliveryFailures = 0
$script:ConsecutiveInputDeliveryFailures = 0
$script:FaultCount = 0
$script:ExternalQemuSnapshot = @()
$script:RunResult = "not-started"
$script:RunError = ""
$script:RegressionResults = [ordered]@{}
$script:ProductionBuildVerified = $false
$script:SoakBuildVerified = $false
$script:KeyboardEvidence = $false
$script:LegalTurnEvidence = $false
$script:PacmanMovementEvidence = $false
$script:GhostMovementEvidence = @{ Red = $false; Pink = $false; Cyan = $false; Orange = $false }
$script:PreviousPacmanPlayingPosition = ""
$script:PreviousGhostObservations = @{}

function Write-Validation([string]$Message) {
    $line = "[{0}] {1}" -f [DateTime]::UtcNow.ToString("o"), $Message
    Write-Host $line
    Add-Content -LiteralPath $ValidationPath -Value $line
}

function Assert-File([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw ("Missing {0}: {1}" -f $Description, $Path)
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Write-Json([string]$Path, [object]$Value) {
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Write-CsvRecord([string]$Path, [object]$Record) {
    if (!(Test-Path -LiteralPath $Path)) {
        $Record | ConvertTo-Csv -NoTypeInformation | Set-Content -LiteralPath $Path -Encoding utf8
    } else {
        $lines = @($Record | ConvertTo-Csv -NoTypeInformation)
        if ($lines.Count -le 1) { return }
        $encoding = [System.Text.UTF8Encoding]::new($false)
        for ($attempt = 0; $attempt -lt 12; ++$attempt) {
            $stream = $null
            $writer = $null
            try {
                $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                    [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)
                $stream.Position = $stream.Length
                $writer = [System.IO.StreamWriter]::new($stream, $encoding, 1024, $true)
                $writer.WriteLine($lines[1])
                $writer.Flush()
                return
            } catch [System.IO.IOException] {
                if ($attempt -ge 11) { throw }
                Start-Sleep -Milliseconds 100
            } finally {
                if ($writer) { $writer.Dispose() }
                if ($stream) { $stream.Dispose() }
            }
        }
    }
}

function Invoke-LoggedCommand([string]$Executable, [string[]]$Arguments,
                              [string]$Description, [string]$LogPath) {
    Write-Validation "$Description begin"
    $output = & $Executable @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($output) { $output | Add-Content -LiteralPath $LogPath -Encoding utf8 }
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode. See $LogPath"
    }
    Write-Validation "$Description complete exitCode=$exitCode"
}

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function Read-QmpResponse([System.Net.Sockets.NetworkStream]$Stream,
                          [string]$ExpectedId, [int]$TimeoutMs) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $pending = ""
    $buffer = New-Object byte[] 4096
    do {
        if ($Stream.DataAvailable) {
            $read = $Stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            $pending += [System.Text.Encoding]::UTF8.GetString($buffer, 0, $read)
            while (($newlineIndex = $pending.IndexOf([char]10)) -ge 0) {
                $line = $pending.Substring(0, $newlineIndex).TrimEnd([char]13)
                $pending = $pending.Substring($newlineIndex + 1)
                if ([string]::IsNullOrWhiteSpace($line)) { continue }
                $message = $line | ConvertFrom-Json
                if ($message.error) { throw "QMP command error: $line" }
                if ($message.id -eq $ExpectedId) { return $line }
            }
        } else {
            Start-Sleep -Milliseconds 2
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for QMP response id=$ExpectedId after ${TimeoutMs}ms."
}

function Send-Qmp([string]$CommandLine) {
    if (!$script:QemuProcess) { throw "Refusing QMP access before this run owns a QEMU process." }
    $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $script:QmpPort -ErrorAction SilentlyContinue)
    $ownedListener = @($listeners | Where-Object { $_.OwningProcess -eq $script:QemuProcess.Id })
    $foreignListener = @($listeners | Where-Object { $_.OwningProcess -ne $script:QemuProcess.Id })
    if ($foreignListener.Count -gt 0) {
        throw "Refusing QMP access: another process owns port $script:QmpPort."
    }
    if ($ownedListener.Count -eq 0) {
        throw "Refusing QMP access: owned QEMU PID $($script:QemuProcess.Id) does not own listening port $script:QmpPort."
    }
    $maximumCapabilityAttempts = 3
    for ($capabilityAttempt = 1; $capabilityAttempt -le $maximumCapabilityAttempts; ++$capabilityAttempt) {
        $client = [System.Net.Sockets.TcpClient]::new()
        $retryCapability = $false
        try {
            $client.Connect("127.0.0.1", $script:QmpPort)
            $stream = $client.GetStream()
            $stream.ReadTimeout = 1500
            $newline = [string][char]13 + [string][char]10
            $capabilityRequest = '{"execute":"qmp_capabilities","id":"pacman-capabilities"}' + $newline
            $capabilityBytes = [System.Text.Encoding]::UTF8.GetBytes($capabilityRequest)
            $stream.Write($capabilityBytes, 0, $capabilityBytes.Length)
            try {
                [void](Read-QmpResponse $stream "pacman-capabilities" 5000)
            } catch {
                if ($_.Exception.Message -notlike "Timed out waiting for QMP response id=pacman-capabilities*") {
                    throw
                }
                if ($capabilityAttempt -ge $maximumCapabilityAttempts) { throw }
                $retryCapability = $true
                Write-Validation "qmp.capabilities.retry attempt=$capabilityAttempt/$maximumCapabilityAttempts reason=$($_.Exception.Message)"
            }
            if (!$retryCapability) {
                $request = @{
                    execute = "human-monitor-command"
                    arguments = @{ "command-line" = $CommandLine }
                    id = "pacman-hmp"
                } | ConvertTo-Json -Compress
                $request = $request + $newline
                $requestBytes = [System.Text.Encoding]::UTF8.GetBytes($request)
                $stream.Write($requestBytes, 0, $requestBytes.Length)
                $response = Read-QmpResponse $stream "pacman-hmp" 10000
                return $response
            }
        } finally {
            if ($client) { $client.Close() }
        }
        if ($retryCapability) { Start-Sleep -Milliseconds 250 }
    }
    throw "QMP capability negotiation failed after $maximumCapabilityAttempts attempts."
}

function Quote-WindowsArgument([string]$Value) {
    if ($Value.Contains('"')) { $Value = $Value.Replace('"', '\"') }
    if ($Value -match '\s') { return '"' + $Value + '"' }
    return $Value
}

function Get-SerialText {
    if (!$script:SerialReader) { return }
    try {
        $endPosition = $script:SerialReader.Length
        if ($script:SerialReader.Position -gt $endPosition) { $script:SerialReader.Position = 0 }
        $chunkBuilder = [System.Text.StringBuilder]::new()
        while ($script:SerialReader.Position -lt $endPosition) {
            $available = [int][Math]::Min(65536, $endPosition - $script:SerialReader.Position)
            $bytes = New-Object byte[] $available
            $read = $script:SerialReader.Read($bytes, 0, $available)
            if ($read -le 0) { break }
            [void]$chunkBuilder.Append([System.Text.Encoding]::UTF8.GetString($bytes, 0, $read))
        }
        if ($chunkBuilder.Length -eq 0) { return }
        $script:SerialPending += $chunkBuilder.ToString()
        $lastNewline = $script:SerialPending.LastIndexOf([char]10)
        if ($lastNewline -lt 0) { return }
        $completeText = $script:SerialPending.Substring(0, $lastNewline + 1)
        $script:SerialPending = $script:SerialPending.Substring($lastNewline + 1)
        # Kernel PIT heartbeats can interrupt a native-app serial write mid-line.
        # Remove the in-band heartbeat and its newline so the app record rejoins.
        $completeText = [regex]::Replace($completeText,
            '\[KERNEL-HEARTBEAT\] ticks=[0-9A-Fa-f]+ source=PIT_IRQ0\r?\n', '')
        $lines = $completeText.Split([char[]]@([char]10), [StringSplitOptions]::RemoveEmptyEntries)
        $tailBuilder = [System.Text.StringBuilder]::new()
        $eventLines = [System.Collections.Generic.List[string]]::new()
        foreach ($rawLine in $lines) {
            $line = $rawLine.TrimEnd([char]13)
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            [void]$tailBuilder.AppendLine($line)
            if ($line -match 'PacMan (timing|key event|(?:Red|Pink|Cyan|Orange) state|requested direction|buffered turn accepted|direction blocked|tunnel wrap|death state entered|Game Over entered|session restarted|power pill consumed|ghost entered frightened|ghost eaten|level complete|gameplay resumed)') {
                $eventLines.Add($line)
            }
            if ($line -match 'PacMan key event seq=(?<seq>\d+) action=(?<action>down|up) direction=(?<direction>Up|Down|Left|Right) tick_ms=(?<tick>\d+)') {
                $eventRecord = [pscustomobject]@{
                    Sequence = [long]$Matches.seq
                    Action = $Matches.action
                    Direction = $Matches.direction
                    TickMs = [long]$Matches.tick
                    Line = $line
                }
                $script:LatestInputEvent = $eventRecord
                $script:InputEventRecords.Add($eventRecord)
                if ($eventRecord.Action -eq "down") {
                    $script:KeyboardEvidence = $true
                    if ($eventRecord.Sequence -gt $script:LatestKeyDownSeq) {
                        $script:LatestKeyDownSeq = $eventRecord.Sequence
                    }
                } elseif ($eventRecord.Sequence -gt $script:LatestKeyUpSeq) {
                    $script:LatestKeyUpSeq = $eventRecord.Sequence
                }
            }
            if ($line -match 'PacMan timing elapsed_ms=(?<elapsed>\d+)') {
                $sample = @{}
                foreach ($field in [regex]::Matches($line, '(?<name>[a-z0-9_]+)=(?<value>[A-Za-z0-9,._-]+)')) {
                    $sample[$field.Groups["name"].Value] = $field.Groups["value"].Value
                }
                $script:LatestTiming = $sample
                if ($sample.ContainsKey("pacman_state")) {
                    $script:LatestGameState = $sample["pacman_state"]
                }
            }
            if ($line -match 'PacMan (?<ghost>Red|Pink|Cyan|Orange) state x=(?<x>-?\d+) y=(?<y>-?\d+)') {
                $ghostSample = @{}
                foreach ($field in [regex]::Matches($line, '(?<name>[a-z0-9_]+)=(?<value>[A-Za-z0-9,._-]+)')) {
                    $ghostSample[$field.Groups["name"].Value] = $field.Groups["value"].Value
                }
                $name = $Matches.ghost
                $position = "{0},{1}" -f $ghostSample["x"], $ghostSample["y"]
                $movementExpected = $ghostSample["movement_expected"] -eq "1"
                if ($script:PreviousGhostObservations.ContainsKey($name)) {
                    $previousGhost = $script:PreviousGhostObservations[$name]
                    if ($movementExpected -and $previousGhost.MovementExpected -and
                        $position -ne $previousGhost.Position) {
                        $script:GhostMovementEvidence[$name] = $true
                    }
                }
                $script:PreviousGhostObservations[$name] = @{
                    Position = $position
                    MovementExpected = $movementExpected
                }
                $script:LatestGhosts[$name] = $ghostSample
                if ($ghostSample.ContainsKey("movement_expected") -and
                    $ghostSample["movement_expected"] -eq "1" -and
                    $ghostSample.ContainsKey("progress_age_ms") -and
                    [long]$ghostSample["progress_age_ms"] -ge 5000) {
                    if (!$script:ReportedStalls.ContainsKey($name) -or !$script:ReportedStalls[$name]) {
                        $fault = [pscustomobject]@{
                            Ghost = $name
                            Position = "{0},{1}" -f $ghostSample["x"], $ghostSample["y"]
                            State = $ghostSample["condition"]
                            Release = $ghostSample["release"]
                            AgeMs = [long]$ghostSample["progress_age_ms"]
                        }
                        $script:PendingFaultCaptures.Add($fault)
                        $script:ReportedStalls[$name] = $true
                    }
                } elseif ($script:LatestGhosts[$name].ContainsKey("movement_expected") -and
                    ($script:LatestGhosts[$name]["movement_expected"] -eq "0" -or
                     [long]$script:LatestGhosts[$name]["progress_age_ms"] -lt 5000)) {
                    $script:ReportedStalls[$name] = $false
                }
                Write-CsvRecord $GhostCsvPath ([pscustomobject]@{
                    observed_utc = [DateTime]::UtcNow.ToString("o")
                    elapsed_seconds = if ($script:RunWatch) { [Math]::Round($script:RunWatch.Elapsed.TotalSeconds, 3) } else { 0 }
                    ghost = $name
                    x = $ghostSample["x"]
                    y = $ghostSample["y"]
                    direction = $ghostSample["dir"]
                    condition = $ghostSample["condition"]
                    release_state = $ghostSample["release"]
                    active = $ghostSample["active"]
                    collision_active = $ghostSample["collision_active"]
                    movement_expected = $ghostSample["movement_expected"]
                    progress_age_ms = $ghostSample["progress_age_ms"]
                    possible_stall = $ghostSample["possible_stall"]
                    pacman_state = $script:LatestGameState
                })
            }
            if ($line.Contains("PacMan death state entered")) { ++$script:DeathCount }
            if ($line.Contains("PacMan Game Over entered")) { ++$script:GameOverCount }
            if ($line.Contains("PacMan session restarted")) { ++$script:SessionRestartCount }
            if ($line.Contains("PacMan power pill consumed")) { ++$script:PowerPillCount }
            if ($line.Contains("PacMan ghost entered frightened")) { ++$script:FrightenedCount }
            if ($line.Contains("PacMan ghost eaten")) { ++$script:GhostEatenCount }
            if ($line.Contains("PacMan tunnel wrap")) { ++$script:TunnelWrapCount }
        }
        $script:SerialTail += $tailBuilder.ToString()
        if ($script:SerialTail.Length -gt 1048576) {
            $script:SerialTail = $script:SerialTail.Substring($script:SerialTail.Length - 1048576)
        }
        if ($eventLines.Count -gt 0) {
            Add-Content -LiteralPath $GameEventsPath -Value $eventLines.ToArray()
        }
    } catch {
        Write-Validation "serial.read.error=$($_.Exception.Message)"
        throw
    }
}

function Wait-ForSerial([string]$Needle, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Get-SerialText
        if ($script:SerialTail.IndexOf($Needle, [System.StringComparison]::Ordinal) -ge 0) { return $true }
        if ($script:QemuProcess -and $script:QemuProcess.HasExited) { return $false }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Send-GuestKey([string]$Key, [int]$HoldMs = 160) {
    [void](Send-Qmp ("sendkey {0} {1}" -f $Key, $HoldMs))
}

function Wait-InputAction([string]$Action, [string]$Direction,
                          [long]$PreviousSequence, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Get-SerialText
        for ($eventIndex = $script:InputEventRecords.Count - 1; $eventIndex -ge 0; --$eventIndex) {
            $eventRecord = $script:InputEventRecords[$eventIndex]
            if ($eventRecord.Sequence -le $PreviousSequence) { break }
            if ($eventRecord.Action -eq $Action -and $eventRecord.Direction -eq $Direction) {
                return $eventRecord
            }
        }
        if ($script:QemuProcess -and $script:QemuProcess.HasExited) { return $null }
        Start-Sleep -Milliseconds 20
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Send-ObservedDirection([string]$Key, [int]$HoldMs) {
    Get-SerialText
    ++$script:InputAttemptCount
    $direction = switch ($Key) {
        "up" { "Up"; break }
        "down" { "Down"; break }
        "left" { "Left"; break }
        "right" { "Right"; break }
        default { throw "Unsupported Pac-Man direction: $Key" }
    }
    $beforeDown = $script:LatestKeyDownSeq
    $beforeUp = $script:LatestKeyUpSeq
    $sendUtc = [DateTime]::UtcNow
    $deliveryWatch = [System.Diagnostics.Stopwatch]::StartNew()
    Send-GuestKey $Key $HoldMs
    $down = Wait-InputAction "down" $direction $beforeDown 12
    $deliveryWatch.Stop()
    if (!$down) {
        Write-CsvRecord $InputCsvPath ([pscustomobject]@{
            sent_utc = $sendUtc.ToString("o")
            qmp_key = $Key
            hold_ms = $HoldMs
            down_event = "missing"
            up_event = "unknown"
            delivery_ms = ""
            guest_tick_ms = ""
            result = "FAIL"
        })
        ++$script:InputDeliveryFailures
        ++$script:ConsecutiveInputDeliveryFailures
        Write-Validation "input.missing action=down key=$Key consecutive_failures=$script:ConsecutiveInputDeliveryFailures"
        if ($script:ConsecutiveInputDeliveryFailures -ge 3) {
            throw "Pac-Man missed three consecutive injected key-down events; stopping the gameplay run."
        }
        return
    }
    $latency = $deliveryWatch.Elapsed.TotalMilliseconds
    $script:InputLatencies.Add($latency)
    $upTimeout = [Math]::Max(12, [int][Math]::Ceiling($HoldMs / 1000.0) + 10)
    $up = Wait-InputAction "up" $direction $beforeUp $upTimeout
    $record = [pscustomobject]@{
        sent_utc = $sendUtc.ToString("o")
        qmp_key = $Key
        hold_ms = $HoldMs
        down_event = $down.Sequence
        up_event = if ($up) { $up.Sequence } else { "missing" }
        delivery_ms = [Math]::Round($latency, 3)
        guest_tick_ms = $down.TickMs
        result = if ($up) { "PASS" } else { "DOWN_ONLY" }
    }
    Write-CsvRecord $InputCsvPath $record
    $upSeq = if ($up) { $up.Sequence } else { "missing" }
    Write-Validation ("input.sent key={0} hold_ms={1} event={2}/{3} delivery_ms={4:N3}" -f
        $Key, $HoldMs, $down.Sequence, $upSeq, $latency)
    if (!$up) {
        ++$script:InputDeliveryFailures
        ++$script:ConsecutiveInputDeliveryFailures
        Write-Validation "input.missing action=up key=$Key consecutive_failures=$script:ConsecutiveInputDeliveryFailures"
        if ($script:ConsecutiveInputDeliveryFailures -ge 3) {
            throw "Pac-Man missed three consecutive injected key-up events; stopping the gameplay run."
        }
        return
    }
    $script:ConsecutiveInputDeliveryFailures = 0
    ++$script:InputActionCount
}

function Convert-PpmToPng([string]$PpmPath, [string]$PngPath) {
    Assert-File $PpmPath "QEMU framebuffer PPM"
    $converter = Join-Path $GuidexosRoot "tools\ppm_to_png.py"
    Assert-File $converter "PPM-to-PNG helper"
    $output = & python $converter $PpmPath $PngPath 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Screenshot conversion failed: $($output -join ' ')" }
}

function Capture-Screenshot([string]$Name) {
    $ppmPath = Join-Path $ScreenshotDir ($Name + ".ppm")
    $pngPath = Join-Path $ScreenshotDir ($Name + ".png")
    $qemuPath = $ppmPath.Replace('\', '/')
    [void](Send-Qmp ('screendump "{0}"' -f $qemuPath))
    Convert-PpmToPng $ppmPath $pngPath
    Write-Validation "screenshot.PASS name=$Name ppm=$ppmPath png=$pngPath"
    return $pngPath
}

function Save-FaultCaptures {
    while ($script:PendingFaultCaptures.Count -gt 0) {
        $fault = $script:PendingFaultCaptures[0]
        $script:PendingFaultCaptures.RemoveAt(0)
        ++$script:FaultCount
        $elapsed = if ($script:RunWatch) { [int]$script:RunWatch.Elapsed.TotalSeconds } else { 0 }
        $name = "fault-{0}-{1:D2}-{2}" -f $fault.Ghost, $elapsed, $script:FaultCount
        Write-Validation "ghost.possible_stall ghost=$($fault.Ghost) position=$($fault.Position) condition=$($fault.State) release=$($fault.Release) progress_age_ms=$($fault.AgeMs)"
        [void](Capture-Screenshot $name)
    }
}

function Add-HostHealthSample([double]$ElapsedSeconds, [bool]$Final = $false) {
    if (!$script:QemuProcess) { return }
    Get-SerialText
    $script:QemuProcess.Refresh()
    if ($script:QemuProcess.HasExited) {
        throw "Owned QEMU exited during gameplay with code $($script:QemuProcess.ExitCode)."
    }
    $process = Get-Process -Id $script:QemuProcess.Id -ErrorAction Stop
    $cpuNow = [double]$process.CPU
    $cpuDelta = $cpuNow - $script:QemuCpuPrevious
    $secondsDelta = $ElapsedSeconds - $script:QemuSamplePreviousSeconds
    $cpuPercent = if ($secondsDelta -gt 0) { 100.0 * $cpuDelta / $secondsDelta / $Vcpus } else { 0.0 }
    $script:QemuCpuPrevious = $cpuNow
    $script:QemuSamplePreviousSeconds = $ElapsedSeconds
    $os = Get-CimInstance Win32_OperatingSystem
    $freeBytes = [double]$os.FreePhysicalMemory * 1024.0
    if ($script:LatestTiming.ContainsKey("turn_accepts") -and
        [long]$script:LatestTiming["turn_accepts"] -gt 0) {
        $script:LegalTurnEvidence = $true
    }
    if ($script:LatestTiming["pacman_state"] -eq "Playing" -and
        $script:LatestTiming.ContainsKey("pacman")) {
        $position = [string]$script:LatestTiming["pacman"]
        if ($script:PreviousPacmanPlayingPosition -and
            $position -ne $script:PreviousPacmanPlayingPosition) {
            $script:PacmanMovementEvidence = $true
        }
        $script:PreviousPacmanPlayingPosition = $position
    }
    $sample = [pscustomobject]@{
        wall_utc = [DateTime]::UtcNow.ToString("o")
        elapsed_seconds = [Math]::Round($ElapsedSeconds, 3)
        guest_elapsed_ms = $script:LatestTiming["elapsed_ms"]
        guest_ticks_ms = $script:LatestTiming["guest_ticks_ms"]
        simulation_ms = $script:LatestTiming["simulation_ms"]
        simulation_steps = $script:LatestTiming["sim_steps"]
        pacman_state = $script:LatestTiming["pacman_state"]
        frame_sequence = $script:LatestTiming["frame_seq"]
        guest_fps = if ($script:LatestTiming.ContainsKey("fps_milli")) { [Math]::Round([double]$script:LatestTiming["fps_milli"] / 1000.0, 3) } else { "" }
        frame_avg_us = $script:LatestTiming["frame_avg_us"]
        frame_p95_ms = $script:LatestTiming["frame_p95_ms"]
        frame_p99_ms = $script:LatestTiming["frame_p99_ms"]
        sim_backlog_ms = $script:LatestTiming["backlog_ms"]
        sim_max_backlog_ms = $script:LatestTiming["max_backlog_ms"]
        sim_dropped_ms = $script:LatestTiming["dropped_ms"]
        sim_catchup_frames = $script:LatestTiming["catchup_frames"]
        input_key_down = $script:LatestTiming["key_down"]
        input_key_up = $script:LatestTiming["key_up"]
        accepted_turns = $script:LatestTiming["turn_accepts"]
        turn_latency_avg_ms = $script:LatestTiming["turn_latency_avg_ms"]
        turn_latency_max_ms = $script:LatestTiming["turn_latency_max_ms"]
        blocked_turns = $script:LatestTiming["blocked_turns"]
        pacman_x_y = $script:LatestTiming["pacman"]
        held_direction_mask = $script:LatestTiming["held_mask"]
        pacman_progress_age_ms = $script:LatestTiming["pacman_progress_age_ms"]
        qemu_pid = $script:QemuProcess.Id
        qemu_cpu_pct_of_vcpus = [Math]::Round($cpuPercent, 3)
        qemu_working_set_mb = [Math]::Round($process.WorkingSet64 / 1MB, 1)
        qemu_private_mb = [Math]::Round($process.PrivateMemorySize64 / 1MB, 1)
        host_free_memory_gb = [Math]::Round($freeBytes / 1GB, 2)
        final_sample = $Final
    }
    Write-CsvRecord $MetricsCsvPath $sample
    $sample | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $LogDir "health-samples.jsonl") -Encoding utf8
    Write-Validation ("health elapsed_s={0:N1} sim_ms={1} steps={2} state={3} fps={4} frame_avg_us={5} p95={6} p99={7} backlog_ms={8} dropped_ms={9} cpu_pct_2vcpu={10:N2} ws_mb={11} private_mb={12} free_gb={13}" -f
        $ElapsedSeconds, $sample.simulation_ms, $sample.simulation_steps, $sample.pacman_state,
        $sample.guest_fps, $sample.frame_avg_us, $sample.frame_p95_ms, $sample.frame_p99_ms,
        $sample.sim_backlog_ms, $sample.sim_dropped_ms, $cpuPercent,
        $sample.qemu_working_set_mb, $sample.qemu_private_mb, $sample.host_free_memory_gb)
    if ($freeBytes -lt 2GB) {
        throw "Host free memory fell below 2 GiB; stopping the owned VM to protect other work."
    }
    if ($freeBytes -lt ($MemoryMB * 1MB + 2GB)) {
        throw "Host memory headroom is too low for the configured QEMU guest."
    }
    Save-FaultCaptures
}

function Get-PacmanIsGameOver([string]$State) {
    return $State -eq "GameOver"
}

function Get-LatencyPercentile([int]$Percentile) {
    if ($script:InputLatencies.Count -eq 0) { return 0.0 }
    $sorted = @($script:InputLatencies | Sort-Object)
    $rank = [Math]::Max(0, [Math]::Ceiling($Percentile * $sorted.Count / 100.0) - 1)
    return [Math]::Round([double]$sorted[$rank], 3)
}

function Get-WindowAverage([object[]]$Samples, [string]$Property) {
    $values = @($Samples | Where-Object { $_.$Property -ne "" } | ForEach-Object { [double]$_.$Property })
    if ($values.Count -eq 0) { return $null }
    return [Math]::Round(($values | Measure-Object -Average).Average, 3)
}

function Confirm-OwnedQemuProcess {
    if (!$script:QemuProcess) { return $false }
    $processInfo = Get-CimInstance Win32_Process -Filter ("ProcessId = {0}" -f $script:QemuProcess.Id) -ErrorAction SilentlyContinue
    if (!$processInfo) { return $false }
    return ($processInfo.ExecutablePath -and
        [string]::Equals($processInfo.ExecutablePath, $QemuPath, [System.StringComparison]::OrdinalIgnoreCase) -and
        $processInfo.CommandLine -and $processInfo.CommandLine.Contains($OwnershipId))
}

function Stop-OwnedQemu {
    if (!$script:QemuProcess) { return }
    try {
        $script:QemuProcess.Refresh()
        if ($script:QemuProcess.HasExited) { return }
        if ($script:QmpPort -gt 0) {
            try {
                [void](Send-Qmp "quit")
                Write-Validation "cleanup.qmp_quit requested pid=$($script:QemuProcess.Id)"
            } catch {
                Write-Validation "cleanup.qmp_quit failed pid=$($script:QemuProcess.Id) reason=$($_.Exception.Message)"
            }
        }
        if (!$script:QemuProcess.WaitForExit(8000)) {
            if (Confirm-OwnedQemuProcess) {
                Write-Validation "cleanup.verified_exact_process_stop pid=$($script:QemuProcess.Id) ownership=$OwnershipId"
                Stop-Process -Id $script:QemuProcess.Id -Force -ErrorAction Stop
                [void]$script:QemuProcess.WaitForExit(8000)
            } else {
                Write-Validation "cleanup.skip_stop identity_check_failed pid=$($script:QemuProcess.Id) ownership=$OwnershipId"
            }
        }
    } catch {
        Write-Validation "cleanup.error pid=$($script:QemuProcess.Id) reason=$($_.Exception.Message)"
    }
}

function Build-PacmanPackages {
    Assert-File $CmakePath "CMake"
    Assert-File $NinjaPath "Ninja"
    Assert-File $ClangPath "LLVM clang++"
    Assert-File $LldPath "LLVM lld"
    Assert-File (Join-Path $SdkInclude "guidexos\abi.h") "guideXOS Native ELF ABI header"
    Assert-File (Join-Path $GuidexosRoot "resources\generated\level1.gximg") "Pac-Man maze resource"
    Assert-File (Join-Path $GuidexosRoot "resources\generated\pacpics.gximg") "Pac-Man sprite resource"

    $cmakeBase = @(
        "-S", $GuidexosRoot,
        "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=$NinjaPath",
        "-DGUIDEXOS_SERVER_ROOT=$($ServerRoot.Replace('\', '/'))",
        "-DGUIDEXOS_CLANG=$ClangPath",
        "-DGUIDEXOS_LLD=$LldPath",
        "-DPACMAN_HOSTED_DANGER_TEST=OFF",
        "-DPACMAN_HOSTED_RED_MOVEMENT_TEST=OFF",
        "-DPACMAN_HOSTED_PINK_MOVEMENT_TEST=OFF",
        "-DPACMAN_HOSTED_CYAN_MOVEMENT_TEST=OFF",
        "-DPACMAN_HOSTED_ORANGE_MOVEMENT_TEST=OFF",
        "-DPACMAN_HOSTED_POWER_PILL_TEST=OFF",
        "-DPACMAN_HOSTED_LEVEL_TEST=OFF",
        "-DPACMAN_BAREMETAL_LEVEL_VALIDATION=OFF",
        "-DPACMAN_HOSTED_FRUIT_TEST=OFF",
        "-DPACMAN_BAREMETAL_FRUIT_VALIDATION=OFF"
    )
    $observedConfigure = $cmakeBase + @(
        "-B", $BuildObserved,
        "-DGUIDEXOS_PACKAGE_ROOT=$($ObservedPackageRoot.Replace('\', '/'))",
        "-DGUIDEXOS_LOCAL_BIN_DIR=$($ObservedBin.Replace('\', '/'))",
        "-DPACMAN_ENABLE_DIAGNOSTICS=ON"
    )
    Invoke-LoggedCommand $CmakePath $observedConfigure "diagnostic gameplay configure" (Join-Path $LogDir "build-diagnostics.log")
    $observedBuild = @("--build", $BuildObserved, "--target", "pacman-native", "-j", "2")
    Invoke-LoggedCommand $CmakePath $observedBuild "diagnostic Native ELF build" (Join-Path $LogDir "build-diagnostics.log")

    $productionConfigure = $cmakeBase + @(
        "-B", $BuildProduction,
        "-DGUIDEXOS_PACKAGE_ROOT=$($ProductionPackageRoot.Replace('\', '/'))",
        "-DGUIDEXOS_LOCAL_BIN_DIR=$($ProductionBin.Replace('\', '/'))",
        "-DPACMAN_ENABLE_DIAGNOSTICS=OFF"
    )
    Invoke-LoggedCommand $CmakePath $productionConfigure "production configure" (Join-Path $LogDir "build-production.log")
    $productionBuild = @("--build", $BuildProduction, "--target", "pacman-native", "-j", "2")
    Invoke-LoggedCommand $CmakePath $productionBuild "production Native ELF build" (Join-Path $LogDir "build-production.log")

    $testTargets = @("--build", $BuildObserved, "--target", "pacman-game-tests",
        "pacman-audio-tests", "pacman-endurance-tests", "-j", "2")
    Invoke-LoggedCommand $CmakePath $testTargets "Pac-Man regression test build" (Join-Path $LogDir "regression-build.log")
    foreach ($test in @(
        @{ Name = "gameplay"; Path = (Join-Path $BuildObserved "pacman-game-tests.exe") },
        @{ Name = "audio"; Path = (Join-Path $BuildObserved "pacman-audio-tests.exe") },
        @{ Name = "endurance"; Path = (Join-Path $BuildObserved "pacman-endurance-tests.exe") }
    )) {
        Assert-File $test.Path "$($test.Name) test executable"
        $output = & $test.Path 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -LiteralPath (Join-Path $TestDir ($test.Name + ".log")) -Encoding utf8
        if ($exitCode -ne 0) { throw "$($test.Name) regression suite failed with exit code $exitCode." }
        $script:RegressionResults[$test.Name] = "PASS"
        Write-Validation "regression.$($test.Name)=PASS output=$((@($output) -join ' ') -replace '\s+', ' ')"
    }

    $diagnosticElf = Join-Path $ObservedBin "pacman.elf"
    $productionElf = Join-Path $ProductionBin "pacman.elf"
    $diagnosticPackage = Join-Path $ObservedPackageRoot "PacMan"
    $productionPackage = Join-Path $ProductionPackageRoot "PacMan"
    Assert-File $diagnosticElf "diagnostic Pac-Man Native ELF"
    Assert-File $productionElf "production Pac-Man Native ELF"
    Assert-File (Join-Path $diagnosticPackage "app.json") "diagnostic Pac-Man manifest"
    Assert-File (Join-Path $productionPackage "app.json") "production Pac-Man manifest"

    $manifest = Get-Content -LiteralPath (Join-Path $diagnosticPackage "app.json") -Raw | ConvertFrom-Json
    $productionManifest = Get-Content -LiteralPath (Join-Path $productionPackage "app.json") -Raw | ConvertFrom-Json
    if ($manifest.id -ne "com.guidexos.pacman" -or $manifest.kind -ne "NativeElf" -or
        $productionManifest.id -ne "com.guidexos.pacman") {
        throw "Generated Pac-Man package does not use the canonical Native ELF manifest."
    }
    $productionBytes = [System.IO.File]::ReadAllBytes($productionElf)
    if ($productionBytes.Length -lt 20 -or $productionBytes[0] -ne 0x7f -or
        $productionBytes[1] -ne 0x45 -or $productionBytes[2] -ne 0x4c -or
        $productionBytes[3] -ne 0x46 -or $productionBytes[4] -ne 2 -or
        $productionBytes[5] -ne 1 -or $productionBytes[18] -ne 0x3e -or
        $productionBytes[19] -ne 0) {
        throw "The normal production output is not a little-endian AMD64 ELF64."
    }
    $productionText = [System.Text.Encoding]::ASCII.GetString($productionBytes)
    foreach ($diagnosticString in @(
        "PacMan timing elapsed_ms=",
        "PacMan key event seq=",
        "PacMan requested direction:",
        "PacMan buffered turn accepted"
    )) {
        if ($productionText.Contains($diagnosticString)) {
            throw "Production ELF unexpectedly contains enabled gameplay diagnostics: $diagnosticString"
        }
    }
    $productionCache = Get-Content -LiteralPath (Join-Path $BuildProduction "CMakeCache.txt") -Raw
    if ($productionCache -notmatch 'PACMAN_ENABLE_DIAGNOSTICS:BOOL=OFF' -or
        $productionCache -match 'PACMAN_(HOSTED|BAREMETAL)_[A-Z_]+:BOOL=ON') {
        throw "Production configuration has diagnostics or a test hook enabled."
    }
    $observedCache = Get-Content -LiteralPath (Join-Path $BuildObserved "CMakeCache.txt") -Raw
    if ($observedCache -notmatch 'PACMAN_ENABLE_DIAGNOSTICS:BOOL=ON' -or
        $observedCache -match 'PACMAN_(HOSTED|BAREMETAL)_[A-Z_]+:BOOL=ON') {
        throw "The soak build needs diagnostics ON with every test hook OFF."
    }
    Write-Validation "production.PASS elf=$productionElf sha256=$(Get-Sha256 $productionElf) diagnostics=OFF test_hooks=OFF"
    Write-Validation "soak.package sha256=$(Get-Sha256 $diagnosticElf) diagnostics=ON test_hooks=OFF"
    $script:ProductionBuildVerified = $true
    $script:SoakBuildVerified = $true
    return $diagnosticPackage
}

function Start-IsolatedGuest([string]$PackagePath) {
    Assert-File (Join-Path $ServerEsp "EFI\BOOT\BOOTX64.EFI") "guideXOS UEFI bootloader"
    Assert-File (Join-Path $ServerEsp "kernel.elf") "guideXOS kernel in Server ESP"
    Assert-File (Join-Path $ServerEsp "ramdisk.img") "guideXOS boot ramdisk"
    Assert-File $QemuPath "QEMU x86_64 executable"
    Assert-File $FirmwareCodePath "x86_64 UEFI firmware code"

    New-Item -ItemType Directory -Path $EspDir, $FirmwareDir, $LogDir, $ScreenshotDir -Force | Out-Null
    Get-ChildItem -LiteralPath $ServerEsp -Force | Copy-Item -Destination $EspDir -Recurse -Force
    $firmwareCopy = Join-Path $FirmwareDir "edk2-x86_64-code.fd"
    Copy-Item -LiteralPath $FirmwareCodePath -Destination $firmwareCopy

    $guestApps = Join-Path $EspDir "Apps"
    New-Item -ItemType Directory -Path $guestApps -Force | Out-Null
    $guestPacman = Join-Path $guestApps "PacMan"
    if (Test-Path -LiteralPath $guestPacman) {
        $resolvedApps = (Resolve-Path -LiteralPath $guestApps).Path.TrimEnd('\') + '\'
        $resolvedPackage = (Resolve-Path -LiteralPath $guestPacman).Path
        if (!$resolvedPackage.StartsWith($resolvedApps, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a package outside run-owned ESP\Apps."
        }
        Remove-Item -LiteralPath $resolvedPackage -Recurse -Force
    }
    Copy-Item -LiteralPath $PackagePath -Destination $guestPacman -Recurse -Force
    $stagedFiles = @(Get-ChildItem -LiteralPath $guestPacman -Recurse -File)
    if ($stagedFiles.Count -lt 4) { throw "Staged Pac-Man package is incomplete." }
    foreach ($file in $stagedFiles) {
        $relative = $file.FullName.Substring($guestPacman.Length + 1).Replace('\', '/')
        Write-Validation "guest.package.file path=/Apps/PacMan/$relative bytes=$($file.Length) sha256=$(Get-Sha256 $file.FullName)"
    }

    $freeMemoryBytes = [double](Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory * 1024.0
    $requiredMemoryBytes = $MemoryMB * 1MB + 4GB
    if ($freeMemoryBytes -lt $requiredMemoryBytes) {
        throw "Insufficient RAM headroom: free_gb=$([Math]::Round($freeMemoryBytes / 1GB, 2)) guest_mb=$MemoryMB required_headroom_gb=4"
    }
    $logicalProcessors = (Get-CimInstance Win32_Processor |
        Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
    if ($logicalProcessors -lt ($Vcpus + 2)) {
        throw "Insufficient host CPU capacity for $Vcpus vCPUs with two host logical CPUs left available."
    }
    $script:ExternalQemuSnapshot = @(Get-CimInstance Win32_Process |
        Where-Object { $_.Name -match '^qemu-system-.*\.exe$' } |
        Select-Object ProcessId, Name, ExecutablePath, CommandLine)

    $sourceFiles = @(
        @{ Role = "bootloader"; Path = (Join-Path $ServerEsp "EFI\BOOT\BOOTX64.EFI") },
        @{ Role = "kernel"; Path = (Join-Path $ServerEsp "kernel.elf") },
        @{ Role = "ramdisk"; Path = (Join-Path $ServerEsp "ramdisk.img") },
        @{ Role = "firmware_code"; Path = $firmwareCopy }
    )
    $inputs = [ordered]@{
        ownership_id = $OwnershipId
        run_id = $RunId
        runtime_directory = $RunDir
        pacman_repository = $PacmanRoot
        pacman_branch = (& git -C $PacmanRoot branch --show-current).Trim()
        pacman_head = (& git -C $PacmanRoot rev-parse HEAD).Trim()
        pacman_worktree_status = (& git -C $PacmanRoot status --short --branch) -join [Environment]::NewLine
        server_workspace_read_only = $ServerRoot
        server_esp_source = $ServerEsp
        qemu = $QemuPath
        qemu_version = (& $QemuPath --version | Select-Object -First 1)
        acceleration = $Acceleration
        memory_mb = $MemoryMB
        vcpus = $Vcpus
        server_artifacts = @($sourceFiles | ForEach-Object {
            [pscustomobject]@{
                role = $_.Role
                path = $_.Path
                length = (Get-Item -LiteralPath $_.Path).Length
                sha256 = Get-Sha256 $_.Path
            }
        })
        external_qemu_processes_at_start = $script:ExternalQemuSnapshot
        boot_image = "Copied Server ESP directory exposed as a run-private QEMU FAT disk."
        firmware_variables = "Not required: UEFI removable-media fallback loads EFI/BOOT/BOOTX64.EFI; no writable variable store is attached."
    }
    Write-Json $InputsPath $inputs
    Write-Validation "isolation.esp_source=$ServerEsp copied_to=$EspDir"
    Write-Validation "isolation.disk=run-owned writable FAT backing; source ESP remains unchanged"
    Write-Validation "isolation.firmware=run-owned read-only copy; no writable variable store required"
    Write-Validation "isolation.network=none qmp=loopback-only dynamic-port display=none monitor=none"
    Write-Validation "resource.capacity free_gb=$([Math]::Round($freeMemoryBytes / 1GB, 2)) guest_mb=$MemoryMB host_logical_cpu=$logicalProcessors qemu_vcpus=$Vcpus"
    Write-Validation "qemu.external_instances_at_start=$($script:ExternalQemuSnapshot.Count) left_untouched=1"

    $script:QmpPort = Get-FreeTcpPort
    $vmName = "pacman-soak-$OwnershipId"
    $diskSpec = "file=fat:rw:{0},format=raw,if=ide,index=0,media=disk" -f $EspDir
    $arguments = @(
        "-machine", ("pc,accel={0}" -f $Acceleration),
        "-name", $vmName,
        "-smp", [string]$Vcpus,
        "-m", ("{0}M" -f $MemoryMB),
        "-mem-prealloc",
        "-vga", "std",
        "-display", "none",
        "-monitor", "none",
        "-drive", ("if=pflash,format=raw,readonly=on,unit=0,file={0}" -f $firmwareCopy),
        "-drive", $diskSpec,
        "-serial", ("file:{0}" -f $SerialPath),
        "-qmp", ("tcp:127.0.0.1:{0},server=on,wait=off" -f $script:QmpPort),
        "-d", "guest_errors,cpu_reset",
        "-D", $QemuDebugPath,
        "-no-reboot",
        "-no-shutdown",
        "-rtc", "base=utc,clock=host",
        "-net", "none"
    )
    $argumentString = (@($arguments | ForEach-Object { Quote-WindowsArgument $_ }) -join " ")
    $commandLine = ('"{0}" {1}' -f $QemuPath, $argumentString)
    Set-Content -LiteralPath (Join-Path $LogDir "qemu-command-line.txt") -Value $commandLine
    Write-Json $ProcessPath ([ordered]@{
        ownership_id = $OwnershipId
        process_role = "owned Pac-Man hosted soak QEMU"
        executable = $QemuPath
        arguments = $arguments
        command_line = $commandLine
        qmp_endpoint = "127.0.0.1:$script:QmpPort"
        acceleration = $Acceleration
        serial_log = $SerialPath
        debug_log = $QemuDebugPath
        runtime_esp = $EspDir
        runtime_firmware = $firmwareCopy
        started_utc = [DateTime]::UtcNow.ToString("o")
        pid = $null
    })
    Write-Validation "qemu.start name=$vmName qmp=127.0.0.1:$script:QmpPort ram_mb=$MemoryMB vcpus=$Vcpus acceleration=$Acceleration"
    $script:QemuProcess = Start-Process -FilePath $QemuPath -ArgumentList $argumentString -WorkingDirectory (Split-Path -Parent $QemuPath) -RedirectStandardOutput $QemuStdoutPath -RedirectStandardError $QemuStderrPath -PassThru
    $script:ProcessStartUtc = [DateTime]::UtcNow
    try { $script:QemuProcess.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch { }
    $processRecord = Get-Content -LiteralPath $ProcessPath -Raw | ConvertFrom-Json
    $processRecord.pid = $script:QemuProcess.Id
    $processRecord.started_utc = $script:ProcessStartUtc.ToString("o")
    Write-Json $ProcessPath $processRecord
    Write-Validation "qemu.pid=$($script:QemuProcess.Id) executable=$QemuPath ownership=$OwnershipId"

    $qmpReady = $false
    $qmpLastError = "no QMP connection attempt completed"
    $qmpDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Min(120, $StartupTimeoutSeconds))
    while ([DateTime]::UtcNow -lt $qmpDeadline -and !$qmpReady) {
        try {
            [void](Send-Qmp "info status")
            $qmpReady = $true
        } catch {
            $qmpLastError = $_.Exception.Message
            if ($script:QemuProcess.HasExited) { throw "QEMU exited during startup: $qmpLastError" }
            Start-Sleep -Milliseconds 250
        }
    }
    if (!$qmpReady) { throw "QMP did not become available on its unique endpoint. Last error: $qmpLastError" }
    $serialOpenDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while (!(Test-Path -LiteralPath $SerialPath) -and [DateTime]::UtcNow -lt $serialOpenDeadline) {
        Start-Sleep -Milliseconds 100
    }
    Assert-File $SerialPath "QEMU serial output"
    $script:SerialReader = [System.IO.File]::Open($SerialPath, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)

    if (!(Wait-ForSerial "[KERNEL] Entering main loop" $StartupTimeoutSeconds)) {
        throw "The copied guideXOS guest did not reach its interactive main loop."
    }
    Write-Validation "boot.PASS UEFI removable-media boot reached guideXOS kernel main loop"
    [void](Capture-Screenshot "desktop-after-boot")
    Send-GuestKey "f12" 180
    if (!(Wait-ForSerial "app=Nexgen PacMan launch begin" 30)) {
        throw "The guest F12 App Model shortcut did not enter the Nexgen PacMan launch path."
    }
    if (!(Wait-ForSerial "PacMan initial frame render complete" 45)) {
        throw "Pac-Man Native ELF did not render its initial guest frame."
    }
    $requiredEvidence = @(
        "App Model package discovered package=PacMan id=com.guidexos.pacman",
        "executable=/Apps/PacMan/bin/amd64/pacman.elf",
        "load PASS format=ELF64 machine=amd64",
        "PacMan resources loaded",
        "PacMan interactive frame presented"
    )
    foreach ($needle in $requiredEvidence) {
        if ($script:SerialTail.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
            throw "Missing real-guest launch evidence: $needle"
        }
    }
    Write-Validation "guest.launch.PASS shortcut=F12 manifest=discovered elf=loaded resources=loaded frame=presented execution=bare-metal-UEFI"
    [void](Capture-Screenshot "game-start")
    if (!(Wait-ForSerial "PacMan gameplay resumed" 20)) {
        Write-Validation "guest.gameplay_resume=not-observed-within-20s; directional input starts session"
    }
    $script:GameplayStartUtc = [DateTime]::UtcNow
    $script:RunWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $script:RunResult = "running"
    Write-Validation "gameplay.start wall_utc=$($script:GameplayStartUtc.ToString('o')) target_minutes=$DurationMinutes initial_input=directional"
    Add-HostHealthSample 0.0
}

function Run-GameplaySoak {
    $route = @(
        "right", "right", "down", "left", "up", "right", "down", "down",
        "left", "up", "left", "left", "down", "right", "up", "right",
        "down", "left", "up", "right", "left", "down", "right", "up"
    )
    $rapidDirections = @("left", "up", "right", "down")
    $inputGapPatternMs = @(210, 310, 250, 370, 200, 290, 340, 230)
    $screenshotMinutes = @(5, 10, 15, 30)
    $nextScreenshotIndex = 0
    $nextSampleSeconds = 60.0
    $nextInputSeconds = 0.0
    $restartAttemptedForGameOver = 0
    $targetSeconds = $DurationMinutes * 60.0

    while ($script:RunWatch.Elapsed.TotalSeconds -lt $targetSeconds) {
        if ($script:QemuProcess.HasExited) { throw "QEMU exited during active gameplay." }
        Get-SerialText
        Save-FaultCaptures
        $elapsed = $script:RunWatch.Elapsed.TotalSeconds

        while ($nextScreenshotIndex -lt $screenshotMinutes.Count -and
               $elapsed -ge ($screenshotMinutes[$nextScreenshotIndex] * 60.0) -and
               $screenshotMinutes[$nextScreenshotIndex] -le $DurationMinutes) {
            $minute = $screenshotMinutes[$nextScreenshotIndex]
            [void](Capture-Screenshot ("gameplay-{0:D2}min" -f $minute))
            ++$nextScreenshotIndex
        }
        if ($elapsed -ge $nextSampleSeconds) {
            Add-HostHealthSample $elapsed
            $nextSampleSeconds += 60.0
        }

        if ($script:GameOverCount -gt $restartAttemptedForGameOver) {
            $beforeRestart = $script:SessionRestartCount
            $restartPresses = 0
            while ($script:SessionRestartCount -le $beforeRestart -and $restartPresses -lt 2) {
                ++$restartPresses
                Write-Validation "gameplay.game_over_restart game_over=$script:GameOverCount press=$restartPresses key=space"
                Send-GuestKey "spc" 180
                $restartDeadline = [DateTime]::UtcNow.AddSeconds(8)
                while ($script:SessionRestartCount -le $beforeRestart -and [DateTime]::UtcNow -lt $restartDeadline) {
                    Get-SerialText
                    Start-Sleep -Milliseconds 100
                }
            }
            if ($script:SessionRestartCount -le $beforeRestart) {
                throw "Pac-Man reached Game Over but the normal Space restart was not observed after $restartPresses bounded attempts."
            }
            $restartAttemptedForGameOver = $script:GameOverCount
            Write-Validation "gameplay.game_over_restart.PASS session_restarts=$script:SessionRestartCount"
            $nextInputSeconds = $script:RunWatch.Elapsed.TotalSeconds + 0.1
            continue
        }

        $elapsed = $script:RunWatch.Elapsed.TotalSeconds
        if ($elapsed -ge $nextInputSeconds) {
            $routeSlot = $script:InputAttemptCount % $route.Count
            $inRapidBurst = ($script:InputAttemptCount % 24) -ge 20
            if ($inRapidBurst) {
                $burstSlot = ($script:InputAttemptCount % 24) - 20
                $direction = $rapidDirections[$burstSlot]
                $holdMs = 130
                $gapMs = 100
            } else {
                $direction = $route[$routeSlot]
                $gapMs = $inputGapPatternMs[$script:InputAttemptCount % $inputGapPatternMs.Count]
                $holdMs = 180 + (($script:InputAttemptCount * 37) % 101)
                if (($script:InputAttemptCount % 96) -eq 64) {
                    $direction = if (($script:InputAttemptCount % 192) -eq 64) { "left" } else { "right" }
                    $holdMs = 1500
                    $gapMs = 180
                    Write-Validation "gameplay.corridor_sweep direction=$direction duration_ms=$holdMs schedule=tunnel-and-long-corridor"
                }
            }
            if (Get-PacmanIsGameOver $script:LatestGameState) {
                $nextInputSeconds = $elapsed + 0.1
            } else {
                Send-ObservedDirection $direction $holdMs
                $nextInputSeconds = $script:RunWatch.Elapsed.TotalSeconds + ($gapMs / 1000.0)
            }
        } else {
            Start-Sleep -Milliseconds 100
        }
    }

    $elapsed = $script:RunWatch.Elapsed.TotalSeconds
    Add-HostHealthSample $elapsed $true
    [void](Capture-Screenshot ("gameplay-{0}min-final" -f $DurationMinutes))
    $script:RunWatch.Stop()
    $script:RunResult = "complete"
    Write-Validation "gameplay.complete elapsed_seconds=$([Math]::Round($elapsed, 3)) duration_target_minutes=$DurationMinutes"
}

function Write-RunSummary {
    $endUtc = [DateTime]::UtcNow
    $elapsedSeconds = if ($script:RunWatch) { [Math]::Round($script:RunWatch.Elapsed.TotalSeconds, 3) } else { 0 }
    $metrics = if (Test-Path -LiteralPath $MetricsCsvPath) { @(Import-Csv -LiteralPath $MetricsCsvPath) } else { @() }
    $firstFive = @($metrics | Where-Object { [double]$_.elapsed_seconds -le 300.0 })
    $lastFive = @($metrics | Where-Object { [double]$_.elapsed_seconds -ge [Math]::Max(0.0, $elapsedSeconds - 300.0) })
    $summary = [ordered]@{
        outcome = if ($script:RunResult -eq "complete" -and $elapsedSeconds -ge 900.0 -and
            $script:ProductionBuildVerified -and $script:SoakBuildVerified -and
            $script:RegressionResults.Count -eq 3 -and $script:KeyboardEvidence -and
            $script:LegalTurnEvidence -and $script:PacmanMovementEvidence -and
            (@($script:GhostMovementEvidence.Values | Where-Object { $_ }).Count -eq 4) -and
            $script:FaultCount -eq 0 -and $script:InputDeliveryFailures -eq 0) { "A" } else { "B" }
        result = $script:RunResult
        error = $script:RunError
        run_id = $RunId
        ownership_id = $OwnershipId
        runtime_directory = $RunDir
        qemu_pid = if ($script:QemuProcess) { $script:QemuProcess.Id } else { $null }
        started_utc = if ($script:ProcessStartUtc) { $script:ProcessStartUtc.ToString("o") } else { $null }
        gameplay_started_utc = if ($script:GameplayStartUtc) { $script:GameplayStartUtc.ToString("o") } else { $null }
        finished_utc = $endUtc.ToString("o")
        wallclock_gameplay_seconds = $elapsedSeconds
        minimum_acceptance_seconds = 900
        requested_duration_minutes = $DurationMinutes
        guest_model = "guideXOS UEFI bare-metal guest in private QEMU"
        pacman_package = "Normal Native ELF gameplay, diagnostics ON, every gameplay test hook OFF."
        input_schedule = "24-step deterministic route; taps 180-280 ms with 200-370 ms gaps; four 130 ms rapid turns every 24 inputs; 1.5 s corridor sweeps at route slots 65 and 161; automatic Space restart on Game Over."
        input_event_count = $script:InputActionCount
        input_attempt_count = $script:InputAttemptCount
        input_delivery_failures = $script:InputDeliveryFailures
        input_down_delivery_ms = [ordered]@{
            average = if ($script:InputLatencies.Count) { [Math]::Round(($script:InputLatencies | Measure-Object -Average).Average, 3) } else { 0 }
            p95 = Get-LatencyPercentile 95
            p99 = Get-LatencyPercentile 99
        }
        turns = [ordered]@{
            accepted = $script:LatestTiming["turn_accepts"]
            average_guest_request_to_legal_acceptance_ms = $script:LatestTiming["turn_latency_avg_ms"]
            max_guest_request_to_legal_acceptance_ms = $script:LatestTiming["turn_latency_max_ms"]
            blocked_by_wall = $script:LatestTiming["blocked_turns"]
            held_direction_mask = $script:LatestTiming["held_mask"]
        }
        gameplay_events = [ordered]@{
            deaths = $script:DeathCount
            game_over_transitions = $script:GameOverCount
            session_restarts = $script:SessionRestartCount
            power_pills = $script:PowerPillCount
            frightened_transitions = $script:FrightenedCount
            ghosts_eaten = $script:GhostEatenCount
            tunnel_wraps = $script:TunnelWrapCount
            possible_stalls = $script:FaultCount
        }
        final_game_state = $script:LatestTiming
        final_ghost_states = $script:LatestGhosts
        qualification_evidence = [ordered]@{
            production_build_and_diagnostics_off = $script:ProductionBuildVerified
            soak_package_diagnostics_on_test_hooks_off = $script:SoakBuildVerified
            regression_suites = $script:RegressionResults
            directional_key_delivery_observed = $script:KeyboardEvidence
            legal_turn_acceptance_observed = $script:LegalTurnEvidence
            pacman_movement_between_health_samples_observed = $script:PacmanMovementEvidence
            ghosts_moving_between_health_samples = $script:GhostMovementEvidence
            possible_stall_fault_count = $script:FaultCount
        }
        first_five_minute_averages = [ordered]@{
            qemu_cpu_pct_of_vcpus = Get-WindowAverage $firstFive "qemu_cpu_pct_of_vcpus"
            qemu_working_set_mb = Get-WindowAverage $firstFive "qemu_working_set_mb"
            qemu_private_mb = Get-WindowAverage $firstFive "qemu_private_mb"
            guest_fps = Get-WindowAverage $firstFive "guest_fps"
            frame_avg_us = Get-WindowAverage $firstFive "frame_avg_us"
            frame_p95_ms = Get-WindowAverage $firstFive "frame_p95_ms"
            frame_p99_ms = Get-WindowAverage $firstFive "frame_p99_ms"
            sim_dropped_ms = Get-WindowAverage $firstFive "sim_dropped_ms"
        }
        final_five_minute_averages = [ordered]@{
            qemu_cpu_pct_of_vcpus = Get-WindowAverage $lastFive "qemu_cpu_pct_of_vcpus"
            qemu_working_set_mb = Get-WindowAverage $lastFive "qemu_working_set_mb"
            qemu_private_mb = Get-WindowAverage $lastFive "qemu_private_mb"
            guest_fps = Get-WindowAverage $lastFive "guest_fps"
            frame_avg_us = Get-WindowAverage $lastFive "frame_avg_us"
            frame_p95_ms = Get-WindowAverage $lastFive "frame_p95_ms"
            frame_p99_ms = Get-WindowAverage $lastFive "frame_p99_ms"
            sim_dropped_ms = Get-WindowAverage $lastFive "sim_dropped_ms"
        }
        artifacts = [ordered]@{
            full_serial_log = $SerialPath
            qemu_debug_log = $QemuDebugPath
            validation_log = $ValidationPath
            host_metrics = $MetricsCsvPath
            input_events = $InputCsvPath
            ghost_observations = $GhostCsvPath
            screenshots = $ScreenshotDir
            regressions = $TestDir
            package_inputs = $InputsPath
            qemu_process_record = $ProcessPath
        }
    }
    Write-Json $SummaryPath $summary
    Write-Validation "summary.outcome=$($summary.outcome) result=$script:RunResult elapsed_s=$elapsedSeconds input_events=$script:InputActionCount input_attempts=$script:InputAttemptCount input_failures=$script:InputDeliveryFailures deaths=$script:DeathCount restarts=$script:SessionRestartCount stalls=$script:FaultCount"
}

try {
    New-Item -ItemType Directory -Path $RunDir, $LogDir, $ScreenshotDir, $TestDir -Force | Out-Null
    Write-Validation "run.begin id=$RunId ownership=$OwnershipId pacman=$PacmanRoot"
    $runnerPath = $MyInvocation.MyCommand.Path
    Write-Validation ('runner.command powershell -NoProfile -File "{0}" -DurationMinutes {1}' -f $runnerPath, $DurationMinutes)
    Assert-File (Join-Path $ServerEsp "EFI\BOOT\BOOTX64.EFI") "existing guideXOS Server ESP"
    $observedPackage = Build-PacmanPackages
    Start-IsolatedGuest $observedPackage
    Run-GameplaySoak
} catch {
    $script:RunError = $_.Exception.Message
    if ($script:RunResult -eq "running") {
        $script:RunResult = "interrupted"
    } elseif ($script:RunResult -eq "not-started") {
        $script:RunResult = "setup-failed"
    }
    Write-Validation "run.error=$script:RunError"
    throw
} finally {
    Stop-OwnedQemu
    if ($script:SerialReader) {
        $script:SerialReader.Dispose()
        $script:SerialReader = $null
    }
    if (Test-Path -LiteralPath $RunDir) {
        try { Write-RunSummary } catch { Write-Validation "summary.error=$($_.Exception.Message)" }
    }
    if ($script:RunResult -ne "complete") {
        Write-Host "Partial run artifacts: $RunDir"
    } else {
        Write-Host "Completed run artifacts: $RunDir"
    }
}
