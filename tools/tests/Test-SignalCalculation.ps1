[CmdletBinding()]
param([string]$DependencyRoot)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if(-not $DependencyRoot){$DependencyRoot=$repo}
$DependencyRoot=(Resolve-Path -LiteralPath $DependencyRoot).Path
$out=Join-Path $repo ('build\signal-calculation-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'x86 C++ build tools required'}
$compileArgs=@('/nologo','/std:c++latest','/EHsc','/MD','/D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING','/D_CRT_SECURE_NO_WARNINGS','/DDIRECTINPUT_VERSION=0x0800','/DZYDIS_STATIC_BUILD','/DZYCORE_STATIC_BUILD')
foreach($inc in @('src','lib\toolkit\include')){$compileArgs+='/I"'+(Join-Path $repo $inc)+'"'}
foreach($inc in @('external\imgui','external\spdlog\include','external\ModUtils','build\_deps\safetyhook-src\include','build\_deps\zydis-src\include','build\_deps\zydis-build','build\_deps\zydis-src\dependencies\zycore\include','build\_deps\zydis-build\zycore')){$compileArgs+='/I"'+(Join-Path $DependencyRoot $inc)+'"'}
$exe=Join-Path $out 'signal-calculation.exe'
$compileArgs+='"'+(Join-Path $repo 'tools\tests\signal_calculation_fixture.cpp')+'"'
$compileArgs+='/Fe"'+$exe+'"'
foreach($lib in @('build\_deps\safetyhook-build\Release\safetyhook.lib','build\_deps\zydis-build\Release\Zydis.lib','build\_deps\zydis-build\zycore\Release\Zycore.lib')){$compileArgs+='"'+(Join-Path $DependencyRoot $lib)+'"'}
$compileArgs+='user32.lib shell32.lib ole32.lib'
$runner=Join-Path $out 'build.cmd'
@('@echo off',('call "'+$vs+'\VC\Auxiliary\Build\vcvars32.bat" >nul'),'if errorlevel 1 exit /b %errorlevel%',('cl '+($compileArgs -join ' ')),'exit /b %errorlevel%')|Set-Content $runner -Encoding ascii
Push-Location $out
try {
 & $runner
 if($LASTEXITCODE -ne 0){throw 'compile failed'}
 & $exe --shared-events
 if($LASTEXITCODE -ne 0){throw 'shared event lifecycle failed'}
 $header='dbce.outrun2006.calculation-input,1,legacy-defaults,60'
 $rows=@($header)
 for($i=0;$i -lt 48;$i++){
   $gear=if($i -lt 20){2}else{3};$flags=if($i -ge 32 -and $i -le 36){4096}else{0}
   $speed=if($i -lt 32){'0.8'}else{'0.5'}
   $rows+="$i,$($i*17),$speed,0.2,0.01,15,2,$gear,$flags,180,0.8,1"
 }
 $rows+='complete,48'
 function Write-Lf($path,$lines){[IO.File]::WriteAllText($path,($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))}
 $inputPath=Join-Path $out 'synthetic.csv';Write-Lf $inputPath $rows
 $result=@(& $exe $inputPath);if($LASTEXITCODE -ne 0){throw 'valid synthetic input failed'}
 $expected=Join-Path $out 'observed.txt';Write-Lf $expected $result
 & $exe $inputPath (Join-Path $repo 'tools\tests\data\signal-legacy-v1.expected') | Out-Null
 if($LASTEXITCODE -ne 0){throw 'same-case/reset comparison failed'}
 if(-not ($result -match ',constant,-?[1-9]') -or $result[-1] -ne 'complete,48'){throw 'no nonzero observation or complete footer'}
 $mismatch=Join-Path $out 'different.txt';Write-Lf $mismatch @('different')
 & $exe $inputPath $mismatch 2>&1|Out-Null;if($LASTEXITCODE -ne 1){throw 'mismatch must exit 1'}
 $cases=@{
   version=@($header.Replace(',1,',',2,'),'complete,0')
   empty=@($header,'complete,0')
   truncated=$rows[0..48]
   count=@($header,'0,0,0.8,0.2,0.01,15,2,2,0,180,0.8,1','complete,2')
   private=@($header,'0,0,C:\private\capture,0.2,0.01,15,2,2,0,180,0.8,1','complete,1')
   nonfinite=@($header,'0,0,nan,0.2,0.01,15,2,2,0,180,0.8,1','complete,1')
   extra=@($header,'0,0,0.8,0.2,0.01,15,2,2,0,180,0.8,1,physicalOutput=true','complete,1')
   sequence=@($header,'1,0,0.8,0.2,0.01,15,2,2,0,180,0.8,1','complete,1')
   time=@($header,'0,10,0.8,0.2,0.01,15,2,2,0,180,0.8,1','1,9,0.8,0.2,0.01,15,2,2,0,180,0.8,1','complete,2')
   overflow=@($header,'0,4294967296,0.8,0.2,0.01,15,2,2,0,180,0.8,1','complete,1')
   trailing=@($header,'0,0,0.8,0.2,0.01,15,2,2,0,180,0.8,1','complete,1','extra')
 }
 foreach($case in $cases.GetEnumerator()){
   $bad=Join-Path $out ($case.Key+'.csv');Write-Lf $bad $case.Value
   & $exe $bad 2>&1|Out-Null;if($LASTEXITCODE -ne 2){throw "Invalid $($case.Key) must exit 2"}
 }
 $missingLf=Join-Path $out 'missing-lf.csv';[IO.File]::WriteAllText($missingLf,($rows -join "`n")); & $exe $missingLf 2>&1|Out-Null;if($LASTEXITCODE -ne 2){throw 'missing LF must exit 2'}
 $tooLarge=Join-Path $out 'oversize.csv';[IO.File]::WriteAllText($tooLarge,('0'*65537)); & $exe $tooLarge 2>&1|Out-Null;if($LASTEXITCODE -ne 2){throw 'file bound must exit 2'}
 $boundary=Join-Path $out 'clock-boundary.csv'
 Write-Lf $boundary @($header,'0,4294967295,0.8,0.2,0.01,15,2,2,0,180,0.8,1','1,4294967295,0.8,0.2,0.01,15,2,2,0,180,0.8,1','complete,2')
 & $exe $boundary | Out-Null;if($LASTEXITCODE -ne 0){throw 'equal ticks and max clock should be accepted'}
 $recordExe=Join-Path $out 'signal-recording.exe'
 $recordArgs=@($compileArgs | ForEach-Object {$_.Replace('signal_calculation_fixture.cpp','signal_recording_fixture.cpp').Replace('/Fe"'+$exe+'"','/Fe"'+$recordExe+'"')})
 @('@echo off',('call "'+$vs+'\VC\Auxiliary\Build\vcvars32.bat" >nul'),'if errorlevel 1 exit /b %errorlevel%',('cl '+($recordArgs -join ' ')),'exit /b %errorlevel%')|Set-Content $runner -Encoding ascii
 & $runner;if($LASTEXITCODE -ne 0){throw 'recording fixture compile failed'}
 $session=Join-Path $out 'synthetic.osig'
 & $recordExe selftest $inputPath $session;if($LASTEXITCODE -ne 0){throw 'record/read roundtrip tests failed'}
 & $recordExe replay $session;if($LASTEXITCODE -ne 0){throw 'exact file read/recalculation failed'}
 & $recordExe replay ($session+'.mismatch') 2>&1|Out-Null;if($LASTEXITCODE -ne 1){throw 'recorded expected mismatch must exit1'}
 $corrupt=Join-Path $out 'corrupt.osig';$bytes=[IO.File]::ReadAllBytes($session);$bytes[48]=$bytes[48] -bxor 1;[IO.File]::WriteAllBytes($corrupt,$bytes)
 & $recordExe replay $corrupt 2>&1|Out-Null;if($LASTEXITCODE -ne 2){throw 'corrupt file must exit2'}
 $mutedExe=Join-Path $out 'signal-muted.exe'
 $mutedArgs=@($compileArgs | ForEach-Object {$_.Replace('signal_calculation_fixture.cpp','signal_muted_fixture.cpp').Replace('/Fe"'+$exe+'"','/Fe"'+$mutedExe+'"')})
 @('@echo off',('call "'+$vs+'\VC\Auxiliary\Build\vcvars32.bat" >nul'),'if errorlevel 1 exit /b %errorlevel%',('cl '+($mutedArgs -join ' ')),'exit /b %errorlevel%')|Set-Content $runner -Encoding ascii
 & $runner;if($LASTEXITCODE -ne 0){throw 'muted fixture compile failed'}
 foreach($mode in @('legacy','invalid','empty','stop','lease','stale-lease','model','car','invalid-input','exit','mid-frame','overflow','write-failure')){& $mutedExe $mode (Join-Path $out ('muted-'+$mode));if($LASTEXITCODE -ne 0){throw "muted $mode failed"}}
 Write-Host "PASS: actual legacy calculation, constant/periodic reset repeatability, 48-frame warmup/shift/crash/water history; fixed60Hz semantics; numeric/privacy/version/bounds/truncation/order and exits 0/1/2. Memory-only output. Evidence: $out"
}finally{Pop-Location}
