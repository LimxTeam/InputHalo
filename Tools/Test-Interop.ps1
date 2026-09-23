param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Preview
)
$ErrorActionPreference = 'Stop'
$buildDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\Build\windows-msvc'))
$executable = Join-Path $buildDirectory "$Configuration\InputHaloInteropSpike.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build InputHaloInteropSpike first.' }
$previousObsCapture = $env:DISABLE_VULKAN_OBS_CAPTURE
$previousSyncValidation = $env:VK_LAYER_VALIDATE_SYNC
try {
    $env:DISABLE_VULKAN_OBS_CAPTURE = '1'
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    if ($Preview) { & $executable --preview } else { & $executable }
    if ($LASTEXITCODE -ne 0) { throw "Interop test failed ($LASTEXITCODE); inspect $Configuration/interop-results." }
} finally {
    $env:DISABLE_VULKAN_OBS_CAPTURE = $previousObsCapture
    $env:VK_LAYER_VALIDATE_SYNC = $previousSyncValidation
}
