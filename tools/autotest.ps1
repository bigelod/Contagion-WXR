#requires -Version 5.1
<#
.SYNOPSIS
    Launch Contagion, drive the menus to a solo game, capture what the mod logged.

.DESCRIPTION
    Overnight test harness. Not shipped, not part of the mod: it exists so a run
    can be repeated without a human at the keyboard.

    "+map" on the command line does not work for this game (the launcher consumes
    it, and the engine never creates a D3D device), so the menus are driven with
    real mouse input at coordinates taken from the owner's screenshots in
    Pictures\Screenshots\Contagion\LaunchSteps:

        1_TitleScreen  HOST GAME   (118, 768)
        2_Host         SOLO        (1070, 695)
        3_Solo         HOST GAME   (118, 962)   green button
        6_Ready        Ready       (1730, 884)
        7_InGame

    Screen is 1920x1080 and the game is borderless fullscreen, so those are screen
    coordinates.

.EXAMPLE
    .\autotest.ps1 -RunSeconds 30 -Shot
#>
[CmdletBinding()]
param(
    [int]$RunSeconds = 30,
    [switch]$Shot,
    [switch]$NoSender,
    [switch]$KeepRunning,
    [switch]$NoMenu,
    [int]$BootSeconds = 14,
    [int]$LoadSeconds = 30,
    [int]$Sync = -1
)

$ErrorActionPreference = 'Stop'
$here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }

$game = 'E:\Steam\steamapps\common\Contagion'

# The mod and the launcher write to one fixed folder so the PC and the headset
# agree; if D: is missing the mod falls back beside the game, so look there.
$logDir = 'D:\ContagionWXR\Logs'
if (-not (Test-Path $logDir)) { $logDir = $game }
$launcher = Join-Path $game 'ContagionWXR.exe'
$fake = Join-Path $here 'fakesender.ps1'
$ps = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'

if (-not (Test-Path $launcher)) { throw "launcher not found: $launcher" }

Add-Type -Namespace W -Name U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, System.UIntPtr extra);
'@

function Click([int]$x, [int]$y) {
    [void][W.U]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 80
    [W.U]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # left down
    Start-Sleep -Milliseconds 60
    [W.U]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # left up
    Start-Sleep -Milliseconds 250
    Write-Host ("  click ({0},{1})" -f $x, $y) -ForegroundColor DarkGray
}

# --- clean slate -------------------------------------------------------------
Get-Process contagion -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Remove-Item (Join-Path $game 'contagion-wxr-shot.request') -Force -ErrorAction SilentlyContinue

$before = @(Get-ChildItem $logDir -Filter 'contagion-wxr-2*.log' -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName)

# --- fake runtime ------------------------------------------------------------
$sender = $null
if (-not $NoSender) {
    $sender = Start-Process -FilePath $ps -PassThru -WindowStyle Hidden -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$fake`"",
        '-Seconds', ($RunSeconds + $BootSeconds + $LoadSeconds + 60), '-Hz', '90', '-Sync', $Sync
    )
    Start-Sleep -Milliseconds 800
}

# --- the game ----------------------------------------------------------------
$gameProc = Start-Process -FilePath $launcher -PassThru
Write-Host 'launched; waiting for the title screen' -ForegroundColor Cyan
Start-Sleep -Seconds $BootSeconds

if (-not $NoMenu) {
    Write-Host 'menus: HOST GAME -> SOLO -> HOST GAME' -ForegroundColor Cyan
    Click 118 768          # HOST GAME (left menu)
    Start-Sleep -Seconds 2
    Click 1070 695         # SOLO tile
    Start-Sleep -Seconds 3
    Click 118 962          # HOST GAME (green, solo setup)
    Write-Host "loading; waiting up to $LoadSeconds s" -ForegroundColor Cyan
    Start-Sleep -Seconds $LoadSeconds
    Click 1730 884         # Ready (character select)
    Start-Sleep -Seconds 4
}

if ($Shot) {
    'dump' | Set-Content (Join-Path $game 'contagion-wxr-shot.request') -Encoding ASCII
}

Write-Host "in game for $RunSeconds s" -ForegroundColor Cyan
Start-Sleep -Seconds $RunSeconds

# --- what is actually on screen ---------------------------------------------
# The sync pixel is a Clear of the top-left 10x10 of the back buffer, so the
# presented frame's pixel (0,0) must be (sync, 0, 0). A screen grab is used
# rather than the in-game dump because it sees exactly what was presented.
$png = Join-Path $env:TEMP 'contagion-wxr-autotest.png'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$vb = [System.Windows.Forms.SystemInformation]::VirtualScreen
$grab = New-Object System.Drawing.Bitmap $vb.Width, $vb.Height
$g = [System.Drawing.Graphics]::FromImage($grab)
$g.CopyFromScreen($vb.X, $vb.Y, 0, 0, $grab.Size)
$g.Dispose()
$grab.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$px = $grab.GetPixel(0, 0)
$grab.Dispose()
Write-Host ("`nscreen pixel (0,0) = R{0} G{1} B{2}   screenshot {3}" -f $px.R, $px.G, $px.B, $png) -ForegroundColor Cyan
if ($Sync -ge 0) {
    # The present path writes in linear space and the engine encodes to sRGB on
    # the way to the frame, so the byte on screen is not the byte sent. The
    # runtime learns this mapping (XRAPI §5) - the check just needs to know it.
    $x = ($Sync -band 0xFF) / 255.0
    if ($x -le 0.0031308) { $e = 12.92 * $x } else { $e = 1.055 * [Math]::Pow($x, 1.0 / 2.4) - 0.055 }
    $expect = [int][Math]::Round($e * 255.0)
    if ($px.R -eq $expect -and $px.G -eq 0 -and $px.B -eq 0) {
        Write-Host "SYNC PIXEL OK (red $($px.R) = sRGB($Sync))" -ForegroundColor Green
    } else {
        Write-Host "SYNC PIXEL MISMATCH (expected R$expect G0 B0 = sRGB($Sync), got R$($px.R) G$($px.G) B$($px.B))" -ForegroundColor Red
    }
}

# --- stop --------------------------------------------------------------------
if (-not $KeepRunning) {
    Get-Process contagion -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 600
}
if ($sender) { Stop-Process -Id $sender.Id -Force -ErrorAction SilentlyContinue }

# --- report ------------------------------------------------------------------
$new = @(Get-ChildItem $logDir -Filter 'contagion-wxr-2*.log' -ErrorAction SilentlyContinue |
         Where-Object { $before -notcontains $_.FullName } |
         Sort-Object LastWriteTime -Descending)
if ($new.Count -eq 0) { Write-Host 'no game log produced' -ForegroundColor Red; exit 1 }

Write-Host "`n== $($new[0].Name)" -ForegroundColor Cyan
Select-String -Path $new[0].FullName -Pattern 'd3d9|built |xr first|state ' |
    Select-Object -ExpandProperty Line

Write-Host "`n== tail" -ForegroundColor Cyan
Get-Content $new[0].FullName | Select-Object -Last 10

$bmp = Join-Path $game 'contagion-wxr-shot.bmp'
if (Test-Path $bmp) {
    $b = Get-Item $bmp
    Write-Host ("`n== shot: {0} bytes, {1}" -f $b.Length, $b.LastWriteTime) -ForegroundColor Cyan
} else {
    Write-Host "`n== shot: none" -ForegroundColor Yellow
}
