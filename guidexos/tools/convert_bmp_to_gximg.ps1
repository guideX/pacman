param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'
$bytes = [IO.File]::ReadAllBytes($Source)
if ($bytes.Length -lt 54 -or $bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4D) { throw "Not a BMP: $Source" }
$pixelOffset = [BitConverter]::ToInt32($bytes, 10)
$width = [BitConverter]::ToInt32($bytes, 18)
$storedHeight = [BitConverter]::ToInt32($bytes, 22)
$planes = [BitConverter]::ToInt16($bytes, 26)
$bits = [BitConverter]::ToInt16($bytes, 28)
$compression = [BitConverter]::ToUInt32($bytes, 30)
if ($planes -ne 1 -or $bits -ne 24 -or $compression -ne 0) { throw "Only uncompressed 24-bit BMP is supported: $Source" }
$height = [Math]::Abs($storedHeight)
$rowBytes = (($width * 3 + 3) - (($width * 3 + 3) % 4))
$strideBytes = $width * 4
$payloadBytes = $strideBytes * $height
$output = New-Object byte[] (28 + $payloadBytes)
$output[0] = [byte][char]'G'; $output[1] = [byte][char]'X'; $output[2] = [byte][char]'I'; $output[3] = [byte][char]'M'
[Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $output, 4, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$width), 0, $output, 8, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$height), 0, $output, 12, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$strideBytes), 0, $output, 16, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $output, 20, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$payloadBytes), 0, $output, 24, 4)

for ($y = 0; $y -lt $height; $y++) {
    $sourceRow = if ($storedHeight -gt 0) { $height - 1 - $y } else { $y }
    $sourceOffset = $pixelOffset + $sourceRow * $rowBytes
    $destinationOffset = 28 + $y * $strideBytes
    for ($x = 0; $x -lt $width; $x++) {
        $sourcePixel = $sourceOffset + $x * 3
        $destinationPixel = $destinationOffset + $x * 4
        $output[$destinationPixel] = $bytes[$sourcePixel]
        $output[$destinationPixel + 1] = $bytes[$sourcePixel + 1]
        $output[$destinationPixel + 2] = $bytes[$sourcePixel + 2]
        $output[$destinationPixel + 3] = 0
    }
}

$parent = Split-Path -Parent $Destination
if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
[IO.File]::WriteAllBytes($Destination, $output)
Write-Output ("Converted {0} -> {1} ({2}x{3}, {4} bytes)" -f $Source, $Destination, $width, $height, $output.Length)
