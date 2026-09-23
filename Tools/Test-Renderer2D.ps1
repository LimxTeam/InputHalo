param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$buildDirectory = Join-Path $PSScriptRoot '..\Build\windows-msvc'
$executable = Join-Path $buildDirectory "$Configuration\InputHaloRenderer2DSmoke.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build the $Configuration preset first." }
$previousObsCapture = $env:DISABLE_VULKAN_OBS_CAPTURE
$previousSyncValidation = $env:VK_LAYER_VALIDATE_SYNC
try {
    $env:DISABLE_VULKAN_OBS_CAPTURE = '1'
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    Push-Location $buildDirectory
    try {
        & $executable
        if ($LASTEXITCODE -ne 0) { throw "Renderer2D smoke failed: $LASTEXITCODE. See renderer2d-smoke-logs/inputhalo.log." }
    } finally { Pop-Location }
} finally {
    $env:DISABLE_VULKAN_OBS_CAPTURE = $previousObsCapture
    $env:VK_LAYER_VALIDATE_SYNC = $previousSyncValidation
}
