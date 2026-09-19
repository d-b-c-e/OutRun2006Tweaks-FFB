[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root ('build/packages/wheel-settings-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')) }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new package directory; existing packages are immutable.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
foreach ($name in 'dinput8.dll','WheelFfb.dll','force-profiles.ini') { Copy-Item -LiteralPath (Join-Path $root "build/bin/$name") -Destination $out }
foreach ($name in 'OutRun2006Tweaks.ini','OutRun2006Tweaks.lods.ini') { Copy-Item -LiteralPath (Join-Path $root $name) -Destination $out }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-WheelSettings.ps1') -Destination (Join-Path $out 'Install.ps1')
@('@echo off','powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*','if errorlevel 1 echo Installation failed. Read the message above.','pause') | Set-Content -LiteralPath (Join-Path $out 'Install.bat') -Encoding ascii
Copy-Item -LiteralPath (Join-Path $root 'docs/INSTALL-WHEEL-SETTINGS.md') -Destination (Join-Path $out 'README.md')
$provenance = Join-Path $out 'provenance'
New-Item -ItemType Directory -Path $provenance | Out-Null
foreach ($name in 'VERSION','NATIVE-VERSION','NATIVE-PROVENANCE.json','MANIFEST.txt') { Copy-Item -LiteralPath (Join-Path $root "lib/toolkit/$name") -Destination $provenance }
Copy-Item -LiteralPath (Join-Path $root 'docs/NATIVE-PIN-2026-09-17.md') -Destination $provenance
Get-ChildItem -LiteralPath $root -File | Where-Object Name -match '^LICENSE' | Copy-Item -Destination $out
$files = foreach ($file in Get-ChildItem -LiteralPath $out -File) { [ordered]@{name=$file.Name; sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()} }
$manifest = [ordered]@{schemaVersion=1; architecture='x86'; sourceCommit=(& git -C $root rev-parse HEAD).Trim(); sourceDirty=[bool](& git -C $root status --porcelain); builtUtc=(Get-Item -LiteralPath (Join-Path $out 'dinput8.dll')).LastWriteTimeUtc.ToString('o'); baselineToolkit='v0.8.0'; nativeToolkitOverride='v0.13.0'; nativeComponent='0.6.0'; files=@($files)}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'package-manifest.json') -Encoding UTF8
Compress-Archive -LiteralPath $out -DestinationPath ($out + '.zip')
Write-Host "Package: $out.zip"
Get-FileHash -LiteralPath ($out + '.zip') -Algorithm SHA256
