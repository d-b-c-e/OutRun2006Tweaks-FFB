<#
.SYNOPSIS
    Rebuilds a deployed OutRun2006Tweaks.ini from the shipped template, keeping
    every value the deployed file already has.

.DESCRIPTION
    A config that has been in place a while loses its comments: settings get
    appended by tools, sections get edited, and the explanations that shipped
    with it drift away. What is left is a wall of names and numbers, and nobody
    can tune force feedback from that.

    This takes the template's structure and commentary and the deployed file's
    VALUES, so you keep your tuning and get the explanations back. Anything in
    the deployed file that the template does not know about is preserved in a
    clearly marked block at the end rather than dropped.

    Nothing is written until you confirm, and the original is copied aside
    first. Line endings are preserved.

.PARAMETER Ini
    The deployed OutRun2006Tweaks.ini to rebuild.

.PARAMETER Template
    Template to take structure and comments from. Defaults to the repo's.

.PARAMETER WhatIf
    Report what would change and write nothing.

.EXAMPLE
    .\tools\Sync-IniComments.ps1 -Ini "D:\Games\OutRun2006\OutRun2006Tweaks.ini" -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)] [string]$Ini,
    [string]$Template
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Template) { $Template = Join-Path $repo 'OutRun2006Tweaks.ini' }
foreach ($f in $Ini, $Template) { if (-not (Test-Path $f)) { throw "not found: $f" } }

function Read-Values($path) {
    $map = [ordered]@{}; $sec = $null
    foreach ($line in (Get-Content $path)) {
        $t = ($line -split '[#;]', 2)[0].Trim()
        if ($t -match '^\[(.+)\]$') { $sec = $Matches[1].Trim(); continue }
        if ($sec -and $t -match '^([^=]+)=(.*)$') { $map["$sec/$($Matches[1].Trim())"] = $Matches[2].Trim() }
    }
    $map
}

$deployed = Read-Values $Ini
$fromTemplate = Read-Values $Template

$raw = [IO.File]::ReadAllText($Ini)
$nl = if ($raw -match "`r`n") { "`r`n" } else { "`n" }

# Walk the template, substituting the deployed value wherever there is one.
$out = New-Object System.Collections.Generic.List[string]
$section = $null
$kept = 0; $defaulted = 0
foreach ($line in (Get-Content $Template)) {
    $t = ($line -split '[#;]', 2)[0].Trim()
    if ($t -match '^\[(.+)\]$') { $section = $Matches[1].Trim(); $out.Add($line); continue }
    if ($section -and $t -match '^([^=]+)=(.*)$') {
        $key = $Matches[1].Trim()
        $id = "$section/$key"
        if ($deployed.Contains($id)) {
            # Keep the template's spacing around '=' so the file stays uniform.
            $sep = if ($line -match '^\s*[^=]+?(\s*=\s*)') { $Matches[1] } else { ' = ' }
            $out.Add("$key$sep$($deployed[$id])")
            $kept++
        } else {
            $out.Add($line)      # template default, with its comment
            $defaulted++
        }
        continue
    }
    $out.Add($line)
}

# Anything the deployed file had that the template does not mention.
$orphans = @($deployed.Keys | Where-Object { -not $fromTemplate.Contains($_) })
if ($orphans) {
    $out.Add('')
    $out.Add('# ---------------------------------------------------------------------------')
    $out.Add('# Settings found in your config that the shipped template does not define.')
    $out.Add('# Kept so nothing of yours is lost. They may be from an older version, or')
    $out.Add('# from another tool; tools\Check-IniCoverage.ps1 will say whether the code')
    $out.Add('# still reads them.')
    $out.Add('# ---------------------------------------------------------------------------')
    $lastSec = $null
    foreach ($o in ($orphans | Sort-Object)) {
        $parts = $o -split '/', 2
        if ($parts[0] -ne $lastSec) { $out.Add(''); $out.Add("[$($parts[0])]"); $lastSec = $parts[0] }
        $out.Add("$($parts[1]) = $($deployed[$o])")
    }
}

Write-Host ("values kept from your config : {0}" -f $kept) -ForegroundColor Green
Write-Host ("template defaults filled in  : {0}" -f $defaulted) -ForegroundColor Cyan
Write-Host ("settings preserved at the end: {0}" -f $orphans.Count) -ForegroundColor $(if ($orphans.Count) { 'Yellow' } else { 'Cyan' })
foreach ($o in $orphans) { Write-Host "    $($o -replace '/', ' -> ')" -ForegroundColor Yellow }

$text = ($out -join $nl)
if (-not $text.EndsWith($nl)) { $text += $nl }

if ($WhatIfPreference) {
    Write-Host "`nWhatIf: nothing written." -ForegroundColor Cyan
    exit 0
}
if ($PSCmdlet.ShouldProcess($Ini, 'rebuild with template comments')) {
    $backup = "$Ini.before-comments-" + (Get-Date -Format 'yyyyMMdd-HHmmss')
    Copy-Item $Ini $backup -Force
    [IO.File]::WriteAllText($Ini, $text)
    Write-Host "`nRewritten. Your previous file: $(Split-Path $backup -Leaf)" -ForegroundColor Green
}
