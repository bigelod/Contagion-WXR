# DEPLOY.md — where the mod goes, and how it gets there

**2026-11-17, revised.** The owner clarified the delivery model: the headset runs
its **own Steam install** of Contagion inside WinlatorXR, no game files are
moved, and mod files reach it **manually** through a network share. Only the PC
copy is deployed by script.

This corrects the first version of this file, which wrongly treated
`E:\tmp\Q3Mods\Contagion` as a deploy target. It is a **reference copy for
comparison**, and writing mod files into it would destroy its only use.

---

## 1. The delivery path

```
   src\  --build-->  version.dll  --+
                                    |
                          Release\ContagionWXR\   (mod-only transfer set)
                                    |
             +----------------------+----------------------+
             |                                             |
      script: deploy.ps1 -Apply                  manual: network share
             |                                             |
   E:\Steam\steamapps\common\Contagion          Cx File Explorer (Quest)
        (PC test target)                                    |
                                                container file manager
                                                            |
                                            the headset's Steam install,
                                            next to contagion.exe
```

Two different mechanisms on purpose. The PC loop is fast and scripted because it
is the cheap test. The device loop is manual because the game lives inside a
container on the headset and nothing on the PC can write there.

---

## 2. `Release\ContagionWXR` — the transfer set

This folder is **exactly what gets copied to the game root**, on PC or on the
headset. Nothing else belongs in it; no subfolders, no build intermediates, no
game files. If it is not something that goes beside `contagion.exe`, it is in the
wrong place.

| File | Source | Notes |
|---|---|---|
| `version.dll` | build output, `Release\|Win32` | the proxy + mod |
| `version_o.dll` | generated from `%WINDIR%\SysWOW64\version.dll` | must be **x86**; never from `System32` |
| **`ContagionWXR.exe`** | `launcher/` build output | starts the game with `WINEDLLOVERRIDES` set, logs what it did |
| `contagion-wxr-launcher.ini` | staging config | optional launcher settings; every key has a working default |
| `contagion-wxr.ini` | `Release\` staging config | tunables editable on device without a rebuild |
| `handsfix.txt` | optional, player-created later | per `TIPS.md`, ship the override with the guess |
| `README.txt` | hand-written | what this is, how to launch, how to uninstall |

**Launch the game through `ContagionWXR.exe`, not `contagion.exe`.** It does two
jobs, and the second is why it exists now:

1. Sets `WINEDLLOVERRIDES=version=n,b` (appending to an existing value), verifies
   the mod files are present, starts the game in the right working directory, and
   logs the arguments and the exit code. On native Windows the variable is inert,
   so the same binary works in both places.
2. **Starts the game suspended and injects `version.dll` before its entry point.**
   Measured four times on 2026-11-17/18: the game creates its `IDirect3D9` before
   a `version.dll` proxy's `DllMain` runs, so a creation hook installed there
   never fires and no per-frame hook exists. Injection cannot lose that race.

   Verified in a sandbox: `LoadLibraryA` returned a module handle inside the
   suspended target, and the injected DLL had already written its log line before
   `ResumeThread` was called. `inject=0` in the ini disables it and falls back to
   the proxy; if injection fails for any reason the launcher says so, resumes, and
   the proxy still gets the mod in — later, but working.

It does not patch anything itself. It decides *when* the mod loads, which is the
one thing a proxy cannot.

It is also the natural home for an auto-updater: on the Quest the game is a
Steam install started through a Steam emulator (Goldberg), so there is no Steam
client to launch it and no natural place to check for mod updates. One entry
point that decides what should be running, and can replace it before starting,
is worth having.

The transfer set carries **no game files**. In particular: no `client.dll`, no
`engine.dll`, no `.vpk`, and **never** a `contagion\cfg\config.cfg` — a staged
copy of that would silently override the player's settings, which is the
"saved config defeats a shipped default" trap in `TIPS.md`.

`version_o.dll` is the one that will bite: a 64-bit copy produces a load failure
inside Wine whose message points somewhere else entirely. The script generates it
and asserts the PE machine type is `0x014c` before it will deploy anything.

Everything lands **at the game root, beside `contagion.exe`** — not in `engine\`,
not in `contagion\bin\`. That is where the `version.dll` load chain finds it
(`BINARY_RECON.md` §3).

```
<game root>\
  contagion.exe
  version.dll          <- ours
  version_o.dll        <- real 32-bit version.dll, renamed
  ContagionWXR.exe     <- ours (launcher)
  contagion-wxr-launcher.ini  <- ours (launcher settings)
  engine\              <- untouched
  contagion\           <- untouched
  vpks\                <- untouched
```

Logs are **not** written here. Both the launcher and the mod write to
`D:\ContagionWXR\Logs`, which they create if it does not exist; see §3 below.

Owner's decision, 2026-11-17: the ini sits at the game root, not in a `VR\`
subfolder. Keep the transfer set flat accordingly.

---

## 3. The four locations, and what each one is for

| Path | Role | Written by |
|---|---|---|
| `E:\Steam\steamapps\common\Contagion` | **PC test target** — the real Steam install, native Windows | `tools\deploy.ps1 -Apply` |
| `Release\ContagionWXR` | **transfer set** — mod-only files, staged | the build, or by hand |
| `Release\` shortcuts | owner's navigation aids | owner |
| `E:\tmp\Q3Mods\Contagion` | **reference copy — read only.** A copy of the game kept for comparison; **not** a deploy target | owner; automation must not write here |

Shortcuts in `Release\` resolve as follows **[verified]**:

| Shortcut | Resolves to |
|---|---|
| `Contagion - PC Testing - Shortcut.lnk` | `E:\Steam\steamapps\common\Contagion` |
| `Contagion - Quest - Shortcut.lnk` | `E:\tmp\Q3Mods\Contagion` |
| `contagion.exe - PC Shortcut.lnk` | `…\Contagion\contagion.exe`, working dir the install root |

On 2026-11-17 the two game copies were byte-identical: 4,176 files, same names,
same sizes, neither containing a single mod file. That makes the reference copy
useful as a baseline — and means **any difference between them is meaningful**,
so never add mod files to it.

One caveat worth settling before relying on it: if the headset's install came
from Steam separately, the two copies may be the same *product* but not
necessarily the same *build*. `tools\deploy.ps1 -CheckDrift` reports name and
size differences; that is the cheapest way to find out.

---

## 4. The manual Quest procedure

Nothing here is automated, and that is a deliberate property of the environment,
not a gap. The steps, so they are the same every time:

1. Build, and stage into `Release\ContagionWXR` (`deploy.ps1` writes
   `Release\ContagionWXR.manifest.txt` beside it with SHA-256s and the build
   stamp).
2. Copy the **contents** of `Release\ContagionWXR` to the network share.
3. On the Quest, open the share in **Cx File Explorer** and copy the files to a
   location the container can see.
4. In the container's file manager, copy them into the game root — beside
   `contagion.exe`, in the Steam library folder.
5. Launch, then read the log and check its first lines against
   `Release\ContagionWXR.manifest.txt` (see §6).

**Do not copy the folder itself**, only its contents. A `ContagionWXR\` directory
inside the game root would still work for the loader but would put the log and
the ini in the wrong place and confuse the next deploy.

**Copy-over, not merge-and-leave.** The set is small and fixed; removing the old
files first costs nothing and prevents a stale `version_o.dll` from surviving a
change of mind.

---

## 5. Handshake paths — `Z:\tmp` is the device test

Owner's decision, 2026-11-17:

```
if  Z:\tmp exists        -> handshake dir = Z:\tmp\xr      (real hardware)
else                     -> handshake dir = D:\xrtemp      (desktop test)
```

Notes that make this correct rather than merely short:

- **Test `Z:\tmp`, not `Z:\`.** `Z:` is the container's host-root mapping and is
  present on some desktop Wine setups too. `Z:\tmp` is the marker that means
  "this is the Android side", and it is a better device test than `Z:\` — which
  is what `Docs/general_wxr/XRAPI_V0_4_DESC.md` §2 suggests.
- **The test is on the parent; the files go in the child.** `Z:\tmp` is the
  test, `Z:\tmp\xr` is where `version`, `vr` and (read) `system` live.
- **`D:\xrtemp` is the whole fallback directory**, not `D:\xrtemp\xr` — it
  replaces the path, not just the leaf. It will be created on first use.
- Hardcoding `D:` follows the owner's decision; the general guidance's
  `<current drive>:/xrtemp` variant is the alternative if `D:` is ever absent.
  Do not silently change it — an ini key can override later if it ever matters.
- **On PC this path is the point of the exercise.** With `Z:\` absent, the PC
  target exercises the fallback and the device exercises the real path. If the
  handshake hardcodes `Z:\`, PC testing tests nothing and the first real test is
  on the headset.

Files in that directory, per `Docs/general_wxr/XRAPI_V0_4_DESC.md` §2:

| File | Direction | Contents |
|---|---|---|
| `version` | we write | `0.5` |
| `vr` | we write, delete on clean shutdown | `VR` |
| `system` | we read | seven lines: manufacturer, product codename, Android version, security patch, screen size, per-eye size, `BINARY_UDP` |

The runtime **wipes the directory at startup**, so write after it is up, or
re-write on a timer. This is `DEFECTS.md` D26 and it is not implemented yet.

**The desktop fallback directory is shared with the owner's other ports.**
Verified 2026-11-17: `D:\xrtemp` already contained a `version` (`0.5`), a `vr`,
and a `system` reading `Quest 3 (fake-xrapi)` — left by another port's tester.
Two consequences:

- a pre-existing `system` is **not** evidence that a runtime is running, and the
  handshake must not treat it as authoritative;
- while testing on the PC, check whether a fake XrApi is also sending UDP to
  7872/7873, because our client would happily consume it.

The mod logs which directory it chose and whether `system` was present, which is
enough to tell these apart in the log.

---

## 6. Verification

Three checks, in increasing cost.

**1. Before copying — the manifest.** `deploy.ps1` writes
`Release\ContagionWXR.manifest.txt` beside the transfer set: build stamp, file
names, sizes and SHA-256. That is the reference for the other two checks.

**2. After copying — the script, on PC.** `match=True` per file per target, or
stop. A copy that reports success without a hash comparison is a copy you debug
later.

**3. On the device — the log.** Both logs go to **`D:\ContagionWXR\Logs`**:

- the mod writes **one log per run** (`contagion-wxr-YYYYMMDD-HHMMSS.log`, keep
  the last 5; launcher logs are not counted and never deleted by the mod);
- the launcher writes `contagion-wxr-launcher-YYYYMMDD-HHMMSS.log` there too.

The folder is created if missing, including `D:\ContagionWXR`. If `D:` is
absent or unwritable the mod falls back beside the game, then to `%TEMP%`, and
the `logfile` line says where it actually landed. `CWXR_LOGDIR` overrides the
folder (the smoke tests use it to stay out of the real one).

The mod's lines, in order:

1. the build stamp (`__DATE__ __TIME__`);
2. the full path of the module that loaded;
3. **the SHA-256 of its own file**;
4. the process path;
5. the log file's own path;
6. the handshake directory it chose, and whether `Z:\tmp` was found.

Line 3 is what makes remote verification possible. The owner reads four lines,
compares the hash against the manifest printed on the PC, and knows the right
binary is running without trusting a filename or a timestamp. It costs about
thirty lines and removes the entire class of "did the copy actually take?".

**The launcher now proves attachment.** After injecting it watches the log
folder for three seconds and records one of two lines:

```
inject : mod log appeared: D:\ContagionWXR\Logs\contagion-wxr-....log - DllMain ran
inject : NO mod log in D:\ContagionWXR\Logs after 3 s - the DLL did not attach
```

A non-zero `LoadLibraryA` result alone is not proof (a module with the same base
name would also return a handle), and on 2026-11-20 the device produced exactly
that: a successful-looking injection and no mod log. The `module at 0x... =`
line names the module that was actually loaded.

**Why one file per run, not a fixed name:** NTFS file tunnelling can reuse a
deleted file's creation time and *append* the next run to the same file, which
makes line 1 an old build stamp and silently passes this check against the wrong
binary. `mirrors-edge-WXR` was bitten by exactly this and now parses the stamp
out of the file rather than assuming it (`DEFECTS.md` D40).

---

## 7. `.gitignore` traps to fix before the first commit of source

The current `.gitignore` is a generic C/C++ template. Two rules matter here; one
is already resolved.

| Rule | Collides with | Status |
|---|---|---|
| `build/` | `TOOLS.md` names `build/build-and-deploy.ps1` | **resolved** — scripts live in `tools\`, source will live in `src\`, and `build\` stays the ignored intermediate directory |
| `*.dll` | harmless for our output, but a `version_o.dll` accidentally placed in the repo is invisible to `git status` | acceptable; keep generated DLLs out of the tree by policy, not by ignore rule |
| `*.log` | our log is written into the game install, not the repo | fine |

Worth adding so a test artefact is never mistaken for source:

```
# Contagion-WXR
*.lnk
contagion-wxr-*.log
version_o.dll
ContagionWXR.manifest.txt
```

`Release/` is already ignored.

---

## 8. The tooling

**`tools/deploy.ps1`** — dry run by default; writes nothing without `-Apply`.

```
tools\deploy.ps1                 # dry run: validate the set, write nothing
tools\deploy.ps1 -Build          # build (regenerates the stamp) and stage
tools\deploy.ps1 -Apply          # deploy to the PC install and verify
tools\deploy.ps1 -Apply -Uninstall   # remove the mod's own files from the PC copy
tools\deploy.ps1 -CheckDrift     # compare the PC install with the reference copy
```

Testing tools, all runnable without a headset:

| Tool | What it does |
|---|---|
| `tools\smoketest.ps1` | loads the DLL in a 32-bit host and asserts the log's first lines |
| `tools\fakesender.ps1` | sends synthetic XrApi packets to `127.0.0.1:7872` at a chosen rate |
| `tools\test-xrapi.ps1` | fake sender + 32-bit host, then asserts the XrApi lines |
| `tools\check-exports.ps1` | fails if the proxy's export set drifts from the real DLL |

It reads only `Release\ContagionWXR`, copies only its manifest, refuses a non-x86
`version.dll`, generates `version_o.dll` from `SysWOW64`, and refuses to
overwrite a `version.dll` it did not place unless given `-Force`. It **never
writes to `E:\tmp\Q3Mods\Contagion`.**

Two guards learned the hard way on 2026-11-17:

- **A running game blocks the copy.** Windows holds a loaded image open, so
  `-Apply` now refuses while `contagion.exe` is running instead of failing
  halfway with a sharing violation.
- **"Different" is not "foreign".** The first version of the overwrite guard
  refused to replace our own older build. It now reads the target and treats a
  file containing our log-filename marker (`contagion-wxr-`) as ours, and only
  stops for something genuinely unfamiliar.

The **build** half is milestone 2. When a project file exists, the same script
gains `-Build`:

1. wipe `obj\` and the build output folder;
2. `msbuild src\ContagionWXR.vcxproj /p:Configuration=Release /p:Platform=Win32`;
3. read `__DATE__ __TIME__` out of the built DLL and **refuse to stage** if it is
   older than the newest file under `src\`;
4. stage the transfer set, then continue as above.

Step 3 is the one that has caught a real mistake in this family. Testing a stale
build as new is invisible without it and costs a whole session.

---

## 9. Open questions

1. **Is `E:\tmp\Q3Mods\Contagion` a copy of the headset's Steam install, or a
   second PC copy?** It decides whether a drift check against it tells us
   anything about the device.
2. **What is the game root on the headset** — the Steam library folder inside the
   container? Worth writing down once, because every manual deploy retraces it.
3. **Does a container-side file manager preserve file timestamps and hashes
   through the network share?** If it does not, the log's self-hash (§6) is the
   only reliable check — which is another reason to implement it early.
4. **Should the transfer set become a single archive** (`.zip`) at some point, so
   Cx File Explorer moves one file instead of five? Cheap, and it removes a class
   of partial-copy mistakes. Not needed while the set is five files that rarely
   change.
5. **Would `adb push` be worth automating after all?** `mirrors-edge-WXR` pushes
   its overlay to a configured Quest game directory with `adb`, and `adb` is
   installed on this machine (from the Unity Android SDK). The owner's manual
   network-share flow needs no extra moving parts, so this stays an option, not a
   substitution — but it is the obvious upgrade if the manual step ever becomes
   the slow part of the loop.
