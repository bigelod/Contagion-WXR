# DEFECTS.md — live defect register

Opened 2026-11-17. Supersedes the numbering in `Docs/ClaudeCode/BUG_FIXES.md`,
which remains readable as history. Where an entry corresponds to a ClaudeCode
entry the old number is given as `(CC #n)`.

**Status vocabulary**

| Status | Means |
|---|---|
| **OPEN** | found, not addressed |
| **CONFIRMED** | independently re-verified against the source this session |
| **DISPROVEN** | the inherited assumption is provably wrong |
| **DEFERRED** | real, deliberately not now; reason given |
| **FIXED (untested)** | changed in code, not verified on the Quest 3 |
| **FIXED (verified)** | owner has confirmed on hardware |
| **NOT A DEFECT** | investigated and withdrawn |

Nothing here has been observed at runtime. The code has never compiled. Every
entry is either read from source **[code]**, measured from the shipped binaries
**[bin]**, or contradicts `Docs/general_wxr/` **[spec]**.

---

## A. Blocking — nothing builds, nothing runs

### D01 — `CUserCmd` and `CViewSetup` are placeholders, not layouts — **OPEN** (CC #1a, #11)

`hooks.h` declares `struct CUserCmd { char pad0[12]; QAngle viewangles; };` and
the code writes `buttons`, `forwardmove`, `sidemove`, `mousedx`, `mousedy`,
`impulse`. Six compile errors.

`CViewSetup` is likewise a hand-written guess: the field order does not match any
Source release (in SDK2007 `aspectRatio` sits before the viewmodel fields, and
there are members the struct here omits). Since it is written through
`setup->origin` and `setup->originViewmodel`, a wrong layout is a memory
corruption, not a wrong picture.

**Fix:** derive both from the binaries. `CViewSetup` has RTTI
(`BINARY_RECON.md` §5), and its layout can be confirmed by logging 64 bytes from
a pointer and finding the container width and height as consecutive ints. Do
not write a struct until that log exists.

**Measured 2026-11-18.** The live camera is a `CViewSetup` at `g_pViewRender+4`,
reached as the first argument of vtable slot 7. Confirmed fields: `x,y,width,
height` at `+0x00`, `fov` at `+0x24`, `fovViewmodel` at `+0x28`, `origin` at
`+0x2C`, `angles` at `+0x38`, `zNear/zFar` at `+0x44/+0x48`. The identification
is behavioural: origin read `(-231, -98, -2795)`, yaw `-123`, fov `73.7` — world
values, not a plausible-looking guess. See `CAMERA.md`.

### D02 — The hooks are installed on the wrong object and would crash — **DISPROVEN / OPEN** (CC #11)

`Hooks_Init` resolves `0x107B5F98` and calls it `g_pClientMode`. It is
**`g_pViewRender`**, proven by the assignment guard string
`"GetViewRenderInstance() must be implemented by game."` (`BINARY_RECON.md` §8.2).

Worse, the pattern's **first** match in this binary resolves a *different* global,
`0x106B1F88`, so the code does not even reach `0x107B5F98`.

`hkOverrideView` then treats a garbage stack value as `CViewSetup*` and writes to
it; `hkCreateMove` passes that garbage to the original function as
`(float, CUserCmd*)` before doing anything else. Expected outcome on a real
launch: access violation.

**Fix:** delete both hooks. Replace with the behavioural identification in
`PLAN.md` §3.

### D03 — Indices 14 and 21 are nullary and cannot be `OverrideView` / `CreateMove` — **DISPROVEN**

Every call site through `0x107B5F98` at index 14 (five) and through
`0x1089D0CC` at index 21 (six) pushes **no arguments**, and several sit at
function prologues immediately after the previous function's `ret`. At one index-14
site the return value is tested with `test al,al` — it returns a bool.

`OverrideView(CViewSetup*)` and `CreateMove(float, CUserCmd*)` are thiscall
methods with stack arguments. Neither can be these.

**No object in the binary is called at both index 14 and index 21**, which would
have to be true if both were `IClientMode` methods.

**Fix:** indices must be derived per object, from the object's own vtable, and
confirmed by argument shape. See `PLAN.md` §3.

### D04 — Duplicate translation unit — **OPEN** (CC #1b)

`ContagionVR.vcxproj` lists both `d3d9_hook.cpp` and `d3d9_hook_1.cpp`; both
define `D3D9Hook_Init` and `D3D9Hook_Shutdown`. `_1` is the newer. Drop the
older from the project and from the tree.

### D05 — `Xr_SetHaptics` shadows its own parameter — **OPEN** (CC #1c)

`xrapi.cpp:154`: `void Xr_SetHaptics(float l, float r){ std::lock_guard<std::mutex> l(g_hapMtx); … }`.
`l` is both parameter and guard, in one scope. Rename the guard.

### D06 — `<mutex>` is not included where it is used — **OPEN** (CC #1d)

`xrapi.cpp` uses `std::mutex` and `std::lock_guard`; the include list has
`<thread>` and `<atomic>` but not `<mutex>`. It compiles today only if a
transitive include supplies it. Include it.

### D07 — `version.dll` proxy does not export the full set — **OPEN** (new)

`dllmain.cpp` forwards ten names. The 32-bit system `version.dll` exports
**seventeen** — verified against `C:\Windows\SysWOW64\version.dll` **[verified]**.
Seven are missing from the forwarder list:

`GetFileVersionInfoByHandle`, `GetFileVersionInfoExW`,
`GetFileVersionInfoSizeExW`, **`GetFileVersionInfoSizeW`**, `VerFindFileW`,
`VerInstallFileW`, `VerLanguageNameW`.

A missing export is not a silent no-op: an importer resolving it fails, which
presents as "the game will not start".

**But the opposite mistake is worse, and `XOPS-WXR` paid for it.** That proxy
forwarded exports the *real* DLL did not provide, and a call through the
forwarder landed on **address 0** (`SIBLING_PORTS.md` §2.1). So neither "all of
them" nor "the ones we need" is the right list:

> Forward the **intersection** — the exports the `version_o.dll` we actually ship
> provides, plus the ones the game's importers require. Generate it at build
> time from that DLL, not from a hand-written list.

That also settles which `version_o.dll` matters: the one Wine loads in the
container, not the one on the build machine, if they differ. Enumerate the *system* DLL's exports at
build time and generate the forwarder list rather than hand-maintaining it.

### D08 — `LoadLibraryA("version_o.dll")` inside `DllMain` — **OPEN** (new)

Runs under the loader lock, and is unnecessary: the export forwarders already
cause the loader to resolve `version_o.dll` on first call. Move it to the init
thread.

### D09 — No log, no build stamp, no deploy guard — **OPEN** (CC #20)

There is no logging of any kind. This is why none of the inherited claims could
be checked, and why `PLAN.md` gates everything on it.

**Fix:** `__DATE__ __TIME__` compiled in, logged on line 1, plus a
build-and-deploy script that refuses a binary older than the newest source file
and verifies SHA-256 at each target.

---

## B. Camera and aim mathematics

### D10 — Yaw is taken from `asin`, so it saturates at ±90° — **CONFIRMED** (CC #2)

`math.cpp:59` `QuatToSourceAngles`: `pitch` from `atan2`, **`yaw` from `asin`**,
`roll` from `atan2`. One angle of any Euler triple must come from `asin` and it
is the middle rotation; taking yaw from it means turning past 90° reverses.
There is also no OpenXR→Source basis change, so the three angles are rotations
about the wrong axes as well as mislabelled.

**Fix:** rewrite. Rotate the canonical axes by the quaternion, flip for
handedness, derive `yaw = atan2(fwd.y, fwd.x)` (full ±180°), `pitch` from
`fwd.z`, and take **roll as the residual rotation about the head's own forward
axis** — the signed angle between the head's actual up vector and the up vector
of the yaw/pitch-only basis, measured about `fwd`.

Do **not** derive roll from the up vector before yaw and pitch are removed.
`Interstellar-Marines-WXR` names that exact mistake: a world-up projection let
yaw and pitch leak into roll, so the horizon tilted when the player looked
around. Its fix is the same shape as above — take roll from the quaternion's own
local Z, never from a world-space up vector. See `SIBLING_PORTS.md` §1.4.

**How we will know:** log yaw while the owner turns a full circle. It must sweep
360° monotonically. The inherited code cannot pass that test.

### D11 — The lean is applied in the wrong frame — **CONFIRMED** (CC #3)

`GetHeadDeltaClamped` returns the head delta in **physical room space**
converted to Source axes and scaled to units. `hkOverrideView` adds it straight
to `setup->origin`, which is **world space**. Nothing rotates one into the other.

Result: leaning forward always moves the view along the same world axis,
regardless of which way the player faces. This is `TIPS.md`'s
*measure offsets in the frame you will use them in* — no amount of clamp tuning
fixes a wrong frame.

**Fix:** rotate the room-space delta by the accumulated turn offset `g_yawOffset`
(D14) — **by that alone**, not the full view yaw, because the HMD's own yaw is
already in the delta.

### D12 — The lean clamp is per-axis, so a diagonal lean reaches ~26 cm — **OPEN** (new)

`math.cpp:40-42` clamps `x`, `y` and `z` independently to ±6 units. The intent
is "~15 cm", and 6 units is 15.2 cm. But a pure diagonal can reach
`6·√3 ≈ 10.4` units ≈ **26 cm**, and a two-axis diagonal ≈ 21 cm.

That is a real anti-wall-peek margin given away. The rule says the hull is never
written, and it is not — but the *visual* offset still lets a player push their
eye through a wall edge further than intended.

`Interstellar-Marines-WXR` hits the same requirement and uses a **magnitude**
clamp, `Vector3.ClampMagnitude(hPos, 0.3f)`, in `VRHeadTracker.LateUpdate` —
device-tested, magnitude-based, and with a deliberately larger 0.3 m budget than
our 0.15 m. Per-axis clamping is not what this family does.

**Fix:** clamp the vector by magnitude: `if (len > kMax) v *= kMax/len`. One
line, and it makes the constant mean what it says.

### D13 — `setup->angles` is never written, so head roll does nothing — **CONFIRMED** (CC #16)

`hkOverrideView` writes `origin` and `originViewmodel` only. Orientation comes
entirely from `cmd->viewangles`, set a tick earlier. Head tilt is invisible, and
every orientation change has a tick of extra latency.

**Fix:** set `setup->angles` in the view hook, including roll (once D10 gives
correct angles). Keep the split: `CreateMove` owns logical aim (yaw/pitch),
the view hook owns the visual camera (full orientation plus lean).

### D14 — Smooth turning does not accumulate, and is frame-rate dependent — **CONFIRMED** (CC #6)

`hkCreateMove` writes the HMD yaw absolutely, then `Input_Update` adds the
stick's turn to the same `cmd`. The next tick the absolute write discards it, so
the stick can only ever deflect the view by one frame's worth of turn.

Also: `turn = rx * turnSpeedDegPerSec * (1.0f/60.0f)` — a hardcoded 60 Hz
assumption, while `CreateMove` receives the real `frametime` and ignores it. Turn
speed will scale with frame rate. And there is no wrap to ±180° and no pitch
clamp to ±89°.

**Fix:** persistent `g_yawOffset += stickX * degPerSec * dt`, then
`viewangles.yaw = wrap180(hmdYaw + g_yawOffset)` and
`viewangles.pitch = clamp(hmdPitch, -89, 89)`. The same `g_yawOffset` is what
D11 needs.

### D15 — Outbound FOV echoes the inbound packet, and the guard does not fire — **CONFIRMED, corrected** (CC #9)

`SendLoop` reads `fr.fovH/fovV` (from the inbound packet) and sends them back,
with `if(fH<10) fH=104.5f;`.

ClaudeCode said the guard catches every tangent so the code always sends 104.5.
**It does not.** The one sample packet in
`Docs/general_wxr/SixDOFinatorCalibration/SamplePoseData.txt` carries `99.00` and
`103.40` in those fields; both exceed 10, so the guard never runs and the mod
transmits the runtime's own numbers as its own FOV.

Even where the echo happens to be numerically acceptable, it is not what the game
rendered. `TIPS.md` is explicit: send the FOV you actually rendered with, taken
from the live frame every frame, because games change FOV for zoom, aspect and
cutscenes.

**Fix:** drop the echo entirely. Send the FOV the game is actually rendering
with, taken from the live view **every frame** — and derive it from the real
projection rather than from a constant.

`XOPS-WXR` does exactly this: it recovers its outbound pair (`80.7° / 65.0°`)
from the actual projection matrix, recomputing when the aspect changes, and
sends degrees (`SIBLING_PORTS.md` §2.5). That is the only version of this that
survives a zoom, an aspect change or a cutscene. Keep a configured pair as an
override for tuning, defaulting to 104.5 / 104.5 when nothing better is
available.

---

## C. Sync pixel

### D16 — Wrong size, wrong hook, wrong time, and invented values — **CONFIRMED, FIXED AND VERIFIED 2026-11-18** (CC #4, #8)

Three independent deviations plus one harmful invention:

| Inherited | Required |
|---|---|
| 16x16 | **10x10** |
| Drawn in `EndScene` | **last, immediately before `Present`** |
| Alpha blending toggled only | blending **and** all three filters off, fog and Z off, render target 0 set to the back buffer |
| `if(!fr.hasData) frameId = GetTickCount() & 0xFF` | **echo `HMD_SYNC` exactly**, or draw the last good value, or draw nothing |

The `GetTickCount()` fallback is the serious one. `XrFramesync` calibrates itself
to the distinct red values it observes, persists the table, and derives the
game's frame rate from changes in this pixel. Clock-derived values pollute that
table, and they are emitted on exactly the frames where the mod is deliberately
in `MODE_VR = 0` — menus and pre-connection.

**Fix:** see `PLAN.md` §6. Draw in `Present`, set the render target explicitly,
10x10 at (0,0), `RGB(clamp(sync), 0, 0)`, last.

**Implemented and verified 2026-11-18.** `Clear(1, {0,0,10,10}, D3DCLEAR_TARGET,
XRGB(sync,0,0))` at `Present`; the back-buffer dump and a screen grab both show
`(0,0)..(9,9)` as the only red block, `(10,10)` as scene colour, green 0.

**The red value that arrives is not the byte we pass, and that is correct.** 127
was stored and presented as **187**; 12 as **61**. Those are the linear→sRGB
values (sRGB(0.498)=0.734) — the engine's back buffer is written in linear space
and the clear colour is encoded on the way in. `XRAPI_V0_4_DESC.md` §5 settles
the question: the runtime **learns the observed mapping** (22 distinct values,
step 12), and explicitly says not to pre-correct or disable
`D3DRS_SRGBWRITEENABLE`. So we echo `HMD_SYNC` exactly and leave the colour space
alone. Verified monotonic and distinct at both ends of the range tested.

---

## D. Window and input

### D17 — The window is never made borderless — **OPEN** (CC #7)

Nothing touches window style or size. This is a `Docs/general_wxr/CLAUDE.md`
non-negotiable and a prerequisite for D16: with a title bar, the runtime samples
chrome at (0,0) and head tracking degrades silently.

**Fix:** idempotent `EnforceWindow()` called per frame, stripping
`WS_CAPTION | WS_THICKFRAME | WS_BORDER` and sizing to the display. Log window,
surface and render sizes on change, and expect the engine to fight
(`CheckRenderer()`-style consistency checks win a once-a-second argument).

**Run 1 makes this smaller than it was.** On the PC copy the window is *already*
borderless and exactly screen-sized: `style = 0x14000000` (`WS_VISIBLE |
WS_CLIPSIBLINGS`, no caption/frame) and client 1920x1080 on a 1920x1080 screen
(`RUNTIME_CONFIRMATIONS.md` §4). Nothing needs doing there, which matches what
Interstellar Marines found in its container. So this entry is now **container
only**: check the same two numbers under 1024x768 on the Quest and act only if
they differ. Keep the implementation, skip the urgency.

**Run 6 measures the device itself as fullscreen**: `windowed=0`, 1920x1080 at
60 Hz, `hwnd=0x006D064C`. A fullscreen device already fills the display with no
chrome, and `D3DERR_DEVICELOST` is the documented cost of restyling it. So the
likely correct action is **none** on both PC and Quest; verify the logged
`windowed=` flag on the device before touching any window style.

**Three constraints from `mirrors-edge-WXR`, one of which is a measured hazard**
(`SIBLING_PORTS.md` §3.2):

1. **Never restyle the window from the hook's thread.** That port measured a
   cross-thread deadlock at **0.9 fps with a black screen** doing exactly what
   this fix would naively do on the render thread. Subclass the window and
   **POST** the work to the thread that owns it.
2. **Rate-limit and give up.** Re-applied per frame but acted on every ~30th
   frame, and after ~30 failed attempts log *"the window keeps leaving 0,0 —
   giving up"* and stop. Fighting forever costs frames and floods the log.
3. **Skip it entirely if the device is fullscreen.** Stripping styles from a
   fullscreen device fights `Reset` and yields `D3DERR_DEVICELOST`. If the
   container runs Contagion fullscreen, the correct action may be none — decide
   from the logged window styles, not from a guess.

### D18 — The window class is `Valve001` after all — **WITHDRAWN / NOT A DEFECT** (new, then corrected)

I claimed `Valve001` appears in none of the shipped binaries. Run 1 shows the
window is exactly that class; it is present in `engine.dll` as **UTF-16**, and my
scan searched 8-bit strings only (`RUNTIME_CONFIRMATIONS.md` §4).

The inherited `strstr(cls, "Valve001")` lookup would therefore have found the
window on this game. The remaining, smaller points are real and live in D17/D19:
the lookups must not fall back to `GetForegroundWindow` (which was not ours even
on PC), and all three inputs the game receives under a container need testing.

**Lesson for the file, not just the entry:** a static "X does not exist" needs a
run-time check before anyone acts on it. This was the second confident wrong
negative in the project's history; the first cost a project's worth of offsets.

### D19 — Input blocking is unscoped: no console, no chat, and no focus handling — **OPEN** (CC #20)

`wndproc.cpp` swallows every key except Escape. `~` and the chat keys are gone.
On a device with no debugger the console is a diagnostic channel; this removes
it in the name of not needing it.

**The second half is missing entirely: focus.** `mirrors-edge-WXR` had to swallow
`WM_KILLFOCUS`, `WM_ACTIVATE(WA_INACTIVE)`, `WM_ACTIVATEAPP(FALSE)` and
`WM_NCACTIVATE(FALSE)` because UE3 treats them as "lost focus" — it pauses audio
and sets `bIgnoreMoveInput`. A container is exactly the environment that steals
focus, and a Source game has the same reactions: a self-pause, audio ducking, or
movement that silently stops. Nothing in the inherited code handles this, and the
symptom ("it renders but ignores the stick") would have been blamed on the input
mapping (`SIBLING_PORTS.md` §3.2).

**Fix:** allow the console key through; allow all keyboard input while a menu is
open; swallow the four focus-loss messages **while on device** (`Z:\tmp` present)
and leave alt-tab alone on the PC test target; and re-assert foreground from the
window's own thread, bounded, with a log line. Drive all of it from the same
single gate as D22.

### D20 — Pause detection rests on fabricated interfaces — **CONFIRMED** (CC #5)

`IGameUI` is declared locally with `IsGameUIActive` at index 8. `GameUI011` does
exist (good), but the real `IGameUI` has more methods before that one, so the
index is a guess and a wrong index calls a *valid but different* function —
which `__try/__except` cannot catch. `ICvar` is declared with `FindVar` at index
1 and `ConVar::GetBool` at index 13; both are guesses on invented layouts.

**The `__try/__except` wrappers do not rescue any of this.** They catch access
violations, not calls that land on the wrong valid function, which is the likely
outcome and the worse one because it corrupts state silently.

**Fix:** `g_pCVar` is an **exported global** in every module of interest
(`BINARY_RECON.md` §4) — obtain the object from `GetProcAddress`, not from a
guessed interface version. Then derive `FindVar`'s index and *confirm it with a
log line* before use. For pause detection, prefer the ConVars (`gameui_activated`,
`gameui_hidden`, `gameui_ingame_menu_active` all exist **[bin]**) over `IGameUI`.

### D21 — Input command names are wrong in four places — **DISPROVEN / OPEN** (CC #10)

From `contagion/cfg/config.cfg` **[verified]**:

| Inherited | Real |
|---|---|
| `phone` | **`phone_or_blend`** |
| `inv_next` / `inv_prev` | **`weaponselect_next`** / **`weaponselect_prev`** |
| melee = `IN_ATTACK2` | **`+melee`** |
| `vocalize <Event>` | **`+voicecmds`** already exists as a bound command |

`drop`, `dropammo` and `impulse 100` are correct. `dropunusedammo` is the third
drop command and is unused.

**Fix:** use the real names, and call the game's own command path rather than
synthesising keys (`Docs/general_wxr/PC_INPUT.md`).

### D22 — `cmd->buttons = buttons` discards what the game set — **CONFIRMED** (CC #10c)

Every tick the whole button field is replaced with a value derived only from XrApi.
Anything the engine had already set — and anything the runtime's own mouse
contributes — is dropped. Deliberate, probably, but undocumented and combined
with a keyboard block that is installed on the wrong window (D18).

**Fix:** make it an explicit decision, log it in the code, and OR rather than
assign if the runtime's mouse is meant to survive.

### D23 — Class detection is a stub — **CONFIRMED** (CC #10d)

`Input_DetectPlayerClass()` returns `Human` unconditionally; the zombie branch is
dead code.

**Deferred** with the human/zombie profile work. Nothing before it depends on it.

---

## E. XrApi client

### D24 — No quaternion normalisation — **CONFIRMED** (CC #13)

All three quaternions are stored exactly as parsed. `Docs/general_wxr/CLAUDE.md`
makes normalisation on ingest unconditional: the rotation shortcut scales by
`|q|²` for a non-unit quaternion, and tester packets in this family have been
1.4 %–10.8 % long. Real headsets are unit to ~1e-4, so this is a *tester* hazard
that presents as a deep pose bug.

**Fix:** four lines in the parser, degenerate case → identity.

### D25 — The socket is never drained — **CONFIRMED** (CC #14)

`RecvLoop` blocks on `recvfrom` and parses one datagram per iteration. The newest
packet does win eventually, but under a burst the thread works through a backlog
of stale poses instead of discarding them.

**Fix:** drain until `WSAEWOULDBLOCK`, keep the newest.

Related, comment only: `Xr_Init` binds 7872 and falls back to 7873 with a comment
calling it a "fallback". From 0.4 the runtime sends the same packet to both.
Binding one is correct; the comment invites the next reader to bind both.

### D26 — Handshake is incomplete — **CONFIRMED** (CC #12)

Writes `Z:\tmp\xr\version` = `0.4` and stops. Missing:

- the **`vr`** file (`VR`), deleted on clean shutdown;
- any read of **`system`** — seven lines, the headset identity, and the cheapest
  "am I on a device?" test available;
- a **desktop fallback** path, so one binary runs on both;
- version **0.5** rather than 0.4 (0.4's packet plus the InputAPI listener at no
  cost).

Also: the runtime **wipes the directory at startup**, so files written before it
is up are lost. The inherited code writes once, 1.2 s after `DllMain`, and never
rechecks.

**The policy to copy** is the one `Interstellar-Marines-WXR` implemented and
device-tested on 7 Nov 2026 (`XRAPI.cs`, `Docs/OpenCode/DeepSeek41Flash/HANDSHAKE.md`
there); see `SIBLING_PORTS.md` §2.

| Rule | Why |
|---|---|
| Path: `Z:\tmp\xr` on device; **test `Z:\tmp`**, not `Z:\` | `Z:` alone is not a device marker; `Z:\tmp` is (owner's decision) |
| Fallback directory: `D:\xrtemp` | desktop test box has no device mapping; replaces the whole path, not just the leaf |
| Write with plain `CreateFileA`/`WriteFile`, **ASCII, no BOM** | the runtime matches `version` with `startsWith`; a BOM makes `0.5` unrecognised and the result is total silence, not an error |
| Write `version` **and** `vr` unconditionally at init, then **re-check every 2 s** and rewrite if missing or wrong | the runtime wipes the directory at startup; a one-shot write can be erased before it is read |
| Read `system`; retry every 2 s until it parses; **do not latch a partial read** | it is written by the runtime at its own startup, so it may be absent or half-written |
| Delete `vr` on clean shutdown; leave `version` | `vr` is a live claim, `version` is a protocol statement |
| Log the chosen directory and whether it was `Z:\tmp` or the fallback | one line answers "did I even find the device?" |

### D27 — Packet parser is fragile in two ways — **OPEN** (new)

- It requires `parts.size() >= 30`, which is fine for 19-char bool strings but
  rejects a legitimate short packet. Bound-check instead of gating on a total.
- It reads `boolStr` and then treats the *next* token as the HMD name. With the
  real 21-char string the flags (`TF`/`FF`) are inside `boolStr` at indices 19–20
  and are silently discarded. Harmless in mono; required in any
  runtime-owned-presentation mode (`MODE_VR = 2` with `MODE_3D = -1`), where bool
  19 is the only way to learn the runtime's decision.

The thumb-direction indices 3–6 and 14–17 remain guesses. Build the table from a
device log of one press per control.

### D28 — Haptics decay is tied to the send loop, not to time — **CONFIRMED** (CC #20)

`g_hapticL *= 0.9f` every ~11 ms, so the decay rate drifts with scheduling. The
runtime stores only values `> 0`, so sending 0 does not clear — decay and keep
sending is the right shape, but it should be scaled by elapsed time.

Nothing calls `Xr_SetHaptics` anyway, and the function does not compile (D05).

---

## F. Hooking, D3D9, build

### D29 — The bundled MinHook cannot survive a short prologue — **FIXED 2026-11-17** (CC #18)

`minhook.cpp` copies five bytes, writes a `JMP rel32`, and appends a jump back.
No disassembly, no relocation of relative instructions. Any function whose first
five bytes contain a relative jump or call, or whose prologue is shorter than
five bytes, is silently corrupted — on a device with no debugger.

**Fixed:** real MinHook is vendored at `third_party/minhook/` (BSD-2, HDE32/HDE64,
trampolines, `LICENSE.txt` alongside), compiled into the DLL. `D3d9_Init` uses
`MH_CreateHook` on the d3d9 function addresses.

**A related discovery made the hard way, worth keeping:** the device's **vtable is
per-instance and is freed when the device is released.** The first attempt read
the dummy device's vtable, released the dummy, then `VirtualProtect`ed the block
— which by then reported `MEM_RESERVE`/`PAGE_NOACCESS` and failed with error 487.
Patching a dummy's vtable is therefore *not* a way to reach the game's device, even
though the technique is widely described that way. The vtable **entries**, however,
point into `d3d9.dll`'s shared code, so hooking the functions themselves works.

### D30 — The dummy D3D9 device may fail under Wine, silently — **CONFIRMED** (CC #19)

`D3D9Hook_Init` creates a throwaway `IDirect3DDevice9` against
`GetDesktopWindow()` to read the vtable, trying `HAL` then `REF`, then giving up.
The vtable indices used (Reset 16, EndScene 42) are correct for D3D9. The risk is
device creation under Wine on DXVK/WineD3D on Android, and the failure path
returns `void`, so nothing upstream learns.

**Fix:** log success/failure explicitly. If creation fails on device, take the
device from the game (`shaderapidx9.dll`) rather than manufacturing a second one.

**Measured 2026-11-17 — it fails on Windows too, and for a structural reason.**
A throwaway device is created successfully in a host process, but **inside the
game it fails with `0x88760868` (`D3DERR_DEVICELOST`)**, for both `HAL` and
`REF`, because the game already holds the adapter. So "create a dummy to read the
vtable" is not merely a Wine risk; it does not work while the game is running.

The working route, now implemented and verified in game on 2026-11-18:
**own the interface instead of searching for it.** MinHook the plain
`__stdcall` `Direct3DCreate9` export, return our own thin `IDirect3D9` wrapper
(whose `CreateDevice`/`CreateDeviceEx` are ours), and take the real
`IDirect3DDevice9` from that call's `out` parameter. Its own vtable slots are
then patched for `Present`/`BeginScene`/`EndScene`. Patching a *device* table
is safe once the pointer came from `CreateDevice`; patching d3d9's shared
`IDirect3D9` table is not (D43). See `PLAN.md` §6 and `NIGHT_REPORT.md`.

### D31 — DirectX SDK June 2010 is required for `d3dx9` — **OPEN** (CC #20)

`ID3DXLine` and `ID3DXFont` are used by the voice wheel, which is deferred.
`d3dx9.lib` is a large setup cost for a milestone-10 feature.

**Fix:** for the first build, use pure D3D9 for the sync square (a
`DrawPrimitiveUP` with `XYZRHW|DIFFUSE`, as `DrawFilledRect` already does — that
part has no d3dx dependency). Either drop d3dx entirely or isolate it behind the
voice-wheel build.

### D32 — Fixed `Sleep`s and a modal error box at init — **OPEN** (CC #20)

`Sleep(1200)` then `Sleep(600)` before hooking is a guess that will be wrong on
slower hardware — and a Quest 3 under Box86 *is* slower hardware. `Hooks_Init`
failure raises a `MessageBoxA`, which on a headset blocks the game behind a
dialog the player may not be able to dismiss.

**Fix:** poll for the condition, and log instead of showing a modal.

### D33 — `PatternScan` ignores `GetModuleInformation`'s result — **OPEN** (CC #20)

It would scan from an uninitialised base on failure. Check the return value.

### D34 — `Xr_Shutdown` closes sockets before joining threads — **OPEN** (CC #20)

It races the blocked `recvfrom`. It works because the close unblocks the call,
but the order is backwards and the reason is not obvious.

### D35 — `Hooks_Init` has no retry and `WndProcHook_Init` runs once — **OPEN** (CC #20)

Both run once, 1.8 s in. If `client.dll` is not loaded yet, or the window does
not exist, or the engine recreates the window on a video-mode change, the hook is
lost with no record. Combined with D18, the window hook is lost *every* time.

---

## G. Withdrawn, or promoted to rules

### D36 — Monocular is "2× cheaper than stereo" — **NOT A DEFECT** (CC #21)

Arithmetic inverted in the phrasing: mono is 1×, SBS is 2×, so stereo is twice as
expensive rather than mono being half price. The decision is right.

### D37 — Visual-only lean with no collision change — **NOT A DEFECT** (CC #22)

Correct, and now a hard rule. Keep the behaviour and keep the comment. See D12
for the one part of it that is too permissive.

### D38 — Hand pose routed through the head transform — **DEFERRED** (CC #17)

For a *viewmodel offset*, expressing the hand in head-local space is defensible.
The `0.15` scale factor is not: it has no stated frame of reference, which is
exactly the kind of value `TIPS.md` says cannot transfer. Deferred with hands
(milestone 8); re-express the grip offset in the gun's frame when it is picked up.

### D39 — Numeric formatting is locale-dependent in both directions — **OPEN** (new, sibling-port review)

`ParseFloat` uses `std::stof`, and `SendLoop` builds its packet with
`sprintf_s("%.3f ...")`. **Both honour the C locale's decimal separator.** If
anything in the game calls `setlocale(LC_NUMERIC, ...)` for a comma-decimal
language, then:

- inbound `0.210` is parsed as `0` (stopping at the `.`), so every pose collapses
  to zero and the world freezes — with no error anywhere;
- outbound `0.500` is written as `0,500`, splitting one field into two and
  shifting every field after it, which the runtime reads as garbage.

`Interstellar-Marines-WXR` hit this from the other side and documents the fix in
`XRAPI.BuildOutPacket`: **always format invariantly**. In C# that is
`CultureInfo.InvariantCulture`; in C there is no direct equivalent, so:

- **Inbound:** parse with `_strtod_l` against a cached `_create_locale(LC_NUMERIC,
  "C")`, or scan the number by hand (which also avoids per-packet `std::string`
  allocation — IM's parser rewrite is the model, `SIBLING_PORTS.md` §2.5).
- **Outbound:** build the six fields with the same C locale, or post-process the
  buffer to replace `,` with `.`. Cheaper still: format integers and scale by hand.

This is invisible on an English machine and permanent on a localised one, which
is the worst combination for a device-only test loop. **Fix before the first
deploy**, and log the effective decimal separator once at startup.

### D40 — The log will silently concatenate runs, which breaks the build-stamp check — **OPEN** (new, sibling-port review)

The whole verification ritual says *"line 1 of the log is the build stamp"*.
That is only true if the file is new.

`mirrors-edge-WXR` records that **NTFS file tunnelling** reused creation times
and silently concatenated successive runs into one log, which forced it to parse
the stamp out of the file's own first line instead of assuming it
(`SIBLING_PORTS.md` §3.8). Delete-and-recreate within the tunnelling window is
exactly what a mod does on every launch.

The failure is quiet and precisely wrong: line 1 holds an *old* stamp, the check
passes, and a stale binary is tested as new — the one mistake the freshness
guard exists to prevent.

**Fix:** open with `CREATE_ALWAYS` **and** write one file per run
(`contagion-wxr-<yyyymmdd-hhmmss>.log`, keep the last N), or carry an explicit
session banner and an incrementing session number that the check reads. Do not
rely on the path alone. Also log the **file size at open** for the first few
runs while this is being confirmed.

### D41 — The pose stream can be applied twice, or from a stale sample — **OPEN** (new, sibling-port review)
Two sibling ports handle pose freshness explicitly and we do not:

- `mirrors-edge-WXR` **latches a frame only when `HMD_SYNC` has advanced**, so
  one pose is consumed per game frame and a duplicated datagram cannot be
  applied as if it were new (`SIBLING_PORTS.md` §3.3).
- `Docs/general_wxr/XRAPI_UPGRADE.md` §2 gives the same rule for anything
  differenced: advance the previous sample **only when the sync value moved**.

Neither the inherited receive loop nor any planned code consults `HMD_SYNC`
except as a number to draw. At 72–90 Hz in against a 60–66 Hz tick, this matters
for `CreateMove`, and it matters for the sync pixel (a repeated id is a repeated
frame as far as the runtime's FPS estimate is concerned).

**Fix:** treat `HMD_SYNC` as the pose's identity. Keep the last consumed sync
per consumer (tick hook, view hook); skip a pose whose sync equals the last one
consumed, and hold the last good pose if a newer one does not arrive. Log a
counter of skipped duplicates behind the verbose flag.

### D42 — A fresh binary can carry a stale build stamp — **FIXED 2026-11-17, untested** (found in our own code)

With `__DATE__ __TIME__` compiled into `dllmain.cpp`, an incremental build that
recompiled other files but not `dllmain.cpp` produced a **new** `version.dll`
whose line 1 reported the **previous** build time. Observed directly: a 19:00
build logged `built 17:24:58`.

This is the exact confusion the stamp exists to prevent, and it would have been
invisible without watching the number change between builds.

**Fix:** the stamp now lives in its own translation unit, `src/buildstamp.cpp`,
which the build script regenerates before every build so it always recompiles.
`dllmain.cpp` declares `extern const char kBuildStamp[]`. Verified: the next
build logged its own time.

**Generalisable rule:** a build stamp must live in a file that is touched on
every build, or it reports when that file last changed, not when the binary was
built.

### D43 — D3D9 COM detours were `__fastcall`, so no detour could have worked — **FIXED 2026-11-18**

Every D3D9 detour in the earlier builds was declared `HRESULT __fastcall
HookX(IDirect3DDevice9* self, void* edx, ...)`, on the theory that COM methods are
thiscall and `this` belongs in `ECX`. On 32-bit Windows, D3D9's
`STDMETHODCALLTYPE` is **`__stdcall`**: `this` is the first *stack* argument.
`__fastcall` reads `ECX` (garbage) as the object and pops a different number of
bytes than the caller expects, which corrupts the stack. This is a direct
explanation for the "patched and proven written, never entered" and the
`0xC0000005` outcomes of 2026-11-18.

The distinction matters twice over:

- **D3D9 COM interfaces:** `__stdcall`. `this` on the stack. Fixed in all
  Present/BeginScene/EndScene/CreateDevice detours and in the wrapper.
- **Source engine C++ interfaces** (`IViewRender` and friends): MSVC member
  functions, i.e. `__thiscall`, `this` in `ECX`. The `__fastcall` idiom remains
  correct there, and `camera.cpp` relies on it — but its thunks do not touch the
  signature at all, they forward it.

**Rule:** never guess a calling convention from "COM" or "virtual"; check
`STDMETHODCALLTYPE` versus plain `virtual`, and prefer a naked forwarding thunk
when the arity is unknown.

### D44 — Log pruning could delete the file it was writing — **FIXED 2026-11-20**

`PruneOldLogs()` ran *after* the new log was created, and its pattern
(`contagion-wxr-*.log`) also matched the launcher's own logs. On device storage
the last-write timestamp has two-second granularity, so the new file can tie
with files from the same run; if five files sorted at least as new, the new log
— still open for writing — was deleted. Its handle stays valid, so the mod keeps
logging into a file nobody can see.

**Fix:** prune *before* creating this run's file, and narrow the pattern to
`contagion-wxr-2*.log` so launcher logs are neither counted nor deleted.

This is one of the candidate explanations for the 2026-11-20 device run, which
produced a launcher log and no mod log (`RUNTIME_CONFIRMATIONS.md` Run 7). It is
fixed either way; the launcher now also proves attachment by watching for the
file.
