[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GalleryExe,

    [ValidateSet("d3d11", "d3d12", "vulkan", "opengl")]
    [string[]]$Backends = @("d3d12", "vulkan", "opengl"),

    [string]$OutputRoot = (Join-Path $env:TEMP "AYUI-layer-backend-matrix")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$galleryPath = (Resolve-Path -LiteralPath $GalleryExe).Path
$runner = Join-Path $PSScriptRoot "RunLayerVisualRegression.ps1"
if (-not [IO.Path]::IsPathRooted($OutputRoot)) {
    $OutputRoot = Join-Path (Get-Location).Path $OutputRoot
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$results = @()
foreach ($backend in $Backends) {
    $backendOutput = Join-Path $OutputRoot $backend
    Write-Host "`n=== AYUI retained Layer backend: $backend ==="
    try {
        & $runner `
            -GalleryExe $galleryPath `
            -Backend $backend `
            -OutputDir $backendOutput
        $results += [pscustomobject]@{
            Backend = $backend
            Status = "PASS"
            Output = $backendOutput
            Detail = "36 captures"
        }
    } catch {
        $results += [pscustomobject]@{
            Backend = $backend
            Status = "FAIL"
            Output = $backendOutput
            Detail = $_.Exception.Message
        }
    }
}

Write-Host "`nCross-backend retained Layer matrix:"
$results | Format-Table Backend, Status, Detail, Output -AutoSize

$failures = @($results | Where-Object { $_.Status -ne "PASS" })
if ($failures.Count -ne 0) {
    throw "$($failures.Count) retained Layer backend matrix run(s) failed"
}

Write-Host "PASS: all requested retained Layer backends completed the 36-capture matrix."
