# Builds and runs the pure [DirectInput.Shifter] GearMode = hpattern gate fixture (src/hpattern_gate.hpp). No game,
# no dependencies beyond the Windows SDK headers.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $repo ('build\hpattern-gate-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'x86 C++ build tools required' }
$exe = Join-Path $out 'hpattern_gate_fixture.exe'
$src = Join-Path $repo 'tools\tests\hpattern_gate_fixture.cpp'
$runner = Join-Path $out 'build.cmd'
@('@echo off', ('call "' + $vs + '\VC\Auxiliary\Build\vcvars32.bat" >nul'), 'if errorlevel 1 exit /b %errorlevel%',
  ('cl /nologo /std:c++17 /EHsc /W4 "' + $src + '" /Fo"' + $out + '\\" /Fe"' + $exe + '"'), 'exit /b %errorlevel%') |
    Set-Content -LiteralPath $runner -Encoding ascii
& $runner | Out-Host
if ($LASTEXITCODE) { throw 'hpattern gate fixture build failed' }
& $exe
if ($LASTEXITCODE) { throw 'hpattern gate fixture failed' }
