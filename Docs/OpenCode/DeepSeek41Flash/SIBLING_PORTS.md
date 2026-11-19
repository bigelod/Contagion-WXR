# SIBLING_PORTS.md — what to copy from three ports that solved the same problems

**2026-11-17.** Three sibling WinlatorXR ports were read for transferable
technique, not for game trivia. Two have no source; one has readable managed
code.

| Port | Local path | Engine | Source available? | Value to us |
|---|---|---|---|---|
| **Interstellar-Marines-WXR** | `D:\…\Interstellar-Marines-WXR` | Unity (managed C#) | **Yes** — the mod's own C# is in the tree, and the game's `Assembly-CSharp.dll` is dnSpy-able | **Highest.** A complete, device-confirmed XrApi client, sync pixel, input gate and config system |
| **XOPS-WXR** | `D:\…\XOPS-WXR` | native, closed | No | Input: the reference for "call the game's own consumer, never synthesise Windows input" |
| **mirrors-edge-WXR** | `D:\…\mirrors-edge-WXR` | Unreal Engine 3, native, closed | No | Camera takeover and presentation-mode choices in a closed engine |

Read this file **after** `BINARY_RECON.md`. It is about method: the things these
three learned by shipping, which no amount of reading Contagion's binaries will
tell us.

Where a sibling's approach differs from ours, the difference is stated rather
than resolved silently — several of these are decisions the owner should make.

---

## 1. Interstellar-Marines-WXR — the closest thing to a reference implementation

The same model wrote the notes in that repository
(`Docs/OpenCode/DeepSeek41Flash/` there), so its conventions and ours agree. Its
`HANDSHAKE.md` is the best short document in this family on XrApi handshakes.

### 1.1 Handshake: the policy worth copying wholesale

`XRAPI.cs` — see also `DEFECTS.md` D26.

- **ASCII writes, deliberately.** `File.WriteAllText(..., Encoding.ASCII)` with
  the comment that the runtime matches `version` with `startsWith`, so a stray
  BOM makes `0.4` unrecognised and the result is *silence*, not an error. In C
  this is `CreateFileA` + `WriteFile` with no preamble — which is what the
  inherited code already does, and must keep doing. **Never** use a
  BOM-emitting writer.
- **Write unconditionally, then re-check every 2 s.** `TickHandshake` recreates
  the directory, rewrites `version` and `vr` if missing or wrong, retries
  `system` until it parses, and re-applies detected handedness. The watchdog
  exists because the runtime wipes the directory at its own startup.
- **Do not latch a partial `system` read.** Fewer than two lines, or an empty
  product, means "retry in 2 s", not "it is a Quest 3".
- **`vr` is deleted on clean shutdown; `version` is left in place** — `vr` is a
  live claim, `version` is a protocol statement.
- **Log the chosen directory on every init and on every rewrite.** One line
  answers "did I find the device at all?".

Its path resolution is slightly different from the owner's decision for this
project, and worth knowing as a fallback shape:

```
IM:      Z:/tmp/xr  if it exists
         Z:/tmp     exists -> create Z:/tmp/xr and use it
         otherwise   -> xrtemp beside the DLL
Contagion (owner's decision, 2026-11-17):
         Z:\tmp exists -> Z:\tmp\xr
         otherwise     -> D:\xrtemp
```

Both test the parent, not `Z:\`. Ours hardcodes `D:`, theirs derives from the
DLL location. Ours is simpler to reason about on this machine; theirs is more
portable. Do not change ours without asking.

### 1.2 `system` is a capability file, not two lines

`ParseSystemFov` reads the whole file for any line containing `fov`, accepts
`fovx/fovy`, `fov_h/fov_v` or a two-number `fov 90 90`, and converts values
below 1.5 as tangents. The seven documented lines do not include FOV, but "the
runtimes have grown optional fields" is the stated reason for tolerating them.

The headset mapping matches the general guidance, and the result drives a
**controller-orientation correction, not a handedness swap**:

| Make | Product | Model | `flipHands` |
|---|---|---|---|
| META / OCULUS | `EUREKA`, `PANTHER` | Quest 3 | false |
| META | `HOLLYWOOD` | Quest 2 | **true** (controllers report rolled 180°) |
| META | `SEACLIFF` | Quest Pro | true, untested |
| META | `MONTEREY` | Quest 1 | false |
| missing / unknown | — | Quest 3 default | false |

Precedence: an explicit `flipHands=true|false` in config always wins;
`auto`/absent lets detection decide. That two-level design — *detect by default,
override explicitly, never nag* — is the pattern `TIPS.md` asks for and it is
implemented here without a menu.

**Transferable test to steal:** delete `vr` externally while the game runs; it
must reappear within ~2 s. That is a one-line device check of the whole
watchdog.

### 1.3 FOV: precedence, not a constant

`GetEffectiveFov` resolves, in order:

1. configured per-axis value (`fov_h`, `fov_v`), if `> 1`;
2. FOV published in `system`, if any;
3. the inbound packet's floats 26/27, **converted from tangent to degrees**
   (`2·atan(f)/π·180`);
4. `104.5`.

And the author is explicit that the outbound FOV **does not have to equal the
camera's real FOV** — it is the angle the runtime paints the captured frame
across, so declaring *less* shrinks the same scene toward the centre at full
render resolution. That is a genuinely useful framing and it slightly softens
`TIPS.md`'s "send the FOV you actually rendered with": the value is a
presentation control as well as a report. See `DEFECTS.md` D15 — the rule that
survives is **do not echo the inbound tangents as if they were degrees**, and do
not hardcode a single pair.

Their tuning loop is also worth copying (`VRFovTuner.cs`): a 10° grid, clamped
20–160, written straight back to config from inside the headset. And a
non-obvious interaction that will bite us too:

> **At `MODE_VR = 0` the runtime sends no packets at all**, so there is no live
> thumbstick data in exactly the state you want to tune in. What still works is
> the runtime's own screen-mode emulation, which maps the left stick to the
> arrow keys — so the keyboard is the only live input channel while paused.

That matters beyond FOV: **any in-headset settings menu must be reachable while
paused, and while paused the controller stream is dead.** Plan for that now
(`PLAN.md` §7).

### 1.4 Head pose: the roll trap, and a magnitude clamp

`VRHeadTracker.LateUpdate` is the closest analogue to our planned camera write.

- **Lean is clamped by magnitude**: `Vector3.ClampMagnitude(hPos, 0.3f)` — not
  per axis. Confirms `DEFECTS.md` D12 and gives a calibration point: they use
  0.3 m, we intend 0.15 m.
- **Eye offset is separate from lean** (`eye_height`, `eye_forward`), applied to
  the camera's own mount point, tunable live. For us the equivalent is the
  player's view offset, which we must **not** write to the hull — so this is a
  config-driven number on `CViewSetup::origin` only.
- **Roll is taken from the quaternion, never from a world-space up vector.**
  The comment is explicit: deriving roll from a world up vector *"is what made
  yaw and pitch leak into the quad's roll"*. That is a direct correction to the
  naive form of our own `DEFECTS.md` D10 fix, which said "roll from the up
  vector". The correct shape:
  1. `yaw` and `pitch` from the head's forward axis;
  2. build the yaw/pitch-only basis;
  3. roll = the **residual** signed angle about the forward axis between the
     head's actual up and that basis's up.
  Or, equivalently in another engine's terms, take the local-Z component of the
  head's own Euler decomposition. What must not happen is measuring roll against
  *world* up before yaw and pitch are removed.
- **Roll is held, not raw.** A 0.5° deadband kills yaw/pitch coupling, and the
  last roll is held within 10° of straight up/down where Euler roll is
  gimbal-locked. Cheap comfort, worth copying when roll is implemented.
- **On a dropped packet, the camera and the last roll are left alone** —
  "zeroing the roll here would unroll the world underneath a camera that has not
  moved". That is the `freeze, never zero` rule stated for orientation.

### 1.5 Sync pixel: own the draw, keep the size configurable

`SyncPixelQuad.cs`, and the comment above it is the most useful part.

- **It used to die on respawn.** Two causes: the quad was parented to whichever
  camera had the highest depth (a menu-owned camera, so it was destroyed with
  the menu), and a `static bool alreadyCreated` was set once and never reset.
  The pixel disappeared for the rest of the session.
- **The fix was to own everything**: its own `DontDestroyOnLoad` object, its own
  camera at depth 1000 that clears nothing and renders no layers, existing only
  to get an `OnPostRender` callback **after every game camera has drawn**. Then
  a screen-space immediate-mode quad at exact pixel coordinates: no texture, no
  blending, no filtering.
- **Size is config** (`sync_pixel_size`, floor 2, default 10). They ran 150 for
  a while on a letterboxing theory; on device it bought nothing and the player
  could see a big red square. Ten is correct and proven.
- **Green must read 0** with the stated reason: the runtime uses it to confirm
  it is looking at our pixel and not at scene content that happens to be red.
- **Blue stays 0** (mono).

For us the equivalent of "own the draw" is **hook `Present`**, set render target
0, and draw last — which `PLAN.md` §6 already specifies. What this port adds is
the *reason*: anything that hangs off a game object or a game camera can be
destroyed or reordered by the game. `EndScene` has exactly that character in a
Source engine (called per render target, not per frame).

**One honest disagreement.** When no packet has arrived, IM draws
`Time.frameCount % 256` so the runtime can still calibrate its colour table.
`Docs/general_wxr/CLAUDE.md` says never fabricate a sync value. Both have a
rationale; ours is the project rule, so follow it first, and switch knowingly
rather than by accident if menus turn out to confuse the runtime.

### 1.6 Input: one gate, the game's own vocabulary, explicit edges

Three things here are directly reusable, and one is a warning.

**a. One gate, not nine.** `VRInputGate.AllowNative()` is the single answer to
"may the game's own mouse and keyboard pass through this frame". Every blocking
patch asks it. The comment records why this matters: the same three-line
preamble used to be copy-pasted nine times, one copy differed, and the resulting
bug survived for days. For us: **one function**, consulted by the window hook,
the tick hook and the menu logic — `DEFECTS.md` D18 and D22 are both symptoms of
not having one.

**b. Never infer pause from the cursor or the window.** The bug is worth
quoting, because our inherited code is one step away from it: under Winlator
`Cursor.visible` is *permanently true*, so `cursorVisible && !cursorLocked` was
always true, which forced "paused" every frame, which pinned `MODE_VR = 0`,
which stopped the runtime sending UDP at all — 267 receive timeouts in a
session. The fix: use the **game's own authoritative UI signal**, not an OS or
window property.

For Contagion that maps exactly onto `DEFECTS.md` D20: use the game's ConVars
(`gameui_activated`, `gameui_hidden`, `gameui_ingame_menu_active`) and *never*
`GetForegroundWindow`, cursor visibility, or "is the window active" as a menu
test. Under a container all three lie.

**c. Publish input in the game's own vocabulary.** `VRInput.overridesBool` is
cleared and rebuilt each frame from named actions the game already understands
(`MoveForward`, `Fire`, `Use`, `Reload`, `Slot01`…), and patches consume it.
That is `PC_INPUT.md` §3.2 implemented: supply the state the game reads, in its
own names. For Source the equivalent vocabulary is `CUserCmd::buttons` and the
console commands, which is why `BINARY_RECON.md` §7's real command names matter.

**d. Held versus edge, decided per action and written down.** Examples from
their code:

| Action | Form | Why |
|---|---|---|
| `Use` | published as a **level**; the edge is withheld from the game | the game's own edge handler raycasts from the camera (the head); the toggle is done off the dominant hand instead. The level must still go through, because the game releases its hold only when the input goes false |
| `Jump`, `Fire`, `Crouch`, `Run` | held | natural continuous states |
| `Reload`, slot switch | **edge**, with a hold-flag (`bHeld`, `rightStickUpHeld`) | a held trigger must not fire the action every frame |
| Weapon slot | edge + 0.35 s cooldown | stops a flick of the stick cycling three slots |
| Silencer | double-tap left trigger (< 0.35 s) | modifier-free secondary action |
| Rate of fire | hold > 0.6 s, 1 s cooldown | |

That table is the concrete answer to `PLAN.md` §5's "held vs edge-triggered"
decision, and it is worth copying the *shape* even where the actions differ.

**e. Where the two ports must diverge.** IM implements smooth turning by
rotating the **character transform**:

```csharp
local.transform.Rotate(0, rs.x * speed * Time.deltaTime, 0);
```

Contagion cannot do that. The player entity is server-authoritative and the
engine owns its yaw; writing the entity's angles client-side would be fought by
the engine and would be a cheat vector. Our equivalent is the persistent
`g_yawOffset` added to `viewangles.yaw` (`DEFECTS.md` D14). Same user-facing
behaviour, different mechanism, and the reason is worth remembering so nobody
"simplifies" it into an entity rotation.

### 1.7 Config: hot-reload forces a proper writer

`VRConfigFile.cs` exists because `VRManager.PollConfig` re-reads `config.txt`
every 2 s — so an in-memory change is reverted within two seconds unless it is
written to disk. Two details worth copying:

- **Rewrite every matching key, not just the first.** The parser applies lines in
  order and the last wins, so a stale duplicate silently defeats the write.
- **Accept alias spellings** (`fov`/`fov_h`/`fovh`/`fovx`) when searching, so a
  rewrite updates the line the user actually typed instead of appending a second
  one below it.

And the config carries `config_version=2` with a note about what the migration
did — the `TIPS.md` rule about versioning before you need to, actually
implemented.

Their config also shows the honest cost of tuning: nearly every value has a
comment saying **where it was measured and what a wrong value looked like**
(`eye_height` raised because the body blocked the camera; `far_clip` because the
fog follows it; `hudScale 0.60` found on device). That is the documentation
`TIPS.md` §"Document what you learned" asks for, and it is why this port's
config file is a better design document than most of the prose.

For us: `contagion-wxr.ini` at the game root (owner's decision), with the same
two rules — rewrite every duplicate, accept aliases — plus a `cfg_version` key
from the first release.

---

## 2. XOPS-WXR — the input reference, and how to find a camera with no symbols

X Operations 0.96 (2003), in-house engine, **Direct3D 8.1**, 32-bit, no source.
`Docs/general_wxr/PC_INPUT.md` §4 is a case study of this port; what follows is
the part that is not already in the general guidance.

### 2.1 Injection: proxy the DLL the game actually loads, and export only what exists

It proxies **`d3d8.dll`**, and its `.def` exports **only `Direct3DCreate8`** —
deliberately. An earlier version also forwarded `DebugSetMute` and `Validate*`,
which DXVK 1.10.1 does **not** export, and the result was a **jump to address 0**
when the game called one. `README.md:202`, `src/d3d8probe.cpp:8969-8977`.

This is the mirror image of our `DEFECTS.md` D07, and together they give the
correct rule:

| Mistake | Result |
|---|---|
| Importers need an export we do not provide | load-time failure of whatever imports it — "the game will not start" |
| We forward an export the *real* DLL does not provide | call through the forwarder lands on **address 0** |

So the forwarder list is not "all 17 of the system DLL's exports" and not "the
ten we happen to need". It is **the intersection**: enumerate the exports of the
`version_o.dll` we actually ship (Wine's or Windows', whichever loads in the
container), enumerate the imports/GetProcAddress uses the game makes of
`version.dll`, and forward the intersection plus what importers require. Generate
it at build time; do not hand-maintain two lists that can drift.

Also worth copying: **`DllMain` does only IAT patching** — no `LoadLibrary`
under the loader lock (`src/d3d8probe.cpp:8984-8998`). That is the same
correction as our `DEFECTS.md` D08.

And it hooks by **patching vtables and the IAT directly** (`PatchSlot`,
`PatchIat`) rather than shipping a detour library. We are keeping real MinHook
(D29); the reason to mention it is that both techniques are viable here, and a
direct vtable write has no prologue-length problem at all.

### 2.2 Finding the camera with no symbols: differential scanning plus *active probing*

The streaks are not pattern scans — there are none in the source. The method was:

1. **Snapshot** all committed memory regions, run the game a frame, snapshot
   again, and rank the addresses that changed in a camera-like way
   (`BuildSnapshot` 2860–3060, ranking 3060–3157).
2. **Active probing**: for each candidate, drive it with a slowly oscillating
   value (a sine) and ask the human *"does the view turn?"*. One candidate at a
   time, so the answer is unambiguous (2868–2896, 4674–4890).
3. **Corroborate** with a physical test — a "gravity/ballistic" check on
   candidate Y values (2988–3053), plus plausible-height and shape filters
   (2937–2953).
4. Freeze the winners into a fixed watch list with named fields (3189–3198).

That is a genuinely different tool from ours, and it is **the fallback if
vtable identity stalls**: instead of reading disassembly, *perturb and watch*.
It is also the most direct possible application of `TIPS.md`'s "measure, do not
infer", and it needs nothing but a log line, a config key and the owner's eyes.

Applied to Contagion, the equivalent probe is the one `PLAN.md` §3.2 already
describes from the other direction: write a candidate `CViewSetup` field and ask
whether the world moved. The two approaches meet in the middle.

### 2.3 What it writes, when, and what it deliberately does not

- **Writes happen in `BeginScene`** — after the game's simulation update and
  before its draw (3563–3565, 8468). Order is the whole mechanism: too early and
  the game overwrites you; too late and the frame is already transformed.
- **Position is deliberately never written.** The game re-clamps Y every frame,
  so the HMD's translation is applied **render-side only** (3657–3661,
  5724–5729, 5757–5765). This is exactly our visual-only-lean rule (`DEFECTS.md`
  D37) arrived at independently, and it is the strongest evidence that the rule
  is right rather than merely cautious.
- **Yaw is relative, pitch is absolute.** Recentre reference + accumulated
  right-stick yaw + HMD yaw; pitch is a straight write with a switchable sign
  (3612–3616, 3377–3387). That is structurally our `g_yawOffset` (D14), and the
  asymmetry is interesting: aim needs a stable absolute pitch, but the yaw must
  accumulate to allow turning.
- **Recentre captures the game's own presented yaw and position**, and on level
  start it waits **6 frames** (`g_spawnGrace`) before recentring (3532–3551,
  3569–3594). The grace period exists because the game has not placed the player
  yet. Our inherited code recentres on the first packet ever (D15), which is the
  same mistake in the opposite direction.
- **Aim from the dominant hand** is derived from a laser endpoint 30 m ahead →
  `atan2(dir.x, dir.z)` / `asin(dir.y)` → added to the head's yaw/pitch
  (3913–3960). Then the split our plan calls for appears explicitly: aim is
  written in the input phase (3986–4005, called from `hkGetKeyboardState` at
  2401) **and again after `EndScene`** (8520–8522), while `BeginScene` restores
  the head view (8421–8429). CRUCIAL: one write for the *logical* aim the game
  and server act on, another for the *visual* view. Same conclusion as
  `PROJECT_OVERVIEW.md` §5, in a different engine.

### 2.4 The sync pixel as a `Clear`, not a draw

`DrawSyncPixel()` (`src/d3d8probe.cpp:6993–7015`) does not draw a quad. It calls:

```c
D3DRECT r = { 0, 0, 10, 10 };
dev->Clear(1, &r, D3DCLEAR_TARGET, D3DCOLOR_XRGB(r8, 0, aerBlue), 1.0f, 0);
```

with `r8 = clamp((int)(sync + 0.5f), 0, 255)`, **green forced 0**, blue = the AER
eye flag (0/255) in AER and 0 otherwise.

**This is better than what `PLAN.md` §6 currently specifies, and we should take
it.** A `Clear` of a sub-rect:

- cannot be affected by alpha blending, texture filtering, fog, Z or the shader
  in use — the entire render-state class of sync-pixel bugs disappears;
- needs no vertex buffer, no FVF, no draw call;
- writes exact bytes, which is what `XrFramesync` samples.

It also explains why they needed no state save/restore at all: a Clear has no
state of its own to leak. The one caveat for us is D3D9 ordering — `Clear` must
target the back buffer as render target 0, and if it must sit inside a
`BeginScene`/`EndScene` pair in this engine, we do that. `PLAN.md` §6 is updated
to try `Clear` first and keep the quad as the fallback.

**No gamma, sRGB or HDR handling exists in this port** — a grep for
`gamma|sRGB|HDR` over the source returns nothing. That matches the rule: echo the
value and let the runtime calibrate.

### 2.5 XrApi client: the two decisions worth arguing about

Conventional and correct: `0.4` written as the first line of `version`, handshake
at `Z:\tmp\xr` with `<EXE drive>:\xrtemp` then `D:\xrtemp` as fallback
(164–177) — **the same shape as the owner's decision for us, independently
arrived at**; port 7872 with 7873 only as a fallback if 7872 is taken; a
non-blocking recv loop that drains and keeps the newest (262, 378–387);
`QNormalise` on all three poses (349–351); conversion by negating Z after
applying the recentre inverse (98–121).

Two things are worth disagreeing with, carefully.

**a. Outbound FOV is recovered from the projection matrix.** It computes
`80.7° / 65.0°` from the actual matrix rather than using a constant or the
inbound packet, and recomputes if the aspect patch runs (307–320, 436–440,
4938–4975). **This is better than both prior plans for Contagion.** The inbound
FOV floats are tangents of half-angles *the runtime wants*; the outbound pair is
the angle the runtime paints the frame across. Neither is "what the game
rendered", and a constant is a guess. Reading the real projection is a
measurement. Add to `PLAN.md` §2.2: log the actual horizontal/vertical FOV from
the live view each time it changes, and send that.

**b. `MODE_VR` is always 1 — menus included** (8637–8642, 5538–5543). Menus are
a centre-third floating panel with a hand laser and the runtime's own mouse, and
the pose stream never stops. `MODE_3D` drops to mono in menus so flat UI is
never eye-alternated (8659–8666).

This directly contradicts `Docs/general_wxr/XRAPI_V0_4_DESC.md` §4 and the
Interstellar Marines port, which both switch to `MODE_VR = 0` in menus. The
trade is real and worth stating plainly:

| | `MODE_VR = 0` in menus (general guidance, IM) | always `1` (XOPS) |
|---|---|---|
| Menu readability | full flat panel, best | immersive projection, worse |
| Runtime's mouse | returns automatically | must be kept and arbitrated |
| Pose stream | **stops** — no input while paused | keeps flowing |
| In-headset settings | must work with no controller data (IM reads arrow keys) | normal controller input works |
| Gap on return to gameplay | hold last pose across a restart | none |

Our plan keeps `MODE_VR = 0`, because Contagion's menus are flat 2D and the
readability win is the reason the mode exists. But **centralise the mode
selection in one function** so this is one constant away from XOPS's behaviour,
and record the trade so it is not relitigated blind. The AER-in-gameplay /
mono-in-menus half of their choice we should copy regardless if stereo ever
happens: never alternate eyes on flat UI.

### 2.6 Window: strip, size, and re-apply — with one refusal

`FitWindow()` (5621–5653) strips `WS_CAPTION|WS_THICKFRAME|WS_BORDER|WS_DLGFRAME`,
sets `WS_POPUP|WS_VISIBLE`, and `SetWindowPos(0, 0, screenW, screenH)`; it is
idempotent and returns early when already correct. Re-applied **every Present**
(8533–8537), again after `CreateDevice` (8936) and after `Reset` (8807), and the
WndProc filter is re-installed per frame (8537, 2287–2297).

Two details we should take:

- **It refuses to touch the window when the device is fullscreen** (496–501,
  5628, 8749–8765): stripping styles from a fullscreen device fights `Reset` and
  produces `D3DERR_DEVICELOST`. If Contagion runs fullscreen in the container,
  the right answer may be "do nothing" — which is also what IM concluded.
- **`Present` overrides the game's explicit 640×480 destination rect with the
  full client rect** (8549–8588) so D3D stretches to the window. In D3D9
  `Present(NULL, NULL, hwnd, NULL)` already stretches, but the equivalent hazard
  exists for us as **letterboxing**: if the game renders a fixed back-buffer size
  into a different client size, the sync pixel's *screen* corner moves. Log back
  buffer size against client size once, at every change.

### 2.7 Verifying hooks with no debugger — four techniques to steal

This is the most valuable part of the port for us, because it is exactly our
situation.

1. **Bounded first-call trace.** Each hooked entry point logs a line on its
   **first** call only (`HookTrace`, 5574–5589), flushed immediately. Stated
   reason: *"the last trace line before a Wine crash names the hook that was
   entered."* Cheap, bounded, and it converts a silent crash into a location.
2. **Post-install audit dump.** After installing hooks, log every original
   function pointer (`HOOKS setrt=… clear=… present=…`) (8859–8876). One line
   proves the hook resolved where you think it did.
3. **Self-validating code patches.** Before rewriting a call site it checks the
   byte is `E8` **and** that the `rel32` target equals the expected function; on
   mismatch it logs and refuses (5172–5184, 5208–5220). This is the missing half
   of our `DEFECTS.md` D33: a pattern scan must not only *find* a match, it must
   *verify the instruction shape and the decoded target* before patching.
4. **Per-frame counters.** `BeginScene=N EndScene=N` in the log (8590–8598)
   answers "is `EndScene` once per presented frame?" from the log alone — which
   is precisely the claim our `PLAN.md` §6 relies on when it says Source calls
   `EndScene` more than once. **Add this to the recon dump**: count `Present` vs
   `EndScene` per frame in Contagion and settle it with a number.

### 2.8 Build and deploy: mostly behind us, with one trap

Batch scripts with an explicit x86 check (`refuses a non-"for x86" cl.exe` and
links `/MACHINE:X86`, `build/build-probe.bat:31–34,46`), deploy to
**`E:\tmp\Q3Mods\xops096`** — the same staging root as our Quest copy — and
SHA-256 verification with `certutil -hashfile`, comparing **built against
deployed** and failing on mismatch (74–88).

Our `tools/deploy.ps1` already does the hash comparison, so the only thing to
add is the x86 assertion (we have it for `version.dll`) and the *built vs
staged* comparison rather than *staged vs deployed*. Small.

The gap in that port is the one we should not copy: **its freshness guard is
manual** — the docs say "check the first log line matches the installed build",
with no automated stamp check (README 140, 192). And its **release build deletes
the log** when `verbose_debug=false` (5479–5488), which removes the build stamp
exactly when a player needs to report a problem. Ours must keep line 1 always.

### 2.9 Logging: the shape worth copying

`xops_wxr_probe.log` beside the DLL, two levels (`Log()` always on for state
changes and errors; `LogD()` gated by `verbose_debug` from config), **every line
flushed immediately so a crash keeps the log**, and an end-of-session summary of
frames, call sites and input counts (5464–5498, 526–559, 9002–9030).

The end-of-session summary is the piece we do not have planned. It costs one
function and answers "did the session do anything at all?" without scrolling
through a frame log.

---

## 3. mirrors-edge-WXR — a closed engine, a real camera, and the hardest verification

Mirror's Edge (Unreal Engine 3, D3D9, 32-bit, no source). This is the most
mature of the three: its `ENGINE_NOTES.md` is 1,500+ lines of measured
engine behaviour. Only the parts that transfer to a Source fork are here.

### 3.1 The same two lessons as XOPS, from a different engine

- **`d3d9.dll` proxy**, exports forwarded, installed beside the exe; x86
  enforced in every build script and asserted in `tools/check-package.ps1`.
- **Sync pixel is a surface fill, not a draw**: `ColorFill` on the back buffer,
  10×10 at (0,0), inside the `Present` hook (slot 17) immediately before the
  real `Present`; red `= HMD_SYNC & 0xFF`, **green 0**, **blue = 255 only for
  the right eye in AER** (`src/winlatorxr_frame.inl:886–892`).

That is now **two independent ports doing `Clear`/`ColorFill` rather than a
quad**, which is why `PLAN.md` §6 was changed to try a Clear first. It also
confirms the 10×10 choice with a concrete reason: the fill has to survive
back-buffer-to-screen scaling.

- No state is saved around the stamp, because a fill is not a draw. The menu
  panel — which *is* a draw — captures and applies a full `D3DSBT_ALL` state
  block (`src/winlatorxr_frame.inl:224–225, 284–285`). The distinction is
  exactly the one that makes the stamp robust.
- Their menu panel disables sRGB on **texture read** and on **render-target
  write** so bytes are written unconverted (`250`, `259`), and UE3's tone
  mapping is switched off wholesale in the engine profile (`TdTonemapping=False`,
  `src/d3d9.cpp:892`) so that no tone-map pass sits between the scene and the
  stamp. For us: the stamp is written in `Present`, after any gamma or tone
  mapping, which is the same guarantee by a different route — but it means
  **`mat_monitorgamma` / HDR must not be applied after Present**, which they
  cannot be. Confirm once by reading the pixel back.

### 3.2 Window: post the work to the window's own thread — or eat a 0.9 fps deadlock

This is a measured hazard, not a theory.

- **Never restyle or reposition the window from the render thread.** Their
  comment records a cross-thread deadlock: **0.9 fps and a black screen**
  (`src/winlatorxr_frame.inl:40–43, 153–161`). The fix is to subclass the
  window and **POST** the work to the thread that owns it.
- Re-applied per frame but **acted on every 30th frame** (~twice a second), and
  only while the window is not yet correctly placed (`150–191, 81–88`).
- **Gives up after 30 failed attempts**, logging *"the window keeps leaving 0,0
  — giving up on pinning it"* (`188–190`). Fighting forever wastes frames and
  fills the log.
- **Focus messages are the other half of ownership.** UE3 treats
  `WM_KILLFOCUS`, `WM_ACTIVATE(WA_INACTIVE)`, `WM_ACTIVATEAPP(FALSE)` and
  `WM_NCACTIVATE(FALSE)` as lost focus — it pauses audio and sets
  `bIgnoreMoveInput` — so those four are swallowed (`52–79, 109–113`).
- **In a container the foreground is re-asserted from the window's own thread**
  via POST, up to 30 tries; on the PC fallback alt-tab is left alone
  (`114–128, 174–186`).

For Contagion this is directly actionable and it explains a class of symptom we
have not seen yet but will: a game that renders but ignores movement, or pauses
itself, because a container stole focus. See `DEFECTS.md` D17 and D19.

### 3.3 Handshake, and pose latching

`Z:\tmp\xr`, with `D:\xrtemp` as the desktop fallback, **chosen by testing
`Z:\tmp` for existence** — and the comment explains why they test the parent and
not the root:

> `GetFileAttributes` on `Z:\` is avoided because Wine fails it.

(`src/winlatorxr.inl:37–44, 69–82`.)

That is the owner's decision, arrived at independently, for a stated reason. Three
ports, one answer. It also means **our `D:\xrtemp` is the established convention
in this family**, not an arbitrary choice — `DEFECTS.md` D26 stands as written.

They advertise **`0.5`**, with an environment override
(`MEVR_WINLATORXR_API_VERSION`) and a version macro. The env override is a neat
idea for A/B testing a protocol change without a rebuild — worth copying later,
not now.

**Pose freshness is handled explicitly, and we do not do it at all.** Their
receive thread keeps only the newest datagram, and a separate `LatchFrame` waits
for a **newer `HMD_SYNC`** before copying the packet into the frame the renderer
will use — one pose per game frame, and a duplicated datagram cannot be applied
as if it were new (`172–195, 364–373`). See `DEFECTS.md` D41.

They also normalise poses with a **gate** — reject non-finite, require norm² ∈
[0.81, 1.21], then divide (`src/d3d9.cpp:1633–1642`) — and clamp IPD to 45–85 mm
else 63 mm (`548–551`). Detail in §3.6.

### 3.4 The presentation-mode fork, and a warning about `MODE_VR = 0`

`MODE_VR` / `MODE_3D` per state (`src/winlatorxr_frame.inl:897–902`):
`(1,1)` side-by-side, `(1,2)` AER, `(1,0)` mono VR, `(2,0)` flat 2D for
menus/loading/screens. And one rule worth copying verbatim:

> **`MODE_3D` is clamped to 0 unless `MODE_VR == 1`**, and to 0..2 otherwise
> (`248–260`).

A stray stereo toggle should be a visual no-op, not a mode conflict. That is the
"centralise mode selection" advice from `TIPS.md`, implemented as a guard.

The warning matters more:

> **They never send `MODE_VR = 0` at all — that transition froze WinlatorXR** —
> and on shutdown they send **`MODE_VR = 2`** instead (`330–337`).

So the tally on `MODE_VR = 0` for menus is now:

| Port | Uses `0` for menus? | 3D mode | Evidence |
|---|---|---|---|
| Interstellar-Marines | **yes** | mono | device-confirmed on Quest 3; menus work, pose stream stops |
| XOPS | **no** — always `1` | AER | centre-third floating panel instead |
| Mirror's Edge | **no** — never sends `0` | AER | **froze WinlatorXR** on the transition |

**Resolved, 2026-11-17 (owner): `MODE_VR = 0` is safe in mono; it breaks with
AER.** That explains the split exactly — the one port that uses `0` is the one
port running mono, and the two that refuse it are both running alternate-eye.
The general guidance has been corrected accordingly
(`Docs/general_wxr/XRAPI_V0_4_DESC.md` §4).

For us this is good news and it removes a risk: mono is the shipping mode, so
`MODE_VR = 0` in menus stays. The plan still gains three hard rules, which
matter if stereo is ever added:

- **`MODE_3D = 0` unless `MODE_VR == 1`** — a stray combination must be a visual
  no-op, not a runtime state it cannot leave.
- **If AER ever ships, menus must not send `0` while AER was the last 3D mode.**
  Keep `MODE_VR = 1` with `MODE_3D = 0`, or use `2`.
- **Test any mode transition as its own deploy.** This is exactly the class of
  bug that only appears on device, and now we know the shape of it.

### 3.5 Camera: deltas the engine consumes, and a plausibility gate

The engine-specific machinery (GNames/GObjects walk, `UProperty` lookup by name)
does not transfer, but three principles do.

**a. Write a delta the engine consumes, not an absolute it will overwrite.**
They fold the head's rotation in as a **delta** into `TdPlayerController::Rotation`,
because the engine's `UpdateRotation` starts from `ViewRotation = Rotation` — so
the write is consumed rather than discarded (`ENGINE_NOTES.md:558–571`). A
detour that set the rotation absolutely was *the premise that did not transfer*.

For us the equivalent question is sharp: `CreateMove` sets `cmd->viewangles`
absolutely, and Source is expected to consume it as the new view — but that is
exactly the assumption `DEFECTS.md` D02/D03 got wrong about the *hook*, so do not
assume it about the *write*. Instrument it: log `cmd->viewangles` before and
after the engine's own processing for a few frames and see whether our value
survives.

**b. Drop implausible jumps, do not clamp them.** Deltas beyond a time-scaled
step limit (≈11°–45° per frame here) are **discarded**, not clamped
(`src/d3d9.cpp:14080–14084, 14582–14589`). Clamping a bad value still applies a
bad value; discarding it holds the last good pose, which is the rule we already
have. For Contagion this is also cheap anti-cheat insurance at multiplayer time:
a packet glitch cannot become a 180° snap.

**c. Roll cannot travel the aim path.** `UpdateRotation` zeroes
`ViewRotation.Roll`, so their roll, parallax and 6DOF translation are all applied
in the **view matrix** instead (`ENGINE_NOTES.md:589–590, 632–634`) — the
concrete form of our "logical aim and visual camera are separate concerns".

Their view-matrix hook is the fallback we listed, made specific:
`IDirect3DDevice9::SetVertexShaderConstantF` (vtable **slot 94**), register
**`c0`**, row storage, **world space**; copy the constant block, modify, forward
(`src/d3d9.cpp:135, 28059`). Their injection maths:
`M' = T(−o)·M`, i.e. `row3 -= ox*row0 + oy*row1 + oz*row2` (`22325–22333`).

**And a validation lesson that applies to us:** `c0` also carries a 160°×160°
light/shadow matrix, so they re-validate every upload and report
**220,000 accepted vs 91 rejected** (`ENGINE_NOTES.md:809–822`). A constant
register is not unique to the view; identify it by shape and reject the rest.

### 3.6 Quaternion ingestion: normalise, but bound it

They reject non-finite values, require **norm² ∈ [0.81, 1.21]**, then divide by
the norm (`src/d3d9.cpp:1633–1642`). IPD is clamped to 45–85 mm, else 63 mm
(`src/winlatorxr.inl:548–551`).

That is a *gated* normalisation, which is slightly different from the general
rule ("normalise unconditionally"). Both are defensible: unconditional
normalisation repairs a slightly-long tester packet, gated normalisation also
rejects a garbage one. For us: normalise unconditionally **and** reject a
non-finite or wildly out-of-range quaternion by holding the last good pose — the
two rules compose, and the second is the `freeze, never zero` rule applied to
validation.

### 3.7 Verification without a headset: dump the frame

`mevr_shot.request` (a file) makes the mod write `mevr_shot.bmp` of the presented
frame (`src/winlatorxr_frame.inl:288–293, 894`). That lets the sync pixel be
checked **offline, on the PC, with no headset and no owner**.

This is the single best idea in the three repositories for our current position.
Add it to `PLAN.md` §2.2: on demand, dump the back buffer to a BMP. Then the
first sync-pixel build can be verified by opening an image — red value at (0,0),
green zero — before anyone puts on a headset.

### 3.8 Logging: the NTFS trap that breaks "line 1 is the build stamp"

Two things to take, one of which is a genuine threat to our verification ritual.

**a. `%LOCALAPPDATA%` is unreadable on device.** Their log lives there on
Windows but **beside the DLL on the headset**, because the container's
LOCALAPPDATA cannot be read even by adb (`src/d3d9.cpp:325–347`). We chose
"beside the exe" already; this confirms it and explains why.

**b. Rotate the log every run, or "line 1" is a lie.** Their log is rotated to
`mevr_<start-time>.log` each run, **and the build stamp is parsed from the file's
own first line**, because **NTFS file tunnelling reused creation times and
silently concatenated runs into one file** (`349–411`).

That is exactly our failure mode waiting to happen: we write
`contagion-wxr.log` every launch and our whole verification story is "line 1 is
the build stamp". If a run appends to the previous run's file, line 1 is an old
stamp and the check silently passes against the wrong build — the precise
mistake the freshness guard exists to prevent.

**Fix:** truncate explicitly (`CREATE_ALWAYS`) *and* verify by size/rotation —
or write one file per run and keep the last N. Cheap, and it protects the one
ritual everything else depends on.

### 3.9 Configuration: audit every line, and version it

- The ini is read **beside the DLL first, then beside the log**; every line is
  audited (`applied` / `unknown key … ignored`) and summarised as
  `[cfg] N applied, M ignored`; a short read is reported as *"settings past that
  point were NOT applied"* (`src/d3d9.cpp:28754–28791`).
- **No config version key and no migration path exists** — the only
  backward-compatibility logic is a legacy fallback for one renamed key
  (`28716–28720`). That is the gap `TIPS.md` warns about, and it is the one
  thing this port does *not* do well.

For us: `contagion-wxr.ini` gets a `cfg_version`, logs applied/ignored counts,
and reports a truncated read instead of silently defaulting.

### 3.10 Performance: set it at run time, not in the game's file

Their Quest profile rewrites only existing `[SystemSettings]` lines, byte-for-byte
elsewhere, disabling shadows, motion blur, DoF, AO, distortion, lens flares,
volumetrics, bloom, sun haze, tone mapping and PhysX; `ScreenPercentage=100`,
`MaxAnisotropy=0`, `MaxMultisamples=0` (`src/d3d9.cpp:864–894`).

The transferable part is *when*:

> It must be written **after** the engine's own startup save, or the engine
> overwrites it — hence it is re-applied from `Present`, and takes effect from
> the **second launch** (`957–979`).

For Contagion the equivalent hazard is `contagion/cfg/config.cfg`, which the
engine owns and rewrites. **Do not edit it.** Set `mat_cont_*` and the quality
convars through the engine's own ConVar system at startup, every launch, and
log the value read back. That is the same conclusion as `TIPS.md`'s "prefer the
engine's own code paths", with a concrete reason to avoid the file.

They also measured `DetailMode` rather than assuming it (2 vs 1 vs 0 → 520k/525k/
525k world draws), and mono is their performance default with AER measured at
~40 fps against 13–15 for SBS (`src/d3d9.cpp:22445–22450`). That is the first
hard number we have for that trade in this family, and it is worth remembering
if Contagion ever wants depth.

---

## 4. Synthesis for Contagion

Ranked by what it changes in our plan, not by how interesting it was.

### 4.1 Four techniques adopted outright

| Technique | From | Where it lands |
|---|---|---|
| **Sync pixel as a `Clear`/`ColorFill` of a 10×10 rect, not a quad** | XOPS + ME, independently | `PLAN.md` §6 — removes the whole render-state class of bug |
| **Post window work to the window's own thread; rate-limit; give up loudly** | ME, with a measured 0.9 fps deadlock | `DEFECTS.md` D17 |
| **Outbound FOV measured from the live render, never echoed or constant** | XOPS (projection matrix) + ME (measured game FOV) | `DEFECTS.md` D15, `PLAN.md` §2.2h |
| **On-demand frame dump to a BMP, to verify the sync pixel offline** | ME | `PLAN.md` §2.2 — the first sync-pixel build becomes checkable without a headset |

The first is the reason `PLAN.md` §6 changed. The fourth is the reason the sync
pixel can be finished on the PC before the owner is involved at all.

### 4.2 Three things to do differently from our current plan

**a. Rotate the log, or "line 1 is the build stamp" is not true.** ME records
that NTFS file tunnelling reused creation times and **silently concatenated
runs** into one log, which forced them to parse the stamp out of the file rather
than assume it. Our entire verification ritual rests on line 1. Truncate
explicitly and rotate per run (`DEFECTS.md` D40).

**b. The window hook must also own focus.** ME swallows four focus-loss messages
because UE3 pauses audio and ignores movement on them. Source has the same
family of behaviours, and a container is exactly the environment that steals
focus. `DEFECTS.md` D19 now carries the focus half as well as the console key —
recorded here because the symptom ("it renders but ignores the stick") would
otherwise be blamed on the input mapping.

**c. Test `MODE_VR = 0` as its own deploy, with `2` ready.** One port uses it
successfully, two avoid it and one reports it **froze WinlatorXR**
(`SIBLING_PORTS.md` §3.4). The general guidance says device-verified on Quest 3.
That is a protocol-relevant discrepancy and it must be measured, not assumed.
Also adopt ME's guard: **`MODE_3D = 0` unless `MODE_VR == 1`.**

### 4.3 Two open questions settled

**`Z:\tmp\xr` / `D:\xrtemp` is the family convention.** Owner's decision,
XOPS's implementation, ME's implementation — three independent sources, and ME
adds the reason to test `Z:\tmp` rather than `Z:\`: `GetFileAttributes` on the
root fails under Wine. `DEFECTS.md` D26 stands as written, with the reason now
attached.

**Magnitude clamping for lean is what device-tested ports do.** IM
(`ClampMagnitude(hPos, 0.3f)`) and XOPS (deliberately not writing position at
all, applying translation render-side) agree with our D12 fix and D37 rule from
two directions.

### 4.4 Two warnings for our own plan

**Do not assume `CreateMove`'s absolute `viewangles` write is consumed.** ME's
central lesson is that a detour which wrote absolute rotation *did not transfer*
because the engine overwrote it; a **delta** was consumed. Source is expected to
consume `cmd->viewangles` absolutely, and that is probably right — but it is the
same class of assumption that produced D02 and D03. Instrument it: log the value
we wrote and the value the engine used, for a few frames, before building on it.

**Do not write the game's config file.** ME writes engine settings only *after*
the engine's own startup save and re-applies them from `Present`, and they take
effect on the **second launch**. Contagion rewrites `contagion/cfg/config.cfg`.
Set our convars through the engine at startup and log the values read back;
never edit that file (`SIBLING_PORTS.md` §3.10).

### 4.5 What not to copy

- **XOPS's manual freshness guard** and its release build deleting the log —
  the latter removes the build stamp exactly when a player needs to report a
  problem. Ours keeps line 1 always.
- **ME's missing config version and migration** — the one thing that port does
  badly, and the trap `TIPS.md` calls out. We have `cfg_version` from day one.
- **XOPS's `MODE_VR = 1` in menus** — a legitimate choice, but it gives up the
  flat, readable menu that `MODE_VR = 0` exists for. Keep 0, keep the constant.
- **ME's reproducible double-build and static-analysis gate** — both good, both
  expensive, neither needed before the first build exists. Revisit at release.
- **XOPS's differential memory scanner** — excellent, and unnecessary while the
  RTTI/vtable route is still open. Keep it as the fallback if `g_pViewRender`
  cannot be identified, because it needs no disassembly at all.

### 4.6 Which port to consult for what

| Question | Read |
|---|---|
| The XrApi client, handshake, `system`, FOV precedence | **Interstellar-Marines `XRAPI.cs`** — the clearest implementation |
| Sync pixel that survives scene changes | **Interstellar-Marines `SyncPixelQuad.cs`** for the failure modes; **XOPS/ME** for the `Clear` technique |
| Head pose, roll, lean, freeze | **Interstellar-Marines `VRHeadTracker.cs`**, plus **XOPS** for the aim/view split |
| Input: held vs edge, the single gate, don't trust the cursor | **Interstellar-Marines `VRInput.cs` + `VRInputGate.cs`**, plus **XOPS** for the consumer-hook pattern in `PC_INPUT.md` |
| Window ownership, focus, deadlock | **mirrors-edge `winlatorxr_frame.inl`** |
| Verification without a debugger | **XOPS** (first-call trace, audit dump, self-validating patches, frame counters) and **ME** (BMP dump, log rotation) |
| Deriving an unknown address when no symbols exist | **XOPS** (`SIBLING_PORTS.md` §2.2) |
| Not breaking the engine's own state | **mirrors-edge `ENGINE_NOTES.md`** |
