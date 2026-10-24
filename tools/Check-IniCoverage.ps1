<#
.SYNOPSIS
    Checks that the shipped OutRun2006Tweaks.ini and the code agree.

.DESCRIPTION
    Two failures this catches, both of which had actually happened:

    MISSING - the code reads a setting that the shipped template does not
    contain. A user cannot configure, or even discover, something they never
    see. Four whole sections were missing this way ([DirectInput],
    [DirectInput.Shifter], [DirectInput.Aux], [Telemetry]), which hid the
    entire wheel remap layer and the telemetry output.

    DEAD - the template offers a setting the code never reads. Worse than
    useless: someone changes it, nothing happens, and they conclude the mod is
    broken.

    Run it after touching either file. Exit 0 = they agree.

.PARAMETER Ini
    Template to check. Defaults to the repo's OutRun2006Tweaks.ini. Point it at
    a deployed copy to audit an install.
#>
[CmdletBinding()]
param([string]$Ini)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Ini) { $Ini = Join-Path $repo 'OutRun2006Tweaks.ini' }
$source = Join-Path $repo 'src\dllmain.cpp'
foreach ($f in $Ini, $source) { if (-not (Test-Path $f)) { throw "not found: $f" } }

# Legacy wheel settings read directly; upstream settings register next to their hooks.
$read = @{}
foreach ($m in [regex]::Matches((Get-Content $source -Raw), 'ini\.Get\("([^"]+)",\s*"([^"]+)"')) {
    $read["$($m.Groups[1].Value)/$($m.Groups[2].Value)"] = $true
}
foreach ($file in (Get-ChildItem (Join-Path $repo 'src') -Recurse -Filter '*.cpp')) {
    foreach ($m in [regex]::Matches((Get-Content $file.FullName -Raw), 'Setting<[^>]+>\s+\w+\s*\{\s*"([^"]+)",\s*"([^"]+)"')) {
        $read["$($m.Groups[1].Value)/$($m.Groups[2].Value)"] = $true
    }
}

# What the template offers.
$have = @{}
$section = $null
foreach ($line in (Get-Content $Ini)) {
    $t = ($line -split '[#;]', 2)[0].Trim()
    if ($t -match '^\[(.+)\]$') { $section = $Matches[1].Trim(); continue }
    if ($section -and $t -match '^([^=]+)=') { $have["$section/$($Matches[1].Trim())"] = $true }
}

# CDTracks is consumed by its order-preserving parser in hooks_audio.cpp.
$exempt = @('CDTracks/*')
function Test-Exempt($key) {
    foreach ($e in $exempt) { if ($key -like $e) { return $true } }
    return $false
}

# Diagnostic interpolation knobs are intentionally hidden from the shipped template.
$internal = @('Performance/FramerateInterpolationDebugAlpha', 'Performance/FramerateInterpolationDebugLog')
$missing = @($read.Keys | Where-Object { -not $have.ContainsKey($_) -and $_ -notin $internal } | Sort-Object)
$dead    = @($have.Keys | Where-Object { -not $read.ContainsKey($_) -and -not (Test-Exempt $_) } | Sort-Object)
$exempted = @($have.Keys | Where-Object { -not $read.ContainsKey($_) -and (Test-Exempt $_) })

Write-Host ("{0} settings read by the code, {1} offered by {2}" -f $read.Count, $have.Count, (Split-Path $Ini -Leaf)) -ForegroundColor Cyan

if ($missing) {
    Write-Host "`nMISSING - read by the code, absent from the template:" -ForegroundColor Red
    foreach ($k in $missing) { Write-Host "  [$($k -replace '/', '] ')" -ForegroundColor Red }
    Write-Host "  A user cannot configure what they never see." -ForegroundColor Gray
}
if ($dead) {
    Write-Host "`nDEAD - offered by the template, never read:" -ForegroundColor Yellow
    foreach ($k in $dead) { Write-Host "  [$($k -replace '/', '] ')" -ForegroundColor Yellow }
    Write-Host "  Someone will change one of these and conclude the mod is broken." -ForegroundColor Gray
}

if ($exempted.Count) { Write-Host ("`n{0} custom-parsed setting(s) exempted." -f $exempted.Count) -ForegroundColor DarkGray }
if ($missing -or $dead) { Write-Host ""; exit 1 }
Write-Host "The template and the code agree." -ForegroundColor Green
exit 0
