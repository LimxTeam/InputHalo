$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$noticeText=[IO.File]::ReadAllText((Join-Path $projectRoot 'ThirdParty/NOTICE-header.txt'))
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'ThirdParty/licenses') -File | Sort-Object Name | ForEach-Object {
 $noticeText += "`n`n================ " + $_.Name + " ================`n`n" + [IO.File]::ReadAllText($_.FullName)
}
[IO.File]::WriteAllText((Join-Path $projectRoot 'THIRD_PARTY_NOTICES.txt'),$noticeText,[Text.UTF8Encoding]::new($false))
Write-Output 'Third-party notices updated. Rebuild to refresh the embedded notices.'
