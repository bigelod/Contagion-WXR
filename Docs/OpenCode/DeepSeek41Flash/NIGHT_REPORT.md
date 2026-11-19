# NIGHT_REPORT.md — overnight session, 2026-11-18 00:00–03:15

Written for the morning review. The game is left in a **working** state: the
proxy, logging, XrApi client and sync pixel all run, the game survives full
sessions, and nothing that can crash it is installed by default. All camera
changes are off unless an environment variable turns them on.

---

## 1. What works now

| Thing | State |
|---|---|
| `version.dll` proxy loads | **working** |
| Build stamp + self-hash on line 1/3 | **working** |
| XrApi client (handshake, drain, parse, normalise) | **working** — `dup=rx-1`, 0 rejected with a fixed-sync sender |
| Launcher injects before the entry point | **working** |
| Menu automation (headless testing) | **working** — `tools\autotest.ps1` |
| **Per-frame Present hook** | **working** — ~3300 presents in 20 s, `end/present = 2.00` |
| **Sync pixel** | **working and verified** — 10×10 at (0,0), green 0, screen + back-buffer dump agree |
| **Camera read (6a)** | **working** — `CTerrorViewRender`, live `CViewSetup`, exact layout |
| **Camera write (6b, yaw only)** | **working on PC** — 0°→60° changed the rendered view at a fixed position |
| HMD → camera integration | not started (on purpose; see §5) |

Deployed build: `03995AC2034FFB46`, built `03:02:49` (camera build, all camera
code off by default).

---

## 2. The breakthrough: own the `IDirect3D9` instead of patching it

Every route to the game's device failed overnight until one did, and the failing
ones are worth recording because each looked reasonable:

| Route | Result |
|---|---|
| Dummy device, patch its vtable | dummy cannot be created in-game (`D3DERR_DEVICELOST`) |
| Dummy device, MinHook shared d3d9 functions | hooks installed, never called |
| Hook `Direct3DCreate9`, patch `CreateDevice` on the returned object | patch proven written; engine then died `0xC0000005` |
| Hook `QueryInterface` to catch the interface it actually calls | no QI seen on our object; crash persisted without it |
| Scan memory for a device-shaped vtable | candidates were dispatch tables; patching one killed the game |
| **Return our own `IDirect3D9` wrapper** | **works** |

The wrapper approach (PLAN §6, `src/d3d9hook.cpp`):

1. MinHook `d3d9.dll!Direct3DCreate9` and `...Ex` in `DllMain`, while the process
   is still suspended. `d3d9.dll` is loaded deliberately at that moment.
2. Return a thin wrapper of our own. Every method forwards to the real object;
   `QueryInterface` returns **ourselves** for `IDirect3D9`/`IDirect3D9Ex` — the
   critical detail. Forwarding QI would hand the engine an interface that
   bypasses us, which is exactly what the earlier probe observed.
3. `CreateDevice`/`CreateDeviceEx` are ours, so the real `IDirect3DDevice9` comes
   back in `out`. Patch that device's own `Present`/`BeginScene`/`EndScene`
   slots, each with its own saved original.

No scanning, no guessing, nothing patched before it is known to be a device.

### 2.1 Two bugs that explain the whole night

**The detours were `__fastcall`.** D3D9 COM methods are `__stdcall` on x86:
`this` is the first stack argument, not `ECX`. A `__fastcall` detour reads
garbage as the object and pops the wrong number of bytes, corrupting the stack.
This alone accounts for "patched and proven, never entered" and the later
`0xC0000005`. Recorded as **D43**, with the rule: Source engine C++ methods are
`__thiscall` (`this` in `ECX`), D3D9 COM methods are `__stdcall` (`this` on the
stack) — do not treat "COM" and "virtual" as the same convention.

**`GetRenderTargetData` needs a matching format.** The dump asked for
`X8R8G8B8` from an `A8R8G8B8` back buffer and got `D3DERR_INVALIDCALL` on both
the render target and the front buffer. Using `desc.Format` fixed it; the BMP
dump now works, which is what made pixel checks possible without screenshots.

### 2.2 The sync pixel is correct, including the part that looks wrong

`Clear` of a 10×10 rect at (0,0), `XRGB(sync, 0, 0)`, at `Present`. Verified:

- back-buffer BMP: `(0,0)..(9,9)` = the red value, `(10,10)` = scene colour;
- screen grab at the same moment: pixel `(0,0)` identical;
- green stayed 0 in every sample.

**The red that arrives is not the byte passed in.** 127 → 187, 12 → 61: those are
the linear→sRGB encodings (sRGB(0.498)=0.734). The engine's back buffer is
linear. `XRAPI_V0_4_DESC.md` §5 is explicit that the runtime **learns the
mapping** (22 distinct values step 12) and that the game must not pre-correct or
disable `D3DRS_SRGBWRITEENABLE`. So this is correct behaviour, not a defect, and
`tools\autotest.ps1` now compares against the sRGB-encoded expectation rather
than the raw byte.

---

## 3. Milestone 6a: the camera, found behaviourally

`g_pViewRender` (`client.dll+0x7B5F98`) → object `0x60B6F3B8`, vtable
`0x6097CC9C`, RTTI class **`.?AVCTerrorViewRender@@`**.

The probe patches the object's **own vtable slots** with naked forwarding thunks
(`pushad / log / popad / jmp original`) and logs the first calls with their
arguments. Slot 6 is called from `engine.dll`; slot 7, called every frame from
`client.dll`, receives **`g_pViewRender + 4`**, which is the live `CViewSetup`:

```
+0x00 x,y,width,height = 0,0,1920,1080
+0x24 fov = 73.7 (106.3 in game)
+0x2C origin = (-231.3, -98.0, -2795.4)
+0x38 angles = (0.0, -123.0, 0.0)
```

Those are world coordinates and a yaw, which is what makes the layout measured
rather than guessed.

### 3.1 The write is proven, not assumed

A control file (`contagion-wxr-cam-yaw.txt`) changes the yaw offset live. At a
fixed origin `(1172.0, -3000.0, -2813.6)`:

| offset | setup yaw | rendered frame |
|---|---|---|
| 0° | 99.0 | police car and plaza |
| 60° | 159.0 | **same position, view rotated 60°** |

Two screenshots, one run, one position — the only way this test means anything,
because two runs spawn elsewhere. Details, offsets and the next steps are in
**`CAMERA.md`**.

---

## 4. What was deliberately left uninstalled

- **Camera probe and write**: `CWXR_CAMPROBE` / `CWXR_CAM_YAW` only. The
  deployed DLL patches no camera slot without one of them.
- **Shape scan / adopt scan**: removed. It could not tell a device from a
  dispatch table and is superseded by the wrapper.
- **`CWXR_CDPATCH`**: removed. Patching d3d9's shared `IDirect3D9` table is
  what crashed three builds; there is no reason to keep the switch.

**One known wart in the default build:** the `IDirect3D9` wrapper is installed
for every `Direct3DCreate9` caller, including Chromium's menu renderer. That is
transparent (all methods forward) and the game runs full sessions, but it is
worth remembering when reading device logs.

---

## 5. What I would do first in the morning

1. **Check the default build runs** (`tools\autotest.ps1 -RunSeconds 10
   -BootSeconds 26 -LoadSeconds 30 -Sync 127`). It should show `SYNC PIXEL OK`
   and a BMP.
2. **Decide on the camera basis (D10)** before writing the HMD pose. The write
   path is proven; the maths is the open part. My recommendation is one logging
   session with the headset turning 90° left and right, comparing the game's own
   yaw at `+0x38` with the converted quaternion, rather than picking a sign.
3. Then D13 (roll), D11 (position rotated by calibrated yaw), D12 (clamp), and
   the aim/view split from PLAN §3.3.
4. `CWXR_CAM_YAW` and the control file are the fast test loop: any change that
   does not move the PC picture will not move the Quest.

## 6. The harness (keep this — it is the useful artifact)

| Tool | Purpose |
|---|---|
| `tools/autotest.ps1` | launch, drive menus, wait, optional frame dump, screen pixel check, kill |
| `tools/fakesender.ps1` | synthetic XrApi packets; `-Sync n` holds HMD_SYNC at one value |
| `tools/test-xrapi.ps1` | fake sender + 32-bit host; asserts the XrApi log lines |
| `tools/smoketest.ps1` | load the DLL in a 32-bit host; assert line 1 / self-hash |
| `tools/deploy.ps1` | build, stage, deploy, verify, uninstall |
| `tools/check-exports.ps1` | prove the proxy's export set matches the real DLL |

Menu coordinates (1920×1080, fullscreen, from the owner's screenshots):

```
Title        HOST GAME   (118, 768)
Host         SOLO        (1070, 695)
Solo setup   HOST GAME   (118, 962)   green button
Character    Ready       (1730, 884)
```

`autotest.ps1 -RunSeconds 10 -BootSeconds 26 -LoadSeconds 30` reaches the map
reliably. It now always captures the presented frame and reports pixel (0,0).
