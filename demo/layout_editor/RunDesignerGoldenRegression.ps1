[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DesignerExe,

    [string]$BaselineDir = "",

    [string]$OutputDir = (Join-Path $env:TEMP "AYUI-designer-golden"),

    [ValidateRange(0, 255)]
    [int]$PixelTolerance = 2,

    [ValidateRange(0.0, 100.0)]
    [double]$MaxChangedPixelPercent = 0.05,

    [ValidateSet("default", "multi_select", "responsive", "theme")]
    [string[]]$Scenarios = @("default", "multi_select", "responsive", "theme"),

    [switch]$UpdateBaselines
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($BaselineDir)) {
    $scriptDirectory = Split-Path -Parent $PSCommandPath
    $BaselineDir = Join-Path $scriptDirectory "golden/windows-d3d11"
}

function Read-UncompressedTga([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 18) {
        throw "TGA header is truncated: $Path"
    }
    if ($bytes[1] -ne 0 -or $bytes[2] -ne 2) {
        throw "Only uncompressed true-color TGA is supported: $Path"
    }
    $width = [BitConverter]::ToUInt16($bytes, 12)
    $height = [BitConverter]::ToUInt16($bytes, 14)
    $bitsPerPixel = [int]$bytes[16]
    if ($width -le 0 -or $height -le 0 -or
        ($bitsPerPixel -ne 24 -and $bitsPerPixel -ne 32)) {
        throw "Unsupported TGA dimensions or pixel format: $Path"
    }
    $channels = [int]($bitsPerPixel / 8)
    $offset = 18 + [int]$bytes[0]
    $expected = $offset + ([int64]$width * [int64]$height * $channels)
    if ($bytes.Length -lt $expected) {
        throw "TGA pixel payload is truncated: $Path"
    }
    return [pscustomobject]@{
        Bytes = $bytes
        Width = [int]$width
        Height = [int]$height
        Channels = $channels
        Offset = $offset
    }
}

function Compare-Tga(
    [string]$ActualPath,
    [string]$ExpectedPath,
    [int]$Tolerance,
    [double]$AllowedPercent
) {
    $actual = Read-UncompressedTga $ActualPath
    $expected = Read-UncompressedTga $ExpectedPath
    if ($actual.Width -ne $expected.Width -or
        $actual.Height -ne $expected.Height -or
        $actual.Channels -ne $expected.Channels) {
        throw "Golden dimensions differ: actual $($actual.Width)x$($actual.Height)x$($actual.Channels), expected $($expected.Width)x$($expected.Height)x$($expected.Channels)"
    }

    $pixelCount = [int64]$actual.Width * [int64]$actual.Height
    $changed = [int64]0
    $maxDelta = 0
    for ([int64]$pixel = 0; $pixel -lt $pixelCount; ++$pixel) {
        $pixelChanged = $false
        for ($channel = 0; $channel -lt $actual.Channels; ++$channel) {
            $actualIndex = $actual.Offset + ($pixel * $actual.Channels) + $channel
            $expectedIndex = $expected.Offset + ($pixel * $expected.Channels) + $channel
            $delta = [Math]::Abs(
                [int]$actual.Bytes[$actualIndex] - [int]$expected.Bytes[$expectedIndex])
            if ($delta -gt $maxDelta) {
                $maxDelta = $delta
            }
            if ($delta -gt $Tolerance) {
                $pixelChanged = $true
            }
        }
        if ($pixelChanged) {
            ++$changed
        }
    }

    $changedPercent = if ($pixelCount -eq 0) {
        0.0
    } else {
        100.0 * [double]$changed / [double]$pixelCount
    }
    if ($changedPercent -gt $AllowedPercent) {
        throw ("Designer golden mismatch: {0:N4}% pixels changed " +
               "(allowed {1:N4}%, max channel delta {2})" -f
               $changedPercent, $AllowedPercent, $maxDelta)
    }
    return [pscustomobject]@{
        ChangedPixels = $changed
        ChangedPercent = $changedPercent
        MaxChannelDelta = $maxDelta
        Width = $actual.Width
        Height = $actual.Height
    }
}

$designerPath = (Resolve-Path -LiteralPath $DesignerExe).Path
$designerDirectory = Split-Path -Parent $designerPath
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$BaselineDir = [System.IO.Path]::GetFullPath($BaselineDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
New-Item -ItemType Directory -Force -Path $BaselineDir | Out-Null

$results = @()
foreach ($scenario in $Scenarios) {
    $name = "designer_$scenario"
    $captureBase = Join-Path $OutputDir $name
    $actualTga = "$captureBase.tga"
    $actualPng = "$captureBase.png"
    $actualMetrics = "$captureBase.metrics.txt"
    $baselineTga = Join-Path $BaselineDir "$name.tga"
    $baselinePng = Join-Path $BaselineDir "$name.png"
    $baselineMetrics = Join-Path $BaselineDir "$name.metrics.txt"

    foreach ($path in @($actualTga, $actualPng, $actualMetrics)) {
        Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue
    }

    try {
        $env:AY_UI_DESIGNER_CAPTURE_BASE = $captureBase
        $env:AY_UI_DESIGNER_CAPTURE_FRAME = "12"
        $env:AY_UI_DESIGNER_CAPTURE_SCENARIO = $scenario
        $process = Start-Process -FilePath $designerPath `
            -WorkingDirectory $designerDirectory `
            -Wait -PassThru -WindowStyle Hidden
        if ($process.ExitCode -ne 0) {
            throw "AYUI_LayoutEditor scenario '$scenario' exited with code $($process.ExitCode)"
        }
    }
    finally {
        Remove-Item Env:\AY_UI_DESIGNER_CAPTURE_BASE -ErrorAction SilentlyContinue
        Remove-Item Env:\AY_UI_DESIGNER_CAPTURE_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\AY_UI_DESIGNER_CAPTURE_SCENARIO -ErrorAction SilentlyContinue
    }

    if (-not (Test-Path -LiteralPath $actualTga) -or
        -not (Test-Path -LiteralPath $actualMetrics)) {
        throw "Designer scenario '$scenario' did not produce TGA and metrics files"
    }
    $metrics = Get-Content -LiteralPath $actualMetrics -Raw
    if ($metrics -notmatch "queued=yes" -or
        $metrics -notmatch "scenario=$([regex]::Escape($scenario))") {
        throw "Designer scenario '$scenario' rejected or mislabeled the screenshot request"
    }

    if ($UpdateBaselines) {
        Copy-Item -LiteralPath $actualTga -Destination $baselineTga -Force
        Copy-Item -LiteralPath $actualMetrics -Destination $baselineMetrics -Force
        if (Test-Path -LiteralPath $actualPng) {
            Copy-Item -LiteralPath $actualPng -Destination $baselinePng -Force
        }
        Write-Host "UPDATED [$scenario]: $baselineTga"
        continue
    }

    if (-not (Test-Path -LiteralPath $baselineTga)) {
        throw "Golden baseline '$scenario' is missing. Review captures, then rerun with -UpdateBaselines."
    }
    $comparison = Compare-Tga $actualTga $baselineTga `
        $PixelTolerance $MaxChangedPixelPercent
    $results += $comparison
    Write-Host ("PASS [{0}]: {1}x{2}; {3:N4}% pixels changed; max delta {4}." -f
        $scenario, $comparison.Width, $comparison.Height,
        $comparison.ChangedPercent, $comparison.MaxChannelDelta)
}

if (-not $UpdateBaselines) {
    Write-Host "PASS: $($results.Count) Designer golden scenario(s)."
}
