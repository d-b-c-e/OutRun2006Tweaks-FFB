# Builds and runs tools/tests/triple_views_math_test.cpp (the three-view spike's rig-to-camera maths) with MSVC x86.
# Offline: no game, device or display.
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$vs = & (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe') -latest -products * -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$out = Join-Path $root 'build\triple-views-math-test'
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $out 'triple_views_math_test.exe'
$cmd = "`"$vcvars`" >nul && cd /d `"$root`" && cl /nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /utf-8 /I src tools\tests\triple_views_math_test.cpp /Fo`"$out\\`" /Fe`"$exe`""
$text = cmd /c $cmd 2>&1
$text | Select-String -Pattern ' warning | error ' | ForEach-Object { $_.Line }
if ($LASTEXITCODE) { $text | Select-Object -Last 20; throw 'triple views math test build failed' }
& $exe
if ($LASTEXITCODE) { throw 'triple views math checks failed' }
