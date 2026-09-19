[CmdletBinding()]
param([string]$PackagedDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture=Join-Path $root ('build/player installer fixture '+[guid]::NewGuid().ToString('N'))
$package=Join-Path $fixture 'package with spaces'
$game=Join-Path $fixture 'game with spaces'
$outside=Join-Path $fixture 'unrelated working directory'
New-Item -ItemType Directory -Path $package,$game,$outside -Force | Out-Null
function Check($condition,$message){if(-not $condition){throw $message}}
function Hash($path){(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
if($PackagedDirectory){
 Copy-Item -Path (Join-Path $PackagedDirectory '*') -Destination $package -Recurse
} else {
 foreach($name in 'dinput8.dll','WheelFfb.dll','OutRun2006Tweaks.ini','OutRun2006Tweaks.lods.ini','force-profiles.ini'){
  [IO.File]::WriteAllText((Join-Path $package $name),"synthetic $name")
 }
 $files=@(Get-ChildItem -LiteralPath $package -File | ForEach-Object { @{name=$_.Name;sha256=(Hash $_.FullName)} })
 @{schemaVersion=1;architecture='x86';sourceCommit='fixture-runtime';runtimeSourceCommit='fixture-runtime';installerSourceCommit='fixture-installer';files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$package/package-manifest.json"
 Copy-Item -LiteralPath "$root/tools/Install-WheelSettings.ps1" -Destination "$package/Install.ps1"
 Copy-Item -LiteralPath "$root/tools/Install-WheelSettings.bat" -Destination "$package/Install.bat"
}
$exe=[byte[]]::new(128)
$exe[0]=0x4d; $exe[1]=0x5a; $exe[0x3c]=0x60
$exe[0x60]=0x50; $exe[0x61]=0x45; $exe[0x64]=0x4c; $exe[0x65]=0x01
[IO.File]::WriteAllBytes("$game/OR2006C2C.EXE",$exe)
[IO.File]::WriteAllText("$game/OutRun2006Tweaks.ini","; owner settings`r`n[Controls]`r`nVibrationMode=0`r`n")
[IO.File]::WriteAllText("$game/OutRun2006Tweaks.bindings.ini",'owner bindings')
$config=Hash "$game/OutRun2006Tweaks.ini"
$bindings=Hash "$game/OutRun2006Tweaks.bindings.ini"
$powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$command=Join-Path $env:SystemRoot 'System32\cmd.exe'
function Invoke-Player([string]$label,[string]$executable,[string]$arguments,[string]$inputText="`r`n"){
 $start=[Diagnostics.ProcessStartInfo]::new()
 $start.FileName=$executable; $start.Arguments=$arguments; $start.WorkingDirectory=$outside
 $start.UseShellExecute=$false; $start.CreateNoWindow=$true
 # A fixture launched from PowerShell 7 inherits its Core-only module path.
 # Explorer does not; let the child build its own PS5.1 default module path.
 $start.EnvironmentVariables.Remove('PSModulePath')
 $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true; $start.RedirectStandardInput=$true
 $process=[Diagnostics.Process]::Start($start)
 $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
 $process.StandardInput.Write($inputText); $process.StandardInput.Close()
 if(-not $process.WaitForExit(30000)){throw "Player invocation did not finish: $label (no process terminated)"}
 $result=[pscustomobject]@{label=$label;executable=$executable;arguments=$arguments;workingDirectory=$outside;modulePath='child host default (PowerShell 7 inheritance removed)';exitCode=$process.ExitCode;stdout=$stdout.Result;stderr=$stderr.Result}
 $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$fixture/$label.json"
 return $result
}
$version=Invoke-Player 'powershell-version' $powershell '-NoProfile -NonInteractive -Command "$PSVersionTable.PSVersion.ToString()"'
Check ($version.stdout.Trim().StartsWith('5.1.')) 'Player route must use Windows PowerShell 5.1'
$prefix='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$package+'\Install.ps1" -GameDirectory "'+$game+'"'
$installed=Invoke-Player 'omitted-package' $powershell $prefix
Check ($installed.exitCode -eq 0) ('Omitted package failed: '+$installed.stderr)
Check ((Hash "$game/dinput8.dll") -eq (Hash "$package/dinput8.dll")) 'Default did not use script folder'
Check ((Hash "$game/OutRun2006Tweaks.ini") -eq $config -and (Hash "$game/OutRun2006Tweaks.bindings.ini") -eq $bindings) 'Owner settings changed'
$receiptPath=(Get-ChildItem -LiteralPath "$game/.wheel-settings-backups" -Filter receipt.json -Recurse | Select-Object -First 1).FullName
$receipt=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
Check ($receipt.packageRuntimeSource -and $receipt.packageInstallerSource) 'Receipt did not separate runtime/installer provenance'
$restore=Invoke-Player 'restore-omitted-package' $powershell ($prefix+' -Action Restore -BackupDirectory "'+(Split-Path -Parent $receiptPath)+'"')
Check ($restore.exitCode -eq 0 -and -not (Test-Path "$game/dinput8.dll") -and -not (Test-Path "$game/WheelFfb.dll")) 'Player restore did not restore absence'
# Launch the exact shipped batch, with no arguments, as a double-click player
# does. Redirect only the requested game-folder answer and final pause key.
$batch=Invoke-Player 'batch-prompt' $command ('/d /c call "'+$package+'\Install.bat"') ($game+"`r`n`r`n")
Check ($batch.exitCode -eq 0) ('Player batch failed: '+$batch.stderr)
Check ((Hash "$game/dinput8.dll") -eq (Hash "$package/dinput8.dll")) 'Batch did not install from its own folder'
Check ((Hash "$game/OutRun2006Tweaks.ini") -eq $config -and (Hash "$game/OutRun2006Tweaks.bindings.ini") -eq $bindings) 'Batch changed owner settings'
# An explicit package override must still win over the script directory.
$alternate=Join-Path $fixture 'explicit package override'
New-Item -ItemType Directory -Path $alternate | Out-Null
Copy-Item -Path "$package/*" -Destination $alternate -Recurse
[IO.File]::WriteAllText("$alternate/dinput8.dll",'explicit override runtime')
$manifest=Get-Content "$alternate/package-manifest.json" -Raw | ConvertFrom-Json
($manifest.files | Where-Object name -eq 'dinput8.dll').sha256=Hash "$alternate/dinput8.dll"
$manifest | ConvertTo-Json -Depth 8 | Set-Content "$alternate/package-manifest.json"
$override=Invoke-Player 'explicit-package' $powershell ($prefix+' -PackageDirectory "'+$alternate+'" -AllowUnknownProxy')
Check ($override.exitCode -eq 0 -and (Hash "$game/dinput8.dll") -eq (Hash "$alternate/dinput8.dll")) 'Explicit override not honored'
$oldProxy=Hash "$game/dinput8.dll"; $oldNative=Hash "$game/WheelFfb.dll"
# Actual child process fails on the second replacement; the first must roll back.
$lock=[IO.File]::Open("$game/WheelFfb.dll",'Open','Read','Read')
try{$rollback=Invoke-Player 'locked-rollback' $powershell ($prefix+' -AllowUnknownProxy')}
finally{$lock.Dispose()}
Check ($rollback.exitCode -ne 0 -and $rollback.stderr -like '*Install failed:*') 'Locked native file was not refused'
Check ((Hash "$game/dinput8.dll") -eq $oldProxy -and (Hash "$game/WheelFfb.dll") -eq $oldNative) 'Failed player install did not roll back'
# Corrupt package failures must also survive the wrapper's pause/exit sequence.
[IO.File]::AppendAllText("$package/dinput8.dll",'tamper')
$tamper=Invoke-Player 'batch-corrupt-refusal' $command ('/d /c call "'+$package+'\Install.bat" -GameDirectory "'+$game+'" -AllowUnknownProxy')
Check ($tamper.exitCode -ne 0 -and $tamper.stderr -like '*Package hash mismatch:*') 'Batch masked package failure'
Check ((Hash "$game/dinput8.dll") -eq $oldProxy -and (Hash "$game/WheelFfb.dll") -eq $oldNative) 'Corrupt package changed runtime'
Check ((Hash "$game/OutRun2006Tweaks.ini") -eq $config -and (Hash "$game/OutRun2006Tweaks.bindings.ini") -eq $bindings) 'Failure changed owner settings'
Write-Host "PASS: actual PS5.1 -File omitted/explicit package, different CWD and spaced paths, real batch prompt, settings retention, restore, rollback and batch failure exit. Synthetic EXE never launched. Evidence: $fixture"
