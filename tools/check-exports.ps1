#requires -Version 5.1
<#
.SYNOPSIS
    Compare the proxy's export table with the real version.dll it forwards to.

.DESCRIPTION
    Two failure modes bracket the forwarder list, and both are silent until the
    game will not start:

      * an export an importer needs that we do not provide -> load failure;
      * an export we forward that the real DLL does not provide -> call lands on
        address 0 (XOPS-WXR paid for this one).

    So compare the sets and fail on any difference. Run after building, and
    against the version_o.dll you actually ship (SysWOW64 on the build machine;
    re-run inside the container if Wine's version.dll differs).
#>
[CmdletBinding()]
param(
    [string]$Proxy,
    [string]$Target
)

$ErrorActionPreference = 'Stop'

if (-not $Proxy) {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
    $Proxy = Join-Path (Split-Path -Parent $here) 'out\Win32\Release\version.dll'
}
if (-not $Target) {
    $Target = Join-Path $env:WINDIR 'SysWOW64\version.dll'
}

function Get-Exports([string]$Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    $peOff = [BitConverter]::ToInt32($b, 0x3C)
    $optOff = $peOff + 24
    $nSec = [BitConverter]::ToUInt16($b, $peOff + 6)
    $optSize = [BitConverter]::ToUInt16($b, $peOff + 20)
    $secOff = $optOff + $optSize

    $secs = @()
    for ($i = 0; $i -lt $nSec; $i++) {
        $o = $secOff + $i * 40
        $secs += [pscustomobject]@{
            VA   = [BitConverter]::ToUInt32($b, $o + 12)
            VSize = [BitConverter]::ToUInt32($b, $o + 8)
            Raw  = [BitConverter]::ToUInt32($b, $o + 20)
        }
    }
    function R2O([uint32]$rva) {
        foreach ($s in $script:exSecs) {
            if ($rva -ge $s.VA -and $rva -lt ($s.VA + $s.VSize)) { return [int]($s.Raw + ($rva - $s.VA)) }
        }
        return -1
    }
    $script:exSecs = $secs

    $expRva = [BitConverter]::ToUInt32($b, $optOff + 96)
    if ($expRva -eq 0) { return @() }
    $eo = & R2O $expRva
    $nNames = [BitConverter]::ToUInt32($b, $eo + 24)
    $namesRva = [BitConverter]::ToUInt32($b, $eo + 32)
    $no = & R2O $namesRva

    $out = @()
    for ($i = 0; $i -lt $nNames; $i++) {
        $r = [BitConverter]::ToUInt32($b, $no + $i * 4)
        $o2 = & R2O $r
        $s = ''
        while ($b[$o2] -ne 0) { $s += [char]$b[$o2]; $o2++ }
        $out += $s
    }
    return ($out | Sort-Object -Unique)
}

foreach ($p in @($Proxy, $Target)) {
    if (-not (Test-Path $p)) { Write-Host "MISSING: $p" -ForegroundColor Red; exit 1 }
}

$proxyExports = Get-Exports $Proxy
$targetExports = Get-Exports $Target

Write-Host ("proxy  : {0}  ({1} exports)" -f $Proxy, $proxyExports.Count)
Write-Host ("target : {0}  ({1} exports)" -f $Target, $targetExports.Count)

$missing = @($targetExports | Where-Object { $proxyExports -notcontains $_ })
$extra   = @($proxyExports  | Where-Object { $targetExports -notcontains $_ })

if ($missing.Count -like 0 -and $extra.Count -eq 0) {
    Write-Host 'export sets match' -ForegroundColor Green
    exit 0
}
foreach ($m in $missing) { Write-Host "missing (importers will fail): $m" -ForegroundColor Red }
foreach ($e in $extra)   { Write-Host "extra (forward target absent): $e" -ForegroundColor Red }
exit 2
