# PROJECT_OVERVIEW.md — Contagion to standalone VR via WinlatorXR

**Written 2026-10-31.** The goal, the target, and the decisions that frame
every other document in this folder. Read this first, then
[PROJECT_PLAN.md](PROJECT_PLAN.md) for the ordered work,
[CURRENT_STATUS.md](CURRENT_STATUS.md) for where the code actually stands, and
[BUG_FIXES.md](BUG_FIXES.md) for the defect register.

Game-agnostic WinlatorXR guidance lives in [../general_wxr/](../general_wxr/)
and is binding — in particular its `CLAUDE.md`. The earlier Meta AI attempt is
in [../MetaAI/](../MetaAI/) and is **design intent and history, not current
truth**; see `CURRENT_STATUS.md` for the specific points where it is wrong.

Provenance tags used throughout this folder:

| Tag | Means |
|---|---|
| **[verified]** | established from the owner's own install, or by running a check, on 2026-10-31 |
| **[code]** | read directly out of the inherited Meta AI source |
| **[spec]** | from `../general_wxr/` or the XrApi specification |
| **[web]** | from a public source, named inline |
| **[unverified]** | an assumption. Nothing load-bearing may rest on one of these without a named experiment |

---

## 1. The goal

Play **classic flat Contagion** — the 2013 Source-derived zombie survival
shooter — on a **Meta Quest 3 standalone**, with head tracking, through
**WinlatorXR**.

Concretely, the thing the owner should be able to do at the end:

> Put on a Quest 3, launch Contagion in a WinlatorXR container, and play a
> local round where looking around is done with the head, moving and shooting
> are done with the Touch controllers, and the image is locked to the head
> rather than swimming.

**Monocular**, deliberately — one render per frame, one image to both eyes,
`MODE_3D = 0`. This is the mode the owner named in the repository README, and
`../general_wxr/TIPS.md` is explicit that mono is a legitimate shipping mode
rather than a fallback. Stereo is a later, separately justified decision; see
`PROJECT_PLAN.md` §6, Feature 8.

**Scope on day one is singleplayer and local play** (owner's decision,
2026-10-31). Contagion's `gameinfo.txt` declares `"type" "multiplayer_only"`
**[verified]**, so "singleplayer" in practice means a locally hosted server
with bots — `botprofiles.dt` ships with the game **[verified]**. Public-server
multiplayer is a later milestone, not a constraint on the first build.

---

## 2. The target, as actually measured

Everything in this table was read off the owner's install at
`E:\Steam\steamapps\common\Contagion` on 2026-10-31, not inferred.

| Fact | Value | How established |
|---|---|---|
| Steam AppID | 238430 | `steam_appid.txt` **[verified]** |
| Bitness | **32-bit (i386)** — `contagion.exe`, `engine/engine.dll` and `contagion/bin/client.dll` are all PE machine `0x14c` | PE header read **[verified]** |
| Renderer | **Direct3D 9** — `shaderapidx9.dll`, `stdshader_dx9.dll` | `engine/` listing **[verified]** |
| Engine family | A **heavily modified Source fork** descended from the Left 4 Dead 2 branch. A fan wiki calls it *MonoEngine* | [developer.valvesoftware.com](https://developer.valvesoftware.com/wiki/Contagion), [wiki.zombiepanicsource.com](https://wiki.zombiepanicsource.com/Contagion) **[web]** |
| Declares VR support | **No** — `"supportsvr" "0"` | `contagion/gameinfo.txt` **[verified]** |
| Game type | `"multiplayer_only"` | `contagion/gameinfo.txt` **[verified]** |
| Engine DLL location | **`engine/`**, not the usual Valve `bin/` | directory listing **[verified]** |
| Client DLL location | `contagion/bin/client.dll`, reached by the `gamebin` search path | `gameinfo.txt` **[verified]** |
| Physics | **Jolt** — `vphysics_jolt_sse2/sse42/avx2.dll` | `engine/` listing **[verified]** |
| Scripting | Angelscript (`vscript.dll`, `ascompiler/`, `ASCompiler.exe`) | listing **[verified]** |
| Audio | **Wwise** (`AkSoundEngineDLL.dll`) alongside Miles (`Mss32.dll`, `vaudio_miles.dll`) | listing **[verified]** |
| Embedded browser | CEF (`libcef.dll`) | listing **[verified]** |
| Quality presets | `cpu_level_potato.ekv` and `gpu_level_potato.ekv` ship with the game | `contagion/cfg/` **[verified]** |

### Why the engine family is the single most important fact here

The whole Meta AI attempt is built on the premise that Contagion is
**"32-bit Source SDK 2007"** (`../MetaAI/PROJECT_PLAN.md` §1). It is not.

That premise is the root of most of the defects in `BUG_FIXES.md`. Every
hardcoded number in the inherited code — vtable indices 14 and 21,
`g_pClientMode = 0x107B5F98`, the `GameUI011` interface name,
`VEngineClient014`, the `CUserCmd` and `CViewSetup` field layouts — was
reasoned from an SDK this game is not built on. The Jolt physics DLLs and the
CEF and Wwise integrations show the fork has moved on considerably even from
the L4D2 2013 branch.

**The standing rule for this project follows from that:** no offset, vtable
index, interface version or struct layout is trusted until it has been derived
from *these* binaries, or confirmed by a log line printed from inside the
running game. An SDK header is a hypothesis here, not a reference.

---

## 3. The path chosen, and the paths rejected

`../general_wxr/WINLATORXR_DESC.md` lays out five routes. Recording the choice
so it is not relitigated:

| Path | Verdict |
|---|---|
| **A — XrApi, mod the game directly** | **CHOSEN.** 6DOF poses straight into the game's own camera and input. |
| B — InputAPI injection | **Rejected as the primary route.** InputAPI cannot move the mouse, only click and scroll, so head-look cannot go through it. It may still be useful later for keyboard actions that are awkward to reach in code. |
| C — external gesture app | **Rejected.** Delivers much less than a real port, and we have a viable in-process vector. |
| D — feed native head-tracking | **Not available.** Contagion has no TrackIR or FreeTrack support. |
| E — run an existing PCVR build | **Rejected deliberately, not overlooked.** See below. |

### On Path E and the separate "Contagion VR" title

The owner's Steam library also contains **`ContagionVR`** and
**`ContagionVROutbreakDemo`** **[verified]**. These are a *different product* —
a purpose-built VR title — not a VR mode of the game being ported.

Classic Contagion itself declares `"supportsvr" "0"` **[verified]** and exposes
no OpenVR or OpenXR surface, so there is nothing for OXRWXR + OpenComposite to
attach to. Path E does not apply to this target, and the owner's stated goal is
the classic flat game in VR.

Recorded so a later session does not propose "just run the VR version" as a
shortcut: it is a different game.

### Injection vector: `version.dll` proxy

`version.dll` next to `contagion.exe`, forwarding every export to a copy of the
real system DLL renamed `version_o.dll`, with `WINEDLLOVERRIDES=version=n,b`.
This is what the Meta AI attempt chose **[code]** and it is the right call —
`../general_wxr/WINLATORXR_DESC.md` §6 names `version.dll` as the workhorse
vector and cites Contagion specifically as the port that uses it.

Note the placement detail this game's unusual layout creates: `contagion.exe`
sits at the **install root** while the engine DLLs are in `engine/`
**[verified]**. The proxy goes next to the EXE, at the root.

**Angelscript is not an alternative vector.** It looked promising, but
`contagion/default_plugins.json` documents it as *"Angelscript **server**
plugin(s) ... to enable on your server"* **[verified]**. It is a server-side
plugin API with no client camera access, so it cannot do VR. Recorded so this
is not re-investigated.

---

## 4. The architecture, in one diagram

```
  Quest 3  ──  WinlatorXR (Android, arm64)
                 │  Box86 → Wine → container (virtual display)
                 │
                 ├── UDP 7872  poses, buttons, sticks, IPD, FOV, HMD_SYNC ───┐
                 │                                                           │
                 │   ┌───────────────────────────────────────────────────┐   │
                 │   │  contagion.exe (32-bit, D3D9)                     │   │
                 │   │    version.dll  (our proxy → version_o.dll)       │◄──┘
                 │   │      ├── XrApi client thread  (parse, normalise)  │
                 │   │      ├── OverrideView hook    visual head pose    │
                 │   │      ├── CreateMove hook      logical aim + input │
                 │   │      ├── D3D9 Present hook    SYNC PIXEL, overlay │
                 │   │      └── window enforcement   borderless, filling │
                 │   └───────────────────────────────────────────────────┘
                 │                                                           │
                 ◄── UDP 7278  haptics, MODE_VR, MODE_3D, FOV ────────────────┘
                 │
                 └── window capture ──► reads red pixel at (0,0) ──► compositor
```

Three channels, all three mandatory. The third — the red sync pixel drawn into
the game's own output — is the one that is easiest to miss and impossible to do
without; see `../general_wxr/XRAPI_V0_4_DESC.md` §5.

### Protocol version: 0.5

**[spec]** `../general_wxr/XRAPI_UPGRADE.md` §1: 0.5 is 0.4's packet plus the
InputAPI listener at no cost, so ask for 0.5 unless there is a reason not to.
The Meta AI attempt writes `0.4` **[code]**; we will write `0.5`, which keeps
the InputAPI escape hatch available for Path B actions without changing a line
of the parser.

0.6 and 0.7 are **not a roadmap**. Being on 0.5 is not a problem to fix.

### Presentation modes used

| Game state | `MODE_VR` | `MODE_3D` | Why |
|---|---|---|---|
| In a round | 1 | 0 | Mono VR with 6DOF head tracking |
| Menus, pause, loading | **0** | 0 | Stops the pose stream we are not using, returns the runtime's own mouse so flat menus are clickable with no input work, and drops out of immersive presentation so the whole menu is readable at once |

**[spec]** The menu choice comes from `../general_wxr/XRAPI_V0_4_DESC.md` §4,
where it is recorded as device-verified on a Quest 3 in the Interstellar Marines
port. It brings two obligations: keep sending the outbound packet every frame or
the stream cannot be turned back on, and hold the last good pose across the gap
when returning to 1.

The Meta AI attempt reaches the same conclusion by a different route
**[code]** — `GetDesiredVrMode()` returns 0 when the pause menu is open — so
this part of its design carries forward intact.

---

## 5. Design commitments specific to this game

Beyond the non-negotiables in `../general_wxr/CLAUDE.md`, which all apply:

1. **Lean is visual only.** The head's positional offset moves
   `CViewSetup::origin` and nothing else. The player's collision hull is never
   touched. This keeps wall-peeking impossible, which matters for the
   multiplayer milestone and costs nothing now. The Meta AI attempt got this
   right and commented it **[code]**; keep both the behaviour and the comment.
2. **Lean is clamped**, to roughly 15 cm. Source units are inches, so 1 metre
   is 39.37 units and 6 units is about 15 cm — the inherited constant is sound
   **[code]**.
3. **One aim vector, from the head.** `../general_wxr/TIPS.md` warns that
   per-hand camera yaw makes the view jerk on every off-hand action. The head
   drives the view; only the shot may vary.
4. **Logical aim and visual camera are separate concerns.** `CreateMove` owns
   yaw and pitch, because that is what the game and the server act on.
   `OverrideView` owns the full visual orientation including **roll**, plus the
   positional lean. Head tilt should be visible without being sent as aim.
5. **Mono now, so the sync pixel's blue channel stays 0.** It only carries
   meaning under AER.
6. **No wrappers, no renderer rewrite, no 64-bit.** The game's D3D9 already
   works under Box86 and Wine. Resolution is a container setting, not code.
7. **The container is 1024x768**, and that choice inverts two of the usual
   assumptions — see below.
8. **`cpu_level_potato` and `gpu_level_potato` are a gift.** The game ships
   quality presets aimed well below its minimum spec **[verified]**. Reach for
   those before writing a single line of performance code — and verify they
   survive a relaunch, because games of this era silently restore graphics
   settings.

### The container: 1024x768

Owner's decision, 2026-10-31, for **performance and simplicity**. The owner
reports the unmodified game already runs flat in WinlatorXR at this size, so
the cheapest risk in the project — "does it launch at all?" — is closed.

It is a deliberate departure from the usual advice, and two consequences are
worth stating plainly so nobody later reads them as bugs.

**1. There is no free anti-aliasing here, and that is accepted.**
`../general_wxr/WINLATORXR_DESC.md` §2 recommends rendering *above* panel
resolution so the XR layer's downsample filters into anti-aliasing. A Quest 3
panel is roughly 2064x2208 per eye, so at 1024x768 the runtime is **upscaling**,
not downsampling. The image will be soft, and no amount of in-game AA will fix
that — in-game AA costs fill rate and then gets resampled away anyway, so it
stays off per rule 8 above.

This is the right trade for the first build. Getting to a working, measurable
binary matters more than image quality, and raising the container later is a
WinlatorXR setting with no code change. Treat "the image is soft" as expected
at this size, not as a defect to chase.

**2. HUD scaling inverts.** The usual problem is a HUD blitted at fixed pixel
sizes covering *less* of the view as the container grows. At 1024x768 the
opposite applies: 2D art covers **more** of the view than it would on a desktop
monitor, so the HUD may be too large and may sit too far into the periphery to
read comfortably in a headset. Any HUD work is therefore likely to be
*shrinking* content, not enlarging it — the reverse of what
`../general_wxr/WINLATORXR_DESC.md` §2 describes and of what the sibling
Interstellar Marines port needed. Budget for it at milestone 10, and do not
copy that port's direction of travel.

Two smaller notes: 1024x768 is 4:3, which is **closer to square** than 16:9 and
therefore a better fit for a headset panel than a widescreen mode would be; and
the 10x10 sync square is a larger fraction of the frame at this size, which is
harmless and if anything more robust.

---

## 6. What "done" looks like, and what it explicitly is not

**Done, for a first shippable build:**

- Head tracking that feels locked, not swimmy, in a local round on a Quest 3
- Borderless window filling the container display
- A correct sync pixel
- Movement, look, shoot, reload, use, crouch, sprint and jump on the Touch
  controllers, with the mapping confirmed by the owner
- Menus usable without taking the headset off
- A recentre binding
- A log file whose first line is the build stamp

**Not in the first build**, recorded so their absence is not read as an
oversight:

- Stereo or AER
- Hand or weapon models placed in the world
- The voice wheel — designed at length by Meta AI, but it is polish and sits
  behind every item above
- Public-server multiplayer
- Zombie/human profile switching
- Haptics beyond the plumbing being correct

---

## 7. The two failure modes to watch for in ourselves

From `../general_wxr/FIRST_STEPS.md`, and both are already visible in the
inherited work:

1. **Tuning numbers instead of fixing the frame they are measured in.** The
   inherited lean maths adds a room-space offset to a world-space origin
   without rotating it (`BUG_FIXES.md` #3). No amount of adjusting the clamp
   fixes that; the frame is wrong.
2. **Reasoning where a log line would answer it.** On a standalone headset
   there is no debugger and every iteration costs a deploy. The inherited code
   writes **no log at all** **[code]**, which is why none of its 60–90 %
   completion claims could be checked. A log file is milestone 2, before
   anything else.
