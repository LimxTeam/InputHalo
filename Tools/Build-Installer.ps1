param(
    [string]$Compiler = 'C:\Application\Development\Inno Setup 7\ISCC.exe',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
$releaseRoot = Join-Path $projectRoot 'Build\windows-msvc\Release'
Push-Location -LiteralPath $projectRoot
try {
    if (-not $SkipBuild) {
        & cmake --build --preset release --target InputHalo
        if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    }
    foreach ($relative in @('InputHalo.exe','Shaders\Renderer2D.vert.spv','Shaders\Renderer2D.frag.spv','LICENSE.txt','PRIVACY.txt','THIRD_PARTY_NOTICES.txt','Licenses')) {
        if (-not (Test-Path -LiteralPath (Join-Path $releaseRoot $relative))) { throw "Missing release payload: $relative" }
    }
    & $compilerPath "/DSourceRoot=$projectRoot" "/DBuildRoot=$releaseRoot" (Join-Path $projectRoot 'Packaging\InputHalo.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $releaseRoot 'InputHalo.exe')).FileVersion
    $installer = Join-Path $projectRoot "Build\Installer\InputHalo-$version-Preview-windows-x64-setup.exe"
    $hash = Get-FileHash -LiteralPath $installer -Algorithm SHA256
    "$($hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($installer))" | Set-Content -LiteralPath "$installer.sha256" -Encoding ascii
    Get-Item -LiteralPath $installer | Select-Object FullName, Length
    Write-Output "SHA256: $($hash.Hash)"
} finally { Pop-Location }
