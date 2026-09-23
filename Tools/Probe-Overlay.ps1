param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$buildDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\Build\windows-msvc'))
$executable = Join-Path $buildDirectory "$Configuration\InputHaloOverlayProbe.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build InputHaloOverlayProbe for $Configuration first."
}
$previousObsCapture = $env:DISABLE_VULKAN_OBS_CAPTURE
$previousSyncValidation = $env:VK_LAYER_VALIDATE_SYNC
$probeExit = 1
try {
    $env:DISABLE_VULKAN_OBS_CAPTURE = '1'
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    Push-Location $buildDirectory
    try {
        & $executable
        $probeExit = $LASTEXITCODE
    } finally { Pop-Location }
} finally {
    $env:DISABLE_VULKAN_OBS_CAPTURE = $previousObsCapture
    $env:VK_LAYER_VALIDATE_SYNC = $previousSyncValidation
}
if ($probeExit -eq 2) {
    Write-Warning 'No transparent composite alpha mode. Milestone 2 is NOT accepted; no overlay was shown.'
}
exit $probeExit
