#requires -Version 5.1
<#
.SYNOPSIS
    Load the built DLL into a throwaway host process and show what it logged.

.DESCRIPTION
    A cheap smoke test for milestone 2: proves the DLL loads, DllMain runs, the
    log opens, line 1 is the build stamp, and the self-hash works - without
    launching the game.

    The DLL is 32-bit, so the host must be 32-bit. This script re-executes itself
    under SysWOW64 PowerShell when started from a 64-bit shell.

    The DLL is copied to a unique name before loading. Loading it as
    "version.dll" into a host that already has version.dll mapped returns the
    existing module and DllMain never runs - a false pass.
#>
[CmdletBinding()]
param(
    [string]$Dll,
    [int]$WaitSeconds = 6
)

$ErrorActionPreference = 'Stop'

# ---- re-exec under 32-bit PowerShell if needed -------------------------------
if ([IntPtr]::Size -ne 4) {
    $ps32 = Join-Path $env:WINDIR 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Path $ps32)) { throw 'Need 32-bit PowerShell to load a 32-bit DLL.' }
    if (-not $Dll) {
        $Dll = Join-Path (Split-Path -Parent (Split-Path -Parent $PSCommandPath)) 'out\Win32\Release\version.dll'
    }
    & $ps32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Dll $Dll -WaitSeconds $WaitSeconds
    exit $LASTEXITCODE
}

if (-not $Dll) { throw 'Dll not specified' }
if (-not (Test-Path $Dll)) { throw "Not found: $Dll" }

# ---- host -------------------------------------------------------------------
$hostDir = Join-Path $env:TEMP 'cwxr-smoke'
New-Item -ItemType Directory -Force -Path $hostDir | Out-Null
$hostDll = Join-Path $hostDir 'cwxr-smoke.dll'
Copy-Item $Dll $hostDll -Force

# Keep the smoke host's log out of the real folder, but somewhere this script and
# test-xrapi.ps1 can both read after the host is gone.
$logDir = Join-Path $env:TEMP 'cwxr-smoke-logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$env:CWXR_LOGDIR = $logDir

$before = @(Get-ChildItem $logDir -Filter 'contagion-wxr-2*.log' -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName)

# Short client.dll wait: this host will never load it, and the recon dump should
# still run and say so.
$env:CWXR_CLIENT_WAIT_MS = '1200'

Add-Type -Namespace W -Name K -MemberDefinition @'
[DllImport("kernel32", CharSet=CharSet.Ansi, SetLastError=true)] public static extern IntPtr LoadLibraryA(string p);
[DllImport("kernel32", SetLastError=true)] public static extern bool FreeLibrary(IntPtr h);
'@

$h = [W.K]::LoadLibraryA($hostDll)
if ($h -eq [IntPtr]::Zero) {
    $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Write-Host ("LoadLibrary failed, Win32 error {0}" -f $err) -ForegroundColor Red
    if ($err -eq 193) { Write-Host 'Error 193 = wrong bitness for this host process.' -ForegroundColor Yellow }
    Remove-Item $hostDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 2
}

Write-Host ("loaded at 0x{0:X}" -f [int64]$h) -ForegroundColor Green
Start-Sleep -Seconds $WaitSeconds

$new = @(Get-ChildItem $logDir -Filter 'contagion-wxr-2*.log' -ErrorAction SilentlyContinue |
         Where-Object { $before -notcontains $_.FullName } |
         Sort-Object LastWriteTime -Descending)

[void][W.K]::FreeLibrary($h)
Remove-Item $hostDir -Recurse -Force -ErrorAction SilentlyContinue

if ($new.Count -eq 0) {
    Write-Host 'No log was written. Check that the install directory or TEMP is writable.' -ForegroundColor Red
    exit 3
}

Write-Host "`n== $($new[0].FullName)" -ForegroundColor Cyan
Get-Content $new[0].FullName

# Two assertions worth automating: line 1 is a build stamp, and a self-hash
# followed a module path.
$lines = Get-Content $new[0].FullName
$ok = $true
if ($lines[0] -notmatch 'built [A-Z][a-z]{2}') { Write-Host 'FAIL: line 1 is not a build stamp' -ForegroundColor Red; $ok = $false }
if (-not ($lines | Where-Object { $_ -match 'sha256\s*:\s*[0-9A-F]{64}' })) { Write-Host 'FAIL: no 64-hex self-hash' -ForegroundColor Red; $ok = $false }
if (-not ($lines | Where-Object { $_ -match 'end recon' })) { Write-Host 'WARN: recon dump did not complete' -ForegroundColor Yellow }

if ($ok) { Write-Host "`nsmoke test passed" -ForegroundColor Green; exit 0 }
exit 1
