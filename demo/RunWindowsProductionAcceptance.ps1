[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GalleryExe,

    [Parameter(Mandatory = $true)]
    [string]$DesignerExe,

    [Parameter(Mandatory = $true)]
    [string]$VerticalVisualExe,

    [Parameter(Mandatory = $true)]
    [string]$UiUnitTestExe,

    [Parameter(Mandatory = $true)]
    [string]$EditorUnitTestExe,

    [Parameter(Mandatory = $true)]
    [string]$VerticalTestExe,

    [string]$OutputRoot = (Join-Path $env:TEMP "AYUI-windows-production"),

    [ValidateRange(1, 100)]
    [int]$StressRuns = 4
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-Executable([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label executable is missing: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Invoke-CheckedProcess([string]$Path, [string]$Label) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $Path `
        -WorkingDirectory (Split-Path -Parent $Path) `
        -Wait -PassThru -WindowStyle Hidden
    $watch.Stop()
    if ($process.ExitCode -ne 0) {
        throw "$Label exited with code $($process.ExitCode)"
    }
    return [pscustomobject]@{
        Gate = $Label
        Status = "PASS"
        Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
    }
}

$galleryPath = Resolve-Executable $GalleryExe "Gallery"
$designerPath = Resolve-Executable $DesignerExe "Designer"
$verticalVisualPath = Resolve-Executable $VerticalVisualExe "Vertical visual"
$uiTestsPath = Resolve-Executable $UiUnitTestExe "AYUI unit tests"
$editorTestsPath = Resolve-Executable $EditorUnitTestExe "AYEditor unit tests"
$verticalTestsPath = Resolve-Executable $VerticalTestExe "Vertical-slice tests"
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$results = @()
$results += Invoke-CheckedProcess $uiTestsPath "AYUI contracts"
$results += Invoke-CheckedProcess $editorTestsPath "AYEditor shell contracts"
$results += Invoke-CheckedProcess $verticalTestsPath "Scene/UI Flow contracts"

$watch = [Diagnostics.Stopwatch]::StartNew()
& (Join-Path $PSScriptRoot "RunBatchVisualRegression.ps1") `
    -GalleryExe $galleryPath `
    -OutputDir (Join-Path $OutputRoot "gallery-batching")
$watch.Stop()
$results += [pscustomobject]@{
    Gate = "Gallery exact-pixel batching"
    Status = "PASS"
    Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
}

$watch.Restart()
& (Join-Path $PSScriptRoot "RunLayerVisualRegression.ps1") `
    -GalleryExe $galleryPath `
    -Backend "d3d11" `
    -Scales @(1.0, 1.25, 1.5, 2.0) `
    -OutputDir (Join-Path $OutputRoot "layer-dpi-lifecycle")
$watch.Stop()
$results += [pscustomobject]@{
    Gate = "Layer/DPI/lifecycle matrix"
    Status = "PASS"
    Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
}

$watch.Restart()
& (Join-Path $PSScriptRoot "layout_editor/RunDesignerGoldenRegression.ps1") `
    -DesignerExe $designerPath `
    -OutputDir (Join-Path $OutputRoot "designer-golden")
$watch.Stop()
$results += [pscustomobject]@{
    Gate = "Designer real-window golden"
    Status = "PASS"
    Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
}

$applicationRunner = Join-Path `
    (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) `
    "AYApplication/tools/ui_vertical_slice_visual/RunGoldenRegression.ps1"
if (-not (Test-Path -LiteralPath $applicationRunner -PathType Leaf)) {
    throw "Vertical-slice golden runner is missing: $applicationRunner"
}
$watch.Restart()
& $applicationRunner `
    -VisualExe $verticalVisualPath `
    -OutputDir (Join-Path $OutputRoot "vertical-slice-golden")
$watch.Stop()
$results += [pscustomobject]@{
    Gate = "Scene/UI Flow real-window golden"
    Status = "PASS"
    Seconds = [Math]::Round($watch.Elapsed.TotalSeconds, 2)
}

$stressOutput = Join-Path $OutputRoot "designer-reopen-stress"
New-Item -ItemType Directory -Force -Path $stressOutput | Out-Null
$stressTimes = @()
try {
    $env:AY_UI_DESIGNER_CAPTURE_FRAME = "12"
    $env:AY_UI_DESIGNER_CAPTURE_SCENARIO = "default"
    for ($run = 1; $run -le $StressRuns; ++$run) {
        $base = Join-Path $stressOutput ("run_{0:D3}" -f $run)
        $env:AY_UI_DESIGNER_CAPTURE_BASE = $base
        $elapsed = [Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process -FilePath $designerPath `
            -WorkingDirectory (Split-Path -Parent $designerPath) `
            -Wait -PassThru -WindowStyle Hidden
        $elapsed.Stop()
        if ($process.ExitCode -ne 0) {
            throw "Designer stress run $run exited with code $($process.ExitCode)"
        }
        $metrics = "$base.metrics.txt"
        if (-not (Test-Path -LiteralPath "$base.tga") `
            -or -not (Test-Path -LiteralPath $metrics) `
            -or (Get-Content -LiteralPath $metrics -Raw) -notmatch "queued=yes") {
            throw "Designer stress run $run did not complete a valid frame"
        }
        $stressTimes += $elapsed.Elapsed.TotalSeconds
    }
}
finally {
    Remove-Item Env:\AY_UI_DESIGNER_CAPTURE_BASE -ErrorAction SilentlyContinue
    Remove-Item Env:\AY_UI_DESIGNER_CAPTURE_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\AY_UI_DESIGNER_CAPTURE_SCENARIO -ErrorAction SilentlyContinue
}
$results += [pscustomobject]@{
    Gate = "Designer reopen stress ($StressRuns runs)"
    Status = "PASS"
    Seconds = [Math]::Round(($stressTimes | Measure-Object -Sum).Sum, 2)
}

$results | Format-Table Gate, Status, Seconds -AutoSize
$report = [ordered]@{
    schemaVersion = 1
    platform = "windows"
    dpiScales = @(1.0, 1.25, 1.5, 2.0)
    stressRuns = $StressRuns
    stressSeconds = @($stressTimes | ForEach-Object { [Math]::Round($_, 3) })
    gates = @($results)
}
$report | ConvertTo-Json -Depth 5 | Set-Content `
    -LiteralPath (Join-Path $OutputRoot "acceptance-report.json") `
    -Encoding UTF8
Write-Host "PASS: Windows UI production acceptance completed. Report: $(Join-Path $OutputRoot 'acceptance-report.json')"
