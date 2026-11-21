#requires -Version 5.1
<#
.SYNOPSIS
    Exercise the XrApi client on the PC: fake sender + a 32-bit host process.

.DESCRIPTION
    Starts tools\fakesender.ps1, loads the built DLL into a throwaway 32-bit host
    via tools\smoketest.ps1, then prints the XrApi lines from the newest log.

    This is the desktop pass from PLAN.md: it proves the code path runs. It does
    not prove the values are right - that needs a real device capture.
#>
[CmdletBinding()]
param(
    [int]$Seconds = 12,
    [int]$WaitSeconds = 10
)

$ErrorActionPreference = 'Stop'
$here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }

$fake = Join-Path $here 'fakesender.ps1'
$smoke = Join-Path $here 'smoketest.ps1'
$ps = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'

$before = @(Get-ChildItem (Join-Path $env:TEMP 'cwxr-smoke-logs') -Filter 'contagion-wxr-2*.log' -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName)

$senderJob = Start-Job -ScriptBlock {
    param($fake, $seconds)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $fake -Seconds $seconds
} -ArgumentList $fake, $Seconds

Start-Sleep -Milliseconds 800

& $ps -NoProfile -ExecutionPolicy Bypass -File $smoke -WaitSeconds $WaitSeconds | Out-Null
$smokeExit = $LASTEXITCODE

# Let the sender finish, then collect its output so a silent failure is visible.
$null = Wait-Job -Job $senderJob -Timeout 30
$senderOut = Receive-Job -Job $senderJob 2>&1
Remove-Job -Job $senderJob -Force
if ($senderOut) { Write-Host ('sender: ' + ($senderOut -join ' ')) -ForegroundColor DarkGray }

$new = @(Get-ChildItem (Join-Path $env:TEMP 'cwxr-smoke-logs') -Filter 'contagion-wxr-2*.log' -ErrorAction SilentlyContinue |
         Where-Object { $before -notcontains $_.FullName } |
         Sort-Object LastWriteTime -Descending)
if ($new.Count -eq 0) { Write-Host 'no log produced' -ForegroundColor Red; exit 3 }

Write-Host "== $($new[0].FullName)" -ForegroundColor Cyan
$lines = Get-Content $new[0].FullName

$lines | Where-Object { $_ -match 'xr |headset ' } | ForEach-Object { Write-Host $_ }

# Assertions worth automating
$ok = $true
if (-not ($lines | Where-Object { $_ -match 'xr\s+:\s+dir=' }))      { Write-Host 'FAIL: no handshake line' -ForegroundColor Red; $ok = $false }
if (-not ($lines | Where-Object { $_ -match 'xr\s+:\s+listening on 7872' })) { Write-Host 'FAIL: did not bind 7872' -ForegroundColor Red; $ok = $false }
if (-not ($lines | Where-Object { $_ -match 'xr first pkt' }))       { Write-Host 'FAIL: no packet parsed' -ForegroundColor Red; $ok = $false }
if (-not ($lines | Where-Object { $_ -match 'rx=\d+' }))             { Write-Host 'FAIL: no stats line' -ForegroundColor Red; $ok = $false }
if ($lines | Where-Object { $_ -match 'rejected=[1-9]' })            { Write-Host 'FAIL: packets were rejected' -ForegroundColor Red; $ok = $false }

if (-not $ok) { exit 1 }
Write-Host "`nXrApi desktop pass OK (smoke host exit $smokeExit)" -ForegroundColor Green
exit 0
