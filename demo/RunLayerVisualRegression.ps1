[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GalleryExe,

    [ValidateSet("auto", "d3d11", "d3d12", "vulkan", "opengl")]
    [string]$Backend = "auto",

    [string]$OutputDir = (Join-Path $env:TEMP "AYUI-layer-visual")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$galleryPath = (Resolve-Path -LiteralPath $GalleryExe).Path
$galleryDirectory = Split-Path -Parent $galleryPath
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Get-Metric([string]$MetricsPath, [string]$Name) {
    $prefix = "$Name="
    $line = Get-Content -LiteralPath $MetricsPath |
        Where-Object { $_ -like "$prefix*" } |
        Select-Object -First 1
    if ($null -eq $line) {
        throw "$Name missing from $MetricsPath"
    }
    return $line.Substring($prefix.Length)
}

function Invoke-GalleryCapture(
    [string]$Name,
    [string]$RootLayer,
    [string]$Scenario,
    [int]$CaptureFrame,
    [int]$MutationFrame,
    [float]$Scale
) {
    $base = Join-Path $OutputDir $Name
    $env:AY_UI_GALLERY_BATCH_MODE = "overlap"
    $env:AY_UI_GALLERY_CAPTURE_BASE = $base
    $env:AY_UI_GALLERY_CAPTURE_PAGE = "page_layer_visual"
    $env:AY_UI_GALLERY_LAYER_PROBE_SCENARIO = $Scenario
    $env:AY_UI_GALLERY_ROOT_LAYER = $RootLayer
    $env:AY_UI_GALLERY_CAPTURE_FRAME = $CaptureFrame.ToString(
        [Globalization.CultureInfo]::InvariantCulture)
    $env:AY_UI_GALLERY_MUTATION_FRAME = $MutationFrame.ToString(
        [Globalization.CultureInfo]::InvariantCulture)
    $env:AY_UI_GALLERY_CAPTURE_SCALE = $Scale.ToString(
        "0.00", [Globalization.CultureInfo]::InvariantCulture)
    $env:AY_UI_GALLERY_BACKEND = $Backend

    foreach ($output in @("$base.tga", "$base.png", "$base.metrics.txt")) {
        Remove-Item -LiteralPath $output -ErrorAction SilentlyContinue
    }

    $process = Start-Process -FilePath $galleryPath `
        -WorkingDirectory $galleryDirectory `
        -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "$Name exited with code $($process.ExitCode)"
    }

    $tga = "$base.tga"
    $png = "$base.png"
    $metrics = "$base.metrics.txt"
    if (-not (Test-Path -LiteralPath $tga) `
        -or -not (Test-Path -LiteralPath $metrics)) {
        throw "$Name did not produce TGA and metrics outputs"
    }
    return [pscustomobject]@{
        Name = $Name
        Tga = $tga
        Png = if (Test-Path -LiteralPath $png) { $png } else { $null }
        Metrics = $metrics
        DrawCalls = [int](Get-Metric $metrics "drawCalls")
        Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tga).Hash
    }
}

function Compare-Tga(
    [string]$Reference,
    [string]$Candidate,
    [string]$DiffPath,
    [string]$Label,
    [int]$MaxAllowedDelta = 0
) {
    Remove-Item -LiteralPath $DiffPath -ErrorAction SilentlyContinue
    $referenceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Reference).Hash
    $candidateHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Candidate).Hash
    if ($referenceHash -eq $candidateHash) {
        return [pscustomobject]@{ Label = $Label; DifferentPixels = 0; MaxDelta = 0 }
    }

    [byte[]]$a = [IO.File]::ReadAllBytes($Reference)
    [byte[]]$b = [IO.File]::ReadAllBytes($Candidate)
    if ($a.Length -lt 18 -or $b.Length -lt 18) {
        throw "${Label}: capture is not a complete TGA"
    }
    $widthA = [int]$a[12] -bor ([int]$a[13] -shl 8)
    $heightA = [int]$a[14] -bor ([int]$a[15] -shl 8)
    $widthB = [int]$b[12] -bor ([int]$b[13] -shl 8)
    $heightB = [int]$b[14] -bor ([int]$b[15] -shl 8)
    $bytesPerPixelA = [int]$a[16] / 8
    $bytesPerPixelB = [int]$b[16] / 8
    $offsetA = 18 + [int]$a[0]
    $offsetB = 18 + [int]$b[0]
    if ($a[1] -ne 0 -or $b[1] -ne 0 -or $a[2] -ne 2 -or $b[2] -ne 2 `
        -or $widthA -ne $widthB -or $heightA -ne $heightB `
        -or $bytesPerPixelA -ne $bytesPerPixelB `
        -or ($bytesPerPixelA -ne 3 -and $bytesPerPixelA -ne 4)) {
        throw "${Label}: incompatible uncompressed true-color TGA captures"
    }

    $pixelCount = $widthA * $heightA
    $pixelBytes = $pixelCount * $bytesPerPixelA
    if ($offsetA + $pixelBytes -gt $a.Length `
        -or $offsetB + $pixelBytes -gt $b.Length) {
        throw "${Label}: truncated TGA pixel data"
    }

    [byte[]]$diff = [byte[]]::new($b.Length)
    [Array]::Copy($b, $diff, $offsetB)
    $differentPixels = 0
    $violatingPixels = 0
    $maxDelta = 0
    for ($pixel = 0; $pixel -lt $pixelCount; ++$pixel) {
        $ia = $offsetA + $pixel * $bytesPerPixelA
        $ib = $offsetB + $pixel * $bytesPerPixelB
        $different = $false
        $violating = $false
        for ($channel = 0; $channel -lt $bytesPerPixelA; ++$channel) {
            $delta = [Math]::Abs([int]$a[$ia + $channel] - [int]$b[$ib + $channel])
            if ($delta -ne 0) { $different = $true }
            if ($delta -gt $MaxAllowedDelta) { $violating = $true }
            if ($delta -gt $maxDelta) { $maxDelta = $delta }
        }
        if ($different) { ++$differentPixels }
        if ($violating) {
            ++$violatingPixels
            $diff[$ib + 0] = 0
            $diff[$ib + 1] = 0
            $diff[$ib + 2] = 255
        } else {
            $diff[$ib + 0] = 0
            $diff[$ib + 1] = 0
            $diff[$ib + 2] = 0
        }
        if ($bytesPerPixelB -eq 4) { $diff[$ib + 3] = 255 }
    }
    if ($violatingPixels -ne 0) {
        [IO.File]::WriteAllBytes($DiffPath, $diff)
        throw "$Label exceeds $MaxAllowedDelta LSB at $violatingPixels / $pixelCount pixels ($differentPixels pixels differ); max channel delta $maxDelta; heatmap: $DiffPath"
    }
    return [pscustomobject]@{
        Label = $Label
        DifferentPixels = $differentPixels
        MaxDelta = $maxDelta
    }
}

$results = @()
try {
    foreach ($scale in @(1.0, 1.5)) {
        $scaleTag = "s" + [int]($scale * 100)
        $stableRef = Invoke-GalleryCapture `
            "${scaleTag}_stable_immediate" "immediate" "stable" 8 6 $scale
        $fullLayer = Invoke-GalleryCapture `
            "${scaleTag}_full_layer" "layer" "full" 6 6 $scale
        $cleanLayer = Invoke-GalleryCapture `
            "${scaleTag}_clean_layer" "layer" "stable" 8 6 $scale
        $movedRef = Invoke-GalleryCapture `
            "${scaleTag}_moved_immediate" "immediate" "move" 6 6 $scale
        $partialLayer = Invoke-GalleryCapture `
            "${scaleTag}_partial_layer" "layer" "move" 6 6 $scale

        $fullComparison = Compare-Tga $stableRef.Tga $fullLayer.Tga `
            (Join-Path $OutputDir "${scaleTag}_full.diff.tga") `
            "$scaleTag immediate vs full Layer" 1
        $cleanComparison = Compare-Tga $stableRef.Tga $cleanLayer.Tga `
            (Join-Path $OutputDir "${scaleTag}_clean.diff.tga") `
            "$scaleTag immediate vs clean Layer" 1
        $partialComparison = Compare-Tga $movedRef.Tga $partialLayer.Tga `
            (Join-Path $OutputDir "${scaleTag}_partial.diff.tga") `
            "$scaleTag immediate vs partial Layer" 1
        Compare-Tga $fullLayer.Tga $cleanLayer.Tga `
            (Join-Path $OutputDir "${scaleTag}_reuse.diff.tga") `
            "$scaleTag full repaint vs clean retained reuse" 0 | Out-Null

        if ($cleanLayer.DrawCalls -ne 1) {
            throw "$scaleTag clean Layer expected exactly 1 UI draw call, got $($cleanLayer.DrawCalls)"
        }
        if ($partialLayer.DrawCalls -gt $fullLayer.DrawCalls) {
            throw "$scaleTag partial Layer draw calls $($partialLayer.DrawCalls) exceed full Layer $($fullLayer.DrawCalls)"
        }

        $results += [pscustomobject]@{
            Scale = $scale
            Immediate = $stableRef.DrawCalls
            FullLayer = $fullLayer.DrawCalls
            CleanLayer = $cleanLayer.DrawCalls
            PartialLayer = $partialLayer.DrawCalls
            SemanticMaxDelta = [Math]::Max(
                $fullComparison.MaxDelta,
                [Math]::Max($cleanComparison.MaxDelta, $partialComparison.MaxDelta))
            ExactLayerReuse = $true
        }
    }
}
finally {
    foreach ($name in @(
        "AY_UI_GALLERY_BATCH_MODE",
        "AY_UI_GALLERY_CAPTURE_BASE",
        "AY_UI_GALLERY_CAPTURE_PAGE",
        "AY_UI_GALLERY_LAYER_PROBE_SCENARIO",
        "AY_UI_GALLERY_ROOT_LAYER",
        "AY_UI_GALLERY_CAPTURE_FRAME",
        "AY_UI_GALLERY_MUTATION_FRAME",
        "AY_UI_GALLERY_CAPTURE_SCALE",
        "AY_UI_GALLERY_BACKEND")) {
        Remove-Item "Env:\$name" -ErrorAction SilentlyContinue
    }
}

$results | Format-Table -AutoSize
Write-Host "PASS: retained Layer matches immediate GPU output within 1 RGBA8 LSB, and clean reuse is byte-exact, on backend '$Backend'."
