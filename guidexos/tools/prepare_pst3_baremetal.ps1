<#
.SYNOPSIS
    Build a Pac-Man-only x86-64 UEFI test ISO from a production package.

.DESCRIPTION
    Copies the existing guideXOS Server ESP into a new Pac-Man-owned artifact
    directory, replaces only the copied PacMan package, then calls the Server
    release helpers to create and independently verify a FAT32 EFI boot image
    and UEFI El Torito ISO. The source Server repository is read-only.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProductionPackagePath,

    [string]$ServerRoot = "D:\dev\guideXOSServer",

    [string]$ArtifactName = "PST3-GuideXOS-PacMan-UEFI-x64",

    [ValidateRange(96, 512)]
    [int]$EspSizeMB = 128
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    throw "[pst3-baremetal] $Message"
}

function Get-FileRecord([System.IO.FileInfo]$File, [string]$RelativePath) {
    return [ordered]@{
        path = $RelativePath.Replace('\', '/')
        size = [int64]$File.Length
        sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Write-JsonFile([string]$Path, $Value, [int]$Depth = 8) {
    $json = ConvertTo-Json -InputObject $Value -Depth $Depth
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

function Assert-NoReparsePoints([string]$Root) {
    $rootItem = Get-Item -LiteralPath $Root -Force
    if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        Fail "reparse point is not allowed: $Root"
    }
    foreach ($item in Get-ChildItem -LiteralPath $Root -Recurse -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            Fail "reparse point is not allowed: $($item.FullName)"
        }
        if (-not $item.PSIsContainer -and $item.Length -le 0) {
            Fail "empty source file is not allowed: $($item.FullName)"
        }
    }
}

$pacmanRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$artifactParent = Join-Path $pacmanRoot 'guidexos\artifacts\pst3-baremetal'
if ($ArtifactName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$') {
    Fail "invalid artifact name: $ArtifactName"
}
$artifactRoot = [IO.Path]::GetFullPath((Join-Path $artifactParent $ArtifactName))
$allowedPrefix = [IO.Path]::GetFullPath($artifactParent).TrimEnd('\') + '\'
if (-not $artifactRoot.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    Fail "artifact path escapes the Pac-Man artifact directory: $artifactRoot"
}
if (Test-Path -LiteralPath $artifactRoot) {
    Fail "refusing to overwrite an existing artifact directory: $artifactRoot"
}

$serverRoot = (Resolve-Path -LiteralPath $ServerRoot).Path
$sourceEsp = Join-Path $serverRoot 'ESP'
$productionPackage = (Resolve-Path -LiteralPath $ProductionPackagePath).Path
$bootloader = Join-Path $sourceEsp 'EFI\BOOT\BOOTX64.EFI'
$kernel = Join-Path $sourceEsp 'kernel.elf'
$ramdisk = Join-Path $sourceEsp 'ramdisk.img'
foreach ($required in @($bootloader, $kernel, $ramdisk)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        Fail "required UEFI Server input is missing: $required"
    }
}
foreach ($required in @(
    (Join-Path $productionPackage 'app.json'),
    (Join-Path $productionPackage 'bin\amd64\pacman.elf'),
    (Join-Path $productionPackage 'resources\level1.gximg'),
    (Join-Path $productionPackage 'resources\pacpics.gximg')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        Fail "production PacMan package is incomplete: $required"
    }
}
Assert-NoReparsePoints $sourceEsp
Assert-NoReparsePoints $productionPackage

$python = Join-Path $serverRoot 'out\release-tools\venv\Scripts\python.exe'
$espHelper = Join-Path $serverRoot 'tools\release_esp.py'
$isoHelper = Join-Path $serverRoot 'tools\release_iso.py'
$gptHelper = Join-Path $PSScriptRoot 'wrap_pst3_gpt_image.py'
foreach ($required in @($python, $espHelper, $isoHelper, $gptHelper)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        Fail "guideXOS release helper input is missing: $required"
    }
}

$null = New-Item -ItemType Directory -Path $artifactRoot
$staging = Join-Path $artifactRoot 'esp-source'
$null = New-Item -ItemType Directory -Path $staging
foreach ($item in Get-ChildItem -LiteralPath $sourceEsp -Force) {
    Copy-Item -LiteralPath $item.FullName -Destination $staging -Recurse -Force
}

# The Server marker describes its older PacMan binary. Keep it out of the
# staged image and retain the original component hashes in this artifact's
# provenance file instead.
$oldIdentity = Join-Path $staging 'build-identity.txt'
if (Test-Path -LiteralPath $oldIdentity -PathType Leaf) {
    Remove-Item -LiteralPath $oldIdentity
}
$stagedPackage = Join-Path $staging 'Apps\PacMan'
if (Test-Path -LiteralPath $stagedPackage) {
    $resolvedPackage = [IO.Path]::GetFullPath($stagedPackage)
    $stagingPrefix = [IO.Path]::GetFullPath($staging).TrimEnd('\') + '\'
    if (-not $resolvedPackage.StartsWith($stagingPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Fail "staged package path escapes the copied ESP: $resolvedPackage"
    }
    Remove-Item -LiteralPath $resolvedPackage -Recurse -Force
}
$null = New-Item -ItemType Directory -Path $stagedPackage
Copy-Item -Path (Join-Path $productionPackage '*') -Destination $stagedPackage -Recurse -Force

$espFiles = @()
foreach ($file in Get-ChildItem -LiteralPath $staging -File -Recurse -Force | Sort-Object FullName) {
    $relative = $file.FullName.Substring($staging.Length).TrimStart('\')
    $espFiles += (Get-FileRecord $file $relative)
}
$espExpected = Join-Path $artifactRoot 'esp-expected.json'
Write-JsonFile $espExpected ([ordered]@{ files = $espFiles }) 6

$readme = Join-Path $artifactRoot 'README.TXT'
@(
    'guideXOS PST3 Pac-Man live gameplay test',
    'Target: x86-64 UEFI computer; this is a live boot test image.',
    'Boot using UEFI optical media. No operating-system installation is included.',
    'Select the firmware entry for the optical drive and launch Pac-Man from the desktop.',
    'If firmware Secure Boot rejects the unsigned bootloader, temporarily disable Secure Boot.',
    'Do not run storage or installation tools during gameplay qualification.'
) | Set-Content -LiteralPath $readme -Encoding ascii

$fatImage = Join-Path $artifactRoot 'PST3-GuideXOS-PacMan-UEFI-BOOT.img'
$fatReport = Join-Path $artifactRoot 'fat32-verification.json'
$espSizeBytes = [int64]$EspSizeMB * 1024 * 1024
$espArgs = @(
    '-B', $espHelper, 'create', '--source', $staging, '--image', $fatImage,
    '--size', [string]$espSizeBytes, '--expected', $espExpected, '--report', $fatReport
)
Write-Host "[pst3-baremetal] creating verified FAT32 UEFI boot image: $fatImage"
& $python @espArgs
if ($LASTEXITCODE -ne 0) { Fail "release_esp.py failed with exit code $LASTEXITCODE" }

$isoExpected = Join-Path $artifactRoot 'iso-expected.json'
$fatFile = Get-Item -LiteralPath $fatImage
$readmeFile = Get-Item -LiteralPath $readme
$sectorCount = [int64][Math]::Ceiling([double]$fatFile.Length / 512.0)
if ($sectorCount -gt 65535) { $sectorCount = 0 }
$isoDescription = [ordered]@{
    files = @((Get-FileRecord $readmeFile 'README.TXT'))
    bootImage = [ordered]@{
        path = 'UEFI_BOOT.IMG'
        size = [int64]$fatFile.Length
        sha256 = (Get-FileHash -LiteralPath $fatFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        sectorCount = $sectorCount
    }
}
Write-JsonFile $isoExpected $isoDescription 6

$isoPath = Join-Path $artifactRoot 'PST3-GuideXOS-PacMan-UEFI-x64.iso'
$isoReport = Join-Path $artifactRoot 'iso-verification.json'
$isoArgs = @(
    '-B', $isoHelper, 'create', '--boot-image', $fatImage, '--readme', $readme,
    '--output', $isoPath, '--expected', $isoExpected, '--report', $isoReport,
    '--volume-id', 'PST3_PACMAN'
)
Write-Host "[pst3-baremetal] creating and independently verifying UEFI El Torito ISO: $isoPath"
& $python @isoArgs
if ($LASTEXITCODE -ne 0) { Fail "release_iso.py failed with exit code $LASTEXITCODE" }

$diskImage = Join-Path $artifactRoot 'PST3-GuideXOS-PacMan-UEFI-x64.img'
$gptReport = Join-Path $artifactRoot 'gpt-verification.json'
Write-Host "[pst3-baremetal] wrapping the verified EFI filesystem in a GPT disk image: $diskImage"
& $python -B $gptHelper --source-fat32 $fatImage --output $diskImage --report $gptReport
if ($LASTEXITCODE -ne 0) { Fail "wrap_pst3_gpt_image.py failed with exit code $LASTEXITCODE" }

$sourceIdentity = Join-Path $sourceEsp 'build-identity.txt'
$provenance = [ordered]@{
    createdUtc = [DateTime]::UtcNow.ToString('o')
    repository = $pacmanRoot
    serverRoot = $serverRoot
    serverBuildIdentity = if (Test-Path -LiteralPath $sourceIdentity) {
        (Get-Content -LiteralPath $sourceIdentity -Raw).Trim()
    } else { $null }
    bootInputs = @(
        (Get-FileRecord (Get-Item -LiteralPath $bootloader) 'EFI/BOOT/BOOTX64.EFI'),
        (Get-FileRecord (Get-Item -LiteralPath $kernel) 'kernel.elf'),
        (Get-FileRecord (Get-Item -LiteralPath $ramdisk) 'ramdisk.img')
    )
    productionPackageRoot = $productionPackage
    pacmanFiles = @($espFiles | Where-Object { $_.path -like 'Apps/PacMan/*' })
    fat32Image = [ordered]@{
        path = [IO.Path]::GetFileName($fatImage)
        sha256 = (Get-FileHash -LiteralPath $fatImage -Algorithm SHA256).Hash.ToLowerInvariant()
        size = (Get-Item -LiteralPath $fatImage).Length
    }
    iso = [ordered]@{
        path = [IO.Path]::GetFileName($isoPath)
        sha256 = (Get-FileHash -LiteralPath $isoPath -Algorithm SHA256).Hash.ToLowerInvariant()
        size = (Get-Item -LiteralPath $isoPath).Length
    }
    validation = [ordered]@{
        fat32 = $fatReport
        uefiElToritoIso = $isoReport
        gptDiskImage = $gptReport
    }
    gptDiskImage = [ordered]@{
        path = [IO.Path]::GetFileName($diskImage)
        sha256 = (Get-FileHash -LiteralPath $diskImage -Algorithm SHA256).Hash.ToLowerInvariant()
        size = (Get-Item -LiteralPath $diskImage).Length
    }
}
$provenancePath = Join-Path $artifactRoot 'provenance.json'
Write-JsonFile $provenancePath $provenance 10

Write-Host "[pst3-baremetal] complete; artifact directory: $artifactRoot"
Write-Host "[pst3-baremetal] ISO SHA-256: $((Get-FileHash -LiteralPath $isoPath -Algorithm SHA256).Hash)"
Write-Host "[pst3-baremetal] GPT disk image SHA-256: $((Get-FileHash -LiteralPath $diskImage -Algorithm SHA256).Hash)"
Write-Host "[pst3-baremetal] FAT32 boot image SHA-256: $((Get-FileHash -LiteralPath $fatImage -Algorithm SHA256).Hash)"
