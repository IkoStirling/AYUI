[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GalleryExe,

    [string]$OutputDir = (Join-Path $env:TEMP "AYUI-batch-visual")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$galleryPath = (Resolve-Path -LiteralPath $GalleryExe).Path
$galleryDirectory = Split-Path -Parent $galleryPath
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$cases = @(
    @{ Name = "basics";        Page = "page_basics";       Action = "" },
    @{ Name = "images";        Page = "page_images";       Action = "" },
    @{ Name = "input";         Page = "page_input";        Action = "" },
    @{ Name = "collections";   Page = "page_collections";  Action = "" },
    @{ Name = "layout";        Page = "page_layout";       Action = "" },
    @{ Name = "capabilities";  Page = "page_capabilities"; Action = "" },
    @{ Name = "backend";       Page = "page_backend";      Action = "" },
    @{ Name = "animation";     Page = "page_animation";    Action = "" },
    @{ Name = "overlay_modal"; Page = "page_overlay";      Action = "open_modal" }
)

function Get-DrawCalls([string]$MetricsPath) {
    $line = Get-Content -LiteralPath $MetricsPath |
        Where-Object { $_ -like "drawCalls=*" } |
        Select-Object -First 1
    if ($null -eq $line) {
        throw "drawCalls missing from $MetricsPath"
    }
    return [int]($line -replace "drawCalls=", "")
}

$results = @()
try {
    foreach ($case in $cases) {
        $captures = @{}
        foreach ($mode in @("ordered", "overlap")) {
            $stem = "$($case.Name)_$mode"
            $base = Join-Path $OutputDir $stem
            $env:AY_UI_GALLERY_BATCH_MODE = $mode
            $env:AY_UI_GALLERY_CAPTURE_PAGE = $case.Page
            $env:AY_UI_GALLERY_CAPTURE_ACTION = $case.Action
            $env:AY_UI_GALLERY_CAPTURE_SCROLL_Y = "0"
            $env:AY_UI_GALLERY_CAPTURE_BASE = $base

            $process = Start-Process -FilePath $galleryPath `
                -WorkingDirectory $galleryDirectory `
                -Wait -PassThru -WindowStyle Hidden
            if ($process.ExitCode -ne 0) {
                throw "$stem exited with code $($process.ExitCode)"
            }

            $imagePath = "$base.tga"
            $metricsPath = "$base.metrics.txt"
            if (-not (Test-Path -LiteralPath $imagePath) -or -not (Test-Path -LiteralPath $metricsPath)) {
                throw "$stem did not produce both image and metrics"
            }
            $captures[$mode] = @{
                Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $imagePath).Hash
                DrawCalls = Get-DrawCalls $metricsPath
            }
        }

        if ($captures.ordered.Hash -ne $captures.overlap.Hash) {
            throw "$($case.Name): optimized image differs from OrderedRuns baseline"
        }
        if ($captures.overlap.DrawCalls -gt $captures.ordered.DrawCalls) {
            throw "$($case.Name): optimized draw-call count regressed"
        }

        $results += [pscustomobject]@{
            Case = $case.Name
            Ordered = $captures.ordered.DrawCalls
            Overlap = $captures.overlap.DrawCalls
            ExactPixels = $true
        }
    }
}
finally {
    Remove-Item Env:\AY_UI_GALLERY_BATCH_MODE -ErrorAction SilentlyContinue
    Remove-Item Env:\AY_UI_GALLERY_CAPTURE_PAGE -ErrorAction SilentlyContinue
    Remove-Item Env:\AY_UI_GALLERY_CAPTURE_ACTION -ErrorAction SilentlyContinue
    Remove-Item Env:\AY_UI_GALLERY_CAPTURE_SCROLL_Y -ErrorAction SilentlyContinue
    Remove-Item Env:\AY_UI_GALLERY_CAPTURE_BASE -ErrorAction SilentlyContinue
}

$results | Format-Table -AutoSize
Write-Host "PASS: all $($results.Count) Gallery paths are pixel-identical."
