<#
.SYNOPSIS
Install the packaged wheel mod, or restore a timestamped runtime backup.
.DESCRIPTION
Never starts or stops the game. Existing settings, bindings and profiles are
retained byte for byte. Runtime replacement is backed up and rolled back on
failure. Restore changes runtime files only, retaining subsequent settings.
#>
[CmdletBinding()]
param(
    [string]$GameDirectory,
    [string]$PackageDirectory = $PSScriptRoot,
    [ValidateSet('Install','Restore')][string]$Action = 'Install',
    [string]$BackupDirectory,
    [switch]$AllowUnknownProxy
)
$ErrorActionPreference = 'Stop'
if (-not $GameDirectory) { $GameDirectory = (Read-Host 'Folder containing OR2006C2C.EXE').Trim('"') }
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$exe = Join-Path $game 'OR2006C2C.EXE'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Choose the game folder containing OR2006C2C.EXE.' }
$reader = [IO.BinaryReader]::new([IO.File]::OpenRead($exe))
try {
    if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'The game executable is not a Windows executable.' }
    $reader.BaseStream.Position = 0x3c
    $reader.BaseStream.Position = $reader.ReadUInt32()
    if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x14c) { throw 'This package requires the 32-bit PC game.' }
} finally { $reader.Dispose() }
function Assert-Closed {
    if (Get-Process -Name OR2006C2C -ErrorAction SilentlyContinue) { throw 'Exit OutRun normally, then run this installer again. No files were replaced.' }
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
Assert-Closed
$runtime = @('dinput8.dll','WheelFfb.dll')
$seed = @('OutRun2006Tweaks.ini','OutRun2006Tweaks.lods.ini','force-profiles.ini')
$proxyProvenance = [ordered]@{classification='none'; fileVersion=$null; product=$null; sha256=$null}
$proxyPath = Join-Path $game 'dinput8.dll'
if (Test-Path -LiteralPath $proxyPath) {
    $info = (Get-Item -LiteralPath $proxyPath).VersionInfo
    $proxyProvenance.fileVersion = $info.FileVersion
    $proxyProvenance.product = $info.ProductName
    $proxyProvenance.sha256 = Hash $proxyPath
    if ($info.ProductName -eq 'Outrun2006Tweaks') {
        $proxyProvenance.classification = 'OutRun2006Tweaks-' + $info.FileVersion
        Write-Host "Found existing OutRun2006Tweaks $($info.FileVersion); its proxy and settings will be backed up."
    } else {
        $proxyProvenance.classification = 'unrecognized-proxy'
        if ($Action -eq 'Install' -and -not $AllowUnknownProxy) { throw 'The existing dinput8.dll is not identified as OutRun2006Tweaks. Inspect it before choosing -AllowUnknownProxy; this package cannot chain a different proxy.' }
    }
}
$preserved = @()
foreach ($file in Get-ChildItem -LiteralPath $game -File) {
    if ($file.Extension -in '.ini','.cfg','.json','.xml') {
        $preserved += [pscustomobject]@{ name = $file.Name; sha256 = Hash $file.FullName }
    }
}
function Assert-Preserved {
    foreach ($file in $preserved) {
        if ((Hash (Join-Path $game $file.name)) -ne $file.sha256) { throw "Settings retention check failed: $($file.name)" }
    }
}
if ($Action -eq 'Restore') {
    if (-not $BackupDirectory) { throw 'Supply -BackupDirectory with the folder printed by the installer.' }
    $backup = (Resolve-Path -LiteralPath $BackupDirectory).Path
    $receipt = Get-Content -LiteralPath (Join-Path $backup 'receipt.json') -Raw | ConvertFrom-Json
    if ($receipt.gameDirectory -ne $game) { throw 'This backup belongs to a different game folder.' }
    foreach ($item in $receipt.runtime) {
        if ($item.name -notin $runtime) { throw 'Unexpected runtime filename in backup.' }
        $destination = Join-Path $game $item.name
        if (-not (Test-Path -LiteralPath $destination) -or (Hash $destination) -ne $item.installedSha256) {
            throw "Runtime changed since this install: $($item.name). Restore has not started."
        }
        if ($item.existed -and (Hash (Join-Path $backup $item.name)) -ne $item.previousSha256) { throw 'Backup hash mismatch.' }
    }
    Assert-Closed
    foreach ($item in $receipt.runtime) {
        $destination = Join-Path $game $item.name
        if ($item.existed) { Copy-Item -LiteralPath (Join-Path $backup $item.name) -Destination $destination -Force }
        else { Remove-Item -LiteralPath $destination }
    }
    Assert-Preserved
    Write-Host "Restored runtime from $backup. Settings and added profile/template files retained."
    return
}
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $package 'package-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $manifest.architecture -ne 'x86') { throw 'Unsupported package manifest.' }
foreach ($name in $runtime + $seed) {
    $entry = @($manifest.files | Where-Object name -eq $name)
    if ($entry.Count -ne 1 -or (Hash (Join-Path $package $name)) -ne $entry[0].sha256) { throw "Package hash mismatch: $name" }
}
$backup = Join-Path $game ('.wheel-settings-backups/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $backup | Out-Null
$records = @()
foreach ($name in $runtime) {
    $destination = Join-Path $game $name
    $exists = Test-Path -LiteralPath $destination
    $previous = $null
    if ($exists) {
        $previous = Hash $destination
        Copy-Item -LiteralPath $destination -Destination (Join-Path $backup $name)
        if ((Hash (Join-Path $backup $name)) -ne $previous) { throw "Backup verification failed: $name" }
    }
    $records += [pscustomobject]@{name=$name; existed=$exists; previousSha256=$previous; installedSha256=(Hash (Join-Path $package $name))}
}
$settingsBackup = Join-Path $backup 'settings'
New-Item -ItemType Directory -Path $settingsBackup | Out-Null
foreach ($file in $preserved) { Copy-Item -LiteralPath (Join-Path $game $file.name) -Destination (Join-Path $settingsBackup $file.name) }
$receipt = [ordered]@{schemaVersion=1; gameDirectory=$game; packageSource=$manifest.sourceCommit; installedUtc=[DateTime]::UtcNow.ToString('o'); previousProxy=$proxyProvenance; runtime=$records; preserved=$preserved; seeded=@(); status='prepared'}
$receiptPath = Join-Path $backup 'receipt.json'
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
$changed = @()
try {
    Assert-Closed
    foreach ($item in $records) {
        $destination = Join-Path $game $item.name
        # Register first so a partially failed copy is restored as well.
        $changed += $item
        Copy-Item -LiteralPath (Join-Path $package $item.name) -Destination $destination -Force
        if ((Hash $destination) -ne $item.installedSha256) { throw "Installed hash mismatch: $($item.name)" }
    }
    foreach ($name in $seed) {
        $destination = Join-Path $game $name
        if (-not (Test-Path -LiteralPath $destination)) {
            Copy-Item -LiteralPath (Join-Path $package $name) -Destination $destination
            $receipt.seeded += $name
        }
    }
    Assert-Preserved
    $receipt.status = 'installed'
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
} catch {
    $failure = $_
    $rollbackErrors = @()
    foreach ($item in $changed) {
        try {
            $destination = Join-Path $game $item.name
            if ($item.existed) { Copy-Item -LiteralPath (Join-Path $backup $item.name) -Destination $destination -Force }
            elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
        } catch { $rollbackErrors += $_.Exception.Message }
    }
    $receipt.status = 'failed'; $receipt.rollbackErrors = $rollbackErrors
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    throw "Install failed: $failure. Backup: $backup. Rollback errors: $($rollbackErrors -join '; ')"
}
Write-Host "Installed OutRun wheel settings in $game"
Write-Host "Verified both runtime hashes; retained $($preserved.Count) existing settings files."
Write-Host "Backup: $backup"
Write-Host 'Next: start the game normally, press F6, open Setup. F8 stops FFB. This installer has not started the game or enabled FFB.'
Write-Output ([pscustomobject]@{GameDirectory=$game; BackupDirectory=$backup; Receipt=$receiptPath; SourceCommit=$manifest.sourceCommit})
