[CmdletBinding()]
param([string]$OutputDirectory, [string]$RuntimePackageDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$packagingCommit = (& git -C $root rev-parse HEAD).Trim()
$runtimeCommit = $packagingCommit
$runtimeDirectory = Join-Path $root 'build/bin'
if ($RuntimePackageDirectory) {
    $runtimeDirectory = (Resolve-Path -LiteralPath $RuntimePackageDirectory).Path
    $frozen = Get-Content -LiteralPath (Join-Path $runtimeDirectory 'package-manifest.json') -Raw | ConvertFrom-Json
    if ($frozen.schemaVersion -ne 1 -or $frozen.architecture -ne 'x86' -or $frozen.sourceDirty) { throw 'Use a clean, verified x86 runtime package.' }
    if ($frozen.baselineToolkit -ne 'v0.8.0' -or $frozen.nativeToolkitOverride -ne 'v0.13.0' -or $frozen.nativeComponent -ne '0.6.0') { throw 'Frozen runtime provenance differs from this packaging baseline.' }
    foreach ($name in 'dinput8.dll','WheelFfb.dll','force-profiles.ini') {
        $entry = @($frozen.files | Where-Object name -eq $name)
        $hash = (Get-FileHash -LiteralPath (Join-Path $runtimeDirectory $name)).Hash.ToLowerInvariant()
        if ($entry.Count -ne 1 -or $hash -ne $entry[0].sha256) { throw "Frozen runtime package hash mismatch: $name" }
    }
    $runtimeCommit = if ($frozen.runtimeSourceCommit) { $frozen.runtimeSourceCommit } else { $frozen.sourceCommit }
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root ('build/packages/wheel-settings-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')) }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new package directory; existing packages are immutable.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
foreach ($name in 'dinput8.dll','WheelFfb.dll','force-profiles.ini') { Copy-Item -LiteralPath (Join-Path $runtimeDirectory $name) -Destination $out }
foreach ($name in 'OutRun2006Tweaks.ini','OutRun2006Tweaks.lods.ini') { Copy-Item -LiteralPath (Join-Path $root $name) -Destination $out }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-WheelSettings.ps1') -Destination (Join-Path $out 'Install.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-WheelSettings.bat') -Destination (Join-Path $out 'Install.bat')
Copy-Item -LiteralPath (Join-Path $root 'docs/INSTALL-WHEEL-SETTINGS.md') -Destination (Join-Path $out 'README.md')
$provenance = Join-Path $out 'provenance'
New-Item -ItemType Directory -Path $provenance | Out-Null
foreach ($name in 'VERSION','NATIVE-VERSION','NATIVE-PROVENANCE.json','MANIFEST.txt') { Copy-Item -LiteralPath (Join-Path $root "lib/toolkit/$name") -Destination $provenance }
Copy-Item -LiteralPath (Join-Path $root 'docs/NATIVE-PIN-2026-09-17.md') -Destination $provenance
Get-ChildItem -LiteralPath $root -File | Where-Object Name -match '^LICENSE' | Copy-Item -Destination $out
$files = foreach ($file in Get-ChildItem -LiteralPath $out -File) { [ordered]@{name=$file.Name; sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()} }
$manifest = [ordered]@{schemaVersion=1; architecture='x86'; sourceCommit=$runtimeCommit; runtimeSourceCommit=$runtimeCommit; installerSourceCommit=$packagingCommit; packagingSourceCommit=$packagingCommit; sourceDirty=[bool](& git -C $root status --porcelain); builtUtc=(Get-Item -LiteralPath (Join-Path $out 'dinput8.dll')).LastWriteTimeUtc.ToString('o'); baselineToolkit='v0.8.0'; nativeToolkitOverride='v0.13.0'; nativeComponent='0.6.0'; files=@($files)}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'package-manifest.json') -Encoding UTF8
Compress-Archive -LiteralPath $out -DestinationPath ($out + '.zip')
Write-Host "Package: $out.zip"
Get-FileHash -LiteralPath ($out + '.zip') -Algorithm SHA256
