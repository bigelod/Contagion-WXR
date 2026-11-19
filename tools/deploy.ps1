#requires -Version 5.1
<#
.SYNOPSIS
    Stage and deploy the Contagion-WXR mod files.

.DESCRIPTION
    Two jobs, deliberately separate:

      * PACKAGE  - Release\ContagionWXR is the transfer set. This validates it,
                   generates version_o.dll from the 32-bit system DLL, and writes
                   Release\ContagionWXR.manifest.txt with SHA-256s so the manual
                   copy to the headset can be verified.
      * DEPLOY   - copy that set to the PC test install, with verification.

    E:\tmp\Q3Mods\Contagion is a READ-ONLY reference copy of the game, kept for
    comparison. This script never writes to it, and refuses if asked to.

    Dry run by default. Nothing is written without -Apply.

.PARAMETER Apply
    Actually generate version_o.dll, write the manifest, and deploy.

.PARAMETER Force
    Overwrite a version.dll in a target that this script did not place.

.PARAMETER CheckDrift
    Compare the PC install against the reference copy by name and size. Informational only.

.PARAMETER NoManifest
    Skip manifest generation.

.EXAMPLE
    .\deploy.ps1
    Validate the transfer set, show the PC deploy plan, write nothing.

.EXAMPLE
    .\deploy.ps1 -Apply
    Generate version_o.dll, write the manifest, deploy to the PC install, verify.

.EXAMPLE
    .\deploy.ps1 -Apply -CheckDrift
    As above, plus a report on how the PC install differs from the reference copy.
#>
[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$Build,
    [switch]$Force,
    [switch]$CheckDrift,
    [switch]$NoManifest,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
$RepoRoot  = Split-Path -Parent $PSScriptRoot
$Stage     = Join-Path $RepoRoot 'Release\ContagionWXR'
$Manifest  = Join-Path $RepoRoot 'Release\ContagionWXR.manifest.txt'

$PcTarget        = 'E:\Steam\steamapps\common\Contagion'   # deploy target
$QuestReference  = 'E:\tmp\Q3Mods\Contagion'                # READ ONLY, never written

# The only files this script may create or overwrite in a game copy.
# Everything lands at the game root, beside contagion.exe.
$ManifestFiles = @(
    'version.dll'
    'version_o.dll'
    'ContagionWXR.exe'
    'contagion-wxr-launcher.ini'
    'contagion-wxr.ini'
    'handsfix.txt'
    'README.txt'
)

function Write-Step($t) { Write-Host "`n== $t" -ForegroundColor Cyan }
function Write-Ok($t)   { Write-Host "   $t" -ForegroundColor Green }
function Write-Info($t) { Write-Host "   $t" -ForegroundColor Gray }
function Write-Warn2($t){ Write-Host "   $t" -ForegroundColor Yellow }
function Write-Bad($t)  { Write-Host "   $t" -ForegroundColor Red }

function Get-PeMachine([string]$Path) {
    try {
        $fs = [IO.File]::OpenRead($Path)
        try {
            $br = New-Object IO.BinaryReader($fs)
            $fs.Position = 0x3C
            $peOff = $br.ReadInt32()
            $fs.Position = $peOff + 4
            return [int]$br.ReadUInt16()
        } finally { $fs.Close() }
    } catch { return 0 }
}

function Get-Sha([string]$Path) { (Get-FileHash -Path $Path -Algorithm SHA256).Hash }

function Test-IsOurDll([string]$Path) {
    # Distinguishes "an older build of ours" from "somebody else's version.dll".
    # Every build embeds the log filename format string, which is a reliable
    # marker and survives rebuilds that change the hash.
    try {
        $bytes = [IO.File]::ReadAllBytes($Path)
        $text  = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
        return $text.Contains('contagion-wxr-')
    } catch { return $false }
}

function Get-BuildStamp([string]$Path) {
    # The mod logs "built <__DATE__> <__TIME__>" on line 1. __DATE__ is "Nov 17 2026"
    # (day padded with a space), __TIME__ is "hh:mm:ss".
    try {
        $bytes = [IO.File]::ReadAllBytes($Path)
        $text  = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
        $m = [regex]::Match($text, 'built [A-Z][a-z]{2} [ 0-9]\d \d{4} \d{2}:\d{2}:\d{2}')
        if ($m.Success) { return $m.Value }
    } catch { }
    return '(no build stamp found)'
}

function Find-MSBuild {
    # Prefer the VS 2022 install: it carries the v143 toolset the project asks
    # for. The newest install on this machine is VS 18 / v145, which would fail
    # the project's PlatformToolset.
    $vsw = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vsw)) { return $null }
    $p = & $vsw -version '[17.0,18.0)' -products * -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if ($p) { return $p }
    return (& $vsw -latest -products * -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1)
}

function Invoke-Build {
    $msbuild = Find-MSBuild
    if (-not $msbuild) { Write-Bad 'MSBuild not found (vswhere). Build manually or check the VS install.'; exit 1 }
    $proj = Join-Path $RepoRoot 'src\ContagionWXR.vcxproj'
    if (-not (Test-Path $proj)) { Write-Bad "Project missing: $proj"; exit 1 }

    # Regenerate the stamp translation unit every build. Without this, an
    # incremental build that does not recompile the file holding __DATE__ leaves
    # an old stamp in a new binary - which happened on 2026-11-17 and defeats the
    # whole point of the stamp.
    $stampPath = Join-Path $RepoRoot 'src\buildstamp.cpp'
    $stampText = @'
// buildstamp.cpp - GENERATED by tools/deploy.ps1 before every build.
//
// Do not edit: your changes are overwritten.
//
// This file exists as its own translation unit so the compiler recompiles it on
// every build. With the stamp in dllmain.cpp, an incremental build that did not
// touch dllmain left the OLD stamp in a NEW binary - seen on 2026-11-17, when a
// 19:00 build reported "built 17:24:58". That is exactly the stale-build
// confusion the stamp exists to prevent, so the stamp now lives here.

extern const char kBuildStamp[];
const char kBuildStamp[] = "built " __DATE__ " " __TIME__;
'@
    Set-Content -Path $stampPath -Value $stampText -Encoding ASCII

    Write-Info "msbuild: $msbuild"
    & $msbuild $proj /p:Configuration=Release /p:Platform=Win32 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { Write-Bad "Build failed (exit $LASTEXITCODE)"; exit 1 }

    $launcherProj = Join-Path $RepoRoot 'launcher\Launcher.vcxproj'
    if (Test-Path $launcherProj) {
        & $msbuild $launcherProj /p:Configuration=Release /p:Platform=Win32 /nologo /v:minimal
        if ($LASTEXITCODE -ne 0) { Write-Bad "Launcher build failed (exit $LASTEXITCODE)"; exit 1 }
    }

    $dll = Join-Path $RepoRoot 'out\Win32\Release\version.dll'
    if (-not (Test-Path $dll)) { Write-Bad "Build produced nothing at $dll"; exit 1 }

    # Freshness: refuse to stage a binary older than the newest source file.
    # Testing a stale build as new is invisible without this (TOOLS.md).
    $newestSrc = Get-ChildItem (Join-Path $RepoRoot 'src') -Recurse -File |
                 Where-Object { $_.Extension -in '.cpp', '.h', '.def', '.vcxproj' } |
                 Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $dllTime = (Get-Item $dll).LastWriteTime
    if ($newestSrc -and $dllTime -lt $newestSrc.LastWriteTime) {
        Write-Bad ("version.dll is OLDER than {0} - refusing to stage" -f $newestSrc.Name)
        exit 1
    }

    Write-Ok ("built {0}  ({1} bytes)  {2}" -f (Split-Path $dll -Leaf), (Get-Item $dll).Length, (Get-BuildStamp $dll))

    # Stage into the transfer set.
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null
    Copy-Item $dll (Join-Path $Stage 'version.dll') -Force
    Write-Ok "staged version.dll into $Stage"

    $launcherExe = Join-Path $RepoRoot 'out\Win32\Release\ContagionWXR.exe'
    if (Test-Path $launcherExe) {
        Copy-Item $launcherExe (Join-Path $Stage 'ContagionWXR.exe') -Force
        Write-Ok ("staged ContagionWXR.exe ({0} bytes)" -f (Get-Item $launcherExe).Length)
    }

    # Ship a launcher ini if there is not one already; it only sets defaults that
    # are already the defaults, but it documents them where a player will look.
    $launcherIni = Join-Path $Stage 'contagion-wxr-launcher.ini'
    $needIni = $true
    if (Test-Path $launcherIni) {
        $existing = Get-Content $launcherIni -Raw
        if ($existing -match 'inject\s*=') { $needIni = $false }
    }
    if ($needIni) {
        $ini = @'
; ContagionWXR launcher settings. Optional: every key has a working default.
;
; exe    - the game executable, absolute or relative to this file.
;          Default: contagion.exe beside the launcher.
; args   - extra command-line arguments, e.g.  args=-novid -console
; wait   - 1 = launcher waits for the game and logs its exit code (default)
;          0 = launcher starts the game and exits
; inject - 1 = start suspended and load version.dll before the game's entry
;          point, so hooks exist before the game creates its D3D device (default)
;          0 = rely on the version.dll proxy alone
exe=contagion.exe
args=
wait=1
inject=1
'@
        Set-Content -Path $launcherIni -Value $ini -Encoding ASCII
        Write-Ok 'wrote contagion-wxr-launcher.ini (defaults)'
    }
}

# ---------------------------------------------------------------------------
# 0. Build (optional)
# ---------------------------------------------------------------------------
if ($Build) {
    Write-Step 'Build'
    Invoke-Build
}

# ---------------------------------------------------------------------------
# 0b. Uninstall (destructive, requires -Apply to actually delete)
# ---------------------------------------------------------------------------
if ($Uninstall) {
    Write-Step "Uninstall from $PcTarget"
    foreach ($name in $ManifestFiles) {
        $p = Join-Path $PcTarget $name
        if (-not (Test-Path $p)) { continue }
        if ($Apply) {
            Remove-Item $p -Force
            Write-Ok "removed $name"
        } else {
            Write-Info "would remove $name"
        }
    }
    if (-not $Apply) { Write-Warn2 'Dry run. Re-run with -Apply -Uninstall to delete.' }
    exit 0
}

# ---------------------------------------------------------------------------
# 0. Preflight
# ---------------------------------------------------------------------------
Write-Step 'Preflight'

if (-not (Test-Path $Stage)) {
    Write-Bad "Transfer set missing: $Stage"
    exit 1
}

$stageFiles = @()
foreach ($name in $ManifestFiles) {
    if (Test-Path (Join-Path $Stage $name)) { $stageFiles += $name }
}
if ($stageFiles.Count -eq 0) {
    Write-Bad "Nothing staged. $Stage contains none of: $($ManifestFiles -join ', ')"
    exit 1
}
Write-Ok "Transfer set: $Stage"
Write-Info ("  {0} file(s): {1}" -f $stageFiles.Count, ($stageFiles -join ', '))

# version.dll, if present, must be x86.
if ($stageFiles -contains 'version.dll') {
    $m = Get-PeMachine (Join-Path $Stage 'version.dll')
    if ($m -ne 0x014c) {
        Write-Bad ("version.dll is not x86 (PE machine 0x{0:X4}); refusing." -f $m)
        exit 1
    }
    Write-Ok 'version.dll is x86'
}

# version_o.dll comes from SysWOW64, never System32.
$voStage = Join-Path $Stage 'version_o.dll'
$voSys   = Join-Path $env:WINDIR 'SysWOW64\version.dll'
if (-not (Test-Path $voStage)) {
    if (-not (Test-Path $voSys)) {
        Write-Bad "Neither $voStage nor $voSys exists. A 32-bit version.dll is required."
        exit 1
    }
    if ($Apply) {
        Copy-Item $voSys $voStage -Force
        if ((Get-PeMachine $voStage) -ne 0x014c) {
            Write-Bad 'The SysWOW64 copy is not x86; refusing. (Is this a 32-bit Windows?)'
            exit 1
        }
        if (-not ($stageFiles -contains 'version_o.dll')) { $stageFiles += 'version_o.dll' }
        Write-Ok 'Generated version_o.dll from SysWOW64 (x86 verified)'
    } else {
        Write-Warn2 'version_o.dll absent; it would be generated from SysWOW64 on -Apply'
    }
}

# Refuse to touch the reference copy even if someone adds it as a target later.
if ($Stage -like "$QuestReference*") {
    Write-Bad 'The transfer set must not live inside the reference copy.'
    exit 1
}

$mode = 'DRY RUN (nothing will be written)'
if ($Apply) { $mode = 'APPLY' }
Write-Host "`nMode: $mode" -ForegroundColor Magenta

# ---------------------------------------------------------------------------
# 1. Manifest - the reference used to verify the manual copy to the headset
# ---------------------------------------------------------------------------
if (-not $NoManifest) {
    Write-Step 'Manifest'
    $lines = @()
    $lines += '# Contagion-WXR transfer set'
    $lines += '# Generated ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
    $lines += '# Copy the CONTENTS of Release\ContagionWXR to the game root, beside contagion.exe.'
    $lines += '# Then confirm the mod log''s self-hash against this file.'
    $lines += ''
    foreach ($name in $stageFiles) {
        $p = Join-Path $Stage $name
        if (-not (Test-Path $p)) { continue }
        $f = Get-Item $p
        $lines += ('{0,-20} {1,10} bytes  SHA256 {2}' -f $f.Name, $f.Length, (Get-Sha $p))
    }
    $dll = Join-Path $Stage 'version.dll'
    if (Test-Path $dll) {
        $lines += ''
        $lines += 'Build stamp: ' + (Get-BuildStamp $dll)
        $lines += 'PE machine : 0x{0:X4}' -f (Get-PeMachine $dll)
    }
    if ($Apply) {
        Set-Content -Path $Manifest -Value $lines -Encoding ASCII
        Write-Ok "Wrote $Manifest"
    } else {
        Write-Warn2 "Would write $Manifest"
        $lines | ForEach-Object { Write-Info "  $_" }
    }
}

# ---------------------------------------------------------------------------
# 2. Deploy to the PC install
# ---------------------------------------------------------------------------
Write-Step "pc  ->  $PcTarget"
$results = @()

# A loaded DLL cannot be replaced: Windows holds the image section open. Refuse
# clearly rather than failing halfway with a sharing violation.
if ($Apply) {
    $running = @(Get-Process -Name 'contagion' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Write-Bad ("contagion.exe is running (PID {0}). Close the game, then deploy." -f ($running[0].Id))
        exit 1
    }
}

if (-not (Test-Path (Join-Path $PcTarget 'contagion.exe'))) {
    Write-Bad 'contagion.exe not found there.'
    $results += [pscustomobject]@{ File = '-'; Status = 'TARGET MISSING' }
} else {
    foreach ($name in $stageFiles) {
        $src = Join-Path $Stage $name
        $dst = Join-Path $PcTarget $name
        if (-not (Test-Path $src)) { continue }

        $srcHash = Get-Sha $src
        $existed = Test-Path $dst
        $dstHash = $null
        if ($existed) { $dstHash = Get-Sha $dst }

        if ($name -eq 'version.dll' -and $existed -and $dstHash -ne $srcHash -and -not $Force -and -not (Test-IsOurDll $dst)) {
            Write-Warn2 "version.dll present, different, and not ours; -Force required (skipped)"
            $results += [pscustomobject]@{ File = $name; Status = 'SKIPPED (foreign)' }
            continue
        }
        if ($existed -and $dstHash -eq $srcHash) {
            Write-Ok "$name already matches ($($srcHash.Substring(0,16)))"
            $results += [pscustomobject]@{ File = $name; Status = 'up to date' }
            continue
        }
        if (-not $Apply) {
            $verb = 'would add'
            if ($existed) { $verb = 'would update' }
            Write-Info "$verb $name"
            $results += [pscustomobject]@{ File = $name; Status = "$verb (dry run)" }
            continue
        }

        Copy-Item $src $dst -Force
        if ((Get-Sha $dst) -eq $srcHash) {
            Write-Ok "$name copied, match=True ($($srcHash.Substring(0,16)))"
            $results += [pscustomobject]@{ File = $name; Status = 'match=True' }
        } else {
            Write-Bad "$name copied but hash differs"
            $results += [pscustomobject]@{ File = $name; Status = 'HASH MISMATCH' }
        }
    }
}

# ---------------------------------------------------------------------------
# 3. Drift check: PC install vs the read-only reference copy
# ---------------------------------------------------------------------------
if ($CheckDrift) {
    Write-Step 'Drift check (pc install vs reference copy)'
    if ((Test-Path $PcTarget) -and (Test-Path $QuestReference)) {
        $a = @{}
        Get-ChildItem $PcTarget -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
            $a[$_.FullName.Substring($PcTarget.Length + 1)] = $_.Length
        }
        $b = @{}
        Get-ChildItem $QuestReference -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
            $b[$_.FullName.Substring($QuestReference.Length + 1)] = $_.Length
        }
        $onlyPc = @($a.Keys | Where-Object { -not $b.ContainsKey($_) })
        $onlyQ  = @($b.Keys | Where-Object { -not $a.ContainsKey($_) })
        $sizeDf = @($a.Keys | Where-Object { $b.ContainsKey($_) -and $b[$_] -ne $a[$_] })

        Write-Info ("pc {0} files / reference {1} files" -f $a.Count, $b.Count)
        Write-Info ("only pc: {0}   only reference: {1}   size differs: {2}" -f `
                    $onlyPc.Count, $onlyQ.Count, $sizeDf.Count)
        $onlyPc | Select-Object -First 10 | ForEach-Object { Write-Warn2 "only pc        : $_" }
        $onlyQ  | Select-Object -First 10 | ForEach-Object { Write-Warn2 "only reference : $_" }
        $sizeDf | Select-Object -First 10 | ForEach-Object { Write-Warn2 "size differs   : $_" }
        if ($onlyPc.Count -eq 0 -and $onlyQ.Count -eq 0 -and $sizeDf.Count -eq 0) {
            Write-Ok 'The two copies agree by name and size. (Mod files will legitimately appear as "only pc".)'
        }
    } else {
        Write-Warn2 'One of the copies is missing; drift check skipped.'
    }
}

# ---------------------------------------------------------------------------
# 4. Summary
# ---------------------------------------------------------------------------
Write-Step 'Summary'
$results | Format-Table -AutoSize

$failures = @($results | Where-Object { $_.Status -eq 'HASH MISMATCH' -or $_.Status -eq 'TARGET MISSING' })
if ($failures.Count -gt 0) {
    Write-Bad "`n$($failures.Count) problem(s)."
    exit 2
}

if (-not $Apply) {
    Write-Warn2 "`nDry run complete. Re-run with -Apply to write."
} else {
    Write-Ok "`nPC deploy complete."
    Write-Info 'For the headset: copy the CONTENTS of Release\ContagionWXR to the'
    Write-Info 'network share, then into the game root via the container file manager.'
    Write-Info 'Verify with Release\ContagionWXR.manifest.txt and the log''s self-hash.'
}
