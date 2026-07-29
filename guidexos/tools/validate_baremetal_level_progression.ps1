<#
.SYNOPSIS
    Proves the Pac-Man level-completion path on the real guideXOS Server QEMU
    bare-metal path through Level 4.

.DESCRIPTION
    This harness builds a distinct validation ELF with
    PACMAN_BAREMETAL_LEVEL_VALIDATION=ON, stages it under a separate package
    identity, and launches it through the normal bare-metal App Model shell
    command.  The validation hook only prepares one normal pill in Pac-Man's
    next legal look-ahead cell.  The ordinary game update then consumes that
    pill and performs the real LevelComplete, timer, reset, Ready, and Playing
    transitions.  Production D:\Apps\PacMan is never modified.
#>

[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipKernelBuild,
    [switch]$FruitValidation,
    [switch]$ProductionValidation,
    [int]$TimeoutSeconds = 120,
    [string]$KernelElfPath = "",
    [string]$ValidationPackagePath = ""
)

$ErrorActionPreference = "Stop"
if ($FruitValidation -and $ProductionValidation) {
    throw "FruitValidation and ProductionValidation are mutually exclusive."
}
$PacmanRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ServerRoot = "D:\dev\guideXOSServer"
$KernelElf = if ([string]::IsNullOrWhiteSpace($KernelElfPath)) {
    Join-Path $ServerRoot "kernel\build\amd64\bin\kernel.elf"
} else { $KernelElfPath }
$BuildDir = if ($ProductionValidation) { Join-Path $PSScriptRoot "..\build" } elseif ($FruitValidation) { Join-Path $PSScriptRoot "..\build-baremetal-fruit-validation" } else { Join-Path $PSScriptRoot "..\build-baremetal-level-validation" }
$Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$Ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$EspSource = Join-Path $ServerRoot "ESP"
$RunId = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss-fff"), ([guid]::NewGuid().ToString("N").Substring(0, 8))
$RunName = if ($ProductionValidation) { "production" } elseif ($FruitValidation) { "fruit" } else { "level-progression" }
$RunDir = Join-Path $ServerRoot "logs\baremetal-pacman-$RunName\$RunId"
$QemuEsp = Join-Path $RunDir "esp"
$StageDir = Join-Path $QemuEsp "Apps\PacMan"
$SerialLog = Join-Path $RunDir "qemu-serial.log"
$SerialTailLog = Join-Path $RunDir "qemu-serial-tail-200.log"
$QemuErrLog = Join-Path $RunDir "qemu-stderr.log"
$QemuStdoutLog = Join-Path $RunDir "qemu-stdout.log"
$QemuDebugLog = Join-Path $RunDir "qemu-debug.log"
$QemuDebugTailLog = Join-Path $RunDir "qemu-debug-tail-200.log"
$QemuCommandLog = Join-Path $RunDir "qemu-command-line.txt"
$QemuPidLog = Join-Path $RunDir "qemu-pid.txt"
$QemuExitLog = Join-Path $RunDir "qemu-exit-code.txt"
$ValidationLog = Join-Path $RunDir "validation.log"
$SummaryLog = Join-Path $RunDir "validation-summary.txt"
$OutcomeLog = Join-Path $RunDir "outcome.txt"
$QemuProcess = $null
$QmpPort = 0
$HarnessRequestedQemuStop = $false
$ValidationElfName = if ($ProductionValidation) { "pacman.elf" } elseif ($FruitValidation) { "pfruit.elf" } else { "pacval.elf" }
$ValidationTarget = if ($ProductionValidation) { "pacman-native" } elseif ($FruitValidation) { "pacman-baremetal-fruit-validation" } else { "pacman-baremetal-level-validation" }
$ValidationPackageId = if ($ProductionValidation) { "com.guidexos.pacman" } elseif ($FruitValidation) { "com.guidexos.pacman.baremetal-fruit-validation" } else { "com.guidexos.pacman.baremetal-level-validation" }
if ([string]::IsNullOrWhiteSpace($ValidationPackagePath)) {
    $ValidationPackagePath = if ($ProductionValidation) { "D:\Apps\PacMan" } elseif ($FruitValidation) { "D:\Apps\PacManBareMetalFruitValidation" } else { "D:\Apps\PacManBareMetalLevelValidation" }
}

New-Item -ItemType Directory -Path $RunDir -Force | Out-Null

function Write-Validation([string]$Message) {
    $line = "[$(Get-Date -Format o)] $Message"
    Write-Host $line
    Add-Content -LiteralPath $ValidationLog -Value $line
}

function Assert-Path([string]$Path, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path)) { throw "Missing $Description`: $Path" }
}

function Read-Serial {
    if (!(Test-Path -LiteralPath $SerialLog)) { return "" }
    for ($attempt = 0; $attempt -lt 8; ++$attempt) {
        try { return Get-Content -LiteralPath $SerialLog -Raw -ErrorAction Stop } catch {
            Start-Sleep -Milliseconds 75
        }
    }
    return ""
}

function Text-Count([string]$Text, [string]$Needle) {
    if (!$Text -or !$Needle) { return 0 }
    $count = 0
    $offset = 0
    while (($index = $Text.IndexOf($Needle, $offset, [StringComparison]::Ordinal)) -ge 0) {
        ++$count
        $offset = $index + $Needle.Length
    }
    return $count
}

function Wait-Serial([string]$Needle, [int]$Minimum = 1, [int]$Seconds = $TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if ((Text-Count (Read-Serial) $Needle) -ge $Minimum) { return $true }
        if ($QemuProcess -and $QemuProcess.HasExited) {
            Write-Validation "qemu.exited.waiting needle=$Needle exitCode=$($QemuProcess.ExitCode)"
            return $false
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Wait-SerialRegex([string]$Pattern, [int]$Seconds = $TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if ((Read-Serial) -match $Pattern) { return $true }
        if ($QemuProcess -and $QemuProcess.HasExited) {
            Write-Validation "qemu.exited.waiting regex=$Pattern exitCode=$($QemuProcess.ExitCode)"
            return $false
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    return $false
}

function Send-Qmp([string]$CommandLine) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect("127.0.0.1", $QmpPort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 1500
        Start-Sleep -Milliseconds 100
        $buffer = New-Object byte[] 8192
        while ($stream.DataAvailable) { [void]$stream.Read($buffer, 0, $buffer.Length) }
        $capabilities = '{"execute":"qmp_capabilities"}' + "`r`n"
        $bytes = [Text.Encoding]::UTF8.GetBytes($capabilities)
        $stream.Write($bytes, 0, $bytes.Length)
        Start-Sleep -Milliseconds 60
        while ($stream.DataAvailable) { [void]$stream.Read($buffer, 0, $buffer.Length) }
        $request = @{ execute = "human-monitor-command"; arguments = @{ "command-line" = $CommandLine } } |
            ConvertTo-Json -Compress
        $request += "`r`n"
        $bytes = [Text.Encoding]::UTF8.GetBytes($request)
        $stream.Write($bytes, 0, $bytes.Length)
        Start-Sleep -Milliseconds 80
        $response = New-Object Text.StringBuilder
        while ($stream.DataAvailable) {
            $read = $stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            [void]$response.Append([Text.Encoding]::UTF8.GetString($buffer, 0, $read))
        }
        return $response.ToString()
    } finally { if ($client) { $client.Close() } }
}

function Send-Key([string]$Key) {
    $wireKey = if ($Key -eq "grave") { "0x29" } else { $Key }
    $response = Send-Qmp "sendkey $wireKey"
    if ($response -match "invalid parameter|unknown command|error") { throw "QMP sendkey failed for $Key`: $response" }
    Start-Sleep -Milliseconds 130
}

function Send-ShellText([string]$Text) {
    foreach ($character in $Text.ToLowerInvariant().ToCharArray()) {
        $key = switch ($character) {
            ' ' { 'spc'; break }
            '.' { 'dot'; break }
            '-' { 'minus'; break }
            '/' { 'slash'; break }
            '_' { 'shift-minus'; break }
            default { [string]$character }
        }
        Send-Key $key
    }
}

function Capture-Screenshot([string]$Name) {
    $path = Join-Path $RunDir $Name
    [void](Send-Qmp ("screendump " + ($path -replace '\\', '/')))
    Start-Sleep -Milliseconds 150
    Write-Validation "screenshot path=$path"
    return $path
}

function Stage-ValidationPackage {
    $elf = Join-Path $ValidationPackagePath "bin\amd64\$ValidationElfName"
    Assert-Path (Join-Path $ValidationPackagePath "app.json") "validation manifest"
    Assert-Path $elf "validation Native ELF"
    Assert-Path (Join-Path $ValidationPackagePath "resources\level1.gximg") "validation level resource"
    Assert-Path (Join-Path $ValidationPackagePath "resources\pacpics.gximg") "validation sprite resource"
    $bytes = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($elf))
    if ($ProductionValidation) {
        foreach ($marker in @("PACMAN_HOSTED_FRUIT_TEST", "PACMAN_BAREMETAL_FRUIT_VALIDATION", "PACMAN_BAREMETAL_LEVEL_VALIDATION", "FRUITVAL", "BMLVL")) {
            if ($bytes.Contains($marker)) { throw "Production ELF contains validation marker $marker`: $elf" }
        }
        Write-Validation "package.validationMarker=absent production=$elf"
    } else {
        $marker = if ($FruitValidation) { "PACMAN_BAREMETAL_FRUIT_VALIDATION=ON" } else { "PACMAN_BAREMETAL_LEVEL_VALIDATION=ON" }
        if (!$bytes.Contains($marker) -or (!$FruitValidation -and !$bytes.Contains("BMLVL")) -or ($FruitValidation -and !$bytes.Contains("FRUITVAL"))) {
            throw "Validation ELF marker scan failed: $elf"
        }
    }
    New-Item -ItemType Directory -Path (Join-Path $QemuEsp "Apps") -Force | Out-Null
    if (Test-Path -LiteralPath $StageDir) {
        $appsRoot = (Resolve-Path (Join-Path $QemuEsp "Apps")).Path.TrimEnd('\')
        $resolvedStage = (Resolve-Path $StageDir).Path
        if (!$resolvedStage.StartsWith($appsRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove validation staging path outside the isolated QEMU Apps root: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
    New-Item -ItemType Directory -Path (Join-Path $StageDir "bin\amd64") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $StageDir "resources") -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $ValidationPackagePath "app.json") -Destination (Join-Path $StageDir "app.json") -Force
    Copy-Item -LiteralPath $elf -Destination (Join-Path $StageDir "bin\amd64\$ValidationElfName") -Force
    Copy-Item -LiteralPath (Join-Path $ValidationPackagePath "resources\level1.gximg") -Destination (Join-Path $StageDir "resources\level1.gximg") -Force
    Copy-Item -LiteralPath (Join-Path $ValidationPackagePath "resources\pacpics.gximg") -Destination (Join-Path $StageDir "resources\pacpics.gximg") -Force
    $files = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File | ForEach-Object {
        $_.FullName.Substring($StageDir.Length + 1).Replace('\', '/')
    })
    $expected = @("app.json", "bin/amd64/$ValidationElfName", "resources/level1.gximg", "resources/pacpics.gximg")
    if ((Compare-Object $expected $files).Count -ne 0) { throw "Validation package tree is not exact: $($files -join ',')" }
    foreach ($file in $files) {
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $StageDir $file.Replace('/', '\'))
        Write-Validation "package.hash path=/Apps/PacMan/$file sha256=$($hash.Hash)"
    }
    if (-not $ProductionValidation) {
        Write-Validation "package.validationMarker=present source=$ValidationPackagePath qemuMount=/Apps/PacMan reason=bare-metal-discovery-single-package-boundary"
    }
}

function Save-Tails {
    if (Test-Path -LiteralPath $SerialLog) { Get-Content $SerialLog -Tail 200 | Set-Content $SerialTailLog }
    if (Test-Path -LiteralPath $QemuDebugLog) { Get-Content $QemuDebugLog -Tail 200 | Set-Content $QemuDebugTailLog }
}

function Write-FrameEvidence([string]$Phase) {
    $lines = @(Get-Content -LiteralPath $SerialLog -ErrorAction SilentlyContinue | Where-Object { $_ -match 'PacMan frame seq=|frame PASS app=' })
    $last = if ($lines.Count -gt 0) { $lines[$lines.Count - 1] } else { "none" }
    Write-Validation "frame.evidence phase=$Phase last=$last"
}

function Classify-QemuOutcome {
    $serial = Read-Serial
    $debug = if (Test-Path $QemuDebugLog) { Get-Content $QemuDebugLog -Raw } else { "" }
    $stderr = if (Test-Path $QemuErrLog) { Get-Content $QemuErrLog -Raw } else { "" }
    $exitCode = if (Test-Path $QemuExitLog) { (Get-Content $QemuExitLog -Raw).Trim() } else { "unknown" }
    $resets = [regex]::Matches($debug, '(?m)^CPU Reset \(CPU \d+\)').Count
    if ($serial -match '(?i)NATIVE-ELF-FAULT|triple fault') { $class = "guest fault" }
    elseif ($stderr -match '(?i)fatal|abort|assertion') { $class = "QEMU host fault" }
    elseif ($resets -gt 2 -and $serial -notmatch 'lifecycle PASS') { $class = "guest reset" }
    elseif ($HarnessRequestedQemuStop -and $serial -match 'VALIDATION_RESULT=FAIL') { $class = "harness stopped after validation failure" }
    elseif ($HarnessRequestedQemuStop) { $class = "harness stopped healthy guest" }
    else { $class = "unmarked QEMU exit" }
    Set-Content -LiteralPath $OutcomeLog -Value "classification=$class`nqemuExitCode=$exitCode`nqemuCpuResetRecords=$resets"
    Write-Validation "qemu.outcome classification=$class exitCode=$exitCode cpuResetRecords=$resets"
}

try {
    Write-Validation "classification.target=bare-metal/UEFI QEMU (not physical hardware)"
    Write-Validation "execution.path=UEFI bootloader -> guideXOS kernel -> FAT VFS -> normal App Model -> Native ELF"
    Assert-Path $EspSource "guideXOS ESP"
    Assert-Path $Qemu "QEMU"
    Assert-Path $Ovmf "OVMF firmware"
    if (!$SkipBuild) {
        if ($ProductionValidation) {
            Write-Validation "build.pacman target=$ValidationTarget production=ON"
            & "C:\mingw64\bin\cmake.exe" --build $BuildDir --target $ValidationTarget -j2
            if ($LASTEXITCODE -ne 0) { throw "PacMan production build failed" }
        } else {
            $serverRootCmake = $ServerRoot.Replace('\', '/')
            $levelValidationFlag = if ($FruitValidation) { 'OFF' } else { 'ON' }
            $fruitValidationFlag = if ($FruitValidation) { 'ON' } else { 'OFF' }
            $diagnosticsFlag = if ($FruitValidation) { 'OFF' } else { 'ON' }
            Write-Validation "build.pacman target=$ValidationTarget diagnostics=$diagnosticsFlag"
            & "C:\mingw64\bin\cmake.exe" -S (Join-Path $PacmanRoot "guidexos") -B $BuildDir -G Ninja `
                "-DGUIDEXOS_SERVER_ROOT=$serverRootCmake" "-DGUIDEXOS_PACKAGE_ROOT=D:/Apps" `
                "-DPACMAN_ENABLE_DIAGNOSTICS=$diagnosticsFlag" "-DPACMAN_BAREMETAL_LEVEL_VALIDATION=$levelValidationFlag" `
                "-DPACMAN_BAREMETAL_FRUIT_VALIDATION=$fruitValidationFlag" `
                -DPACMAN_HOSTED_DANGER_TEST=OFF -DPACMAN_HOSTED_LEVEL_TEST=OFF -DPACMAN_HOSTED_FRUIT_TEST=OFF
            if ($LASTEXITCODE -ne 0) { throw "PacMan validation configure failed" }
            & "C:\mingw64\bin\cmake.exe" --build $BuildDir --target $ValidationTarget -j2
            if ($LASTEXITCODE -ne 0) { throw "PacMan validation build failed" }
        }
    }
    if (!$SkipKernelBuild) {
        Write-Validation "build.server kernel=amd64"
        & "C:\mingw64\bin\mingw32-make.exe" -C (Join-Path $ServerRoot "kernel") ARCH=amd64 -j2
        if ($LASTEXITCODE -ne 0) { throw "guideXOS kernel build failed" }
    }
    Assert-Path $KernelElf "guideXOS kernel ELF"
    Assert-Path (Join-Path $ServerRoot "ESP\EFI\BOOT\BOOTX64.EFI") "UEFI bootloader"
    New-Item -ItemType Directory -Path $QemuEsp -Force | Out-Null
    Get-ChildItem -LiteralPath $EspSource -Force | Copy-Item -Destination $QemuEsp -Recurse -Force
    Copy-Item -LiteralPath $KernelElf -Destination (Join-Path $QemuEsp "kernel.elf") -Force
    Stage-ValidationPackage
    Write-Validation "boot-artifact kernel=$KernelElf qemuEsp=$QemuEsp"

    $probe = New-Object System.Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
    $probe.Start(); $QmpPort = ([Net.IPEndPoint]$probe.LocalEndpoint).Port; $probe.Stop()
    $disk = "fat:rw:$QemuEsp"
    $arguments = '-machine "pc,accel=tcg" ' +
        '-drive "if=pflash,format=raw,readonly=on,file=' + $Ovmf + '" ' +
        '-drive "file=' + $disk + ',format=raw,if=ide,index=0,media=disk" ' +
        '-m 1024M -vga std -display none ' +
        '-serial "file:' + $SerialLog + '" ' +
        '-qmp "tcp:127.0.0.1:' + $QmpPort + ',server=on,wait=off" ' +
        '-d guest_errors,int,cpu_reset -D "' + $QemuDebugLog + '" ' +
        '-no-reboot -no-shutdown -rtc base=utc,clock=host ' +
        '-netdev user,id=net0 -device e1000,netdev=net0 ' +
        '-device isa-debug-exit,iobase=0x501,iosize=0x04'
    Set-Content -LiteralPath $QemuCommandLog -Value "`"$Qemu`" $arguments"
    Write-Validation "qemu.command=$Qemu $arguments"
    $QemuProcess = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory (Split-Path $Qemu) `
        -RedirectStandardOutput $QemuStdoutLog -RedirectStandardError $QemuErrLog -PassThru
    Set-Content -LiteralPath $QemuPidLog -Value $QemuProcess.Id
    Write-Validation "qemu.pid=$($QemuProcess.Id)"
    if (!(Wait-Serial "[KERNEL] Entering main loop" 1 $TimeoutSeconds)) { throw "Kernel did not reach main loop" }
    Write-Validation "boot.PASS kernel reached main loop"
    Write-Validation "qmp.status=$((Send-Qmp 'info status').Trim())"
    Capture-Screenshot "desktop-after-boot.ppm" | Out-Null

    # The bare-metal shell is the normal App Model launch surface. Open it
    # with the same desktop shortcut key used by the platform and dispatch the
    # distinct validation package by its manifest id.
    Send-Key "grave"
    Send-ShellText "desktop.launch $ValidationPackageId"
    Send-Key "ret"
    if (!(Wait-Serial "launch begin path=package-relative runtime=native-elf" 1 30)) {
        throw "Validation package did not enter normal App Model launch"
    }
    if ($ProductionValidation) {
        if (!(Wait-Serial "PacMan interactive frame presented" 1 30)) { throw "Production Pac-Man did not present an interactive frame" }
        Write-Validation "launch.PASS production App Model target resolved"
        Capture-Screenshot "production-first-frame.ppm" | Out-Null
        Write-FrameEvidence "production-first-frame"
        if (!(Wait-Serial "PacMan fixed-step simulation started" 1 30)) { throw "Production Pac-Man fixed-step loop did not start" }
        # QEMU's monitor key names are firmware/keyboard-layout dependent on
        # this PS/2 path.  Try the named arrows and the guideXOS extended
        # codes, while the app still proves acceptance through its normal
        # buffered-turn log.
        foreach ($inputKey in @("right", "0x103", "0xE04D", "up", "0x100", "0xE075")) {
            try { Send-Key $inputKey } catch { Write-Validation "input.candidate=$inputKey rejected" }
        }
        if (!(Wait-Serial "PacMan requested direction: right" 1 10)) { throw "Production Pac-Man input was not accepted" }
        Capture-Screenshot "production-input-frame.ppm" | Out-Null
        Write-FrameEvidence "production-input"
    } elseif ($FruitValidation) {
        if (!(Wait-Serial "FRUITVAL PREPARE_SPAWN" 1 30)) { throw "Fruit validation preparation not observed" }
        Write-Validation "launch.PASS fruit validation App Model target resolved"
        Capture-Screenshot "fruit-before-trigger.ppm" | Out-Null
        $fruitMarkers = @(
            @{ Needle = "FRUITVAL SPAWN_OBSERVED"; Name = "fruit-visible.ppm"; Phase = "fruit-visible" },
            @{ Needle = "FRUITVAL COLLECTION_OBSERVED"; Name = "fruit-collected.ppm"; Phase = "fruit-collected" },
            # The next-stage preparation is emitted after the Level 4 fruit
            # expiry has been observed and the normal reset is complete.
            @{ Needle = "FRUITVAL PREPARE_LEVEL5_EXPIRATION"; Name = "fruit-level4-expired.ppm"; Phase = "fruit-level4-expired" },
            @{ Needle = "FRUITVAL LEVEL5_EXPIRATION_OBSERVED"; Name = "fruit-level5-expired.ppm"; Phase = "fruit-level5-expired" },
            @{ Needle = "FRUITVAL LEVEL8_EXPIRATION_OBSERVED"; Name = "fruit-level8-expired.ppm"; Phase = "fruit-level8-expired" }
        )
        foreach ($phase in $fruitMarkers) {
            if (!(Wait-Serial $phase.Needle 1 $TimeoutSeconds)) { throw "Missing fruit marker: $($phase.Needle)" }
            Capture-Screenshot $phase.Name | Out-Null
            Write-FrameEvidence $phase.Phase
        }
    } else {
    if (!(Wait-Serial "BMLVL 01 VALIDATION_READY" 1 30)) { throw "BMLVL 01 not observed" }
    Write-Validation "launch.PASS validation App Model target resolved"
    Capture-Screenshot "level1-validation-ready.ppm" | Out-Null
    Write-FrameEvidence "validation-ready"

    $phaseMarkers = @(
        @{ Needle = "BMLVL 04 FINAL_CONSUMABLE_LOOKAHEAD old=1 new=1"; Name = "level1-final-active.ppm"; Phase = "level1-final-active" },
        @{ Needle = "BMLVL 07 LEVEL_COMPLETE_ENTER old=1 new=1"; Name = "level1-level-complete.ppm"; Phase = "level1-level-complete" },
        @{ Needle = "BMLVL 14 READY_ENTER old=1 new=2"; Name = "level2-ready.ppm"; Phase = "level2-ready" },
        @{ Regex = "PacMan frame seq=.*Playing.*level=2"; Name = "level2-playing.ppm"; Phase = "level2-playing" },
        @{ Needle = "BMLVL 06 REMAINING_ZERO old=2 new=2"; Name = "level2-final-eaten.ppm"; Phase = "level2-final-eaten" },
        @{ Needle = "BMLVL 14 READY_ENTER old=2 new=3"; Name = "level3-ready.ppm"; Phase = "level3-ready" },
        @{ Regex = "PacMan frame seq=.*Playing.*level=3"; Name = "level3-playing.ppm"; Phase = "level3-playing" },
        @{ Needle = "BMLVL 06 REMAINING_ZERO old=3 new=3"; Name = "level3-final-eaten.ppm"; Phase = "level3-final-eaten" },
        @{ Needle = "BMLVL 14 READY_ENTER old=3 new=4"; Name = "level4-ready.ppm"; Phase = "level4-ready" },
        @{ Regex = "PacMan frame seq=.*Playing.*level=4"; Name = "level4-playing.ppm"; Phase = "level4-playing" }
    )
    foreach ($phase in $phaseMarkers) {
        if ($phase.Regex) {
            if (!(Wait-SerialRegex $phase.Regex $TimeoutSeconds)) { throw "Missing progression regex: $($phase.Regex)" }
        } elseif (!(Wait-Serial $phase.Needle 1 $TimeoutSeconds)) {
            throw "Missing progression marker: $($phase.Needle)"
        }
        Capture-Screenshot $phase.Name | Out-Null
        Write-FrameEvidence $phase.Phase
    }
    }

    Send-Key "esc"
    if (!(Wait-Serial "lifecycle PASS window/resource cleanup complete" 1 10)) {
        throw "Validation Pac-Man did not exit cleanly"
    }
    Write-Validation "cleanup.app PASS Escape closed validation Pac-Man"

    if ($ProductionValidation) {
        Send-Key "grave"
        Send-ShellText "desktop.launch $ValidationPackageId"
        Send-Key "ret"
        if (!(Wait-Serial "launch begin path=package-relative runtime=native-elf" 2 30) -or
            !(Wait-Serial "PacMan interactive frame presented" 2 30)) {
            throw "Production Pac-Man did not relaunch through the normal App Model"
        }
        Capture-Screenshot "production-relaunch-frame.ppm" | Out-Null
        Write-FrameEvidence "production-relaunch"
        Send-Key "esc"
        if (!(Wait-Serial "lifecycle PASS window/resource cleanup complete" 2 10)) {
            throw "Production Pac-Man relaunch did not clean up"
        }
        Write-Validation "cleanup.app PASS production relaunch and Escape cleanup"
    }

    $serial = Read-Serial
    $required = if ($ProductionValidation) { @(
        "PacMan interactive frame presented",
        "PacMan fixed-step simulation started",
        "PacMan requested direction: right",
        "frame PASS app=",
        "lifecycle PASS window/resource cleanup complete"
    ) } elseif ($FruitValidation) { @(
        "FRUITVAL SPAWN_OBSERVED",
        "FRUITVAL COLLECTION_OBSERVED",
        "FRUITVAL PREPARE_LEVEL5_EXPIRATION",
        "FRUITVAL LEVEL5_EXPIRATION_OBSERVED",
        "FRUITVAL LEVEL8_EXPIRATION_OBSERVED",
        "FRUITVAL EXTRA_LIFE_OBSERVED",
        "frame PASS app=",
        "PacMan frame seq=",
        "lifecycle PASS window/resource cleanup complete"
    ) } else { @(
        "BMLVL 06 REMAINING_ZERO old=1 new=1",
        "BMLVL 06 REMAINING_ZERO old=2 new=2",
        "BMLVL 06 REMAINING_ZERO old=3 new=3",
        "BMLVL 07 LEVEL_COMPLETE_ENTER old=1 new=1",
        "BMLVL 10 LEVEL_INCREMENT old=1 new=2",
        "BMLVL 10 LEVEL_INCREMENT old=2 new=3",
        "BMLVL 10 LEVEL_INCREMENT old=3 new=4",
        "BMLVL 12 MAZE_RESET old=1 new=2",
        "BMLVL 12 MAZE_RESET old=2 new=3",
        "BMLVL 12 MAZE_RESET old=3 new=4",
        "BMLVL 14 READY_ENTER old=1 new=2",
        "order=input>consumable-lookahead>level-completion>pacman-movement>animation>ghosts>collision>timers>render-dirty",
        "frame PASS app=",
        "PacMan frame seq=",
        "lifecycle PASS window/resource cleanup complete"
    ) }
    foreach ($needle in $required) { if (!$serial.Contains($needle)) { throw "Missing required evidence: $needle" } }
    if ($ProductionValidation) {
        $serial | Select-String -Pattern 'PacMan interactive frame presented|PacMan fixed-step simulation started|PacMan buffered turn accepted|PacMan frame seq=|frame PASS app=' |
            Set-Content -LiteralPath $SummaryLog
        Write-Validation "evidence.PASS production rendering/input/lifecycle through QEMU"
        Write-Validation "VALIDATION_RESULT=PASS"
        exit 0
    }
    if ($FruitValidation) {
        $serial | Select-String -Pattern 'FRUITVAL|PacMan frame seq=|frame PASS app=' | Set-Content -LiteralPath $SummaryLog
        Write-Validation "evidence.PASS fruit spawn/collection/expiration/extra-life through QEMU"
        Write-Validation "VALIDATION_RESULT=PASS"
        exit 0
    }
    if ($serial -notmatch "PacMan frame seq=.*Playing.*level=2") { throw "Missing Level 2 Playing frame evidence" }
    foreach ($level in @(1, 2, 3)) {
        $sameLevel = "old=$level new=$level"
        if ((Text-Count $serial "BMLVL 06 REMAINING_ZERO $sameLevel") -ne 1) { throw "Remaining-zero count mismatch for $sameLevel" }
    }
    foreach ($transition in @("old=1 new=2", "old=2 new=3", "old=3 new=4")) {
        if ((Text-Count $serial "BMLVL 10 LEVEL_INCREMENT $transition") -ne 1) { throw "Level increment count mismatch for $transition" }
    }
    $serial | Select-String -Pattern 'BMLVL (05|06|07|08|09|10|11|12|13|14|15|16|17)|PacMan frame seq=|frame PASS app=' |
        Set-Content -LiteralPath $SummaryLog
    Write-Validation "evidence.PASS final-pill/LevelComplete/timer/reset/Ready/Playing through Level4"
    Write-Validation "VALIDATION_RESULT=PASS"

    exit 0
} catch {
    try { Write-Validation "qmp.registers=$((Send-Qmp 'info registers').Trim())" } catch { Write-Validation "qmp.registers=unavailable" }
    Write-Validation "VALIDATION_RESULT=FAIL reason=$($_.Exception.Message)"
    exit 1
} finally {
    if ($QemuProcess -and $QemuProcess.HasExited) {
        $QemuProcess.Refresh()
        try { $code = [int]$QemuProcess.ExitCode } catch { $code = "unknown" }
        Set-Content -LiteralPath $QemuExitLog -Value $code
        Write-Validation "qemu.exit code=$code"
    }
    if ($QemuProcess -and !$QemuProcess.HasExited) {
        $HarnessRequestedQemuStop = $true
        Write-Validation "cleanup stopping owned qemu pid=$($QemuProcess.Id)"
        Stop-Process -Id $QemuProcess.Id -Force -ErrorAction SilentlyContinue
        $QemuProcess.WaitForExit(3000)
        if ($QemuProcess.HasExited) {
            $QemuProcess.Refresh()
            try { $code = [int]$QemuProcess.ExitCode } catch { $code = "unknown" }
            Set-Content -LiteralPath $QemuExitLog -Value $code
        }
    }
    Save-Tails
    Classify-QemuOutcome
}
