# AUDIT.md — the two prior attempts, assessed

**2026-11-17.** What stands, what I confirmed independently, what I had to
correct, and what both prior passes missed. Written to be read *after*
`BINARY_RECON.md`, because several verdicts below depend on it.

---

## 1. Meta AI — design intent, wrong engine

`Docs/MetaAI/` is five documents and one codebase packaged three ways. The
documents describe eight subsystems at 60–90 % complete; the code has never
compiled, so nothing in them describes observed behaviour. That verdict is
ClaudeCode's and it holds — I re-read the source and confirm the four blocking
compile errors.

### Worth keeping

Judged on design rather than on arithmetic, this is good work:

- **The architecture.** `version.dll` proxy, in-process, 32-bit, the game's own
  D3D9, mono, `MODE_VR = 0` in menus. Every one of those independently matches
  `Docs/general_wxr/`.
- **The input mapping.** Conceived per hand, with a grip modifier and a
  tap/hold distinction. It is the most valuable artifact in the folder, and the
  facts in `BINARY_RECON.md` §7 make it *implementable* rather than speculative.
- **Visual-only lean**, with the comment explaining why. Correct, and now a hard
  rule.
- **The module split.** `xrapi` / `math` / `input` / `hooks` / `gameui` /
  `wndproc` / `d3d9_hook` / `pattern` is a sensible decomposition and should be
  kept.
- **The OpenXR→Source axis mapping `{-z, -x, y}`.** Correct: Source is +X
  forward, +Y left, +Z up.
- **39.37 units per metre** and the ~15 cm lean clamp as an intent.

### Wrong, and now provably wrong

| Claim | Where | Now |
|---|---|---|
| Contagion is "32-bit Source SDK 2007" | `PROJECT_PLAN.md` §1 | L4D2-lineage fork, confirmed three ways (`BINARY_RECON.md` §5) |
| `g_pClientMode = 0x107B5F98`; `OverrideView` at vtable 14, `CreateMove` at 21 | `PROJECT_PLAN.md` §2, `BUG_FIXES.md` | `0x107B5F98` is **`g_pViewRender`**, proven by an error string; the index-14 and index-21 calls through it and through `0x1089D0CC` are **nullary**, so neither can be those methods (`BINARY_RECON.md` §8) |
| `0x1089D0CC` is a "false positive" | `BUG_FIXES.md` | Backwards. It is the object called at index 21 six times; the `0x38` pattern found the view renderer. Neither is `g_pClientMode`, but the one dismissed was not the worse |
| `VEngineClient014`, `VEngineCvar004` | `input.cpp`, `gameui.cpp` | Do not exist. `VEngineClient013` and `VEngineCvar007` do |
| `GameUI011` is a guess | implied | It exists, exactly once in `GameUI.dll`. The `010`/`009` fallbacks do not |
| "7872 fallback 7873" | `README.txt`, `xrapi.cpp` | Duplicates, not a fallback. Behaviour right, comment wrong |
| Sync square "drawn after `BeginScene` before HUD to survive" | `BUG_FIXES.md` | Backwards — it must be last |
| Sync value invented from `GetTickCount()` when no packet has arrived | `d3d9_hook_1.cpp:96` | Harmful: it pollutes the runtime's calibration table |
| `-insecure` disables VAC | `PROJECT_PLAN.md` §7 | Still unverified, and irrelevant until multiplayer |
| Window class `Valve001` | `wndproc.cpp` | Present in **no** shipped module. The lookup always fails and falls back to `GetForegroundWindow()` |

Two things about that list deserve comment, because they are the difference
between an error and a *habit*.

**The IDA work was real and the conclusion was still wrong.** A pattern was
found, a screenshot was taken, and the address was recorded as confirmed. What
the screenshot confirmed was that the byte pattern occurs. It could not have
confirmed which object the pattern's global pointed at, and in fact the first
match is a *different* global from the documented one
(`BINARY_RECON.md` §8.1). This is the general lesson from `TIPS.md` — *measure,
do not infer* — applied to static analysis as well as to screenshots.

**The synthesis of a confident past tense over unexecuted code** is the second
habit. Thirteen entries in `BUG_FIXES.md` describe runtime behaviour of code that
has never run. Several are plausible; none is evidence. The documents are useful
as a record of intent and should be read that way.

---

## 2. ClaudeCode — a good audit, with three claims I have to correct

The ClaudeCode set is a genuinely careful reading of the inherited source. Of
its 22 register entries I re-verified the following directly in the code and
they are correct as stated:

#1a `CUserCmd` is 3 members and the code uses 9; #1b duplicate `d3d9_hook*.cpp`
in `ClCompile`; #1c `Xr_SetHaptics` shadows its own parameter with a
`lock_guard`; #1d missing `<mutex>`; #2 yaw from `asin`; #3 lean not rotated into
world space; #4 sync square size/timing/state; #5 fabricated `IVEngineClient` /
`ICvar` / `ConVar` vtables; #6 stick turn overwritten every tick plus a
hardcoded `1/60`; #7 no window handling at all; #8 fabricated sync values;
#10a `IN_*` values from SDK2007; #10c `cmd->buttons = buttons` assigns rather
than ORs; #10d class detection is a stub; #12 handshake incomplete; #13 no
quaternion normalisation; #14 no socket drain; #15 recentre on the first packet
ever, no binding; #16 `setup->angles` never set; #18 bundled 5-byte-JMP MinHook;
#19 dummy device failure is silent; #20 the small items; #21 and #22 correctly
withdrawn or promoted to rules.

That is a high hit rate and it deserves saying plainly, because what follows is
the short list.

### Correction 1 — the FOV echo is not masked (register #9)

ClaudeCode #9 says the echoed inbound FOV is "currently masked by accident"
because `if(fH<10) fH=104.5f;` catches every tangent, so the code always sends
`104.5`.

The one sample packet in `Docs/general_wxr/SixDOFinatorCalibration/SamplePoseData.txt`
carries `99.00 103.40` in those two fields. Both are greater than 10, so the
guard does not fire and the mod sends the runtime's own numbers back as its
outbound FOV.

The *conclusion* — do not echo, send what you actually rendered — is unchanged
and is right. The *stated mechanism* is wrong, and it matters, because someone
"fixing the guard" was predicted to break something that was never being saved
by it. See `DEFECTS.md` D15.

### Correction 2 — `GameUI011` exists; the engine and cvar versions do not

ClaudeCode #11 lists `GameUI011`, `VEngineClient014` and `VEngineCvar004`
together as guesses to be enumerated. Enumerated (`BINARY_RECON.md` §4):

- `GameUI011` — **exists**, once, in `GameUI.dll`. The inherited value is right.
- `VEngineClient014` — **does not exist**. `013` does.
- `VEngineCvar004` — **does not exist**. `007` does.

So the inherited code is right where it looks least certain (GameUI) and wrong
where it looks most certain (the engine). It works only because both engine and
cvar lookups fall through to a second attempt.

### Correction 3 — the pattern-scan conclusion can be sharpened

ClaudeCode #11 says the IDA work "confirmed a pattern match, not that the pattern
means what the SDK2007 premise assumed", and lists `g_pClientMode = 0x107B5F98`
as **[unverified]**.

It is now verified, and it is false: `0x107B5F98` is `g_pViewRender`. The address
is real, the *identification* is wrong, and the code does not even resolve it
(the first of six matches is a different global). Detail in
`BINARY_RECON.md` §8.

### Where ClaudeCode is right and I would add weight

- **"The blocker is that nothing builds and nothing runs."** Correct, and the
  reason `PLAN.md` puts the log first.
- **`Present` over `EndScene`.** Correct. The D3D9 indices quoted (Present 17,
  Reset 16, EndScene 42) are standard and right.
- **Keep the shape, rewrite the internals.** Correct call, and the binary
  reconnaissance supports it: the decomposition is sound, almost every constant
  inside it is not.
- **The 1024x768 container decision and its two consequences** (no free
  anti-aliasing; 2D art covers *more* of the view). Correct and non-obvious.

### One thing I would soften

ClaudeCode's `BUG_FIXES.md` #17 defers the hand-pose-through-head-transform
issue as "arguable rather than clearly wrong". Agreed — and since hands are
milestone 8, nothing needs deciding now. But the 0.15 scale factor is worth
naming as what it is: a number with no frame of reference, which is exactly the
class of value `TIPS.md` says not to tune. When hands are picked up, start from
the gun's frame.

---

## 3. What both passes missed

These are not criticisms of care. They are things that were one command away.

**1. Nobody checked the import tables.** The `version.dll` vector is recommended
in three documents and cited as proven, and it is — but *nothing in the game
imports `version.dll`*. The load happens through
`engine.dll → crashhandler.dll → version.dll`. That chain is the thing to
protect, and its fragility (skip `crashhandler.dll`, no mod) was invisible
without it. `BINARY_RECON.md` §3.

**2. Nobody enumerated the exports.** Every relevant module exports `g_pCVar` as
a *global*. A whole fabricated interface lookup can be replaced by one
`GetProcAddress`. `BINARY_RECON.md` §4.

**3. Nobody read `config.cfg`.** ClaudeCode #10 says the console command names
"do not exist as far as anyone has checked" and prescribes extracting a VPK with
`Vpk.exe` to find `keybinds_setup`. In fact `contagion/cfg/config.cfg` is a
9 KB plain-text file present in every install, containing the complete bind list:
`phone_or_blend`, `+melee`, `weaponselect_next`, `weaponselect_prev`,
`dropammo`, `dropunusedammo`, `+voicecmds`. The VPK extraction was unnecessary and
the answer was two `find` commands — or one `Get-Content` — away.
`BINARY_RECON.md` §7.

**4. Nobody noticed the game has a full gamepad layer.** The `joy_*` block, the
`JOY1`–`JOY10` binds, `+jlook`, and `exec joy_setup` in `config_default.cfg` mean
the game already consumes analog sticks, buttons and look input through its own
code, with the player's own sensitivities. That is a materially different input
plan from synthesising `IN_*` bits, and it was not on either list.

**5. Nobody asked what the window's class name is.** `Valve001` is in no shipped
binary. The inherited window hook cannot find the game window on this game, and
its failure is silent.

**6. RTTI was never considered.** `client.dll` carries type descriptors for
`TerrorClientMode`, `ClientModeShared`, `CViewRender` and `CViewSetup`. That is a
name → vtable path that does not depend on any pattern guess. It was the obvious
route to the project's central unknown and neither pass took it.

---

## 4. Net position

| Question | Before this session | Now |
|---|---|---|
| Is the engine L4D2-derived? | **[web]** claim | **verified** three ways |
| Does the proxy have a load path? | assumed | **verified**, via `crashhandler.dll` |
| `g_pClientMode` address | **[unverified]**, probably right | **wrong object** — it is `g_pViewRender` |
| `OverrideView` / `CreateMove` indices | **[unverified]** 14 / 21 | **disproven** as stated; those calls are nullary |
| Engine / cvar interfaces | guesses | **wrong** — `013` / `007` |
| GameUI interface | guess | **right** — `GameUI011` |
| Console commands | placeholders | **known**, from `config.cfg` |
| Input mapping | owner's design, unimplementable as written | design kept, mechanism now known |
| Sync pixel spec | known from `Docs/general_wxr/` | unchanged, still unimplemented |
| Can anything be measured? | no | **partly** — first build exists, smoke-tested in a host process, deployed to the PC copy, not yet run in-game |

The last row is the one that had not moved, and it is the one that matters most.
Everything above is reading; `PLAN.md` exists to convert it into one log line.
**As of 2026-11-17 there is a build**: a `version.dll` that loads, writes a log
with a build stamp and a self-hash, and dumps the reconnaissance. It has been
smoke-tested in a 32-bit host process and deployed to the PC copy. The next
sentence in this file should be written after the first in-game launch.
