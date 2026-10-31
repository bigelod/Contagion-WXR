# BUG_FIXES.md — defect register, opened 2026-10-31

Every defect found in the inherited Meta AI code, ranked by consequence, with a
status line. This is the work list that milestones 0–2 in
[PROJECT_PLAN.md](PROJECT_PLAN.md) burn down.

**Status vocabulary**, following the convention in the sibling
Interstellar-Marines-WXR port:

| Status | Means |
|---|---|
| **OPEN** | found, not yet addressed |
| **FIXED `<date>`, untested** | changed in code, not yet verified on the Quest 3 |
| **FIXED `<date>`, verified** | owner has confirmed on hardware |
| **DEFERRED** | real, but deliberately not now; the reason is given |
| **NOT A DEFECT** | investigated and withdrawn; kept so it is not re-raised |

**Nothing in this file has been observed at runtime.** The code has never
compiled, so every entry is a defect found by reading. Each is tagged with how
it was established: **[code]** read from the source, **[spec]** contradicts
`../general_wxr/`, **[verified]** checked against the owner's install,
**[unverified]** an assumption of the inherited code that may or may not hold.

Line references point into
`../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/`, the variant
with the bundled MinHook. The other two folders are byte-identical apart from
that one file.

---

## 1. The project does not compile — four blocking errors — OPEN

**[code]** Status overrides everything else in this file: no line of the
inherited code has ever executed.

**a. `CUserCmd` is missing six members that the code uses.**
[src/hooks.h:3](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/hooks.h#L3)
declares:

> `struct CUserCmd{ char pad0[12]; QAngle viewangles; };`

while `input.cpp` and `hooks.cpp` read and write `cmd->buttons`,
`cmd->forwardmove`, `cmd->sidemove`, `cmd->mousedx`, `cmd->mousedy` and
`cmd->impulse`. Six compile errors.

The 12-byte prefix is itself a guess carried from Source SDK 2007, where
`CUserCmd` has a vtable pointer, `command_number` and `tick_count` ahead of
`viewangles`. On this engine fork that is **[unverified]**.

**b. Duplicate symbols.** `d3d9_hook.cpp` and `d3d9_hook_1.cpp` are both listed
in `ClCompile` in `ContagionVR.vcxproj`, and both define `D3D9Hook_Init` and
`D3D9Hook_Shutdown`. `_1` is the newer file — it is the one with the frame-id
marker. The older one must be dropped from the project.

**c. A redeclared parameter.**
[src/xrapi.cpp:154](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/xrapi.cpp#L154):

> `void Xr_SetHaptics(float l, float r){ std::lock_guard<std::mutex> l(g_hapMtx); ... }`

`l` is both the parameter and the lock guard, in the same scope.

**d. Missing include.** `xrapi.cpp` uses `std::mutex` and `std::lock_guard`
without `#include <mutex>`.

**Fix:** all four are mechanical. (a) must wait until the real `CUserCmd`
layout is derived from the shipped binaries — see #11.

---

## 2. Yaw is capped at ±90°, so the player can never turn around — OPEN

**[code]** The highest-consequence *logic* defect. In
[src/math.cpp:59](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/math.cpp#L59),
`QuatToSourceAngles` extracts Euler angles and assigns them to the wrong names:

- `pitch` is taken from `atan2` of the terms that give rotation about X
- **`yaw` is taken from `asin`**, whose range is −90° to +90°
- `roll` is taken from `atan2` of the terms that give rotation about Z

In any Euler extraction exactly one angle comes from `asin` and it is the
*middle* rotation of the sequence, which is bounded. Taking **yaw** from it
means the reported heading saturates at ±90°: turn your head past 90° and the
yaw starts coming *back*. The world would fold in half.

There is also no basis change from OpenXR's convention (+X right, +Y up, −Z
forward) to Source's (+X forward, +Y left, +Z up), so the three angles are not
merely mislabelled, they describe rotations about the wrong axes.

**Fix:** rewrite. Rotate the canonical OpenXR axes by the quaternion, flip for
Source's handedness, and derive the angles from the resulting basis vectors —
`yaw = atan2(forward.y, forward.x)` with full ±180° range, `pitch` from
`forward.z`, `roll` from the up vector. `../general_wxr/XRAPI_V0_4_DESC.md` §6
is explicit that you should rotate the axes and flip the results rather than
trying to mirror the quaternion itself.

**How we will know:** log the reported yaw while the owner turns a full circle.
It must sweep 360° monotonically. This is the test the inherited code could
never have passed.

---

## 3. The lean is applied in the wrong frame — OPEN

**[code]** `GetHeadDeltaClamped`
([src/math.cpp:30](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/math.cpp#L30))
returns the head's offset from the recentre origin, converted to Source axes
and scaled to units. `hkOverrideView` then adds it straight to
`setup->origin`.

But that delta is expressed in **physical room space**, and `setup->origin` is
in **game world space**. Nothing rotates one into the other. So "lean forward"
always moves the view along a fixed world axis, regardless of which way the
player is facing in the game.

This is precisely the failure `../general_wxr/TIPS.md` describes under *measure
offsets in the frame you will use them in*: no amount of adjusting the clamp
fixes it, because the frame is wrong.

**Fix:** rotate the room-space delta by the accumulated turn offset
(`g_yawOffset` from #6) before adding it. **By that offset alone** — not by the
full view yaw — because the HMD's own yaw is already expressed in the physical
delta. Adding it twice would make the lean swing as the head turns.

The axis mapping itself (`{-z, -x, y}`) is **correct** and should be kept; only
the missing rotation is at fault.

---

## 4. The sync pixel is the wrong size, in the wrong place, at the wrong time — OPEN

**[spec]** Three deviations from `../general_wxr/XRAPI_V0_4_DESC.md` §5, in a
subsystem the general guidance calls the highest-value thing in the whole
project:

| Inherited | Required | Why it matters |
|---|---|---|
| **16x16** square | **10x10** | Harmless in itself, but gratuitous divergence from the documented and proven value |
| Drawn in **`EndScene`** | Last thing before **`Present`** | Source-family engines call `EndScene` more than once per frame and render into offscreen targets. The square may land in a texture, or be overwritten by the HUD drawn afterwards |
| Blend state partly restored | Filtering **and** blending off, plus fog and Z | The runtime samples exact byte values. Anything that alters them corrupts the id |

The inherited `../MetaAI/BUG_FIXES.md` makes this worse by recording, as a fix,
that the square is "drawn after `BeginScene` before HUD to survive". That is
backwards: it must be drawn **after** the HUD, or the HUD covers it.

**Fix:** hook `Present` (D3D9 vtable index 17; `EndScene` is 42, `Reset` is
16). Set render target 0 to the back buffer explicitly, disable alpha blending,
fog and Z, set point sampling on all three filters, fill 10x10 at (0,0), then
call through. See also #8.

---

## 5. The engine, cvar and GameUI interfaces are fabricated — OPEN

**[code]** Three invented vtable layouts, each of which would dispatch a call
to an arbitrary function pointer.

**a. `IVEngineClient`** — in both
[src/input.cpp:11](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/input.cpp#L11)
and `voice_wheel.cpp`:

> `class IVEngineClient { virtual void unused0()=0; virtual void ClientCmd(const char*)=0; };`

This asserts `ClientCmd` is at vtable index 1. In the real interface it is tens
of entries in. Every `SendCmd()` call — `inv_next`, `drop`, `vocalize`, the
flashlight — would jump somewhere arbitrary.

**b. `ICvar`** — `gameui.cpp` declares `virtual void* _pad()` then `FindVar`,
asserting index 1. Source's `ICvar` derives from `IAppSystem`, which contributes
several virtuals of its own first.

**c. `ConVar::GetBool`** — calls vtable index 13, a bare guess, on an object
cast to a locally declared class that has its *own* vtable because it declares a
virtual destructor.

The `__try` / `__except` wrappers around these calls do not rescue them. A
structured-exception handler catches an access violation; it does not catch a
call that lands on a *valid* but *wrong* function, which is the likely outcome
and is far worse, because it corrupts state silently.

**Fix:** enumerate the interfaces the shipped DLLs actually export. Each Source
module exposes `CreateInterface`, and the interface-name strings are present in
the binaries — list them rather than guessing a version number. Then derive each
method's index and **confirm it with a log line** before relying on it. Prefer
the smallest surface that does the job: for issuing console commands, one
correct `ClientCmd` is all this project needs.

---

## 6. Stick turning cannot work — the yaw is overwritten every frame — OPEN

**[code]** In `hkCreateMove`
([src/hooks.cpp:40](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/hooks.cpp#L40))
the HMD yaw is written first:

> `cmd->viewangles.yaw = ang.yaw;`

and then `Input_Update` adds the stick's contribution:

> `cmd->viewangles.yaw += turn;`

`cmd` is a fresh command each tick, so next frame the HMD assignment overwrites
the sum. The stick's turn is discarded as fast as it is applied, and **smooth
turning does nothing**. On a Quest 3 the player could only ever face the
direction their body faces — which, seated, means they cannot turn around.

Two smaller faults in the same code path:

- The turn rate is scaled by a hardcoded `1.0f/60.0f` rather than by real
  elapsed time, so turn speed varies with frame rate. `CreateMove` receives a
  frametime argument, which the hook accepts and ignores.
- There is no wrap to ±180° and no pitch clamp to ±89°, both of which
  `../MetaAI/CURRENT_PROGRESS.md` lists as known-remaining.

**Fix:** keep a persistent `g_yawOffset`. Accumulate
`g_yawOffset += stickX * turnSpeedDegPerSec * dt` using the real frametime, then
set `viewangles.yaw = wrap180(hmdYaw + g_yawOffset)` and
`viewangles.pitch = clamp(hmdPitch, -89, 89)`. The same `g_yawOffset` is what #3
needs to rotate the lean.

---

## 7. The window is never made borderless — OPEN

**[spec]** Nothing in the inherited code touches window style or size.
`wndproc.cpp` subclasses the window procedure, but only to swallow input
messages.

This violates a `../general_wxr/CLAUDE.md` non-negotiable — *the window must be
borderless and fill the display, re-applied every frame* — and it is a
**prerequisite for #4**. With a title bar present, the runtime samples the
chrome at (0,0) instead of our pixel, and head tracking silently degrades. The
general guidance names this as the single most common cause of "I implemented
sync and nothing changed".

**Fix:** an idempotent `EnforceWindow()` that strips
`WS_CAPTION | WS_THICKFRAME | WS_BORDER` and sizes to the display metrics,
called from the per-frame hook. Watch for the engine fighting it: Source-family
engines run consistency checks that reset the display mode when surface, render
and client sizes disagree, and will win a once-per-second argument with a
hand-rolled resize. Log all three sizes whenever they change.

Milestone 3 in [PROJECT_PLAN.md](PROJECT_PLAN.md).

---

## 8. Fabricated sync values when no packet has arrived — OPEN

**[spec]** `DrawFrameIDMarker`
([src/d3d9_hook_1.cpp:92](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/d3d9_hook_1.cpp#L92)):

> `if(!fr.hasData){ frameId = (int)(GetTickCount() & 0xFF); }`

This invents sync ids from the system clock whenever the pose stream is quiet —
which includes every frame before the first packet, and **every frame while the
mod is deliberately in `MODE_VR = 0` for menus.**

That is actively harmful rather than merely useless. `XrFramesync` calibrates
itself to the distinct red values it observes, building a lookup table it then
persists; feeding it clock-derived values pollutes that table, and the runtime
also derives the game's frame rate from changes in this pixel. The general
guidance is unambiguous: **echo `HMD_SYNC` exactly as received.**

**Fix:** draw the last good value, or draw nothing. Never a fabricated one.

Related, same file: the `& 0xFF` mask is harmless in practice — the runtime's
values step by 12 within 0–255 — but the correct form is a clamp of the received
float, not a mask of an invented integer.

---

## 9. Outbound FOV echoes the inbound tangents — OPEN (currently masked)

**[spec]** `SendLoop`
([src/xrapi.cpp:112](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/xrapi.cpp#L112))
takes the FOV out of the *inbound* packet and sends it straight back out.

Those are different quantities. Inbound floats 26–27 are **tangents of
half-angles**, all below 1 for any sane field of view. Outbound FOV is in
**degrees**, and the runtime applies it **only if greater than 1** — so echoing
tangents means sending values the runtime discards, and the port appears to work
only because the runtime keeps its own default.

The defect is currently masked by accident: the guard
`if(fH<10) fH=104.5f;` catches every tangent, so the code always sends
`104.5`, which is the right answer. It is right for the wrong reason, and the
first person to "fix" the guard would break it.

**Fix:** drop the echo. Send a tunable pair, default `104.5 104.5`, loaded from
a flag file so the owner can tune on device without a rebuild — the pattern
HaloCEWXR uses. Treat the values as tuning knobs judged by eye through the
headset, not as a unit conversion to get right.

---

## 10. Input mapping rests on unverified flag values and invented command names — OPEN

**[unverified]** The mapping table is the owner's design and is sound. Its
*bindings* are guesses in two ways.

**a. The `IN_*` bit values** in
[src/input.cpp:32](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/input.cpp#L32)
are transcribed from Source SDK 2007's `in_buttons.h`. They are plausible and
stable across most Source branches, but this fork is not any SDK — see #11.

**b. The console commands do not exist as far as anyone has checked.**
`phone`, `drop`, `dropammo` and the eight `vocalize <id>` strings are
placeholders. `../MetaAI/CURRENT_PROGRESS.md` admits this, and
`contagion_vr_voices.txt` says so in its own comments: *"Edit commands after
testing in console"*, *"Default uses generic Source vocalize concepts — replace
with real Contagion ones"*.

**c. `cmd->buttons = buttons` assigns rather than ORs**, discarding anything
the game had already set. Deliberate, given that keyboard input is being
blocked, but it should be a conscious choice recorded in the code rather than an
accident of style.

**d. Human/zombie detection is a stub.** `Input_DetectPlayerClass()` returns
`PlayerClass::Human` unconditionally, so the zombie branch is dead code.

**Fix:** derive the real command names from the game's own console — `find
drop`, `find vocalize` — and from `contagion/cfg/keybinds_setup`, which is the
authoritative binding list. It lives inside a VPK (`default_keybinds.cfg` just
`exec`s it) **[verified]**, so extract it with the shipped `Vpk.exe`. Confirm
the whole table with the owner at checkpoint 9.

---

## 11. Every offset and interface version descends from the wrong engine — OPEN

**[verified]** The root cause of #1a, #5 and #10a, recorded as its own entry
because it is the project's defining risk rather than one bug.

`../MetaAI/PROJECT_PLAN.md` §1 states Contagion is "32-bit Source SDK 2007".
The Valve Developer Wiki says it is built on the **Left 4 Dead 2 engine
branch**, and the owner's install contains Jolt physics, Wwise audio, CEF and
Angelscript — a fork that has moved well past even L4D2.

So all of these are **[unverified]**, not established:

- `g_pClientMode = 0x107B5F98`
- `OverrideView` at vtable index 14 (`0x38`), `CreateMove` at 21 (`0x54`)
- The `CUserCmd` and `CViewSetup` field layouts in `hooks.h`
- `GameUI011`, `VEngineClient014`, `VEngineCvar004`
- `ConVar::GetBool` at index 13

The IDA work recorded in `../MetaAI/BUG_FIXES.md` is real — a pattern match was
found and a screenshot confirmed it. What it confirmed was that *the pattern
matched*, not that the pattern means what the SDK2007 premise assumed.

**Fix:** derive from the shipped binaries, and confirm each by logging before
writing through it. Log the pattern-scan result, the resolved global, the object
pointer and the first 32 vtable entries, once, at startup. Confirm a struct
layout by reading a field whose value is independently knowable — the viewport
width in `CViewSetup` should equal the container width.

**This is also the project's largest risk**, tracked in
[PROJECT_PLAN.md](PROJECT_PLAN.md) §8 with its fallbacks: a shader-API view
matrix hook, or non-VR mode, which needs no camera work at all.

---

## 12. XrApi handshake is incomplete — OPEN

**[spec]** `EnsureVersionFile`
([src/xrapi.cpp:39](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/xrapi.cpp#L39))
writes `Z:\tmp\xr\version` = `0.4` and stops. Missing:

- **The `vr` file.** `../general_wxr/XRAPI_V0_4_DESC.md` §2: the game writes
  `vr` containing `VR` to announce it wants VR, and deletes it on clean
  shutdown. Leaving it behind can convince the runtime a dead process still
  wants VR.
- **Any read of `system`.** Seven lines of headset information, and the
  cleanest "am I on a headset or a desktop?" test available. The sibling
  Interstellar Marines audit calls adding this *"the highest-value small
  addition in the project"*.
- **A desktop fallback path.** `Z:\` is hardcoded, so the same binary cannot run
  on a desktop for UDP-Tester work. The general guidance's pattern: fall back to
  a local directory when `Z:\` does not exist, giving one binary for two
  environments with no conditional compilation.
- **Version `0.5` rather than `0.4`.** 0.5 is 0.4's packet plus the InputAPI
  listener at no cost.

Also: the runtime **wipes this directory at startup**, so files must be written
after it is up, or rewritten. The inherited code writes once, 1.2 s after
`DllMain`, and never checks again.

**Fix:** all of the above, at milestone 4.

---

## 13. No quaternion normalisation on ingest — OPEN

**[spec]** `ParseXrString` stores the three quaternions exactly as parsed. A
`../general_wxr/CLAUDE.md` non-negotiable says to normalise every quaternion on
ingest, **unconditionally**.

The reason is specific and was paid for in another port: the rotation shortcut
is a rotation only when `|q| = 1`; for a non-unit quaternion it also scales by
`|q|²`. Real headset packets are unit to about 1e-4, but **UDP Tester packets
often are not** — captures used in the Shogo port were 1.4 % to 10.8 % long,
which stretched each hand's basis by a different amount and produced an
asymmetry that looked like a deep bug in the pose maths.

**Fix:** a four-line `QNormalise` applied to all three quaternions in the
parser, with the degenerate case falling back to identity.

---

## 14. The socket is never drained — OPEN

**[spec]** `RecvLoop` blocks on `recvfrom` and parses one datagram per
iteration. Because it runs on its own thread this is less harmful than a
one-per-frame read, and the newest packet does win — but there is no explicit
drain, so under a burst the thread works through a backlog of stale poses
instead of discarding them.

**Fix:** drain every queued datagram and keep only the newest, per
`../general_wxr/XRAPI_V0_4_DESC.md` §7.

Also in this file: `Xr_Init` binds 7872 and falls back to 7873, with a comment
calling 7873 a "fallback". From 0.4 the runtime sends the same packet to
**both** — they are duplicates. Binding one is correct, so only the comment is
wrong; worth correcting so the next reader does not bind both and receive
everything twice.

---

## 15. Recentre happens on the first packet ever received — OPEN

**[code]** `SetXrFrame`
([src/math.cpp:13](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/math.cpp#L13))
captures the recentre origin the first time any packet arrives with a valid HMD
pose. The XrApi client starts 1.2 s after `DllMain`, which is during startup —
long before the player is in a map, and likely while the headset is still on a
desk.

There is also **no recentre binding**; `SetRecenter()` exists and nothing calls
it. `../MetaAI/FUTURE_GOALS.md` lists it as not started.

**Fix:** bind recentre from day one — `../general_wxr/FIRST_STEPS.md` says you
will use it constantly — and recentre on spawn as well as on demand. A hold of
both thumbsticks is the inherited suggestion and is a reasonable default.

---

## 16. Visual orientation and roll are never set — OPEN

**[code]** `hkOverrideView` writes `setup->origin` and
`setup->originViewmodel`, but never `setup->angles`. The view's orientation
therefore comes entirely from `cmd->viewangles`, set a tick earlier in
`CreateMove`.

Two consequences:

- **Head roll does nothing.** Tilt your head and the horizon stays level. In a
  headset that reads as the world being bolted to your face, and it is one of
  the cheapest wins in a VR port.
- **One tick of extra latency** on all view orientation, since it routes
  through the command rather than the view setup.

This is the split `PROJECT_OVERVIEW.md` §5 commits to: `CreateMove` owns
*logical aim* (yaw and pitch, which the game and server act on), `OverrideView`
owns the *visual camera* (full orientation including roll, plus the positional
lean).

**Fix:** set `setup->angles` from the HMD in `OverrideView`, including roll.

---

## 17. The hand pose is routed through the head transform — DEFERRED

**[code]** `hkOverrideView` computes the right hand relative to the head and
rotates it by the inverse head orientation before applying it to
`originViewmodel` at a 0.15 scale factor.

`../general_wxr/TIPS.md` names *hands that move with head angle* as a recurring
bug across these ports and says to convert each pose independently, sharing only
the recentre origin.

Here it is arguable rather than clearly wrong: the target is a **viewmodel
offset**, which lives in view space, so expressing the hand in head-local space
is defensible. The 0.15 scale is a pure fudge factor with no stated frame,
which is the part `TIPS.md` would object to — a value that only works where it
was found.

**Deferred** because hands and weapon placement are milestone 8, and nothing
before it depends on this. When it is picked up, read *measure offsets in the
frame you will use them in* first, and express the grip offset in the **gun's**
frame rather than the hand's.

---

## 18. The bundled MinHook cannot handle a short prologue — OPEN

**[code]** `minhook.cpp` is a hand-rolled 5-byte-JMP trampoline implementing
the MinHook API so the project builds with no external dependency. It copies
five bytes, writes a jump, and does not disassemble.

That fails on any function whose first five bytes contain a relative jump or
call, or whose prologue is shorter than five bytes — and it fails by corrupting
the instruction stream, which on a mobile device with no debugger is about the
worst failure mode available. `../MetaAI/FUTURE_GOALS.md` item 9 already flags
this.

Separately, MinHook's behaviour under Box86 is itself **[unverified]**, and
`../general_wxr/TOOLS.md` warns that hooking under Box86 and Wine can differ
from the desktop. Prove the hook on device at milestone 2, before any feature
depends on it.

**Fix:** use real MinHook. Drop the bundled version.

---

## 19. The dummy-device trick may not survive Wine — OPEN

**[code]** `D3D9Hook_Init` creates a throwaway `IDirect3DDevice9` against
`GetDesktopWindow()` purely to read its vtable, then releases it. Vtable
indices used — `Reset` 16, `EndScene` 42 — are **correct** for D3D9.

The risk is the device creation itself: a software-vertex-processing HAL device
on the desktop window, inside Wine, on DXVK or WineD3D, on Android. If it
fails, both fallbacks (`D3DDEVTYPE_REF`, then giving up) leave the hook silently
uninstalled — and `D3D9Hook_Init`'s failure path returns `void`, so nothing
upstream learns about it.

**Fix:** log success or failure explicitly. If creation fails on device, take
the device from the game instead — `shaderapidx9.dll` holds one — rather than
manufacturing a second one.

---

## 20. Smaller items — OPEN

**[code]** Each real, none worth its own section.

- **No log file at all, and no build stamp.** Listed here for completeness;
  they are milestone 2 and the reason nothing else in this file could be
  observed.
- **`Hooks_Init` failure shows a `MessageBoxA`.** A modal dialog on a
  standalone headset blocks the game behind something the player may not be
  able to dismiss. Log it instead.
- **Fixed sleeps for initialisation.** `Sleep(1200)` then `Sleep(600)` before
  hooking is a guess that will be wrong on slower hardware — and a Quest 3
  under Box86 is slower hardware. Poll for the condition instead.
- **`WndProcHook_Init` has no retry.** It runs once, 1.8 s in, and if the
  window does not exist yet or the engine recreates it on a video-mode change,
  the hook is simply lost.
- **Keyboard is blocked wholesale except Escape**, so the developer console
  (`~`) and chat are unreachable in VR. Keep the console open at minimum — on a
  device with no debugger it is a diagnostic channel.
- **`WM_INPUT` is swallowed without calling `DefWindowProc`**, which the raw
  input documentation asks for. Harmless in practice; tidy it.
- **Haptic decay is tied to the 11 ms send loop**, not to frames or elapsed
  time, so the decay rate drifts with scheduling.
- **`static void DrawText(...)` collides with the Windows `DrawText` macro**,
  which expands it to `DrawTextA`. It happens to compile because the call sites
  expand identically. Rename it.
- **`PatternScan` ignores `GetModuleInformation`'s return value** and would
  scan from an uninitialised base on failure.
- **The voice wheel's "dim background" is a 16-sided polygon**, so it does not
  cover the screen corners. Cosmetic, and the whole feature is deferred.
- **`Xr_Shutdown` closes the sockets before joining the threads**, racing the
  blocked `recvfrom`. It works in practice because the close unblocks the call,
  but the order is backwards.

---

## 21. Monocular is "2x cheaper than stereo" — NOT A DEFECT

**[code]** `../MetaAI/PROJECT_PLAN.md` §1 describes monocular VR as
"~2x cheaper than stereo". Strictly the arithmetic is the other way round:
mono costs 1x scene cost and SBS costs 2x, so stereo is twice as *expensive*
rather than mono being half price.

The conclusion is right, the decision is right, and nothing rests on the
phrasing. Recorded only so it is not raised as a finding again.

---

## 22. Visual-only lean with no collision change — NOT A DEFECT

**[code]** `../MetaAI/BUG_FIXES.md` records this as a fix: the original code
added the HMD delta to both the visual origin and the player's collision
origin, enabling a wall-peek; it now writes only `CViewSetup::origin` and
carries the comment `// visual only, no SetAbsOrigin`.

This is **correct and is now a hard rule** for the project
([PROJECT_PLAN.md](PROJECT_PLAN.md) §2, rule 2). Keep the behaviour and keep the
comment. Listed here so nobody "simplifies" it away.
