[CmdletBinding()]
param()
# STD-033 rig-profile controls capability: the packager declares it only for an exact reviewed runtime commit, and the
# installer writes, withdraws, rolls back and restores the game-folder receipt with the runtime it describes. Synthetic
# x86 EXE and packages only; no game or device activity.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$installer = Join-Path $root 'tools/Install-WheelSettings.ps1'
. (Join-Path $root 'tools/ControlsCapability.ps1')
$fixture = Join-Path $root ('build/capability-fixture-' + [guid]::NewGuid().ToString('N'))
$game = Join-Path $fixture 'game'
New-Item -ItemType Directory -Path $game -Force | Out-Null
$checks = 0
function Check($condition, $message) { $script:checks++; if (-not $condition) { throw $message } }
function Hash($path) { (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
$receiptPath = Join-Path $game 'dbce-outrun2006-controls.json'

# The policy: exact commits only.
$policy = Join-Path $fixture 'policy.json'
@{schemaVersion=1; controlsProfileSchema=1; adapter='outrun-remap-1'; reviewedRuntimeCommits=@('reviewed-commit')} | ConvertTo-Json | Set-Content -LiteralPath $policy
$cap = Get-ControlsCapability 'reviewed-commit' $policy
Check ($cap.controlsProfileSchema -eq 1 -and $cap.controlsAdapter -eq 'outrun-remap-1') 'Reviewed commit declares the capability'
Check ($null -eq (Get-ControlsCapability 'other-commit' $policy)) 'Unreviewed commit declares nothing'
Check ($null -eq (Get-ControlsCapability 'reviewed' $policy)) 'A prefix is not the commit'
Check ($null -eq (Get-ControlsCapability '' $policy)) 'No commit declares nothing'
Check ($null -eq (Get-ControlsCapability 'any' (Join-Path $root 'tools/controls-capability.json'))) 'The repository policy lists no unreviewed commit'
# Runtime-tree identity in the real repository: a later documentation/policy commit keeps the capability, a commit with
# other runtime sources does not.
$repoPolicy = Join-Path $root 'tools/controls-capability.json'
$reviewedCommits = @((Get-Content -LiteralPath $repoPolicy -Raw | ConvertFrom-Json).reviewedRuntimeCommits)
if ($reviewedCommits.Count) {
    $head = (& git -C $root rev-parse HEAD).Trim()
    $same = $false
    foreach ($reviewedCommit in $reviewedCommits) {
        & git -C $root diff --quiet $reviewedCommit $head -- src external lib cmake.toml CMakeLists.txt cmkr.cmake
        if ($LASTEXITCODE -eq 0) { $same = $true }
    }
    Check ([bool](Get-ControlsCapability $head $repoPolicy $root) -eq $same) 'HEAD qualifies exactly when its runtime sources equal a reviewed commit''s'
    Check ($null -eq (Get-ControlsCapability (& git -C $root rev-parse eb604ad).Trim() $repoPolicy $root)) 'An earlier runtime (eb604ad) does not qualify'
    if ($reviewedCommits -notcontains $head) { Check ($null -eq (Get-ControlsCapability $head $repoPolicy)) 'Without the repository only exact listed commits qualify' }
}
@{schemaVersion=2; reviewedRuntimeCommits=@('reviewed-commit')} | ConvertTo-Json | Set-Content -LiteralPath $policy
$failed = $false
try { Get-ControlsCapability 'reviewed-commit' $policy | Out-Null } catch { $failed = $_.Exception.Message -like 'Unsupported controls capability policy*' }
Check $failed 'Future policy schema refused'

# Synthetic game and packages.
$exe = [byte[]]::new(128)
$exe[0]=0x4d; $exe[1]=0x5a; $exe[0x3c]=0x60
$exe[0x60]=0x50; $exe[0x61]=0x45; $exe[0x64]=0x4c; $exe[0x65]=0x01
[IO.File]::WriteAllBytes((Join-Path $game 'OR2006C2C.EXE'), $exe)
[IO.File]::WriteAllText((Join-Path $game 'dinput8.dll'), 'old proxy')
[IO.File]::WriteAllText((Join-Path $game 'OutRun2006Tweaks.user.ini'), "[DirectInput]`r`nUseDirectInputRemap = true`r`n")
$owner = Hash (Join-Path $game 'OutRun2006Tweaks.user.ini')
function New-Package([string]$Name, [bool]$Capable, [string]$Flavour) {
    $package = Join-Path $fixture $Name
    New-Item -ItemType Directory -Path $package -Force | Out-Null
    foreach ($file in 'dinput8.dll','WheelFfb.dll','OutRun2006Tweaks.ini','OutRun2006Tweaks.lods.ini','force-profiles.ini') {
        [IO.File]::WriteAllText((Join-Path $package $file), "$Flavour $file")
    }
    $files = @(Get-ChildItem -LiteralPath $package -File | ForEach-Object { @{name=$_.Name; sha256=(Hash $_.FullName)} })
    $manifest = [ordered]@{schemaVersion=1; architecture='x86'; sourceCommit="$Flavour-commit"; runtimeSourceCommit="$Flavour-commit"; files=$files}
    if ($Capable) { $manifest.controlsProfileSchema = 1; $manifest.controlsAdapter = 'outrun-remap-1' }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'package-manifest.json')
    $package
}
$plain = New-Package 'plain' $false 'plain'
$capable = New-Package 'capable' $true 'capable'
$capable2 = New-Package 'capable2' $true 'capable2'

# No capability declared: no receipt.
$r1 = & $installer -GameDirectory $game -PackageDirectory $plain -AllowUnknownProxy
Check (-not (Test-Path -LiteralPath $receiptPath)) 'A package without the capability writes no receipt'
& $installer -Action Restore -GameDirectory $game -BackupDirectory $r1.BackupDirectory

# Declared: the receipt names exactly the installed runtime; restore withdraws it.
$r2 = & $installer -GameDirectory $game -PackageDirectory $capable -AllowUnknownProxy
$receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
Check ($receipt.controlsProfileSchema -eq 1 -and $receipt.adapter -eq 'outrun-remap-1' -and $receipt.file -eq 'dinput8.dll') 'Receipt declares the adapter'
Check ($receipt.sha256 -eq (Hash (Join-Path $game 'dinput8.dll')) -and $receipt.wheelFfbSha256 -eq (Hash (Join-Path $game 'WheelFfb.dll'))) 'Receipt hashes are the installed runtime'
Check ($receipt.runtimeSourceCommit -eq 'capable-commit' -and $receipt.profileSection -eq 'WheelkitProfile' -and $receipt.backupDirectory -eq $r2.BackupDirectory) 'Receipt source, section and backup'
Check ((Hash (Join-Path $game 'OutRun2006Tweaks.user.ini')) -eq $owner) 'Owner user INI untouched'
$backupReceipt = Get-Content -LiteralPath (Join-Path $r2.BackupDirectory 'receipt.json') -Raw | ConvertFrom-Json
Check ($backupReceipt.capability.existed -eq $false -and $backupReceipt.capability.written -eq (Hash $receiptPath)) 'Backup records the receipt it wrote'
& $installer -Action Restore -GameDirectory $game -BackupDirectory $r2.BackupDirectory
Check (-not (Test-Path -LiteralPath $receiptPath)) 'Restore withdraws a receipt that did not exist before'
Check ((Get-Content -LiteralPath (Join-Path $game 'dinput8.dll') -Raw) -eq 'old proxy') 'Restore puts the old proxy back'

# A later package without the capability withdraws it; restoring that install brings the exact receipt back.
$r3 = & $installer -GameDirectory $game -PackageDirectory $capable -AllowUnknownProxy
$written = [IO.File]::ReadAllBytes($receiptPath)
$r4 = & $installer -GameDirectory $game -PackageDirectory $plain -AllowUnknownProxy
Check (-not (Test-Path -LiteralPath $receiptPath)) 'Installing a runtime without the capability withdraws the receipt'
Check ((Hash (Join-Path $r4.BackupDirectory 'dbce-outrun2006-controls.json')) -eq (Get-FileHash -InputStream ([IO.MemoryStream]::new($written))).Hash.ToLowerInvariant()) 'Withdrawn receipt backed up'
& $installer -Action Restore -GameDirectory $game -BackupDirectory $r4.BackupDirectory
Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($receiptPath)) -eq [Convert]::ToBase64String($written)) 'Restore brings the receipt back byte for byte'

# A newer capable runtime replaces the receipt; a failed install rolls the receipt back with the runtime.
$r5 = & $installer -GameDirectory $game -PackageDirectory $capable2 -AllowUnknownProxy
Check ((Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json).runtimeSourceCommit -eq 'capable2-commit') 'Receipt follows the new runtime'
$before = [IO.File]::ReadAllBytes($receiptPath)
$lock = [IO.File]::Open((Join-Path $game 'WheelFfb.dll'), 'Open', 'Read', 'Read')
$failed = $false
try { & $installer -GameDirectory $game -PackageDirectory $plain -AllowUnknownProxy | Out-Null } catch { $failed = $_.Exception.Message -like 'Install failed:*' } finally { $lock.Dispose() }
Check $failed 'Locked runtime fails the install'
Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($receiptPath)) -eq [Convert]::ToBase64String($before)) 'Failed install leaves the receipt as it was'
Check ((Get-Content -LiteralPath (Join-Path $game 'dinput8.dll') -Raw) -eq 'capable2 dinput8.dll') 'Failed install rolls the proxy back'

# A corrupt package is refused before any write, receipt included.
[IO.File]::AppendAllText((Join-Path $plain 'dinput8.dll'), 'tamper')
$failed = $false
try { & $installer -GameDirectory $game -PackageDirectory $plain -AllowUnknownProxy | Out-Null } catch { $failed = $_.Exception.Message -like 'Package hash mismatch:*' }
Check $failed 'Corrupt package refused'
Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($receiptPath)) -eq [Convert]::ToBase64String($before)) 'Refused package leaves the receipt'
Check ((Hash (Join-Path $game 'OutRun2006Tweaks.user.ini')) -eq $owner) 'Owner user INI untouched throughout'
Remove-Item -LiteralPath $fixture -Recurse -Force
Write-Host "PASS: $checks controls capability checks (exact reviewed commit policy; receipt written, withdrawn, rolled back and restored with its runtime). Synthetic only."
exit 0
