[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$installer = Join-Path $root 'tools/Install-WheelSettings.ps1'
$fixture = Join-Path $root ('build/install-fixture-' + [guid]::NewGuid().ToString('N'))
$game = Join-Path $fixture 'game'
$package = Join-Path $fixture 'package'
New-Item -ItemType Directory -Path $game,$package -Force | Out-Null
function Check($condition, $message) { if (-not $condition) { throw $message } }
function Hash($path) { (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
$exe = [byte[]]::new(128)
$exe[0]=0x4d; $exe[1]=0x5a; $exe[0x3c]=0x60
$exe[0x60]=0x50; $exe[0x61]=0x45; $exe[0x64]=0x4c; $exe[0x65]=0x01
[IO.File]::WriteAllBytes((Join-Path $game 'OR2006C2C.EXE'), $exe)
foreach ($name in 'dinput8.dll','WheelFfb.dll','OutRun2006Tweaks.ini','OutRun2006Tweaks.lods.ini','force-profiles.ini') {
    [IO.File]::WriteAllText((Join-Path $package $name), "new fixture $name")
}
$files = @(Get-ChildItem -LiteralPath $package -File | ForEach-Object { @{name=$_.Name;sha256=(Hash $_.FullName)} })
@{schemaVersion=1;architecture='x86';sourceCommit='fixture-only';files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'package-manifest.json')
[IO.File]::WriteAllText((Join-Path $game 'dinput8.dll'), 'old proxy')
[IO.File]::WriteAllText((Join-Path $game 'OutRun2006Tweaks.ini'), "; owner tune`r`n[FFB]`r`nDirectInputFFB=false`r`nUnknown=retain`r`n")
[IO.File]::WriteAllText((Join-Path $game 'OutRun2006Tweaks.bindings.ini'), 'owner bindings')
$originalConfig = Hash (Join-Path $game 'OutRun2006Tweaks.ini')
$originalBindings = Hash (Join-Path $game 'OutRun2006Tweaks.bindings.ini')
$oldProxy = Hash (Join-Path $game 'dinput8.dll')
$failed = $false
try { & $installer -GameDirectory $game -PackageDirectory $package | Out-Null }
catch { $failed = $_.Exception.Message -like 'The existing dinput8.dll*' }
Check $failed 'Unknown proxy must not be overwritten silently'
$result = & $installer -GameDirectory $game -PackageDirectory $package -AllowUnknownProxy
Check ((Hash (Join-Path $game 'dinput8.dll')) -eq (Hash (Join-Path $package 'dinput8.dll'))) 'Proxy not installed'
Check ((Hash (Join-Path $game 'OutRun2006Tweaks.ini')) -eq $originalConfig) 'Owner config changed'
Check ((Hash (Join-Path $game 'OutRun2006Tweaks.bindings.ini')) -eq $originalBindings) 'Owner bindings changed'
Check (Test-Path -LiteralPath (Join-Path $game 'force-profiles.ini')) 'Fresh profile not seeded'
Check ((Hash (Join-Path $result.BackupDirectory 'dinput8.dll')) -eq $oldProxy) 'Backup not verified'
& $installer -Action Restore -GameDirectory $game -BackupDirectory $result.BackupDirectory
Check ((Hash (Join-Path $game 'dinput8.dll')) -eq $oldProxy) 'Old proxy not restored'
Check (-not (Test-Path -LiteralPath (Join-Path $game 'WheelFfb.dll'))) 'Added DLL not removed by restore'
Check ((Hash (Join-Path $game 'OutRun2006Tweaks.ini')) -eq $originalConfig) 'Restore changed owner config'
# A locked second runtime causes the first replacement to roll back.
[IO.File]::WriteAllText((Join-Path $game 'WheelFfb.dll'), 'old output')
$oldOutput = Hash (Join-Path $game 'WheelFfb.dll')
$lock = [IO.File]::Open((Join-Path $game 'WheelFfb.dll'), 'Open', 'Read', 'Read')
$failed = $false
try { & $installer -GameDirectory $game -PackageDirectory $package -AllowUnknownProxy | Out-Null }
catch { $failed = $_.Exception.Message -like 'Install failed:*' }
finally { $lock.Dispose() }
Check $failed 'Locked runtime should reject update'
Check ((Hash (Join-Path $game 'dinput8.dll')) -eq $oldProxy) 'First replacement was not rolled back'
Check ((Hash (Join-Path $game 'WheelFfb.dll')) -eq $oldOutput) 'Locked runtime changed'
[IO.File]::AppendAllText((Join-Path $package 'dinput8.dll'), 'tamper')
$failed = $false
try { & $installer -GameDirectory $game -PackageDirectory $package -AllowUnknownProxy | Out-Null }
catch { $failed = $_.Exception.Message -like 'Package hash mismatch:*' }
Check $failed 'Corrupt package must be rejected'
Check ((Hash (Join-Path $game 'dinput8.dll')) -eq $oldProxy) 'Corrupt package changed target'
Write-Host "PASS: package validation, existing config/binding retention, seeded defaults, verified backup, restore, locked-file rollback, corrupt-package refusal. Synthetic x86 EXE only; no game/device activity. Evidence: $fixture"
