# BINARY_RECON.md — what is actually in the shipped binaries

**2026-11-17.** A read-only pass over
`E:\Steam\steamapps\common\Contagion`, done to stop re-deriving the same
questions from the wrong SDK. Every number here is for **this build** of the
game and nothing else.

This document is the base the rest of the folder depends on. Where it
contradicts `Docs/MetaAI/` or `Docs/ClaudeCode/`, this file wins, because this
file was measured.

---

## 1. Method, and what it can and cannot prove

**What was done, all of it read-only:**

- PE headers: machine type, image base, section table.
- Import directory of every `.exe` and `.dll` in the install (120 files).
- Export directory of the modules that matter.
- ASCII scan for interface version strings, RTTI type-descriptor names, error
  and diagnostic strings, embedded PDB paths.
- Byte-pattern counts for the exact patterns the inherited hook code uses.
- Decode of `mov ecx,[imm32]` reference sites to recover which global each
  virtual call is dispatched on, and with which vtable displacement.
- The game's own `cfg/config.cfg` and `gameinfo.txt`, which are plain text.

**What was not done:**

- **No disassembly.** Everything about instruction *shape* below is inferred
  from byte patterns and from unambiguous prologue idioms (`push esi; mov esi,
  ecx` at a function start, `ret`/`ret 8` before it, padding between). It is
  strong evidence, not a listing.
- **No runtime.** Nothing was launched. Every claim about a value *at run time*
  — what a global contains, what a vtable slot is — is left to `PLAN.md` §2.
- **No writes.** The install is untouched, as the project rules require.

Consequence, stated plainly: this document can say **what code exists and what
it refers to**. It cannot say **what a function does**. That is a disassembler's
job, and it is the one tool this session did not use.

---

## 2. The target, as measured

| Fact | Value |
|---|---|
| Install root | `E:\Steam\steamapps\common\Contagion` **[verified]** |
| `contagion.exe` | 454,144 bytes, PE machine `0x014c` (x86), **imports only `KERNEL32.dll` and `USER32.dll`** **[bin]** |
| Game architecture | `contagion.exe` is a tiny launcher: its strings are `Failed to load the launcher binary`, `%s\%s\launcher.dll`, `LauncherMain`. The real engine is `engine/launcher.dll` + `engine/engine.dll` **[bin]** |
| `engine/engine.dll` | 4,494,336 bytes, x86, imports 15 modules incl. `crashhandler.dll`, `tier0.dll`, `vstdlib.dll`, `steam_api.dll`, `WS2_32.dll` **[bin]** |
| `contagion/bin/client.dll` | 8,143,360 bytes, x86, image base `0x10000000` **[bin]** |
| `engine/GameUI.dll` | 2,761,728 bytes **[bin]** |
| Renderer | D3D9: `engine/shaderapidx9.dll` present; no `d3d9.dll` shim in the install **[verified]** |
| Engine branch | **Left 4 Dead 2 lineage, confirmed three ways** — see §5 **[bin]** |
| Original source path | `D:\Projects\Contagion\ContagionCode2012\source\Contagion\src\...` embedded in `engine.dll` as PDB/assert paths **[bin]** |
| `gameinfo.txt` | `"type" "multiplayer_only"`, `"supportsvr" "0"`, SteamAppId 238430 **[verified]** |

The embedded source path is worth pausing on. It is not source code and carries
no licence problem — it is a path string in a shipped binary — but it tells us
the tree layout: `src/public/tier0/tslist.h`, `src/public/tier1/UtlSortVector.h`,
`src/engine/`. That is a **standard Source tree layout**, which means naming
conventions from public Source will transfer even though the code will not.

It also gives the project a name for the engine fork: **ContagionCode2012**.

---

## 3. The injection vector — and why it works

`Docs/general_wxr/` recommends the `version.dll` proxy and cites Contagion. That
advice is right, but the *reason* matters and nobody had checked it:

**Nothing in the game imports `version.dll` directly.** Not `contagion.exe`
(two imports only), not `engine.dll`, not `client.dll`, not `GameUI.dll`.
`WINEDLLOVERRIDES` changes how a module is resolved; it does not cause a load.

The load happens one level down:

```
engine.dll  --imports-->  crashhandler.dll  --imports-->  version.dll
```

Modules in the install whose import/name tables mention `version.dll`
**[bin]**:

| Module | Note |
|---|---|
| `engine/crashhandler.dll` | imported by `engine.dll`, so this is the live chain |
| `engine/libcef.dll` | CEF; also loaded, but later and only for the browser UI |
| `engine/steamclient.dll` | Steam integration |
| `engine/unicode.dll`, `engine/unicows.dll`, `engine/trkvdbn.dll` | Unicode shims, likely unused here |

So the proxy loads because the DLL search order puts the **executable's**
directory first, and `version.dll` placed next to `contagion.exe` is found there
before Wine's builtin. Put it anywhere else and it will not load.

**Risk this creates, worth writing down before it bites:** if `crashhandler.dll`
is ever not loaded, or is loaded from another directory, or the game is launched
in a mode that skips it, the proxy silently never runs and no log appears. That
is indistinguishable from "the DLL is broken". `PLAN.md` §2 therefore logs the
loading module path, not just the fact of loading.

Two smaller notes:

- The inherited `DllMain` calls `LoadLibraryA("version_o.dll")` inside
  `DLL_PROCESS_ATTACH`. That runs under the loader lock. It normally works, and
  the export forwarders alone would resolve `version_o.dll` on first call
  anyway, so the `LoadLibraryA` is both risky and unnecessary. Move it into the
  init thread.
- `version_o.dll` must be a **32-bit** copy of the system `version.dll`
  (`C:\Windows\SysWOW64\version.dll`), not from `System32`.

---

## 4. Interfaces the shipped modules actually register

These are the version strings present in the binaries. The inherited code asks
for `VEngineClient014`, `VEngineCvar004` and `GameUI011`; **two of those three
do not exist.**

| Requested by inherited code | Present? | Actual string |
|---|---|---|
| `VEngineClient014` | **no** | **`VEngineClient013`** |
| `VEngineClient013` (its fallback) | yes | — |
| `VEngineCvar004` | **no** | **`VEngineCvar007`** |
| `VEngineCvar007` (its fallback) | yes | — |
| `GameUI011` | **yes**, exactly one occurrence in `GameUI.dll` | — |
| `GameUI010`, `GameUI009` (its fallbacks) | **no** | — |

So the inherited code works **only by falling through to its own fallbacks** for
the engine and cvar interfaces, and would return `nullptr` for GameUI if
`GameUI011` were wrong. It happens to be right.

Other registered strings that matter to this project **[bin]**, with run-time
confirmation from 2026-11-17 where available:

| String | Module(s) | Relevance | Resolved at run time? |
|---|---|---|---|
| `VClient016` | `client.dll` | `IClient` — `CreateMove` lives on this object | **yes, `0x66E267D8`** |
| `VEngineRenderView013` | **`engine.dll`**, not `client.dll` | `IViewRender` — see §8 | **null in client.dll**; probe engine.dll |
| `VEngineModel016` | all | model renderer | not probed |
| `VFileSystem018` | all | filesystem | not probed |
| `VEngineVGui001` | all | VGUI | not probed |
| `VGUI_Surface031`, `VGUI_Panel009`, `VGUI_Scheme010` | all | 2D drawing; `VGUI_Surface031` is a sane alternative to D3D9 for an overlay | not probed |
| `VPhysics031` | `client.dll`, `engine.dll` | Jolt-backed physics | not probed |
| `VStudioRender026` | all | studio models | not probed |
| `InputSystemVersion001` | `client.dll`, `GameUI.dll` | `IInputSystem` | not probed |
| `VClientPrediction001` | `client.dll` | prediction | not probed |
| `VEngineGameUIFuncs` variant: `VENGINE_GAMEUIFUNCS_VERSION005` | all | game-UI funcs (not `IGameUI`) | not probed |

**A correction from run 1:** `VEngineRenderView013` is registered by
**`engine.dll`**, not `client.dll`. Probing `client.dll` returned null, which was
an error in my probe list, not a missing interface
(`RUNTIME_CONFIRMATIONS.md` §3). `VEngineCvar007` likewise lives in
`vstdlib.dll` only.

### The exports are more useful than the interfaces

Every module that matters exports the same three things **[bin]**:

| Module | Exports that matter |
|---|---|
| `client.dll` | `CreateInterface`, **`cvar`**, **`g_pCVar`**, plus the whole Angelscript `as*` API |
| `engine.dll` | `CreateInterface`, `cvar`, **`g_pCVar`**, `?F@@YAXPAPAVIEngineAPI@@@Z` |
| `GameUI.dll` | `CreateInterface`, `cvar`, **`g_pCVar`** |
| `vstdlib.dll` | `CreateInterface`, `g_pCVar`, `VStdLib_GetICVarFactory`, `KeyValuesSystem` |
| `MaterialSystem.dll`, `shaderapidx9.dll` | `CreateInterface`, `cvar`, `g_pCVar` |

**`g_pCVar` is an exported global.** The fabricated `ICvar` lookup — loading a
module and guessing an interface version — can be replaced by
`GetProcAddress(GetModuleHandleA("vstdlib.dll"), "g_pCVar")` and a dereference.
One `FindVar` vtable index still has to be derived, but the *object* stops being
a guess. `PLAN.md` §4 records this.

The Angelscript exports in `client.dll` confirm the earlier finding that
Angelscript is present. It is still **server-side** and still not a VR vector.

---

## 5. The engine branch, confirmed three ways

The ClaudeCode documents concluded "L4D2 fork" from the Valve Developer Wiki.
Confirmed independently from the binaries **[bin]**:

1. **RTTI class names** in `client.dll`:
   `TerrorClientMode`, `FullscreenTerrorClientMode`, `TerrorViewport`,
   `C_TerrorPlayer`, `C_TerrorViewModel`, `C_TerrorViewRender`,
   `CTerrorRenderTargets`, `C_TerrorPlayerAnimState`. *Terror* is the L4D2
   prefix.
2. **A leftover string pair**: `eft4dead2..contagion...` in `engine.dll` — the
   branch identifier still in the string table.
3. **The game's own config**: `sv_skyname "sky_l4d_predawn02_hdr"` in
   `contagion/cfg/config.cfg`.

Also present in `client.dll` RTTI, and directly relevant:

| Class | Why it matters |
|---|---|
| `ClientModeShared`, `ClientModeCSNormal` | the client-mode hierarchy exists, SDK-shaped |
| `CViewSetup` | **the type has RTTI**, so it can be located and its layout checked |
| `CViewRender`, `IViewRender`, `CViewRenderBeams` | the view renderer, reachable as `VEngineRenderView013` |
| `CInput`, `CCSInput` | `CInput` exists in this build |
| `CPointCamera`, `CHLTVCamera` | other camera types, for context |
| `C_BasePlayer`, `C_BaseViewModel`, `C_PredictedViewModel` | the player/viewmodel types |

**This is the single most actionable fact in the document.** RTTI gives a
name → type-descriptor → class-hierarchy path to the real vtable of
`TerrorClientMode` / `CViewRender`, which is a *far* better anchor than a byte
pattern. It was not exploited fully here (no disassembler), but it is the route
`PLAN.md` §3 proposes.

---

## 6. The window class **is** `Valve001` — my first answer was wrong

**Corrected 2026-11-17 by run 1.** The window exists at run time:

```
class='Valve001' visible=1 client=1920x1080 style=0x14000000 ex=0x00000008 title='Contagion'
```

My original claim — that `Valve001` appears in none of the 120 shipped
binaries — came from an **8-bit string scan**. It is present in `engine.dll` as
**UTF-16**. The claim is withdrawn; see `RUNTIME_CONFIRMATIONS.md` §4.

What is still true and useful:

- The window has **no chrome already**: `style = 0x14000000` is
  `WS_VISIBLE | WS_CLIPSIBLINGS`, client area equals the screen. So the
  borderless enforcement is a container question, not a PC one.
- `GetForegroundWindow()` was **not** the game window at dump time, even on the
  PC (`MultitaskingViewFrame` held foreground). The inherited fallback is
  unreliable; enumerate by PID and pick by class, as the inherited code actually
  does.
- `D3DProxyWindow` — the Steam overlay's window — is present at full screen size.
  Relevant to the sync pixel at (0,0): the overlay draws into the same swap chain.

**Lesson, recorded because it has now happened twice:** a negative result from a
string scan is only as good as the encodings searched. An ASCII-only sweep
produced a confident wrong answer, and one log line overturned it. Static
findings that say "X does not exist" need a run-time check before they are acted
on.

---

## 7. Input: the real command names

The inherited code issues `phone`, `inv_next`, `inv_prev`, `drop`, `dropammo`,
`vocalize <Event>`, and treats melee as `IN_ATTACK2`. Both prior document sets
list these as unverified placeholders and say the answer needs the in-game
console.

**The answer is in `contagion/cfg/config.cfg` — 9 KB of plain text, present in
every install.** No console session needed. Verified binds **[verified]**:

| Key | Command | Inherited code used | Correct? |
|---|---|---|---|
| `B` | **`phone_or_blend`** | `phone` | **no** |
| `G` | `drop` | `drop` | yes |
| `H` | `dropammo` | `dropammo` | yes |
| `J` | `dropunusedammo` | — | unused |
| `V` | **`+melee`** | `IN_ATTACK2` | **no** |
| `F` | `impulse 100` | `impulse 100` | yes |
| `X` | **`+voicecmds`** | — | the voice-command menu already exists |
| `C` | `+voicerecord` | — | push-to-talk |
| `MWHEELUP` | **`weaponselect_prev`** | `inv_prev` | **no** |
| `MWHEELDOWN` | **`weaponselect_next`** | `inv_next` | **no** |
| `Q` | `weaponselect_last` | — | quick-switch |
| `1`–`4` | `weaponselect <n>` | — | direct slot select |
| `U` | `+unload` | — | |
| `TAB` | `+showscores` | — | |
| `F1` | `+showcontrols` | — | |
| `F3` | `askconnect_accept` | — | |
| `F5` | `jpeg` | — | screenshot |
| `ESCAPE` | `cancelselect` | ESC passed through | works, differently |
| `SHIFT` / `CTRL` / `SPACE` | `+speed` / `+duck` / `+jump` | `IN_*` bits | plausible |
| `E` / `R` | `+use` / `+reload` | `IN_*` bits | plausible |

Two consequences.

**The voice wheel is half-built already.** `+voicecmds` is a first-class
Contagion command bound to a key, so the wheel's job may reduce to driving an
existing menu rather than inventing a `vocalize <Event>` list. That is the
"prefer the engine's own code paths" rule from `TIPS.md`, applied to input.

**`weaponselect_next` / `_prev` are commands, not `IN_*` bits**, so they belong
in the same class as `phone_or_blend`: invoke the command, do not synthesise a
key press. `Docs/general_wxr/PC_INPUT.md` forbids `SendInput`; the in-process
equivalent is to call the game's own command dispatcher or write the state its
input code reads.

### The game already has a full gamepad layer

`config.cfg` contains an entire `joy_*` block — `joystick "1"`, `joy_enabled_new`,
`joy_button_preset`, `joy_movement_stick`, deadzones, per-weapon sensitivities,
`joy_inverty`, `joy_yawsensitivity`, and `bind "JOY1" … "JOY10"` plus
`POV_*`/`Z AXIS` bindings. `config_default.cfg` `exec`s **`joy_setup`** and
**`joy_splitscreen`**.

There is also `cmd1 +jlook` / `cmd2 +jlook`, and a `joy_setup.cfg` referenced but
not present as loose text (it lives in a VPK).

This is a significant unmapped option: **the game can already consume an analog
stick, buttons, and look input through its own gamepad code**, with the user's
own bindings and sensitivities. Feeding *that* is a different — and possibly
much smaller — job than synthesising `IN_*` bits or console commands. It must be
established whether WinlatorXR can deliver an XInput device into the container;
if it can, a large part of the input milestone may be configuration rather than
code. Recorded as a question, not a plan — see `PLAN.md` §5.

> Note: `config.cfg` in this install is the owner's personal config and contains
> a player name, a server name and an external IP. None of that is reproduced
> here and none of it belongs in the repository. Only convar and bind names are
> quoted.

---

## 8. `g_pClientMode` is not at `0x107B5F98` — it is `g_pViewRender`

This is the central finding of the session, and it changes the project's
shape.

### 8.1 The pattern is ambiguous, and the code picks the wrong match

The inherited `Hooks_Init` scans `client.dll` for
`8B 0D ?? ?? ?? ?? 8B 01 FF 50 38` and uses the **first** match, then reads the
global pointer out of the `mov ecx, [imm32]` operand.

That pattern occurs **six times** in `client.dll` **[bin]**, and they resolve to
only two globals:

| # | Site file offset | Site VA | Global resolved |
|---|---|---|---|
| 1 | `0x1A45F4` | `0x101A51F4` | **`0x106B1F88`** |
| 2 | `0x1DF67F` | `0x101E027F` | `0x107B5F98` |
| 3 | `0x229500` | `0x1022A100` | `0x107B5F98` |
| 4 | `0x280DC0` | `0x102819C0` | `0x107B5F98` |
| 5 | `0x285CD7` | `0x102868D7` | `0x107B5F98` |
| 6 | `0x287397` | `0x10287F97` | `0x107B5F98` |

The first match in file order is site 1 — which resolves **`0x106B1F88`**, not
`0x107B5F98`. So the inherited code, run against this exact binary, does not even
find the address the documents claim. The address in
`Docs/MetaAI/PROJECT_PLAN.md` is real, but the code does not reach it.

### 8.2 What `0x107B5F98` actually is

The global has exactly **one** store site, at VA `0x1018F8A6` **[bin]**:

```
call   GetViewRenderInstance        ; e8 ... ; mov ecx, eax
mov    [0x107B5F98], ecx
test   ecx, ecx
jnz    short ok
push   0x10598CD0                   ; "GetViewRenderInstance() must be implemented by game."
call   <import>                     ; error/Warning
```

Read the string:

> **`GetViewRenderInstance() must be implemented by game.`**

That is an assignment guard, and it names the function. `0x107B5F98` is
**`g_pViewRender`** — the `IViewRender` singleton, `VEngineRenderView013`. It is
not, and cannot be, `g_pClientMode`.

Reference counts for the three globals of interest **[bin]**:

| Global | `mov reg,[g]` reads | writes | vtable indices called through it |
|---|---|---|---|
| `0x107B5F98` | 68 | 1 (`= GetViewRenderInstance()`) | 1, 2, 3, 4, 5, 8, 10, 14, 22, 24 |
| `0x1089D0CC` | 288 | 1 (`mov [g], eax`) + 1 (`= 0`) | 2, 4, 6, 13, 19, 21, 27 |
| `0x106B1F88` | 232 (plus many `mov eax,[g]`) | none found | 1, 2, 4, 8, 11, 12, 13, 14, 19 |

### 8.3 The inherited hook targets cannot be what they claim

`hkOverrideView(void* ecx, void* edx, CViewSetup* setup)` reads `setup` from the
stack. But **every** site that calls through `0x107B5F98` at index 14 (0x38)
pushes **no arguments** — and two of them sit at a function prologue, right
after the previous function's `ret` and padding, so there is nothing earlier
that could have pushed one. At one site the return value is tested as a bool:

```
8B 01          mov  eax,[ecx]
8B 40 38       mov  eax,[eax+0x38]
FF D0          call eax
84 C0          test al,al          <-- index 14 returns a bool
75 0B          jnz  ...
```

So index 14 of `IViewRender` is a **nullary, bool-returning** method. It cannot
be `OverrideView(CViewSetup*)`, which takes a stack argument.

The same is true of the other anchor. `8B 0D ?? ?? ?? ?? 8B 01 FF 50 54`
(vtable index 21) occurs **eight** times **[bin]**, resolving to `0x1089D0CC`
(six), `0x1073661C` (one) and `0x1089D0D0` (one). Every one of the six sites on
`0x1089D0CC` also pushes no arguments, and one begins at a function prologue.
So index 21 there is nullary too, and cannot be
`CreateMove(float, CUserCmd*)`.

And decisively: **no object in the binary is called at both index 14 and index
21**. If both were `IClientMode` methods, one object would have to show both.

Finally, the displacement alone is worthless as a discriminator: `call [eax+0x38]`
occurs **137 times** and `call [eax+0x54]` **422 times** in `client.dll`, on many
different objects. A pattern that fixes only the displacement identifies nothing.

### 8.4 What this means for the inherited code

If those hooks had ever been installed, `hkOverrideView` would take whatever
value happens to be at `[esp+8]` at a nullary thiscall and use it as a
`CViewSetup*`, then write three floats through it. `hkCreateMove` would pass that
same garbage pair of registers to the original function as `(float, CUserCmd*)`
*before* doing anything else. The realistic outcome is an access violation, not
a subtle misbehaviour.

This also reframes the Meta AI log entry that called `0x1089D0CC` a "false
positive": **it is the reverse.** The `0x54` search found an object that at least
is called at index 21 six times; the `0x38` search found `IViewRender`. Neither
is `g_pClientMode`, but the one dismissed was not the worse of the two.

### 8.5 Where the camera hook should go instead

`g_pViewRender` is *verified*, and it is arguably a **better** target than
`g_pClientMode` for a visual-only camera:

- It is the object whose job is the view. `IViewRender` exposes `RenderView`,
  `CalcView`, `SetupView`-equivalents, all of which take a `CViewSetup*`.
- It can be obtained **without any pattern scan at all** — either from the
  verified global `0x107B5F98`, or from
  `CreateInterface("VEngineRenderView013")` in `client.dll`.
- The argument-taking methods are simply not called through the
  `mov ecx,[g]; call [eax+N]` shape scanned above — they are reached through a
  cached register. That is why every site found is a nullary getter. It is not
  evidence that the camera methods do not exist.

`PLAN.md` §3 turns this into one deploy: dump `g_pViewRender`'s vtable, hook
every entry, and log which entry receives a pointer whose first two ints are the
container width and height. That identifies the `CViewSetup` writer
behaviourally, with no disassembly and no guessing.

For `CreateMove`, the parallel verified anchor is **`VClient016`**: in this
engine family `CHLClient::CreateMove(int, float, bool)` is the engine-facing
entry point, and the `CUserCmd*` is obtainable from `CInput`/`IInput`. That is a
different design from the inherited "hook a vtable slot on a client-mode object"
and it should be evaluated before writing one line of hook code.

### 8.6 Confirmed at run time, 2026-11-17 — all three objects are named

Run 1 dumped each anchor's RTTI locator; resolving those against the DLLs on
disk gives the class names. Full detail in `RUNTIME_CONFIRMATIONS.md` §2.

| Global | Class |
|---|---|
| `0x107B5F98` | `CTerrorViewRender` → `CCSViewRender` → `CViewRender` → `IViewRender` |
| `0x1089D0CC` | `CInputWin32` → `vgui::IInputInternal` → `vgui::IInput` |
| `0x106B1F88` | `CWEngineSound` → `IWEngineSound` |

So §8.2's string-based identification is confirmed, §8.3's "not a client mode"
is confirmed and the object is VGUI input, and the first pattern match is the
sound engine. `CCSViewRender` is a **Contagion-specific subclass** between
`CViewRender` and `CTerrorViewRender`, which is a warning that the view
renderer's vtable is not the stock SDK2007 layout even by L4D2 standards.

The module was **relocated** (`client.dll` at `0x666F0000`), so every absolute VA
in this document is only usable as `runtimeBase + (va - 0x10000000)`. That is
what the recon does; a literal dereference would read somebody else's memory.

---

## 9. The sync pixel, and the sample packet that contradicts the spec

`Docs/general_wxr/SixDOFinatorCalibration/SamplePoseData.txt` contains one
sample packet. Parsed against the documented layout **[spec]**:

| Field | Value |
|---|---|
| left quat / stick / pos | `0.210 0.290 -0.930 0.040` / `0.0 0.0` / `-0.0358 -0.229 -0.298` |
| right quat / stick / pos | `0.100 -0.300 0.950 -0.080` / `0.0 0.0` / `0.3346 -0.240 -0.2789` |
| HMD quat / pos | `-0.020 -0.120 0.010 0.990` / **`0.037 0.006 -0.017`** |
| IPD | `0.0678` |
| FOV x / y | **`99.00 103.40`** |
| frame id | `176` |
| bool string | **21 chars**, all `F` |
| HMD name | `META` |

Three things follow.

**1. The FOV fields in this packet are degrees, not tangents.** The spec's float
table says floats 26–27 are "tangent of the half-angle". `tan(θ/2) = 99` implies
a full FOV of about 179°, which is not a real headset request. `99.00` and
`103.40` are degrees. Either the spec's wording is wrong, or this sample was
hand-authored for a calibration tool.

Sibling ports disagree with each other, which is the strongest signal that the
doc's wording is at fault:

| Port | Protocol | Treats floats 26–27 as |
|---|---|---|
| Interstellar-Marines | 0.4 | **tangents** — `2·atan(f)` to degrees, used only as a fallback default |
| XOPS | 0.4 | parses them, **logs them, never uses them** |
| Mirror's Edge | **0.5** | **full angles in degrees**, converted to radians for the frustum |

Mirror's Edge is the 0.5 implementation and is device-verified, and this sample
is in degrees. **One sample plus one port is still not a device capture**, so do
not edit `Docs/general_wxr/XRAPI_V0_4_DESC.md` on this evidence — but do stop
treating "tangents" as settled, and log the raw floats on the first real capture
before building anything on either interpretation. The outbound pair should be
the measured render FOV regardless (`DEFECTS.md` D15).

**2. This breaks the inherited FOV guard, which the ClaudeCode audit said was
masking the bug.** `xrapi.cpp` sends the inbound FOV back out with
`if(fH<10) fH=104.5f;`. On this packet `99.00` and `103.40` are both `> 10`, so
the guard does not fire and the mod transmits the *runtime's own* numbers as its
own outbound FOV. See `DEFECTS.md` D15 — the conclusion (do not echo) stands;
the stated mechanism does not.

**3. `y = 0.006` for the head confirms head-centred space.** `Docs/general_wxr/TIPS.md`
makes exactly this point from a Quest 3 capture: there is no floor in XrApi
position space, and a sanity check that assumes one will reject real hardware.
The sample corroborates it, which is a cheap reason to trust the rest of that
section.

The sync pixel itself is unchanged from `Docs/general_wxr/XRAPI_V0_4_DESC.md` §5:
10x10 at (0,0), red = `HMD_SYNC` echoed exactly, green 0, blue 0 in mono, drawn
last with blending, fog, Z and all three filters off, in both halves under SBS.
The inherited implementation is wrong on size, timing, and on inventing values
from `GetTickCount()` when no packet has arrived. Nothing found in the binaries
changes any of that.

---

## 10. Performance: the game's own post-processing switches

The ClaudeCode documents correctly identify the `potato` presets. The player
config adds a set of **Contagion-specific** post-processing convars that are
worth more than anything we could write **[verified]**:

| Convar | Default in the owner's config |
|---|---|
| `mat_cont_color_modify` | `1` |
| `mat_cont_lens_dirt` | `1` |
| `mat_cont_lens_flare` | `1` |
| `mat_cont_volumetric` | `0` |
| `mat_motion_blur_enabled` | `0` (already off) |
| `mat_motion_blur_forward_enabled` | `0` |
| `hud_hide`, `hud_classic`, `cont_show_hud_always` | HUD controls for milestone 10 |
| `fps_max "300"`, `fps_limiter "0"` | no cap; a cap may help consistency |
| `cl_cmdrate "30"`, `cl_updaterate "20"` | local server, low rates |
| `option_duck_method`, `option_ironsight_method` | toggle vs hold — relevant to VR input |

`cpu_level_potato.ekv`, `gpu_level_potato.ekv`, `gpu_mem_level_potato.ekv`,
`mem_level_potato.ekv` and `gpu_renderquality_potato.txt` all ship **[verified]**.
Per `TIPS.md`, reach for those before writing performance code — and verify they
survive a relaunch, which is a known live problem in this family.

---

## 11. What is still unknown after all of this

Deliberately listed, so it is not mistaken for settled:

| Unknown | Why it is still unknown | Cheapest way to settle it |
|---|---|---|
| What `0x107B5F98`'s vtable entries actually do | no disassembly | the vtable-dump deploy, `PLAN.md` §3 |
| What `0x1089D0CC` and `0x106B1F88` are | no disassembly; no diagnostic string at their store sites | same deploy, plus string xrefs in IDA |
| `CViewSetup`'s real layout | RTTI gives the name, not the fields | log 64 bytes from a pointer that looks like a view setup |
| `CUserCmd`'s real layout | as above | log from `VClient016`'s `CreateMove` path |
| Whether `IClientMode` is reachable at all | the two anchors are wrong | find `TerrorClientMode`'s vtable by RTTI, or skip it |
| Whether Input/dinput is reachable for the gamepad approach | not investigated | one experiment with an XInput shim |
| Whether the proxy loads under Box86 at all | never run on device | milestone 2, the log |
