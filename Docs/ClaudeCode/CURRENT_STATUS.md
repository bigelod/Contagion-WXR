# CURRENT_STATUS.md — where the port stands, 2026-10-31

An honest assessment of the repository as it actually is, not as the inherited
documents describe it. Written after reading all of `../general_wxr/`, all of
`../MetaAI/` including the complete source of all three VS2022 variants, and
the owner's own Contagion install.

See [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) for the goal,
[PROJECT_PLAN.md](PROJECT_PLAN.md) for the ordered work, and
[BUG_FIXES.md](BUG_FIXES.md) for the numbered defect register this document
refers to.

---

## The one-line version

**Nothing builds and nothing runs.** The repository is documentation only. The
inherited code is a non-compiling sketch whose design is largely sound and whose
numbers come from the wrong engine branch.

---

## 1. What is in the repository

```
Contagion-WXR/
  LICENSE              MIT
  README.md            three sentences and credits
  .gitignore           modified, uncommitted: adds Release/
  Docs/                untracked — nothing in Docs/ is committed yet
    general_wxr/       the game-agnostic porting framework, 9 files
    MetaAI/            the first attempt: 5 documents + 3 project folders
    ClaudeCode/        this folder
```

**There is no mod source at the repository root.** No solution, no project
file, no build script. Git reports two commits (`f29b7fc`, `9b6e4dd`), a
modified `.gitignore` and an untracked `Docs/`.

### The three Meta AI project folders are one codebase

`Docs/MetaAI/MetaAITestProjects/` contains:

- `ContagionVR_VS2022_Full/`
- `ContagionVR_VS2022_Bundled_NoDeps/`
- `Contagion_XrAPI04_VoiceWheel_FrameID/`

A recursive diff of their `src/` trees shows they are **byte-identical except
that `Bundled_NoDeps` adds `minhook.cpp`** — a hand-rolled 5-byte-JMP
trampoline so the project builds without the MinHook SDK.

They are not three attempts or three stages. They are one codebase packaged
three ways, and there is one thing to review, not three.

---

## 2. It does not compile

This is the headline finding, and it reframes everything the inherited
documents claim. Four blocking errors, all confirmed by reading the code and
the project file:

| # | Error | Evidence |
|---|---|---|
| 1 | **`CUserCmd` is missing six members the code uses.** The struct is declared as `char pad0[12]; QAngle viewangles;` and nothing else, but the code reads and writes `cmd->buttons`, `forwardmove`, `sidemove`, `mousedx`, `mousedy` and `impulse` | [src/hooks.h:3](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/hooks.h#L3) versus `input.cpp` and `hooks.cpp` |
| 2 | **Duplicate symbols.** `d3d9_hook.cpp` and `d3d9_hook_1.cpp` are *both* in the `ClCompile` list and both define `D3D9Hook_Init` and `D3D9Hook_Shutdown` | `ContagionVR.vcxproj` lists both; both files define both functions |
| 3 | **A function redeclares its own parameter.** `void Xr_SetHaptics(float l, float r)` immediately declares `std::lock_guard<std::mutex> l(g_hapMtx)` in the same scope | [src/xrapi.cpp:154](../MetaAI/MetaAITestProjects/ContagionVR_VS2022_Bundled_NoDeps/src/xrapi.cpp#L154) |
| 4 | **Missing include.** `xrapi.cpp` uses `std::mutex` and `std::lock_guard` without including `<mutex>` — it compiles only if a transitive include happens to supply it | `grep` for `#include <mutex>` finds `math.cpp` and `minhook.cpp`, not `xrapi.cpp` |

It also needs the **DirectX SDK June 2010** for `d3dx9.lib`, used only for
`ID3DXLine` and `ID3DXFont` in the voice wheel — a large dependency for a
feature that is deferred to milestone 10.

### What that means for the inherited status reports

`../MetaAI/CURRENT_PROGRESS.md` reports eight subsystems at 60–90 % complete.
`../MetaAI/BUG_FIXES.md` lists thirteen fixes in the past tense.

Since the project has never produced a binary, **no line of that code has ever
executed.** The percentages describe how much was written, not how much works,
and several of the "fixed" entries describe the runtime behaviour of code that
has never run. The documents are a useful record of *intent*. They are not
evidence about *behaviour*, and they should not be cited as such.

---

## 3. Subsystem by subsystem

Assessed against what the code actually does, with the inherited claim beside
it for comparison.

| Subsystem | Inherited claim | Actual state |
|---|---|---|
| Build system | 90 % | **Does not build.** Four blocking errors; duplicate TU in the project file |
| XrApi 0.4 networking | 90 % | **Written, never run.** Structure is reasonable. Missing: quaternion normalisation, the `vr` handshake file, any read of `system`, a desktop fallback path, and socket draining. Writes version `0.4`; we want `0.5` |
| `OverrideView` visual 6DOF | 80 % | **Written, never run, and the maths is wrong.** Lean is applied in the wrong frame (`BUG_FIXES.md` #3). Visual orientation and roll are never set at all |
| `CreateMove` logical aim | 75 % | **Written, never run, and the maths is wrong.** The Euler extraction caps yaw at ±90° (`BUG_FIXES.md` #2) — the player could never turn around |
| Input blocking and remapping | 70 % | **Written, never run.** The mapping table is good and is the most valuable inherited artifact. Turning is structurally broken (`BUG_FIXES.md` #6). `IN_*` values and console command names are guesses |
| Engine / cvar interface access | implied working | **Fabricated.** `IVEngineClient`, `ICvar` and `IGameUI` vtable layouts are invented; the calls would dispatch to arbitrary functions (`BUG_FIXES.md` #5) |
| GameUI pause detection | 65 % | **Written, never run**, and resting on the fabricated interfaces above. The *design* — `MODE_VR = 0` in menus — is right and matches the general guidance |
| Sync pixel ("FrameID square") | 85 % | **Written, never run, and wrong in three ways:** 16x16 rather than 10x10, drawn in `EndScene` rather than last before `Present`, and it fabricates sync values from `GetTickCount()` when no packet has arrived (`BUG_FIXES.md` #4, #8) |
| Voice wheel | 60 % | **Written, never run.** Deferred to milestone 10. Pulls in the whole DirectX SDK dependency |
| **Borderless window** | not mentioned | **Does not exist.** Nothing in the code touches window style or size. This is a `../general_wxr/CLAUDE.md` non-negotiable and a prerequisite for the sync pixel (`BUG_FIXES.md` #7) |
| **Log file** | not mentioned | **Does not exist.** No logging of any kind, which is why none of the above could be checked |
| **Build stamp** | not mentioned | **Does not exist.** No `__DATE__ __TIME__`, no freshness guard |
| **Recentre binding** | not started | Correct — not started. Recentre is currently captured on the first packet ever received, which happens before the player is in a map |
| Haptics | parsing only | Plumbing exists and decays correctly. Nothing ever calls `Xr_SetHaptics`, and the function does not compile |

---

## 4. Where the inherited documents disagree with reality

For the record, so these are not re-cited:

| Claim | Where | Reality |
|---|---|---|
| "32-bit Source SDK 2007" | `MetaAI/PROJECT_PLAN.md` §1 | A modified **Left 4 Dead 2** branch fork, with Jolt, Wwise, CEF and Angelscript. See `PROJECT_OVERVIEW.md` §2 |
| `g_pClientMode = 0x107B5F98`, vtable 14 / 21 | `MetaAI/PROJECT_PLAN.md` §2, `BUG_FIXES.md` | Derived from the SDK2007 premise. The IDA work confirmed a *pattern match*, not that the pattern means what was assumed. All **[unverified]** |
| `GameUI011`, `VEngineClient014`, `VEngineCvar004` | `gameui.cpp`, `input.cpp` | Interface names and versions guessed from SDK2007. Must be enumerated from the shipped DLLs |
| "7872 fallback 7873" | `README.txt`, `xrapi.cpp` | They are **duplicates**, not a fallback — from 0.4 the runtime sends the same packet to both. Binding one is correct, so the behaviour is right and only the comment is wrong |
| "FrameID square ... drawn after `BeginScene` before HUD to survive" | `MetaAI/BUG_FIXES.md` | Backwards. It must be the **last** thing drawn, after the HUD, or the HUD overwrites it |
| Monocular "~2x cheaper than stereo" | `MetaAI/PROJECT_PLAN.md` §1 | Right conclusion, loose arithmetic: mono is 1x scene cost where SBS is 2x, so stereo is twice as expensive, not mono half-price. Immaterial to the decision |
| `-insecure` disables VAC | `MetaAI/PROJECT_PLAN.md` §7 | **[unverified]**, and only relevant once multiplayer is in scope (milestone 11) |

---

## 5. What is genuinely good in the inherited work

Worth saying plainly, because the defect list above is long and the design is
not the problem:

- **The architecture is right.** `version.dll` proxy, in-process, 32-bit, the
  game's own D3D9 renderer, mono, `MODE_VR = 0` for menus. Every one of those
  matches `../general_wxr/` independently.
- **The visual-only lean rule** with the anti-wall-peek rationale, and the
  comment explaining it.
- **The input mapping table.** The owner's own design, thought through per hand
  with a modifier scheme. It is the single most valuable artifact to carry
  forward.
- **The module decomposition** — `xrapi` / `math` / `input` / `hooks` /
  `gameui` / `wndproc` / `d3d9_hook` / `pattern` — is a sensible split and is
  being kept.
- **The units constant** (39.37 units per metre) and the ~15 cm lean clamp are
  both sound.
- **The positional axis mapping** `{-z, -x, y}` from OpenXR to Source is
  correct: Source is +X forward, +Y left, +Z up; OpenXR is +X right, +Y up, −Z
  forward.
- **The bool-string indices** for the buttons it binds agree exactly with
  `../general_wxr/XRAPI_V0_4_DESC.md` §3 — grip 0, menu 1, stick-click 2,
  trigger 7, X 8, Y 9, A 10, B 11, right grip 12, right stick-click 13, right
  trigger 18. The thumb-direction indices it adds (3–6, 14–17) are guesses and
  need a device log.

---

## 6. The blocker, and the next action

**Blocker: there is no build and no log, so nothing can be measured.**

Every remaining question — are the hooks on the right functions, is the pose
maths right, does the sync pixel reach the runtime, does the proxy even load
under Box86 — is unanswerable until a binary loads into Contagion and writes a
line to a file. On a standalone headset with no debugger, the log *is* the
debugger.

**Next action, in order:**

1. ~~**Milestone 1** — unmodified Contagion runs in a container.~~ **Done**:
   the owner reports it runs flat in WinlatorXR at **1024x768** (2026-10-31).
   Three cheap sub-questions remain for the next launch — workarounds used,
   whether the `potato` presets survive a relaunch, and whether CEF and Wwise
   behave. None of them block milestone 2.
2. **Milestone 2** — a fresh `version.dll` that does nothing but write one log
   line with a build stamp, plus the build-and-deploy script with its freshness
   guard. This answers "does the proxy load under Box86 and Wine?", which is
   the cheapest high-value unknown in the project.
3. **Then** derive the real offsets, with the log to confirm them, before
   writing through any pointer.

Milestones 3 onward are in [PROJECT_PLAN.md](PROJECT_PLAN.md) §5.

---

## 7. Open questions for the owner

Carried from `PROJECT_PLAN.md` §1, collected here as the live ask list:

- **How long is the deploy-and-test loop** on the Quest 3? It decides how much
  to batch per iteration.
- **Has anyone in the Contagion modding community documented the client
  camera?** One forum question could save days of the project's largest risk.
- **The exact game build number**, so offsets can be recorded against a version.
- **What workarounds did the flat container launch need?** Each is something an
  installer or launcher has to automate later.
- **Do the `potato` quality presets survive a relaunch**, and do CEF and Wwise
  behave under Wine? Worth checking on the next launch, before any mod code
  exists to blame.

**Answered since this document was opened:**

- ~~Which container resolution?~~ **1024x768**, for performance and simplicity
  (2026-10-31). Its two non-obvious consequences — no free anti-aliasing, and
  HUD scaling inverting — are recorded in `PROJECT_OVERVIEW.md` §5.
- ~~Does the unmodified game run in a container?~~ **Yes**, owner-reported.
