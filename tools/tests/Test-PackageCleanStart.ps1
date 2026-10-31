# Clean start from real package zips, as a player would (clean-install Phase A, file level): a stand-in game folder that
# never had the mod and already holds the player's own files; install with the packaged installer under Windows
# PowerShell 5.1, verify, restore (the installer's undo), reinstall an older package, update, roll back, restore again.
# Restore removes the runtime and the Wheelkit receipt but keeps settings by design, so after it the folder must equal
# the starting folder plus exactly the files the receipt lists as seeded. The player's files never change.
# Inert files only: the stand-in OR2006C2C.EXE is a bare 32-bit PE header; nothing is loaded or launched.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Zip, [string]$OlderZip = '')
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('dbce-outrun-clean-start-' + [guid]::NewGuid().ToString('N'))
$failures = 0
function Check($Condition, [string]$Message) { if (-not $Condition) { $script:failures++; "FAIL $Message" } else { "ok   $Message" } }
$ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$receiptName = 'dbce-outrun2006-controls.json'
function Hash($p) { (Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant() }
function Expand([string]$From, [string]$Name) {
    $to = Join-Path $root $Name
    Expand-Archive -LiteralPath $From -DestinationPath $to
    $m = Get-ChildItem -LiteralPath $to -Recurse -Filter 'package-manifest.json' | Select-Object -First 1
    $meta = Get-Content $m.FullName -Raw | ConvertFrom-Json
    $installer = Get-ChildItem -LiteralPath $m.DirectoryName -Filter '*.ps1' | Where-Object Name -in 'Install.ps1', 'Install-WheelSettings.ps1' | Select-Object -First 1
    [pscustomobject]@{ Dir = $m.DirectoryName; Installer = $installer.FullName; Meta = $meta
                       Dll = ($meta.files | Where-Object name -eq 'dinput8.dll').sha256
                       Capable = ($meta.controlsProfileSchema -eq 1 -and $meta.controlsAdapter -eq 'outrun-remap-1') }
}
function Run($Package, [string[]]$Options) { & $ps -NoProfile -ExecutionPolicy Bypass -File $Package.Installer -GameDirectory $game @Options *> $null; $LASTEXITCODE -eq 0 }
function Install($Package) { Run $Package @('-PackageDirectory', $Package.Dir) }
function Latest { (Get-ChildItem -LiteralPath (Join-Path $game '.wheel-settings-backups') -Directory | Sort-Object Name | Select-Object -Last 1).FullName }
function Restore($Package, [string]$Backup) { Run $Package @('-Action', 'Restore', '-BackupDirectory', $Backup) }
# Every file except the installer's kept backups (retained by design for rollback), by relative path and hash.
function Tree([string[]]$Without = @()) {
    @(Get-ChildItem -LiteralPath $game -Recurse -File | Where-Object { $_.FullName -notmatch '\\\.wheel-settings-backups\\' } |
      Where-Object { $_.FullName.Substring($game.Length + 1) -notin $Without } |
      ForEach-Object { $_.FullName.Substring($game.Length + 1) + '=' + (Hash $_.FullName) } | Sort-Object) -join "`n"
}
function Installed($Package) {
    $dll = Join-Path $game 'dinput8.dll'; $r = Join-Path $game $receiptName
    $ok = (Test-Path $dll) -and (Hash $dll) -eq $Package.Dll
    if ($Package.Capable) { $ok -and (Test-Path $r) -and (Get-Content $r -Raw | ConvertFrom-Json).sha256 -eq $Package.Dll } else { $ok -and -not (Test-Path $r) }
}
function Seeded([string]$Backup) { @((Get-Content (Join-Path $Backup 'receipt.json') -Raw | ConvertFrom-Json).seeded) }
try {
    $cur = Expand $Zip 'current'
    $game = Join-Path $root 'OutRun2006 Coast 2 Coast'
    $null = New-Item -ItemType Directory -Path $game, (Join-Path $game 'Save')
    # A bare x86 PE header: the installer checks the game is the 32-bit executable, nothing more.
    $pe = [byte[]]::new(0x48); $pe[0] = 0x4d; $pe[1] = 0x5a; $pe[0x3c] = 0x40; $pe[0x40] = 0x50; $pe[0x41] = 0x45; $pe[0x44] = 0x4c; $pe[0x45] = 0x01
    [IO.File]::WriteAllBytes((Join-Path $game 'OR2006C2C.EXE'), $pe)
    # The player's own files: their mod settings and a save, which no step may change.
    [IO.File]::WriteAllText((Join-Path $game 'OutRun2006Tweaks.user.ini'), "[DirectInput]`r`nSteeringAxis = 0`r`n[WheelSettings]`r`nView = Simple`r`n")
    [IO.File]::WriteAllBytes((Join-Path $game 'Save\c2c.sav'), [byte[]](1..64))
    $player = 'OutRun2006Tweaks.user.ini', 'Save\c2c.sav' | ForEach-Object { "$_=$(Hash (Join-Path $game $_))" }
    $before = Tree
    Check ($cur.Installer -and $cur.Dll) "package $($cur.Meta.runtimeSourceCommit.Substring(0, 7)) has its installer and a manifest with the DLL hash"
    Check (Install $cur) 'install into a folder that never had the mod'
    Check (Installed $cur) 'installed dinput8.dll is the packaged one; the Wheelkit receipt matches the package''s capability'
    $first = Latest; $seeded = Seeded $first
    Check ($seeded.Count -gt 0) "default settings seeded where absent: $($seeded -join ', ')"
    Check (Restore $cur $first) 'restore (undo the install)'
    Check (-not (Test-Path (Join-Path $game 'dinput8.dll')) -and -not (Test-Path (Join-Path $game 'WheelFfb.dll')) -and -not (Test-Path (Join-Path $game $receiptName))) 'runtime and Wheelkit receipt are gone'
    Check ((Tree $seeded) -eq $before) 'the folder is the starting folder plus only the seeded settings (installer backups aside)'
    if ($OlderZip) {
        $old = Expand $OlderZip 'older'
        Check (Install $old) "install the older package $($old.Meta.sourceCommit.Substring(0, 7))"
        Check (Installed $old) 'older runtime installed exactly'
        $olderBackup = Latest
        Check (Install $cur) 'update to the current package'
        Check (Installed $cur) 'current runtime installed; receipt follows the package'
        Check (Restore $cur (Latest)) 'roll back the update'
        Check (Installed $old) 'rollback restored the older runtime and its receipt state exactly'
        Check (Restore $old $olderBackup) 'restore the older install'
        Check ((Tree $seeded) -eq $before) 'the folder is again the starting folder plus only the seeded settings'
    }
    $after = 'OutRun2006Tweaks.user.ini', 'Save\c2c.sav' | ForEach-Object { "$_=$(Hash (Join-Path $game $_))" }
    Check (($after -join ';') -eq ($player -join ';')) 'the player''s settings and save never changed'
} finally { if (Test-Path $root) { Remove-Item -LiteralPath $root -Recurse -Force } }
"$(if ($failures) { 'FAIL' } else { 'PASS' }) clean-start package lifecycle, $failures failures"
exit [int]($failures -gt 0)
