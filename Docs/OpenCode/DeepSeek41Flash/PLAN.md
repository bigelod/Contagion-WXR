# PLAN.md — what to do next, and why in this order

**2026-11-17.** Derived from `BINARY_RECON.md`, `AUDIT.md` and `DEFECTS.md`.
Ordered so each step leaves something testable and each unknown is retired by
an observation rather than an argument.

The inherited Meta AI plan is still worth reading for the milestone shape
(`Docs/MetaAI/PROJECT_PLAN.md` §5); the ClaudeCode milestones
(`Docs/ClaudeCode/PROJECT_PLAN.md` §5) remain the better sequence. What changes
here is *which* experiments are worth a deploy, because the binary reconnaissance
has narrowed the field sharply.

---

## 1. The ordering principle, restated because it is the whole plan

There is no build and no log. On a standalone headset there is no debugger, so
**the log is the debugger**, and every question below — is the proxy loaded, is
the window found, is the vtable slot the right one — is unanswerable until a
binary writes a line.

So: **milestone 2 is not a milestone, it is the instrument.** Build it once,
well, and make it carry enough information that the next four unknowns retire in
a single deploy instead of four.

One deliberate departure from strict one-change-per-deploy: the **recon dump**
(§2.2) is a read-only diagnostic build that answers several questions at once and
changes no game behaviour. That is safe to batch. Everything after it goes back
to one change per deploy.

---

## 2. Milestone 2 — a DLL that logs, and a deploy script that cannot lie

### 2.1 The minimum viable build

Deliverable: `version.dll` at the install root that writes one file and returns.

- Build stamp: `static const char* kBuild = "built " __DATE__ " " __TIME__;`
  logged on **line 1**.
- Log path: next to the game, **one file per run**
  (`contagion-wxr-YYYYMMDD-HHMMSS.log`, keep the last 5), with a fallback to
  `%TEMP%` if the install is read-only. Do **not** reuse a single fixed filename:
  NTFS file tunnelling can append a run to the previous file, which makes line 1
  an old build stamp and silently passes the freshness check (`DEFECTS.md` D40).
  On device, never use `%LOCALAPPDATA%` — the container's is unreadable even by
  adb, which is why every port in this family logs beside its module.
- Log, in this order:
  1. build stamp;
  2. full path of **this** module (`GetModuleFileNameA(hInstance)` — it proves
     which copy loaded, which is not academic given §3 of `BINARY_RECON.md`);
  3. **SHA-256 of this module's own file**, so the owner can compare four lines
     against `Release\ContagionWXR.manifest.txt` and know the right binary is
     running without trusting a name or a timestamp (`DEPLOY.md` §6);
  4. full path of the process;
  5. the module path that caused the load, if obtainable;
  6. the handshake directory chosen, and whether `Z:\tmp` was found;
  7. the effective decimal separator (see `DEFECTS.md` D39);
  8. OS version, `GetSystemMetrics(SM_CXSCREEN/CYSCREEN)` — the container size,
     which is 1024x768 by decision and must be confirmed as what the game sees;
  9. `isWine` heuristics (presence of `wine_get_version` in `kernel32`) — cheap,
     and it tells us immediately whether we are on device.
- Then a heartbeat thread writing one line every 2 s, so a log that stops is
  distinguishable from a process that never started.
- **Nothing else.** No hooks, no sockets, no D3D. If the DLL loads and the log
  appears, the vector and the toolchain are proven.
- **Buffer early lines**: open the file as the first action and flush every
  line, or an early crash loses the most valuable text.

Acceptance: owner launches the game on the Quest 3, and the log's line 1 carries
the stamp of the binary just built.

### 2.2 The recon dump — the second deploy, and the most valuable one

Same DLL, no behaviour changes, plus a diagnostic thread that writes to the same
log once, ~5 s in. This is what turns `BINARY_RECON.md`'s static guesses into
facts.

**a. Windows.** Enumerate every top-level window in the process; log
`HWND`, class name, title, client size, styles. This retires **D18** forever and
names the window the input hook should use.

**b. Modules.** Log every loaded module with base and size. Confirms the load
order, whether `client.dll` is up yet at that moment, and whether
`crashhandler.dll` is present (the `version.dll` chain).

**c. Interfaces.** `CreateInterface` each of: `VClient016`,
`VEngineClient013`, `VEngineCvar007`, `VEngineRenderView013`,
`VEngineModel016`, `VGUI_Surface031`, `GameUI011` — from `client.dll`,
`engine.dll`, `GameUI.dll` as appropriate. Log the pointer or `null`. This
retires **D20**'s interface half and proves §4 of `BINARY_RECON.md` at run time.

**d. `ICvar` and the pause ConVars.** Take `g_pCVar` from the exported symbol
in `vstdlib.dll` (or `client.dll`), log the pointer, then `FindVar`
`gameui_activated`, `gameui_hidden`, `gameui_ingame_menu_active`, `gameui_paused`.
**Do not call `GetBool` yet** — the index is unverified. Log only "found / not
found" from `FindVar`, whose index must itself be derived (see below) or the
call will land on the wrong function.

> Safer first pass: log `*(void***)g_pCVar` and the first 32 entries of that
> vtable, plus the count of static ConVars if a count is reachable. Naming
> `FindVar` can wait for the IDA pass.

**e. The two anchor globals.** Log `*(void**)0x107B5F98` and
`*(void**)0x1089D0CC`, and for each, the value at `[p-4]` (the RTTI locator
pointer) and the first 32 dwords of `*p`. A vtable whose `[p-4]` points into
`.rdata` is a class vtable, and the `.rdata` neighbourhood can be matched to a
type-descriptor string offline. This is what finally names the objects.

**f. The view setup.** If `VEngineRenderView013` resolves, log its first 32
vtable entries. Do **not** call any of them yet.

**g. Draw-call counts.** Count `BeginScene`, `EndScene` and `Present` over 60
frames and log the totals. This settles, with a number, the claim `PLAN.md` §6
rests on — that a Source-family engine calls `EndScene` more than once per frame
for render targets. `XOPS-WXR` does exactly this (`BeginScene=N EndScene=N`) and
it is the cheapest way to stop arguing from general knowledge
(`SIBLING_PORTS.md` §2.7).

**h. The real FOV.** Log the live horizontal and vertical FOV from the current
view whenever it changes. That value is what the outbound packet should carry;
`XOPS-WXR` recovers it from the actual projection matrix rather than using a
constant or echoing the inbound tangents (`SIBLING_PORTS.md` §2.5). See
`DEFECTS.md` D15.

**i. An on-demand frame dump.** If a file `contagion-wxr-shot.request` exists
beside the exe, write the presented frame to `contagion-wxr-shot.bmp` and delete
the request. `mirrors-edge-WXR` does exactly this, and it is what lets the sync
pixel be checked **offline on the PC** — red value at (0,0), green zero —
without a headset, a deploy, or the owner (`SIBLING_PORTS.md` §3.7). Implement it
in the same `Present` hook that will draw the pixel, so the first sync-pixel
build is verifiable the moment it runs.

**j. Per-hook first-call trace and an audit dump.** For each installed hook,
one line on first entry (flushed immediately) plus one line after install giving
the resolved target and original pointer. `XOPS-WXR`'s reason is worth quoting:
*"the last trace line before a Wine crash names the hook that was entered"*
(`SIBLING_PORTS.md` §2.7).

Acceptance: one log file that answers all ten, plus one BMP, pulled off the
headset. One deploy, roughly ten retired unknowns. This is the cheapest
high-value step in the project and it should be taken before any camera work.

### 2.3 The build-and-deploy script

Write it in the same step as §2.1, before there is anything to debug. Full
detail, real paths and the `.gitignore` traps are in **[DEPLOY.md](DEPLOY.md)**;
the short version:

1. wipe `obj\` and the build output folder;
2. `msbuild src\ContagionWXR.vcxproj /p:Configuration=Release /p:Platform=Win32`;
3. read the `__DATE__ __TIME__` stamp out of the built binary and **refuse to
   deploy** if older than the newest source file;
4. stage mod-only files into `Release\ContagionWXR\`, generating `version_o.dll`
   from the **32-bit** system `version.dll` and asserting its PE machine type;
5. deploy to **`E:\Steam\steamapps\common\Contagion`** (PC test) and
   **`E:\tmp\Q3Mods\Contagion`** (Quest staging), verifying each with SHA-256 and
   printing `match=True`;
6. never write into a game copy except the mod's own deployed files.

Step 3 has caught a stale build being tested as new in this family. It is
invisible without the check and costs a whole session when it happens.

---

## 3. The camera — identify behaviourally, then write

### 3.1 What is now known

- `g_pViewRender` = `0x107B5F98`, verified by its guard string
  (`BINARY_RECON.md` §8.2). Also obtainable as
  `CreateInterface("VEngineRenderView013")` — **no pattern scan needed**.
- The five `IViewRender` call sites found statically are all nullary getters, so
  the argument-taking methods are reached through cached registers and cannot be
  found by pattern. That is a limitation of the scan, not evidence of absence.
- `CViewSetup` has RTTI, so the type exists and is nameable offline.

### 3.2 The experiment that settles it: hook every slot, log the arguments

One deploy, no guessing:

1. From the resolved `g_pViewRender`, take its vtable and its entry count.
2. For each entry, if it is in `client.dll`'s `.text` and at least 5 bytes long,
   install a MinHook detour that records: entry index, `ecx`, and the first four
   stack dwords.
3. For each pointer argument, test whether the memory at `+0` and `+4` equals
   the container width and height (`1024` and `768`), or any plausible pair the
   game reports. That is the `CViewSetup` fingerprint.
4. Log, once per index, the first hit and the fingerprint result; then
   **self-disable after N hits per index** so the log stays small and the frame
   rate survives.
5. Pick the index that receives a `CViewSetup*`.

This is `TIPS.md`'s "measure, do not infer" applied to a vtable, and it is
strictly better than any SDK index: it cannot be wrong about *this* binary.

**Safety:** the detours must call through immediately, must not modify anything,
must be `__fastcall` with a signature wide enough not to disturb the stack, and
must be removed automatically after the sample. Run this build with the game in
a menu first, then in a map, to see which indices fire in each.

**Two habits to build in now, both from `XOPS-WXR`** (`SIBLING_PORTS.md` §2.7):

- **A bounded first-call trace.** Every detour logs one line on its *first*
  call, flushed immediately, and then stays quiet. The last trace line before a
  crash names the hook that was entered — which on a device with no debugger is
  the difference between a location and a shrug.
- **An audit dump after install.** Log every resolved pointer once
  (`OVERRIDEVIEW target=0x… original=0x…`), so one line proves the hook landed
  where the log says it did, rather than leaving it to be inferred from
  behaviour.

### 3.3 The camera write, once the index is known

Order of work, each verified on device:

1. **Orientation only.** Set `setup->angles` from the HMD yaw/pitch/roll, having
   fixed **D10** (basis change + full-range yaw) first. Log yaw while the owner
   turns a full circle; it must sweep 360° monotonically.
2. **Position, clamped and rotated.** Room-space delta, rotated by
   `g_yawOffset` alone (**D11**), magnitude-clamped to 6 units (**D12**).
3. **Recentre**, bound from day one, and also on spawn.

Keep aim and camera split: `CreateMove` (or its successor, §4) owns logical
yaw/pitch; the view hook owns full visual orientation and lean. `setup->angles`
must include roll or the horizon will not tilt (**D13**).

**Fallbacks, ranked, if `g_pViewRender` cannot be driven:**

1. `VEngineRenderView013` through `CreateInterface` (same object, different
   route).
2. `CViewRender`'s vtable via RTTI (`.?AVCViewRender@@`), hooking its
   `CViewSetup`-filling method.
3. A view-matrix write in `shaderapidx9.dll` — the route that needs no camera
   object at all. `mirrors-edge-WXR` makes this concrete on the same API:
   `IDirect3DDevice9::SetVertexShaderConstantF` (**vtable slot 94**), register
   **`c0`**, row storage, world space; copy the constant block, modify, forward.
   Its injection is `M' = T(−o)·M`, i.e. `row3 -= ox*row0 + oy*row1 + oz*row2`.
   Two warnings from that port: `c0` also carries a light/shadow matrix, so
   **validate every upload by shape** (it accepted 220,000 and rejected 91), and
   a constant register is not unique to the view — identify it, do not assume it
   (`SIBLING_PORTS.md` §3.5c).
4. `MODE_VR = 2` non-VR mode, which needs no camera work and is a shippable
   comfort fallback regardless.

### 3.4 The IDA pass, which is now worth doing

Half a day with IDA Free or Ghidra, against `client.dll`, with a concrete goal
list rather than open-ended browsing:

- name the class at `0x107B5F98`'s vtable (via its `[p-4]` RTTI locator) and list
  its methods, so the vtable dump above has names attached;
- identify `0x1089D0CC` and `0x106B1F88` the same way;
- dump `CViewSetup`'s real layout from a function that writes it;
- find `CHLClient::CreateMove` and `CInput::GetUserCmd` for §4;
- derive `ICvar::FindVar` and `ConVar::GetBool` indices, since pause detection
  depends on them;
- locate `TerrorClientMode`'s vtable via RTTI, so the project knows whether
  `IClientMode` is a viable route at all.

Do this **after** §2.2 and §3.2, not before: the runtime dump tells the
disassembler which addresses matter, and doing it first invites another
"pattern matched, therefore the premise is confirmed".

---

## 4. `CreateMove`, which is a different design problem

The inherited approach — hook a client-mode vtable slot — is unsupported by
evidence (**D02**, **D03**). Two candidate routes, in preference order:

**A. Through `VClient016` (verified interface).** In this engine family the
engine calls `IClient::CreateMove(int sequence_number, float input_sample_frametime,
bool active)`; the client then dispatches to the mode. Hooking that gives a
once-per-tick callback with the sequence number, and the `CUserCmd*` is
obtainable through `CInput`/`IInput` by command number. The index has to be
derived from `VClient016`'s vtable — by argument shape again, or in IDA.

**B. Hook whatever the engine calls at index 21 on `0x1089D0CC`.** That object is
called at index 21 six times, which is the right *frequency* for a per-tick
function — but the calls are nullary, so it is not `CreateMove(float, CUserCmd*)`.
It may be `IsConnected()`-shaped, or a tick-count getter. Identify it before
considering it.

Either way, the tick-facing code needs `g_yawOffset` accumulation with real
`frametime`, wrap and clamp (**D14**), and it must not fight the engine's own
mouse-look — the inherited code zeroes `mousedx/mousedy`, which is correct only
if the runtime's mouse is not expected to survive. That is a product decision,
not a code one; see §5.

**Three constraints learned from the siblings, each cheap to obey now and
expensive to retrofit:**

1. **Prove the absolute write is consumed before relying on it.**
   `mirrors-edge-WXR`'s central lesson is that writing an absolute rotation *did
   not transfer* — its engine recomputed from a delta and discarded the write
   (`SIBLING_PORTS.md` §3.5a). Source should consume `cmd->viewangles`, but that
   is the same class of assumption as D02/D03. Log the value we wrote and the
   value the engine actually used, for twenty ticks, once.
2. **Drop implausible pose deltas; do not clamp them.** ME discards jumps beyond
   a step limit instead of clamping, because clamping still applies a bad value.
   If the HMD jumps more than ~30–45°/tick or the position more than ~0.5 m, hold
   the last good pose and log it (`SIBLING_PORTS.md` §3.5b). This is also free
   anti-cheat insurance when multiplayer arrives.
3. **Use `HMD_SYNC` as the pose's identity.** Skip a pose whose sync equals the
   last one this consumer saw; the stream runs faster than the tick
   (`DEFECTS.md` D41).

---

## 5. Input — the decision that should be taken before any input code

Three candidate mechanisms, and picking one early avoids writing the wrong one
twice.

**A. Drive the game's own gamepad layer.** The game ships a full `joy_*` config
surface, `JOY1`–`JOY10` binds, `+jlook`, and `exec joy_setup`
(`BINARY_RECON.md` §7). If WinlatorXR can present an XInput device to the
container, most of the mapping might be *configuration* rather than code, with
the player's own sensitivities and the game's own edge/repeat logic intact. This
is the option most aligned with "prefer the engine's own code paths".

**Unknown, and it gates the option:** can WinlatorXR deliver an XInput (or
DInput) device into a container today? That is a question for the WinlatorXR
community, not something to derive. **Ask before building.**

**B. Call the game's own commands/actions in-process.** `weaponselect_next`,
`phone_or_blend`, `+melee`, `impulse 100` are real commands (**D21**).
`Docs/general_wxr/PC_INPUT.md` forbids `SendInput`/`PostMessage`; the in-process
equivalent is to invoke the command dispatcher directly, or write the state the
input code reads.

**C. Write `CUserCmd::buttons`/`forwardmove`/`sidemove` in the tick hook.** What
the inherited code does. Works, but it is a translation layer that must
re-implement the game's edge detection, and it interacts badly with the runtime's
mouse (**D22**).

**Recommendation:** A if the device question comes back yes; B for the discrete
actions either way; C only for movement and only once §4 works.

Also decide, explicitly, and put in the code:

- **held vs edge-triggered.** A VR trigger is a held state; a game waiting for a
  key-down edge waits forever. This is the bug that hung a respawn in another
  port in this family.
- **what happens in menus.** `MODE_VR = 0` returns the runtime's own mouse, which
  is a *working* input path — do not block it (`PC_INPUT.md` §3.4).
- **whether the mouse is ours or the runtime's.** `MODE_3D = -1` keeps the
  runtime's right-controller-gyro mouse while still delivering 6DOF. For a
  hybrid "head look + stick turn" scheme, that combination may be simpler than
  owning the mouse ourselves.

### The mapping table, restated with real commands

Owner's design from `Docs/MetaAI/PROJECT_PLAN.md` §3, with the mechanism column
corrected. **Confirm with the owner before implementing, and again on device.**

**Left hand**

| Control | Action | Mechanism |
|---|---|---|
| Stick | Move | `forwardmove` / `sidemove` (C) |
| Stick click | Sprint | `IN_SPEED` |
| X | Crouch | `IN_DUCK` (or `+duck`) |
| Trigger | Melee | **`+melee`** (B) |
| Y tap | Flashlight | `impulse 100` (B) |
| Grip | Modifier only | — |
| Grip + Y tap (<300 ms) | Phone | **`phone_or_blend`** (B) |
| Grip + Y hold (>300 ms) | Voice wheel | **`+voicecmds`** exists — consider driving it |

**Right hand**

| Control | Action | Mechanism |
|---|---|---|
| Trigger | Fire | `IN_ATTACK` |
| Grip | Jump | `IN_JUMP` |
| A | Use | `IN_USE` (or `+use`) |
| B | Reload | `IN_RELOAD` (or `+reload`) |
| Stick up/down | Next/prev weapon | **`weaponselect_next` / `weaponselect_prev`** (B) |
| Stick left/right | Smooth turn | `g_yawOffset += x·degPerSec·dt` (**D14**) |
| Grip + A | Drop ammo | `dropammo` (B) |
| Grip + B | Drop item | `drop` (B) |

`dropunusedammo` is a third, distinct drop command; decide whether it gets a
binding.

---

## 6. The sync pixel — unchanged, and still before the camera

Spec is `Docs/general_wxr/XRAPI_V0_4_DESC.md` §5, and nothing in the binary
reconnaissance changes it.

- Hook **`Present`** (D3D9 vtable 17), not `EndScene`.
- **Try a `Clear` first, not a quad.** `XOPS-WXR` draws the pixel with
  `Clear(1, &{0,0,10,10}, D3DCLEAR_TARGET, XRGB(r,0,blue), 1.0f, 0)` and needs no
  render-state handling at all — blending, filtering, fog, Z and the current
  shader cannot touch a Clear (`SIBLING_PORTS.md` §2.4). That removes the entire
  state-restoration class of sync-pixel bug. Keep the quad as the fallback if
  D3D9 ordering fights it.
- Set render target 0 to the back buffer, viewport full.
- If using the quad path: disable alpha blending, fog, Z; point sampling on
  min/mag/mip.
- Fill **10x10** at (0,0) with `RGB(clamp(round(HMD_SYNC),0,255), 0, 0)` green
  forced to 0 and blue 0 (mono — blue is the AER eye flag).
- **Echo `HMD_SYNC` exactly.** No rescale, no gamma, no invented id. If no packet
  has arrived, draw the last good value or nothing.
- Draw it last, after everything including any overlay.
- Blue channel 0 (mono).
- Side-by-side: also draw at the top-left of the right half.

Verify on the desktop by reading the pixel back. On device, the acceptance test
is not the square — it is that head tracking feels **locked**, not swimmy, at
milestone 6.

A useful freebie from `XRAPI_V0_4_DESC.md` §5: a second, differently coloured
10x10 square just below the sync square, encoding the current `MODE_VR`/
`MODE_3D`. At 1024x768 it costs nothing and answers "which mode am I actually in"
from a screenshot, with no log.

Two corroborations from `Interstellar-Marines-WXR`, which is device-confirmed
(`SyncPixelQuad.cs`, `SIBLING_PORTS.md` §1.5):

- **Own the draw; do not hang it on a game object.** That port's square kept
  dying on respawn because it was parented to whichever camera was topmost and
  its "already created" flag was never reset. The fix was a self-owned overlay
  that draws at true present time. For us that is the same conclusion as hooking
  `Present`: the square must survive everything the game does to its own scene.
- **Make the size a config value with a sane floor** (`sync_pixel_size`,
  minimum 2, default 10). They ran 150 for a while on a letterboxing theory; on
  device it bought nothing and was plainly visible to the player. Ten is right;
  the knob exists so a future runtime quirk does not need a rebuild.

One honest disagreement to record rather than resolve by fiat. In menus, when no
packet has arrived, `Interstellar-Marines-WXR` draws `Time.frameCount % 256` so
the runtime can still calibrate its colour table, while
`Docs/general_wxr/CLAUDE.md` says **never fabricate** a sync value. Both
arguments are reasonable. Follow the project rule first (last good value, or
nothing); if the runtime reports a frozen game or fails to calibrate in menus,
the IM approach is the documented alternative — not a thing to invent, a thing to
switch to knowingly.

### Implemented 2026-11-17/18 — and what it took

`src/d3d9hook.cpp`. The pixel and the device are both working, verified on the
PC (`RUNTIME_CONFIRMATIONS.md` Run 6).

- **The device is captured, not searched for.** MinHook `Direct3DCreate9` and
  `...Ex`, return our own thin `IDirect3D9` wrapper, and take the real
  `IDirect3DDevice9` from `CreateDevice`/`CreateDeviceEx`. Then patch that
  device's own `Present` (17), `BeginScene` (41) and `EndScene` (42) slots, each
  with its own saved original.
- **Why not the usual route:** a dummy device cannot be created while the game
  holds the adapter (`D3DERR_DEVICELOST`), and d3d9's shared `IDirect3D9` table
  is not safe to patch (`0xC0000005` on three builds; `DEFECTS.md` D43). The
  wrapper removes the need to find anything.
- **`QueryInterface` must return the wrapper**, not forward to the real object.
  Forwarding would hand the engine an interface that bypasses the capture — which
  is what the 02:06 probe observed.
- **Detours are `__stdcall`**, because D3D9 COM methods are `__stdcall` on x86
  (`this` on the stack). The earlier `__fastcall` detours could never have
  worked; see D43.
- The pixel is a **`Clear` of a 10×10 rect**, gated on render target 0 actually
  being the back buffer (logged once if it is not), and on a frame having
  arrived — never a fabricated value.
- **The presented red is sRGB-encoded** (127 → 187). That is correct: §5 says
  the runtime learns the mapping and the game must not pre-correct.
- **Counters** for `present`/`begin`/`end` every 5 s. Measured: exactly two
  `EndScene` per `Present` in this engine, so `EndScene` was the wrong place.
- **`contagion-wxr-shot.request`** produces `contagion-wxr-shot.bmp` of the
  presented frame. The destination surface must use the back buffer's own format
  (`desc.Format`), or `GetRenderTargetData` returns `D3DERR_INVALIDCALL`.

### Milestone 6a/6b — the camera, measured 2026-11-18

`src/camera.cpp`, off unless a `CWXR_CAM*` variable is set.

- The probe patches `CTerrorViewRender`'s **own vtable slots** with naked
  forwarding thunks (`pushad / log / popad / jmp original`). Function hooks were
  tried first and logged shared base methods on unrelated objects.
- Slot **7** receives the live `CViewSetup` as `g_pViewRender+4` every frame;
  slot 6 is the engine's call but gets a different struct.
- Measured layout: `x,y,width,height` +0x00, `fov` +0x24, `origin` +0x2C,
  `angles` +0x38. Verified by plausible world values (`-231, -98, -2795`, yaw
  `-123`), not by SDK assumption.
- The write is proven: at one fixed origin, yaw 99→159 turned the rendered view
  60°. `CWXR_CAM_YAW` and `contagion-wxr-cam-yaw.txt` are the test knobs.
- **Not done:** HMD basis conversion (D10), pitch/roll (D13), position (D11),
  recentre, and the aim/view split. See `CAMERA.md` §4.

### The launcher, and why it exists

`launcher/launcher.cpp` → `ContagionWXR.exe`, shipped in the transfer set.

Three reasons, in order of importance:

1. **`WINEDLLOVERRIDES=version=n,b` has to be set**, or Wine never resolves
   `version.dll` to ours and the mod silently does not load. A launcher sets it
   every time, appends to an existing value instead of clobbering it, and logs
   the result.
2. **On the Quest there is no Steam client** — the game is a Steam install run
   through a Steam emulator — so "the thing that starts the game" is a gap worth
   filling with one known entry point.
3. **It is where an auto-updater goes.** Deciding what should be running before
   starting it is the same job, and this is the only file that needs to know.

It does not inject or patch anything; the DLL does that. If the D3D hook turns
out to be landing after the game creates its device, the escalation is to have
the launcher start the game **suspended and inject before the entry point** —
which is only possible from a launcher, and is the reason to have one even
before the updater exists.

---

## 7. Window, menus, performance

**Window.** `EnforceWindow()` called from the per-frame hook: strip
`WS_CAPTION | WS_THICKFRAME | WS_BORDER`, size to
`GetSystemMetrics(SM_CXSCREEN/CYSCREEN)`, log window/surface/render sizes when
they change. Use the handle from the recon dump (§2.2a), not `Valve001`. Expect
the engine's own consistency check to fight; if it does, move surface and render
sizes with the window rather than arguing once a second.

**Menus.** `MODE_VR = 0` while a menu is up, so the pose stream stops, the
runtime's mouse returns, and the frame leaves immersive presentation. Keep
sending the outbound packet every frame or you cannot turn the stream back on,
and hold the last good pose across the gap on return. Detect the menu from
ConVars (`gameui_activated`, `gameui_hidden`, `gameui_ingame_menu_active` all
exist **[bin]**), not from a guessed `IGameUI` index, and **never** from
`GetForegroundWindow`, cursor visibility or "is the window active" — all three
lie inside a container (`SIBLING_PORTS.md` §1.6b).

**But `MODE_VR = 0` is safe in mono only — settled 2026-11-17.** It works with
`MODE_3D = 0` (which is how Interstellar Marines uses it) and **breaks
WinlatorXR when combined with AER** (`MODE_3D = 2`). That is why the two ports
that render AER both refuse to send `0`. Mono is our shipping mode, so `0` for
menus stays — with three rules that cost nothing now and matter if stereo ever
comes:

- centralise mode selection in one function, so the choice is one constant;
- **`MODE_3D = 0` unless `MODE_VR == 1`**, so a stray combination is a visual
  no-op rather than a runtime state it cannot leave;
- if AER ever ships, menus must **not** send `0` while AER was the last 3D mode
  — keep `MODE_VR = 1` with `MODE_3D = 0`, or use `2`.

Whichever mode is chosen, remember that at `MODE_VR = 0` the **controller stream
is dead** — so any in-headset settings menu must be reachable and operable
without it, exactly as Interstellar Marines' FOV tuner reads arrow keys because
the runtime's screen-mode emulation maps the stick to them
(`SIBLING_PORTS.md` §1.3).

**Performance.** In this order:

1. the `potato` presets, and verify they survive a relaunch;
2. `mat_cont_lens_dirt 0`, `mat_cont_lens_flare 0`, `mat_cont_color_modify 0`,
   `mat_cont_volumetric 0` — Contagion-specific post FX, all pure loss on a
   mobile GPU behind two translation layers;
3. `mat_motion_blur_enabled 0` (already the default);
4. container resolution as the supersampling control, no code;
5. measure from WinlatorXR's own counters, never from an in-mod timer.

At 1024x768 the runtime is **upscaling**, so the image will be soft. That is
expected, not a defect, and in-game AA is wasted fill. HUD scaling inverts at
this size: expect to *shrink* 2D art, not enlarge it.

---

## 8. Milestone list, updated

Carried from the ClaudeCode plan, with the two new steps inserted and the camera
step restructured.

- [ ] **0 — Feasibility assessed, intake complete.** Open: the deploy-loop
      length, the game build number, whether the WinlatorXR flat launch needed
      workarounds, whether `potato` survives a relaunch.
- [x] **1 — Unmodified game runs flat in a container** (owner-reported,
      1024x768).
- [x] **2 — Our `version.dll` loads and writes a log with a build stamp**,
      plus the build-and-deploy script. **Done 2026-11-17, owner-observed**:
      line 1 carries the stamp of the deployed binary and line 3 its self-hash,
      both matching `Release\ContagionWXR.manifest.txt`. `RUNTIME_CONFIRMATIONS.md`.
- [x] **2b — Recon dump deploy** (§2.2): windows, modules, interfaces, anchor
      globals, view-render vtable. **Done in the same run.** It retired the
      single biggest unknown in the project: the three anchors are named
      (`CTerrorViewRender`, `CInputWin32`, `CWEngineSound`) and the client
      interfaces resolve at run time. Three corrections came out of it — see
      `RUNTIME_CONFIRMATIONS.md` §4 and §6.
- [ ] **3 — Window is borderless and fills the display**, using the handle from
      2b.
- [ ] **4 — XrApi 0.5 client parses real device packets**, with quaternion
      normalisation, drain, `vr` file, `system` read, desktop fallback.
      **Client implemented 2026-11-17 and verified on the desktop**: handshake
      written, 7872 bound, 632 packets parsed with 0 rejected against
      `tools\fakesender.ps1`, first packet logged verbatim, `system` read.
      **Remaining:** one in-game launch to prove it coexists with the game, and
      one real Quest 3 capture to build the button table from. Button
      table built from a device log.
- [x] **5 — Sync pixel: 10x10, exact echo, `Present`, drawn last.** **Done and
      verified 2026-11-18**: screen grab and back-buffer dump both show exactly
      10×10 at (0,0), green 0, red the sRGB encoding of `HMD_SYNC` (which the
      runtime calibrates for). See `RUNTIME_CONFIRMATIONS.md` Run 6.
- [x] **6a — Vtable-argument probe on `g_pViewRender`** (§3.2), one deploy.
      **Done 2026-11-18**: slot 7 takes the live `CViewSetup` at
      `g_pViewRender+4`; slot 6 is the engine's call with a different struct.
      Layout measured; see `CAMERA.md`.
- [ ] **6b — 6DOF head tracking feels locked.** Orientation first, then
      position. Mono. **The big one.** The *write path* is proven on PC
      (yaw 99→159 rotated the frame at a fixed origin, 2026-11-18); the HMD
      basis conversion, pitch/roll, position and recentre are not done.
- [ ] **7 — Mono confirmed and measured on device**, from the runtime's counters.
- [ ] **8 — Hands and weapon placement.** Deferred by design.
- [ ] **9 — Full input mapping, owner-confirmed**, mechanism chosen at §5.
- [ ] **10 — Comfort, polish, settings**, HUD scale, recentre, voice wheel,
      haptics.
- [ ] **11 — Public-server multiplayer.** Out of scope for the first build.

---

## 9. Open questions for the owner

1. **How long is the deploy-and-test loop?** It decides how much to batch. The
   recon dump (§2.2) is deliberately batched; nothing else is.
2. **Can WinlatorXR deliver a gamepad (XInput/DInput) into a container?** If yes,
   §5 option A may replace most of the input work with configuration. Worth one
   question in the community.
3. **Which build number** should offsets be recorded against? `steam.inf` is not
   present as loose text; the console prints a build, or the Steam manifest has
   it.
4. **What workarounds did the flat launch need?** Each becomes an installer step.
5. **Do the `potato` presets survive a relaunch?** Known live problem in this
   family.
6. **Is there a public Contagion camera write-up?** One forum question could
   still save the IDA pass. Since the engine is L4D2-derived, L4D2 gamedata
   (SourceMod) is also worth a look.
7. **Should the mouse belong to the mod or the runtime?** The `MODE_3D = -1`
   hybrid may be simpler and is a product decision, not a technical one.

---

## 10. Documentation governance

- **This folder is the live set.** Update it as things are learned.
- **`Docs/MetaAI/` and `Docs/ClaudeCode/` are archival** and stay frozen. Cite
  them for history and design intent only.
- **Fold general findings back into `Docs/general_wxr/`**, per its own
  `CLAUDE.md`. Candidates from this session:
  - the `version.dll` chain via an indirect importer (anything that imports
    `crashhandler.dll`-shaped helpers), which explains *why* the vector works;
  - "enumerate the game's own `config.cfg` before believing a command name is
    unknown" — a 9 KB text file answered four open questions;
  - "the game may already have a gamepad layer" — check before writing input
    code;
  - "check the import table before trusting an injection vector";
  - **do not change the FOV semantics in `XRAPI_V0_4_DESC.md` on one sample**
    (see `BINARY_RECON.md` §9) — verify on device first.
- **Record the build** every offset was derived from, at the top of
  `BINARY_RECON.md`, whenever it changes.
