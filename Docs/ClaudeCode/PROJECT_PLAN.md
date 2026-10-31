# PROJECT_PLAN.md — Contagion to standalone VR via WinlatorXR

**Written 2026-10-31.** The ordered plan. Filled in from
`../general_wxr/PROJECT_PLAN.md`, whose structure this follows, and sequenced
against `../general_wxr/FIRST_STEPS.md`.

Read [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) first for the goal and the
verified facts about the target. [CURRENT_STATUS.md](CURRENT_STATUS.md) is the
live state; [BUG_FIXES.md](BUG_FIXES.md) is the defect register that milestones
0–2 work through.

Provenance tags are as defined in `PROJECT_OVERVIEW.md` §0.

---

## 1. Intake

> Filled in on 2026-10-31. Items marked `TBD` are genuinely unknown and are a
> prompt to ask, not a gap to paper over. Anything **[verified]** was read off
> the owner's install; see `PROJECT_OVERVIEW.md` §2 for how.

### The game

| Question | Answer |
|---|---|
| Game, version, patch level | Contagion, Steam AppID 238430 **[verified]**. Exact build number `TBD` — needs the value the game prints in console or the Steam manifest buildid |
| Store or distribution | Steam, installed at `E:\Steam\steamapps\common\Contagion` **[verified]** |
| Bitness | **32-bit (i386)** throughout **[verified]** |
| Renderer | **Direct3D 9** (`shaderapidx9.dll`, `stdshader_dx9.dll`) **[verified]** |
| Engine | A heavily modified Source fork off the **Left 4 Dead 2 branch**; fan-named *MonoEngine*. Jolt physics, Wwise audio, CEF, Angelscript **[verified]** / **[web]** |
| Source available? Under what licence? | **No public source.** No Source SDK release covers this fork |
| Mod API or SDK? | A **server-side** Angelscript plugin API (`default_plugins.json`), plus Hammer and an Angelscript compiler for map and server content **[verified]**. **No client-side API** — it cannot reach the camera, so it is not a vector for this port |
| Does a VR build already exist? | Not for this game. `gameinfo.txt` says `"supportsvr" "0"` **[verified]**. The separate `ContagionVR` title in the owner's library is a different product, not a mode of this one — see `PROJECT_OVERVIEW.md` §3 |
| Existing PCVR mod to retarget? | None known. `TBD` — worth one search of the Contagion modding community before milestone 4 |
| Existing modding community or documentation? | Yes: a Steam Workshop (`contagion/workshop`, `addons/*.vpk` present **[verified]**), the Valve Developer Wiki page, and a developer-provided SDK. Whether anyone has documented the **client camera** is `TBD` and is the question worth asking them |
| Native head-tracking support? | **No.** Not a cockpit game; `opentrackWXR` does not apply |

### The port

| Question | Answer |
|---|---|
| Real VR port or injection port? | **Real VR port** — XrApi, 6DOF, Path A (owner's framing; see `PROJECT_OVERVIEW.md` §3) |
| Target headsets | **Meta Quest 3**, standalone, via WinlatorXR. Others untested |
| Presentation mode | **Mono VR** (`MODE_VR = 1`, `MODE_3D = 0`) in a round; `MODE_VR = 0` in menus. Monocular is the owner's stated goal |
| Ship a non-VR mode as a fallback? | Not planned for the first build. It comes almost free once mode selection is centralised, so revisit at milestone 10 |
| Recommended container resolution | **1024x768** — owner's decision, 2026-10-31, for performance and simplicity. Deliberately below panel resolution; see `PROJECT_OVERVIEW.md` §5 for what that costs and what it changes |
| First-person, third-person or tabletop | First-person |
| Hands in view at all? | **Not in the first build.** The existing viewmodel stays as the game draws it. Hand and weapon placement is milestone 8 and is explicitly deferred |
| If injection: is mouse motion needed? | N/A — not an injection port |
| Multiplayer in scope? | **Not for the first build.** Singleplayer and local play, i.e. a locally hosted server with bots, since the game is `"multiplayer_only"` **[verified]**. Public-server play is milestone 11 (owner's decision, 2026-10-31) |

### Constraints

| Question | Answer |
|---|---|
| May the result be published? Where? | Yes — this repository, under its existing MIT `LICENSE` |
| May any of the game's source appear in the repository? | **Moot, and therefore forbidden by default.** There is no source release to copy, so nothing of the game's code, decompiled or otherwise, belongs here. Offsets, interface names and struct layouts we derive are our own findings and are fine to record |
| Which files are release-owned and must never be touched by automation? | `TBD`. There is no `Release/` tree yet, though `.gitignore` already excludes one. Settle this before the first release |
| Who verifies on hardware, and how fast is that loop? | **The owner, on a Quest 3.** Loop length `TBD` — needed to judge how much to batch per deploy |
| Any prior work to inherit or discard? | `Docs/MetaAI/` — **keep the shape, rewrite the internals** (owner's decision, 2026-10-31). See §4 |

---

## 2. Hard rules

Binding on every pass, including agents and scripts.

**Everything in `../general_wxr/CLAUDE.md` applies**, and its non-negotiables
are not restated here. In particular: the sync pixel is not optional; the window
must be borderless and filling, re-applied every frame; normalise every
quaternion on ingest; swap handedness once, at ingest; convert each pose
independently of the head; freeze on the last good pose, never zero; measure
offsets in the frame you will use them in.

**Added for this project:**

1. **No SDK-derived constant is trusted.** This game is not Source SDK 2007
   (`PROJECT_OVERVIEW.md` §2). Every vtable index, interface version, struct
   offset and global address must be derived from *these* binaries and then
   confirmed by a log line from the running game. A number copied from
   `Docs/MetaAI/` or from any Source SDK header is a hypothesis, and must be
   labelled **[unverified]** until a log proves it.
2. **The player's collision hull is never written.** Head position affects
   `CViewSetup::origin` only. No `SetAbsOrigin`, no hull or eye-offset changes.
3. **`Docs/MetaAI/` is history.** It may be read for design intent. Its
   completion percentages, offsets and interface layouts are not current truth
   and must not be cited as evidence.
4. **One change per deploy**, and nothing is done until the owner has verified
   it on the Quest 3.
5. **Diagnostics behind one verbose flag, off by default** — file dumps
   included.
6. **The build stamp is mandatory.** `__DATE__ __TIME__` compiled in, logged on
   line 1, and the deploy script refuses a binary older than the newest source
   file.
7. **The owner's game install is read-only to automation.** Reading
   `E:\Steam\steamapps\common\Contagion` to derive offsets is expected; writing
   anything into it other than the mod's own deployed files is not.

---

## 3. What exists today

See [CURRENT_STATUS.md](CURRENT_STATUS.md) for the detail. In one paragraph:

**Nothing runs, and nothing builds.** The repository contains documentation
only — no mod source at its root. `Docs/MetaAI/MetaAITestProjects/` holds three
folders that are **one identical codebase** (they differ only by the presence of
a bundled `minhook.cpp`), and that codebase **does not compile**: `CUserCmd` is
missing six members the code uses, two translation units define the same two
functions, and one function redeclares its own parameter. Its design intent is
largely sound and worth keeping; its numbers come from the wrong engine branch.

### The blocker

**There is no working build and no log, so nothing can be measured.** Every
other question in this project — are the hooks in the right place, is the pose
maths right, does the sync pixel reach the runtime — is unanswerable until a
binary loads into Contagion and writes a line to a file. That is milestone 2,
and it gates everything.

---

## 4. Approach

### Injection vector

**`version.dll` proxy** at the install root, next to `contagion.exe`, forwarding
every export to `version_o.dll` (a copy of the system `version.dll`), enabled
with `WINEDLLOVERRIDES=version=n,b`.

Why: it is loaded very early, by name, from the game's own directory, and is
almost never actually used for anything. `../general_wxr/WINLATORXR_DESC.md` §6
names it the workhorse vector and cites Contagion specifically. The inherited
code already implements the proxy exports **[code]**, and that part carries
forward.

### Inherited code: keep the shape, rewrite the internals

Owner's decision, 2026-10-31. Concretely:

**Carry forward:**

- The module split — `xrapi` / `math` / `input` / `hooks` / `gameui` /
  `wndproc` / `d3d9_hook` / `pattern`. It is a sensible decomposition.
- The `version.dll` proxy export list and the `WINEDLLOVERRIDES` launch recipe.
- The **input mapping table** (§5 below), which is the owner's own design and
  the most valuable thing in the inherited work.
- The visual-only-lean rule, and its comment.
- Menu detection driving `MODE_VR = 0`.
- The units constant (39.37 units/metre) and the ~15 cm lean clamp.
- The voice wheel *concept*, deferred to milestone 10.

**Rewrite from scratch:**

- Every struct layout and vtable index, derived from the real binaries.
- All pose maths. The Euler extraction is wrong in a way that caps yaw at ±90°
  (`BUG_FIXES.md` #2) and the lean is applied in the wrong frame
  (`BUG_FIXES.md` #3).
- The engine and cvar interface access, which is currently fabricated
  (`BUG_FIXES.md` #5).
- The XrApi client: add quaternion normalisation, the `vr` handshake file, the
  `system` read, a desktop fallback path, and version `0.5`.
- The sync pixel: 10x10, exact echo, drawn genuinely last
  (`BUG_FIXES.md` #4, #8).

**Drop entirely:**

- The bundled 5-byte-JMP `minhook.cpp`. Use real MinHook; the hand-rolled
  trampoline cannot handle a short prologue and this is not the place to
  economise.
- `d3d9_hook.cpp` (the older of the two duplicates). Keep the `_1` variant's
  content as the starting point for the rewrite.
- `GetTickCount()`-derived fake sync values (`BUG_FIXES.md` #8).

### Decisions taken deliberately

| Decision | Choice | Why |
|---|---|---|
| Bitness | **32-bit** | The game is 32-bit **[verified]**; there is no OpenXR runtime to load, so no reason to cross a bitness boundary. Stays in-process |
| Renderer | **Keep the game's D3D9** | Known-good under Box86 and Wine; mono needs no modern API at all |
| Wrappers (dgVoodoo, ReShade) | **None** | Another layer inside two translation layers. WinlatorXR can inject ReShade itself if a player wants to experiment |
| Process model | **In-process**, single DLL | No IPC, no second binary to ship, start and reap |
| Presentation mode | **Mono** (`MODE_3D = 0`); `MODE_VR = 0` in menus | Owner's stated goal; mono is a legitimate shipping mode and costs one render |
| XrApi version | **0.5** | 0.4's packet plus the InputAPI listener at no cost; human-readable in a log and reproducible in the UDP Tester |
| Hooking library | **Real MinHook** | The bundled 5-byte JMP is too fragile for functions with a short prologue |
| Per-frame draw hook | **`Present` (vtable 17)**, not `EndScene` (42) | See the note below — needs device confirmation |
| Supersampling | **Container resolution**, no code | The runtime already filters on downsample |
| Quality settings | **The game's own `potato` presets** where possible | A shipped code path beats a new one, and these already exist **[verified]** |
| Settings surface | **In-game menu** eventually; a flag file for developer options meanwhile | Editing an INI on a headset is painful |

#### Note on `Present` versus `EndScene`

The inherited code draws the sync pixel in `EndScene` **[code]**. That is a
risk worth naming: Source-family engines call `EndScene` more than once per
frame and render into offscreen targets (water reflection, refraction,
render-to-texture HUD elements), so a square drawn there may land in a texture
rather than the back buffer, or be overwritten by the HUD drawn afterwards.
`../general_wxr/XRAPI_V0_4_DESC.md` §5 is emphatic that the square must be the
**last** thing drawn.

**Plan:** hook `Present` and draw immediately before calling through, with the
render target explicitly set to back buffer 0. **[unverified]** — confirm on
device at milestone 5 by checking whether head tracking feels locked, and by
reading back the pixel in a desktop test.

---

## 5. Milestones

From `../general_wxr/FIRST_STEPS.md`. Each leaves something testable and has a
human checkpoint. This list is the project's live status; tick items here.

- [ ] **0 — Feasibility assessed, intake complete.** Mostly done: see §1. Open
      items are the build number, the container resolution, the hardware loop
      length, and one search for prior Contagion camera documentation.
- [x] **1 — Unmodified Contagion runs in a WinlatorXR container.** **Owner
      reports the game runs flat in WinlatorXR** (2026-10-31), at the chosen
      **1024x768**. Resolution settled; the risk that the game cannot be
      launched at all is closed.
      Sub-questions still open, and cheap to answer on the next launch:
      - What workarounds were needed? Each one is something a future installer
        or launcher has to automate.
      - Do the `potato` quality presets stay applied **after a relaunch**?
        Games of this era silently restore graphics settings, and this is a
        live problem in another port in this family.
      - Do **CEF** (`libcef.dll`) and **Wwise** (`AkSoundEngineDLL.dll`) behave?
        They are the two components most likely to misbehave under Wine, and
        finding out now means a later failure is not misattributed to the mod.
- [ ] **2 — Our `version.dll` loads and writes a log with a build stamp.** The
      whole deliverable is one file with one line. Add the build-and-deploy
      script with its freshness guard in the same milestone, before there is
      anything complicated to debug. **This is the current blocker.**
- [ ] **3 — Window is borderless and fills the display.** Strip
      `WS_CAPTION | WS_THICKFRAME | WS_BORDER`, size to screen metrics, and
      re-apply every frame. A prerequisite for the sync pixel, not a cosmetic
      step. Nothing in the inherited code does this at all (`BUG_FIXES.md` #7).
- [ ] **4 — XrApi 0.5 client parses real device packets.** Build it against the
      UDP Tester on the desktop, then have the owner capture **one real packet
      on the Quest 3** and diff it field by field. Build the button table from
      what actually moved, not from any document.
- [ ] **5 — Sync pixel: 10x10, exact echo of `HMD_SYNC`, green 0, drawn last.**
      Before the camera, so a swimmy image later is not misattributed.
- [ ] **6 — 6DOF head tracking feels locked.** **The big one.** Orientation
      first, then position. Mono. This requires the `OverrideView` and
      `CreateMove` hooks to be landing on the right functions, which requires
      milestone 2's log to confirm.
- [ ] **7 — Mono confirmed and measured on device.** Frame rate at the chosen
      container size, read from WinlatorXR's own counters rather than from
      anything we instrument.
- [ ] **8 — Hands and weapon placement.** Deferred by design. The genre wants
      it eventually; nothing above depends on it.
- [ ] **9 — Full input mapping, owner-confirmed.** The table below, confirmed
      before implementing and again on device after.
- [ ] **10 — Comfort, polish, settings.** HUD scale, recentre, an in-game
      settings menu, the voice wheel, haptics.
- [ ] **11 — Public-server multiplayer.** Out of scope for the first build.

### The input mapping table

This is the owner's design, carried over from `../MetaAI/PROJECT_PLAN.md` §3
**[code]**, restated here as the thing to confirm at checkpoint 9. The `IN_*`
flag values and the console command names are **[unverified]** — see
`BUG_FIXES.md` #10.

**Left hand**

| Control | Action | Mechanism |
|---|---|---|
| Stick | Move | `forwardmove` / `sidemove` |
| Stick click | Sprint | `IN_SPEED` |
| X | Crouch | `IN_DUCK` |
| Trigger | Melee | `IN_ATTACK2` **[unverified]** — Contagion's melee binding needs confirming |
| Y (tap) | Flashlight | `impulse 100` |
| Grip | Modifier — no action of its own | |
| Grip + Y, tap under 300 ms | Phone | console command, name `TBD` |
| Grip + Y, hold over 300 ms | Voice wheel (milestone 10) | |

**Right hand**

| Control | Action | Mechanism |
|---|---|---|
| Trigger | Fire | `IN_ATTACK` |
| Grip | Jump | `IN_JUMP` |
| A | Use | `IN_USE` |
| B | Reload | `IN_RELOAD` |
| Stick up/down | Next/previous weapon | `inv_next` / `inv_prev` |
| Stick left/right | Smooth turn | accumulated yaw offset — see below |
| Grip + A | Drop ammo | console command, name `TBD` |
| Grip + B | Drop item | console command, name `TBD` |

**Two things the inherited implementation gets structurally wrong**, both of
which this table depends on:

- **Turning does not work** as written, because `CreateMove` sets
  `viewangles.yaw` from the HMD *after* the stick has added to it, so the stick
  delta is discarded every frame (`BUG_FIXES.md` #6). The fix is a persistent
  `g_yawOffset`, accumulated from the stick, with
  `viewangles.yaw = wrap(hmdYaw + g_yawOffset)`.
- **That same offset is what the lean must be rotated by** (`BUG_FIXES.md` #3).
  The head's positional delta is in physical room space; the room maps onto the
  game world rotated by `g_yawOffset` — and by that alone, because the HMD's own
  yaw is already expressed in the physical delta. Getting this wrong makes
  leaning forward move the view along a fixed world axis regardless of which way
  the player faces.

Also to settle at checkpoint 9: which actions are **held** versus
**edge-triggered**. A VR trigger is a held state, and a game waiting for a
key-down edge will wait forever — this is named in `../general_wxr/FIRST_STEPS.md`
as the cause of a hung respawn in another port.

---

## 6. Features

One subsection per feature, written before implementing. Only the features that
gate the first build are specified here; later ones get written up when they
come up.

### Feature 1 — Code loads and logs

**Goal.** A line of text in a file, written from inside `contagion.exe`,
proving the proxy works under Box86 and Wine and that we know which binary is
running.

**Can it be done at all?** Yes, with high confidence — `version.dll` proxying is
proven in this scene and was used by a previous Contagion attempt.

**What must be proven first.** That Wine's loader accepts the proxy with this
game's export set. The inherited notes record that forwarding only
`GetFileVersionInfoA` was not enough and that `GetFileVersionInfoSizeW` and
`VerQueryValueW` were also needed **[code]** — so start from a full export list.

**Approach.** Proxy exports, `DllMain` spawns a thread, the thread opens a log,
writes `__DATE__ __TIME__` plus the process and module paths, and returns. No
hooks yet. Build-and-deploy script with freshness guard and SHA-256 check in the
same step.

**Fallbacks.** If `version.dll` is rejected: `dinput8.dll`, then `d3d9.dll`.
The game ships `xinput1_3.dll` **[verified]**, which is a further candidate.

**Settings.** None.

**Acceptance criteria.**
- [ ] A log file appears next to the game after launching in the container
- [ ] Line 1 shows the stamp of the binary just built — **owner-observed, on
      the Quest 3**

### Feature 2 — Window ownership

**Goal.** The game fills the container display with no chrome, so the sync pixel
lands on our pixel rather than a title bar.

**What must be proven first.** Whether this engine fights a hand-rolled resize.
Source-family engines run consistency checks that reset the display mode when
surface, render and client sizes disagree. Log window, surface and render
dimensions whenever they change, and watch for a fight.

**Approach.** Idempotent `EnforceWindow()`, called from the per-frame hook.
Prefer letting the container resolution and the game's own fullscreen mode do
the work if they already agree.

**Acceptance criteria.**
- [ ] No title bar, no border, image fills the view — **owner-observed**
- [ ] Still true after a video-mode change and after alt-tab

### Feature 3 — XrApi 0.5 client

**Goal.** Head and hand poses, buttons, sticks, IPD, FOV and `HMD_SYNC`
available to the rest of the mod, every frame, with the handshake correct.

**Approach.**
1. Handshake into `Z:\tmp\xr` with a desktop fallback when `Z:\` is absent.
   Write `version` = `0.5` **and** `vr` = `VR`; delete `vr` on clean shutdown.
   **Read `system`** for the headset identity and as the "am I on a headset?"
   test — the inherited code does none of this except `version` **[code]**.
   Write the files *after* the runtime is up, or be ready to rewrite them: the
   runtime wipes the directory at startup.
2. Bind **one** of 7872 / 7873. From 0.4 the runtime sends the same packet to
   both; binding both means handling every packet twice.
3. **Drain** every queued datagram per frame and keep the newest.
4. Parse 28 floats, frame id, bool string, HMD name. Tolerate extra trailing
   tokens and both bool-string lengths.
5. **Normalise every quaternion on ingest, unconditionally.**
6. Log the raw packet verbatim every few seconds behind the verbose flag.
7. Send on 7278 every frame: haptics, `MODE_VR`, `MODE_3D`, FOV. Send FOV as
   **degrees**, defaulting to `104.5 104.5`, loaded from a flag file so it can
   be tuned on device without a rebuild. Do **not** echo the inbound FOV floats
   — those are tangents of half-angles and the runtime ignores any value below 1
   (`BUG_FIXES.md` #9).
8. Decay haptics locally and keep sending the decreasing number; sending 0 does
   not clear them.

**Acceptance criteria.**
- [ ] Desktop: the UDP Tester drives the whole path
- [ ] One real Quest 3 packet captured and diffed against the tester's
- [ ] Button table built from the owner pressing each button in turn with the
      bool string logged — **owner-observed**

### Feature 4 — Sync pixel

**Goal.** The runtime can match each captured image to the pose it was rendered
for.

**Approach.** In the `Present` hook, as the last thing before calling through:
set render target 0 to the back buffer, disable alpha blending, fog and Z, set
point sampling on all three filters, and fill a 10x10 rect at (0,0) with
`RGB(clamp(HMD_SYNC), 0, 0)`.

**Echo `HMD_SYNC` exactly as received.** No rescaling, no gamma correction, no
invented ids — the runtime calibrates itself to whatever colour space the
renderer produces, and that remapping is gated on green being 0. When no packet
has arrived yet, draw the **last good value**, or nothing. Never a fabricated
one (`BUG_FIXES.md` #8).

**Acceptance criteria.**
- [ ] Desktop: read back the pixel and confirm it equals the value received
- [ ] Device: head tracking feels **locked**, not swimmy — **owner-observed**

### Feature 5 — 6DOF head tracking, mono

**Goal.** The head drives the camera. The first real milestone.

**What must be proven first.** That the `OverrideView` and `CreateMove` hooks
land on the right functions in *this* binary. This is the largest single
unknown in the project and the one most likely to consume time. Plan:

1. Log the pattern-scan result, the resolved global, the object pointer and the
   first 32 vtable entries, once, at startup.
2. Confirm by behaviour, not by assumption — a hook that fires once per frame
   with a plausible `CViewSetup` is the evidence.
3. Only then write anything through the pointers.

**Approach.** Orientation first, position second. `CreateMove` sets yaw and
pitch from `hmdYaw + g_yawOffset` and `hmdPitch`, wrapped to ±180 and clamped to
±89 respectively, and zeroes `mousedx` / `mousedy`. `OverrideView` sets the full
visual orientation including roll, and adds the room-space lean **rotated by
`g_yawOffset`** and clamped. Recentre binding from day one. Freeze on the last
good pose when packets stop.

**Fallbacks.** If the client-mode hooks cannot be landed: a view-matrix hook in
the shader API, or — as the route that needs no camera work at all — non-VR
mode (`MODE_VR = 2`), which stays available precisely for this case.

**Acceptance criteria.** Checkpoint 6 in full — world still when the head is
still, no swimming on a fast turn, leaning moves the view the right way and the
right amount, horizon level. **Owner-observed, and the gate on everything
after it.**

### Feature 8 — Stereo (not in the first build)

Recorded only so the decision is visible: mono ships first. If stereo is ever
wanted, **AER** (`MODE_3D = 2`) is likely the better bargain than SBS on a
mobile GPU behind two translation layers — depth at mono cost, each eye
refreshing at half rate. It needs the IPD from the packet and the eye flag in
the sync square's blue channel. Not a goal.

---

## 7. Verification

**Build and deploy.** `build/build-and-deploy.ps1` (to be written at milestone
2). It must: wipe intermediates; build Release|Win32; read the `__DATE__
__TIME__` stamp out of the binary and **refuse to deploy** if it is older than
the newest source file; copy to each target and verify with SHA-256, printing
`match=True`.

**Confirm the right binary is running.** Line 1 of the log carries the build
stamp. Check it before interpreting any device report. This is not optional
bookkeeping — a stale build tested as new wastes a whole session and is
invisible without the check.

**Desktop pass.** With the XrApi UDP Tester. It proves the code path runs. It
does **not** prove the values are right: its quaternions are often not unit
length and its hand separation does not match a real headset.

**On-device pass** — the only real gate:

- [ ] Build stamp in the log matches what was just built
- [ ] Head tracking still feels locked
- [ ] Frame rate acceptable at the chosen container size, read from
      WinlatorXR's own counters
- [ ] Menus still usable, and the pose stream resumes on returning to a round
- [ ] Previously working features unaffected — name the ones the change could
      plausibly have touched, not all of them

---

## 8. Risks

| Risk | Likelihood | What we do about it |
|---|---|---|
| **The client-mode hooks cannot be landed** on this fork, so there is no camera to write | **High** — this is the project's central risk. The engine is a modified L4D2 fork with no SDK | Log the vtable before writing through it (Feature 5). Fallbacks ranked: shader-API view matrix, then non-VR mode, which needs no camera work at all |
| Struct layouts differ from every Source SDK | **High** | Derive from the binary; confirm by logging a field that has a knowable value, such as the viewport size in `CViewSetup` |
| The dummy-D3D9-device trick fails under Wine/DXVK | Medium | The inherited code creates a throwaway device to read the vtable **[code]**, with `GetDesktopWindow()` as the device window. If that fails, get the device from the game instead, or pattern-scan `shaderapidx9.dll` |
| `EndScene` is called into render targets, so the sync pixel never reaches the captured frame | Medium | Hook `Present`; set render target 0 explicitly; verify by pixel read-back |
| CEF or Wwise misbehaves under Box86/Wine | Medium | Establish at milestone 1, before any mod code exists to blame |
| MinHook behaves differently under Box86 | Medium | Prove the hook on device at milestone 2, before a feature depends on it |
| The engine fights a hand-rolled window resize | Medium | Log window/surface/render sizes on change; move all three together, or let the container do it |
| Tuned values do not transfer from tester to device | Medium | Prefer values whose frame of reference does not depend on the pose. If a number only works where it was found, fix the frame |
| A saved `config.cfg` overrides a shipped default on the test device | Medium | Say so explicitly when handing over a build; check `contagion/cfg/config.cfg` |
| The game silently restores graphics settings on relaunch | Medium | Named as a live problem in another port in this family. Verify `potato` presets stay applied across a restart |
| Workshop addons change models the mod depends on | Low (nothing depends on art yet) | Degrade to stock behaviour rather than drawing something broken |
| Anti-cheat objects to `CreateMove` angle writes | Low while singleplayer | Deferred with multiplayer to milestone 11. `-insecure` is the inherited mitigation **[code]**, unverified |

---

## 9. What we learned

> Append as we go. Not code structure — git shows that. The things that are
> invisible afterwards.

### The engine branch was wrong, and it invalidated a project's worth of numbers

**2026-10-31.** The Meta AI attempt states Contagion is "32-bit Source SDK
2007". The Valve Developer Wiki says it is built on the **Left 4 Dead 2 engine
branch**, and the install contains Jolt physics, Wwise, CEF and Angelscript —
a fork that has moved on even from L4D2.

Everything downstream inherited the error: vtable indices, a `g_pClientMode`
address, `GameUI011`, `VEngineClient014`, and the `CUserCmd` and `CViewSetup`
layouts. The documents report these with a confidence the evidence never
supported — an IDA screenshot confirming a *pattern match* was recorded as
confirming the *premise*.

The lesson is cheap to state and was expensive here: **establish the engine
branch from the shipped files before deriving a single offset**, and tag every
number with how it was established. Five minutes of listing a directory would
have caught it.

### "80 % complete" meant "written", not "compiling"

**2026-10-31.** `../MetaAI/CURRENT_PROGRESS.md` reports eight subsystems at
60–90 %. The code does not build — six missing struct members, a duplicate
translation unit, a redeclared parameter. `../MetaAI/BUG_FIXES.md` lists
thirteen fixes in a confident past tense, several of which describe behaviour of
code that has never executed.

Two practices follow, both already in `../general_wxr/` and both worth the
restatement: **a percentage is meaningless without a build**, and the first
milestone that produces a binary and a log should come before any feature work,
because until then no claim about the code can be checked at all.

### Angelscript is server-side, so it is not a VR vector

**2026-10-31.** `vscript.dll`, `ascompiler/` and `ASCompiler.exe` suggested a
scripting route that might avoid native hooking entirely. The answer is in the
game's own `default_plugins.json`: it is for *"Angelscript **server**
plugin(s) ... to enable on your server"*. No client camera access, so it cannot
do a VR port. Recorded so it is not investigated twice.

### The game ships quality presets below its own minimum spec

**2026-10-31.** `contagion/cfg/` contains `cpu_level_potato.ekv` and
`gpu_level_potato.ekv`. On a mobile GPU behind two translation layers, an
existing shipped low-quality path is worth more than anything we could write,
and `../general_wxr/TIPS.md` prefers the engine's own code paths for exactly
this reason. The `.ekv` files are VICE-encrypted key-values — `vice.exe` ships
alongside them — so they are not readable as text, but they are selectable by
the engine's own quality-level convars.
