#requires -Version 5.1
<#
.SYNOPSIS
    Send synthetic XrApi 0.4 packets to 127.0.0.1:7872, so the client can be
    exercised on the PC with no headset and no runtime.

.DESCRIPTION
    The pose is the sample in Docs/general_wxr/SixDOFinatorCalibration/
    SamplePoseData.txt. HMD_SYNC advances by 12 per packet, which is the step the
    real runtime uses, so the "duplicate sync" counter stays honest. -Sync holds
    it at one value, which is what the sync-pixel check needs.

    This proves the code path runs. It does NOT prove the values are right:
    tester quaternions are often not unit length, and hand separation differs
    from a real headset (TIPS.md). The client normalises on ingest for exactly
    that reason.

.EXAMPLE
    .\fakesender.ps1 -Seconds 20
#>
[CmdletBinding()]
param(
    [int]$Seconds = 20,
    [int]$Hz = 90,
    [int]$Port = 7872,
    [double]$YawSweep = 0.0,   # 0 = static pose; >0 = sweep yaw by this many radians
    [int]$Sync = -1            # >= 0 = hold HMD_SYNC at this value (pixel checks)
)

$ErrorActionPreference = 'Stop'
$inv = [Globalization.CultureInfo]::InvariantCulture

$udp = New-Object Net.Sockets.UdpClient
$ep  = New-Object Net.IPEndPoint ([Net.IPAddress]::Parse('127.0.0.1'), $Port)

$delay = [int](1000 / [Math]::Max(1, $Hz))
$end   = (Get-Date).AddSeconds($Seconds)
$frame = 0
$sent  = 0

# Left over the shoulder, right forward, HMD upright — the committed sample.
$lq = '0.210 0.290 -0.930 0.040'
$lp = '-0.035777777 -0.229 -0.29800004'
$rq = '0.100 -0.300 0.950 -0.080'
$rp = '0.33455563 -0.240 -0.27888894'

try {
    while ((Get-Date) -lt $end) {
        $yaw = if ($YawSweep -ne 0) { [Math]::Sin($sent * 0.03) * $YawSweep / 2 } else { 0.0 }
        # Small yaw about +Y: (0, sin(a/2), 0, cos(a/2))
        $half = $yaw / 2.0
        $hq = ('{0} {1} {2} {3}' -f
            0.0.ToString('F3', $inv),
            [Math]::Sin($half).ToString('F6', $inv),
            0.0.ToString('F3', $inv),
            [Math]::Cos($half).ToString('F6', $inv))

        $f = if ($Sync -ge 0) { [string]($Sync -band 0xFF) } else { [string]::Format($inv, '{0}', ($frame -band 0xFF)) }
        $msg = "client0 $lq 0.0 0.0 $lp $rq 0.0 0.0 $rp $hq 0.037 0.006 -0.017 0.0678 99.00 103.40 $f " +
               "FFFFFFFFFFFFFFFFF" + "FF META"
        $bytes = [Text.Encoding]::ASCII.GetBytes($msg)
        [void]$udp.Send($bytes, $bytes.Length, $ep)

        $frame = ($frame + 12) -band 0xFF
        $sent++
        Start-Sleep -Milliseconds $delay
    }
} finally {
    $udp.Close()
}

Write-Host ("sent {0} packets in {1}s ({2} Hz)" -f $sent, $Seconds, $Hz) -ForegroundColor Green
