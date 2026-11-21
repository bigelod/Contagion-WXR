# RUNTIME_CONFIRMATIONS.md — what the first in-game run proved

**Run 1 — 2026-11-17 18:49:37, PC copy, 1920x1080.**
Log: `E:\Steam\steamapps\common\Contagion\contagion-wxr-20261117-184937.log`

This file records what changed from *static* inference to *observed* fact. Read
it with `BINARY_RECON.md`: where the two disagree, this wins, because this was
measured in the running game.

---

## 1. The proxy loads, and the load chain is the one we predicted

```
[1] built Nov 17 2026 17:24:58
[3] sha256 : 401A58BB…5E74          <- matches the deployed manifest
[2] module : E:\…\Contagion\VERSION.dll
[4] process: E:\…\Contagion\contagion.exe
```

`VERSION.dll` (ours) and `version_o.DLL` both appear in the module list, and
`engine\crashhandler.dll` loads immediately before ours — exactly the
`engine.dll → crashhandler.dll → version.dll` chain from `BINARY_RECON.md` §3.
The vector is **proven on PC**.

`client.dll` arrived **8,000 ms after attach**. The inherited `Sleep(1200)` then
`Sleep(600)` would have hooked nothing on this machine, let alone a Quest 3. The
polling loop was the right call.

**The module is relocated.** `client.dll` loaded at `0x666F0000`, not its
preferred `0x10000000`. Every absolute VA in `Docs/MetaAI/` (`0x107B5F98`,
`0x1089D0CC`, …) is therefore **wrong to dereference directly**; only
base+offset arithmetic works, and the recon logs both. This alone would have
made a literal implementation of the Meta AI plan silently read the wrong
memory — or, worse, a mapped page belonging to something else.

---

## 2. All three anchor globals are identified — by name

Resolved from the runtime RTTI locators, then read out of the DLLs on disk.

| Global | Runtime object | Class hierarchy |
|---|---|---|
| **`0x107B5F98`** | `0x66EBF3B8` | **`CTerrorViewRender`** → `CCSViewRender` → `CViewRender` → **`IViewRender`** |
| **`0x1089D0CC`** | `0x7165EC30` (in `vgui2.dll`) | **`CInputWin32`** → `vgui::IInputInternal` → `vgui::IInput` → `IBaseInterface` |
| **`0x106B1F88`** | `0x66E12A30` | **`CWEngineSound`** → `IWEngineSound` |

Three conclusions, all now certain:

1. **`0x107B5F98` is the view renderer**, as the assignment-guard string said —
   and its class is a Contagion-specific `CCSViewRender` layered over L4D2's
   `CTerrorViewRender`, over Source's `CViewRender`. That is a third data point
   for the engine-branch question and a warning that its vtable is *not* the
   stock SDK2007 one.
2. **`0x1089D0CC` is VGUI input**, not a client mode. Six calls at index 21 are
   VGUI input methods. The Meta AI `CreateMove` hook targeted VGUI's input
   handler.
3. **`0x106B1F88` is the sound engine** — the global the inherited pattern scan
   actually resolves first. It hooks the audio system.

The Meta AI plan's two anchors and the pattern's first match were, between them,
the view renderer, VGUI input, and the sound engine. None was `g_pClientMode`.

`g_pViewRender`'s vtable was dumped with 28 entries and a valid RTTI locator, so
the camera target is **known, live, and named** — which is much better than where
this project stood yesterday.

---

## 3. Interfaces: three right, one in the wrong module

| Probe | Result | Note |
|---|---|---|
| `VClient016` in `client.dll` | **`0x66E267D8`** ✓ | the client interface, as predicted |
| `VEngineClient013` in `engine.dll` | **`0x72C24418`** ✓ | confirms the corrected version number |
| `GameUI011` in `GameUI.dll` | **`0x654843A8`** ✓ | the inherited value was right |
| `VEngineCvar007` in **`vstdlib.dll`** | **`0x72FA2108`** ✓ | registered here, not in client/engine |
| `VEngineCvar007` in `client.dll` / `engine.dll` | null | expected — wrong module |
| `VEngineRenderView013` in `client.dll` | **null** | **wrong module — see below** |
| `VGUI_Surface031` in `client.dll` | null | wrong module |
| `g_pCVar` exported from 4 modules | **`0x72FA2108`** everywhere ✓ | one ICvar, no version guessing needed |

**Correction to `BINARY_RECON.md` §4:** `VEngineRenderView013` is not registered
in `client.dll`. It is almost certainly in `engine.dll` (the engine creates the
view renderer and asks the client for it — which is exactly what the
`GetViewRenderInstance()` guard was about). The next probe adds `engine.dll`.

The `g_pCVar` result is the useful one: the same pointer is exported by
`client.dll`, `engine.dll`, `vstdlib.dll` and `GameUI.dll`, so `ICvar` is
reachable with one `GetProcAddress` and no interface version at all.

---

## 4. The window: `Valve001` exists, and the window is already borderless

```
window: class='Valve001' visible=1 client=1920x1080 style=0x14000000 ex=0x00000008 title='Contagion'
```

Two corrections to my own earlier work, both worth stating plainly:

**a. `Valve001` does exist.** `BINARY_RECON.md` §6 said it appeared in none of
the 120 shipped binaries. That scan searched **8-bit strings only**. It is
present in `engine.dll` as **UTF-16** (`V\0a\0l\0v\0e\0001\0`). The claim was
wrong, and `DEFECTS.md` D18 is withdrawn: the inherited
`strstr(cls, "Valve001")` window lookup would have found this window.

This is the second time a static shortcut produced a confident wrong answer, and
both times the remedy was the same: **run it once and read the log.** The general
lesson (TIPS.md "measure, do not infer") now has a static-analysis corollary:
*a negative result from a string scan is only as good as the encodings you
searched.*

## Run 5 — 2026-11-18 00:16:16, the adopt scan

The scan ran and found something:

```
d3d9 : adopt: scanning for a device vtable (d3d9 0x6D930000..0x6DAC2000)
d3d9 : adopt candidate vtable 0x13B55B38 run=100 patched=yes
d3d9 : adopt done, 1 vtable(s) patched
```

…and that candidate **never presented**: no `first Present`, no counters, no
screenshot. So it was a device-shaped vtable belonging to something else — the
Steam overlay's device is the obvious candidate — and the game's own device was
not in the scan.

Combined with run 4 (code hook real at `0xE9`, IAT slot replaced, `Create9Ex`
covered, no call ever), the conclusion is now solid: **the game creates its
device before this DLL is loaded.** Not a millisecond race we can win by moving
code earlier; the proxy is simply too late on this game.

**Therefore: injection.** `ContagionWXR.exe` starts the game suspended and loads
`version.dll` before its entry point. Sandbox-verified: the injected DLL logged
before `ResumeThread`, so the hook is in place before any game code runs. That
removes the entire class of problem rather than working around another symptom.

The adopt scan stays as a safety net (and now logs every run ≥40 entries, patches
≥60, accepts mapped regions too), but it should never be needed again.

**b. The window is already borderless.** `style = 0x14000000` is
`WS_VISIBLE | WS_CLIPSIBLINGS` — no `WS_CAPTION`, no `WS_THICKFRAME`, no
`WS_BORDER` — and the client area is exactly the 1920x1080 screen. So
`EnforceWindow()` has nothing to do on PC, which is the same conclusion
Interstellar Marines reached for its container. `DEFECTS.md` D17 stays open only
for the container case (1024x768) and is now lower risk than feared.

**c. Foreground was not ours.** At dump time the foreground window was
`MultitaskingViewFrame`, class not ours. So `GetForegroundWindow()` is unreliable
*even on the PC*, before any container is involved. That vindicates the
"never infer pause from window state" rule in `SIBLING_PORTS.md` §1.6 and makes
the focus half of `DEFECTS.md` D19 more important, not less.

Also present: `D3DProxyWindow` (Steam overlay host) at 1920x1080 — see §6.

---

## 5. Module inventory worth keeping

| Module | Why it matters |
|---|---|
| `gameoverlayrenderer.dll` | **Steam overlay is loaded.** It draws into the same swap chain, so it can cover the sync pixel at (0,0). Must be tested, not assumed |
| `C:\Windows\system32\version.dll` **and** our `VERSION.dll` | two modules with the same base name in one process. Something loaded the system one by full path, so not every `version.dll` caller gets our proxy |
| `engine\d3dx9_43.dll` | D3DX9 is present at run time — the voice-wheel `ID3DXLine`/`ID3DXFont` dependency costs nothing on this install |
| `vphysics_jolt_avx2.dll` | Jolt, AVX2 variant, confirmed at run time |
| `AkSoundEngineDLL.dll`, `mss32.dll`, `vaudio_miles.dll` | Wwise and Miles both loaded |
| `server.dll` | the local server runs in-process, as expected for a local round |
| `inputsystem.dll`, `DINPUT.DLL`, `XInput1_3.dll`, `xinput1_4.dll` | three input paths exist; the gamepad question in `PLAN.md` §5 is worth asking |
| `discord-rpc.dll`, `matchmaking.dll`, `mysql_wrapper.dll`, `libmariadb.dll` | extra surface that has nothing to do with VR |

---

## 6. What the static call-site scan says about the camera

Scanning every `mov ecx, [g_pViewRender]` and the following 48 bytes found 58
call sites. The indices that take **arguments** are the interesting ones:

| Index | Argument shape at the call site |
|---|---|
| 13 | one pointer, or one `-1` int (two different sites) |
| 17, 18 | **one float** each (pushed via `fstp [esp]`) — likely a "set distance" pair |
| 21 | **two pointer out-params** (three sites) |
| 23 | one arg, `0` or a register |
| 25 | one pointer |
| 26 | **three args** |
| 30 | two, and one three-arg site |
| 33 | **three args** |
| 34 | one float |

**No call site pushes a stack-built `CViewSetup*`.** A `CViewSetup` is large, so
its caller would `lea` it and push one pointer — the shape is absent. That means
`RenderView`/`Render` are **not** invoked through the `g_pViewRender` global in
`client.dll`; they are called through a cached pointer in a register or a member,
which a byte scan cannot follow.

Consequence for the plan: static scanning has taken this as far as it usefully
can. The runtime vtable probe (`PLAN.md` §3.2) is now the correct next move, and
it is cheaper than before — we know the object, its class, and that its vtable is
at `0x66CCCC9C` with at least 35 entries.

**One bug found in our own recon:** the dump printed only 28 entries and stopped,
but call sites reference indices up to 34. The loop stops at the first
unreadable dword, and the vtable evidently crosses a page boundary. The dump must
tolerate gaps and read 64 entries. Fixed for the next build.

---

## 7. Scoreboard

| Claim | Before | Now |
|---|---|---|
| The proxy loads under the real game | untested | **observed** on PC |
| Build stamp reaches the log | untested | **observed**, matches manifest |
| Self-hash works | smoke test only | **observed**, matches deployed file |
| `version.dll` chain is `crashhandler` | **[bin]** inference | **observed** in the module list |
| `0x107B5F98` is the view renderer | **[bin]**, one string | **observed + named**: `CTerrorViewRender` |
| `0x1089D0CC` is not a client mode | **[bin]** | **observed + named**: `CWin32Input` |
| `0x106B1F88` is the pattern's first match | **[bin]** | **observed + named**: `CWEngineSound` |
| Client interfaces | **[bin]** | **resolved at run time** |
| `Valve001` absent from the binaries | **[bin]** claim | **WRONG** — present as UTF-16 |
| Window needs `EnforceWindow` | assumed | **not on PC**; client == screen, no chrome |
| `GetForegroundWindow` is safe | assumed | **wrong even on PC** |
| `EndScene` fires more than once per frame | assumed from general knowledge | still unknown — next run counts it |

---

## Run 2 — 2026-11-17 20:21:43, PC, XrApi UDP Tester running

Log: `contagion-wxr-20261117-202143.log`. Build `19:19:56`, hash `6F1D29F3…`.

### The XrApi client works inside the game

```
20:22:23  xr first pkt: client0 0.210 0.290 -0.930 0.040 … 99.00 103.40 1 FFFFFFFFFFFFFFFFFFFFF META
20:22:28  xr : rx=287  dup=0 rejected=0 sync=31  ipd=0.0678 fov_in=99.00/103.40
20:23:01  xr : rx=2166 dup=0 rejected=0 sync=118 … q=(0.149 -0.070 0.050 0.985)
```

**2,166 packets, none rejected, none duplicated.** Handshake, drain, parse and
normalisation all behave.

Note the HMD quaternion in the stats line is the *normalised* one: the tester sent
`0.150 -0.070 0.050 0.990` (norm ≈ 1.005) and the log shows `0.149 … 0.985` —
normalisation doing its job, on exactly the tester artefact `TIPS.md` warns about.

The tester sends ~430 packets per 5 s (about 72 Hz), the right shape for a device.

### Interface corrections confirmed

| Probe | Result |
|---|---|
| `VEngineRenderView013` in **engine.dll** | **`0x72C0C054`** ✓ |
| `VGUI_Surface031` in **vguimatsurface.dll** | **`0x71865778`** ✓ |
| `VEngineModel016` in vstdlib.dll | null — wrong module again; it lives in client/engine |
| `VEngineRenderView013` in client.dll | null, as before |

**A new and important detail:** the engine's `VEngineRenderView013` object
(`0x72C0C054`, inside `engine.dll`) is **not** the same object as `g_pViewRender`
(`0x66F6F3B8`, inside `client.dll`, class `CTerrorViewRender`). So "just use
`VEngineRenderView013`" is *not* equivalent to using the client's view renderer.
The camera target stays `g_pViewRender`; the engine interface is a different
thing and must not be assumed interchangeable.

### Vtable lengths

The 64-entry dump is enough to see where each vtable ends:

| Object | Entries before the data runs out |
|---|---|
| `CTerrorViewRender` | **44** (0–43; entry 44 is `.rdata`, not code) |
| `CInputWin32` | 64+ (all still code) |
| `CWEngineSound` | ~50 (entry 50 onward is ASCII: `{cyVaneWise…`) |

That matters for the camera probe: "hook all entries" means the first 44.

### Smaller confirmations

- **ASLR moves `client.dll` every run**: `0x666F0000`, then `0x667A0000`. The
  base+offset arithmetic is not defensive, it is mandatory.
- **The fake `system` file was read again** (`Quest 3 (fake-xrapi)`, 5 lines) and
  logged as such. It is not evidence of a runtime.
- Foreground was **Chrome**, not the game — third run in a row where
  `GetForegroundWindow()` is not ours.

### Smoke-test finding: the device vtable is per-instance

Not a game run, but worth recording because it killed an approach. Creating a
throwaway D3D9 device and patching **its vtable** is the widely described way to
hook `Present`. It does not work here:

- the vtable block is `MEM_PRIVATE`, not a module image;
- after `dummy->Release()` the same address reports
  `MEM_RESERVE` / `PAGE_NOACCESS`, and `VirtualProtect` fails with **error 487**.

So the vtable is allocated with the device and freed with it. The **entries**,
however, point into `d3d9.dll`'s shared code (0x6F2B6320 etc.), which is what gets
hooked now, with real MinHook. Recorded because the failed approach is invisible
until it is tried, and the error code is the tell.

---

## Run 6 — 2026-11-18 02:38–03:00, the wrapper, the sync pixel and the camera

Logs: `contagion-wxr-20261118-023803.log` (wrapper), `…-024617.log` (probe),
`…-025332.log` (yaw write). Build `168960` then `187392` bytes.

### The wrapper works, and the device is real

```
d3d9 : hooked Direct3DCreate9 at 0x72AC4C20 (first byte 0xE9)
d3d9 : Direct3DCreate9 -> real 0x00F7DEC0, wrapped 0x00F9B7B8 (Ex=0)
d3d9 : wrapper: CreateDevice(adapter=0 type=1 flags=0x00000056)
d3d9 : wrapper: windowed=0 1920x1080@60Hz hwnd=0x006D064C format=21 depth=1
d3d9 : device 0x134CF920 from CreateDevice, vtable 0x134D287C present=0x72B46320
d3d9 : Present hooked (original 0x72B46320), begin 0x72AC48F0, end 0x72AC49F0
d3d9 : first Present on device 0x134CF920
d3d9 : present=3375 begin=6750 end=6750 (end/present=2.00) devices=1
```

The game ran the full session and exited only when the harness killed it. Two
engine facts fall out of this:

- **The game is fullscreen**, not borderless (`windowed=0`, 1920×1080@60). That
  changes D17: there is no chrome to remove, and the window is already the right
  size.
- **`EndScene` fires exactly twice per `Present`** in this engine build
  (`end/present = 2.00`), so `EndScene` would have drawn the square twice — one
  more reason `Present` is the correct hook.

### The sync pixel

```
screen pixel (0,0) = R187 G0 B0        (sync=127)
bmp (0,0)..(9,9) = R187 G0 B0, (10,10) = scene
screen pixel (0,0) = R61  G0 B0        (sync=12)
```

The red is the **linear→sRGB encoding** of the byte: sRGB(127/255)=187,
sRGB(12/255)=61. Correct per `XRAPI_V0_4_DESC.md` §5 (the runtime learns the
mapping; do not pre-correct). Green 0 throughout. The square is the only red
block and is exactly 10×10.

### The camera (milestone 6a)

```
cam : g_pViewRender slot 0x60B55F98 = 0x60B6F3B8,
      vtable 0x6097CC9C, class .?AVCTerrorViewRender@@
cam : probe installed: 63 slot(s) patched, 1 skipped
cam : [07] call 1 this=0x60B6F3B8 ret=0x60614CD9 a1=0x60B6F3BC …
cam : [07] a1 +0x20 : 00000000 42937AC6 4256AC08 C3674975 C2C410F4 C52EB5C0 …
```

Slot 7's `a1` is `g_pViewRender+4`, and the values decode to a live
`CViewSetup`: fov 73.74, origin `(-231.3, -98.0, -2795.4)`, angles
`(0, -123, 0)`. Slot 6 is the engine's call (`ret` in `engine.dll`), but its
argument is not the live camera. Full layout in `CAMERA.md`.

### The camera write (milestone 6b, yaw only)

```
cam : yaw offset from file: 0.00
cam : yaw write armed on slot 7: base 99.00, offset 0.00
cam : setup origin=(1172.0 -3000.0 -2813.6) angles=(0.0 99.0 0.0) fov=106.3
cam : yaw offset from file: 60.00
cam : setup origin=(1172.0 -3000.0 -2813.6) angles=(0.0 159.0 0.0) fov=106.3
```

Same origin, yaw +60°, and the rendered frame turned 60° (`cam0.png` /
`cam60.png`). This is the camera write path proven end to end. It is off by
default; see `CAMERA.md` for the switches.

### Scoreboard additions

| Claim | Before | Now |
|---|---|---|
| Engine creates its device through `Direct3DCreate9` | uncertain | **observed**, and our wrapper owns it |
| `Present` fires once per frame | hoped | **observed**, 60 Hz, 3375 frames |
| `EndScene` is once per frame | assumed | **wrong**: exactly 2 per `Present` |
| Sync pixel reaches the frame | unproven | **observed** on screen and in the back buffer |
| `CViewSetup` layout | placeholder (D01) | **measured**: origin +0x2C, angles +0x38 |
| The camera can be driven | unproven | **observed**: yaw write rotates the picture |
| The game runs fullscreen | assumed borderless | **fullscreen 1920×1080@60** |

---

## Run 7 - 2026-11-20 01:13, Quest 3 / WinlatorXR (first device run of the wrapper build)

Collected: `E:\tmp\VRLogs\ContagionWXR\contagion-wxr-launcher-20261120-011333.log`.
Game at `Z:\steam_games\Contagion`, game dir writable (the launcher log was
written there).

```
launcher : Z:\steam_games\Contagion\ContagionWXR.exe
mod      : version.dll present, version_o.dll present
env      : WINEDLLOVERRIDES='version=n,b' (was '')
started  : pid 240 (suspended)
inject   : remote kernel32 0x7BEE0000, LoadLibraryA 0x7BEED118
inject   : LoadLibraryA returned 0x7AFD0000
resumed  : main thread
exited   : code 0 (0x00000000)          [110 s later]
```

### What it proves

- The launcher runs on device, finds the game, sets the Wine override and
  starts the process suspended.
- The remote thread ran and `LoadLibraryA` returned a module base.
- The game then ran for 110 seconds and exited 0, so nothing it loaded took it
  down.

### What it does not prove, and the finding

**There is no mod log at all.** `DllMain` calls `LogInit` before anything else,
so a loaded mod always leaves a log line. No log means our `DllMain` did not
run, whatever address `0x7AFD0000` belongs to.

`contagion.exe` imports only `KERNEL32.dll` and `USER32.dll`; `version.dll` is
imported by `engine\crashhandler.dll` (and `libcef`, `steamclient`, `unicows`),
which load later. So no version.dll was preloaded at injection time, and the
injection is the only path by which our module could have entered.

Known candidates, to be separated by the next run:

1. `0x7AFD0000` is Wine's builtin `version.dll`, not ours (same-base-name
   resolution), and `DllMain` never ran. The launcher now logs the module path
   for that handle.
2. Our module loaded but could not create a log in either the game dir or
   `%TEMP%`. The `logfile` line and the new fixed folder distinguish this.
3. The log existed and was pruned while open (`DEFECTS.md` D44, fixed).

### Changes made in response

- Both logs now go to `D:\ContagionWXR\Logs`, created if missing, with fallback
  to the game dir and then `%TEMP%`; `CWXR_LOGDIR` overrides.
- The mod logs its own log-file path as line 5.
- The launcher names the module behind the returned handle and watches the log
  folder for three seconds, printing either `mod log appeared: ... - DllMain
  ran` or `NO mod log ... - the DLL did not attach`.
- Pruning moved before file creation and restricted to the mod's own names
  (D44).

The next device run's launcher log answers the question without a rebuild.
