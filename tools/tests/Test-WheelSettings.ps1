[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $repo 'build\wheel-settings-fixture'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual C++ x86 build tools are required.' }
$environment = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$includes = @('src','external\imgui','external\spdlog\include','external\ModUtils','build\_deps\safetyhook-src\include','build\_deps\zydis-src\include','build\_deps\zydis-build','build\_deps\zydis-src\dependencies\zycore\include','build\_deps\zydis-build\zycore')
$sources = @('tools\tests\wheel_settings_fixture.cpp','external\imgui\imgui.cpp','external\imgui\imgui_draw.cpp','external\imgui\imgui_tables.cpp','external\imgui\imgui_widgets.cpp')
$arguments = @('/nologo','/std:c++latest','/EHsc','/MD','/D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING','/D_CRT_SECURE_NO_WARNINGS','/DDIRECTINPUT_VERSION=0x0800')
$arguments += $includes | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
$arguments += $sources | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
$arguments += '/Fe"' + (Join-Path $out 'wheel-settings-fixture.exe') + '"'
$arguments += 'user32.lib shell32.lib'
$runner = Join-Path $out 'build-fixture.cmd'
@('@echo off', ('call "' + $environment + '" >nul'), 'if errorlevel 1 exit /b %errorlevel%', ('cl ' + ($arguments -join ' ')), 'exit /b %errorlevel%') | Set-Content -LiteralPath $runner -Encoding ascii
Push-Location $out
try {
    & $runner
    if ($LASTEXITCODE -ne 0) { throw 'UI fixture compilation failed.' }
    $caseDirectory = Join-Path $out ('run-' + [guid]::NewGuid().ToString('N'))
    & (Join-Path $out 'wheel-settings-fixture.exe') $caseDirectory
    if ($LASTEXITCODE -ne 0) { throw 'UI fixture failed.' }
    Write-Host "Fixture evidence: $caseDirectory"
    $gateArguments = @('/nologo','/std:c++latest','/EHsc','/MD','/D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING','/D_CRT_SECURE_NO_WARNINGS','/DDIRECTINPUT_VERSION=0x0800','/DZYDIS_STATIC_BUILD','/DZYCORE_STATIC_BUILD')
    $gateArguments += ($includes + 'lib\toolkit\include') | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
    $gateArguments += '"' + (Join-Path $repo 'tools\tests\ffb_ui_gates_fixture.cpp') + '"'
    $gateArguments += '/Fe"' + (Join-Path $out 'ffb-ui-gates-fixture.exe') + '"'
    $gateArguments += @('build\_deps\safetyhook-build\Release\safetyhook.lib','build\_deps\zydis-build\Release\Zydis.lib','build\_deps\zydis-build\zycore\Release\Zycore.lib') | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
    $gateArguments += 'user32.lib shell32.lib ole32.lib'
    @('@echo off', ('call "' + $environment + '" >nul'), 'if errorlevel 1 exit /b %errorlevel%', ('cl ' + ($gateArguments -join ' ')), 'exit /b %errorlevel%') | Set-Content -LiteralPath $runner -Encoding ascii
    & $runner
    if ($LASTEXITCODE -ne 0) { throw 'FFB gate fixture compilation failed.' }
    & (Join-Path $out 'ffb-ui-gates-fixture.exe')
    if ($LASTEXITCODE -ne 0) { throw 'FFB gate fixture failed.' }
} finally { Pop-Location }
