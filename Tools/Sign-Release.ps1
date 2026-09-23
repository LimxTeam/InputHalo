param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$CertificateThumbprint,
 [Parameter(Mandatory=$true)][ValidatePattern('^https://')][string]$TimestampUrl,
 [Parameter(Mandatory=$true)][string]$SignTool
)
$ErrorActionPreference='Stop'
$targetFile=(Resolve-Path -LiteralPath $Executable).Path
$signToolFile=(Resolve-Path -LiteralPath $SignTool).Path
if ([IO.Path]::GetExtension($targetFile) -ne '.exe') { throw 'Specify the final InputHalo executable.' }
# Uses an existing certificate in CurrentUser\My. No certificate or trust-store changes.
& $signToolFile sign /sha1 $CertificateThumbprint /s My /fd SHA256 /tr $TimestampUrl /td SHA256 /d InputHalo $targetFile
if ($LASTEXITCODE -ne 0) { throw "Signing failed: $LASTEXITCODE" }
& $signToolFile verify /pa /v $targetFile
if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $LASTEXITCODE" }
