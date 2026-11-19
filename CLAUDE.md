# Contagion-WXR

This is a WinlatorXR VR port. Read `Docs/general_wxr/CLAUDE.md` before
planning or writing any code.

**Goal:** classic flat **Contagion** (Steam AppID 238430, 32-bit, Direct3D 9)
in **monocular 6DOF VR** on a **Meta Quest 3** standalone, via WinlatorXR and
XrApi. Not the separate `ContagionVR` title — that is a different product.

## Read in this order

| File | What it is |
|---|---|
| `Docs/general_wxr/CLAUDE.md` | **Standing instructions.** Binding. The choices not to relitigate, the non-negotiables in the code, how to work |
| `Docs/OpenCode/DeepSeek41Flash/README.md` | **The live notes.** Index to the current documentation set |
| `Docs/OpenCode/DeepSeek41Flash/BINARY_RECON.md` | **Measured facts** about the shipped binaries: interfaces, exports, RTTI, the real hook targets, the real command names |
| `Docs/OpenCode/DeepSeek41Flash/RUNTIME_CONFIRMATIONS.md` | **Read first after a game run.** What the last run proved or corrected |
| `Docs/OpenCode/DeepSeek41Flash/NIGHT_REPORT.md` | The overnight session: device capture, sync pixel, camera, and the evidence |
| `Docs/OpenCode/DeepSeek41Flash/CAMERA.md` | **The camera, measured.** Object, slot, `CViewSetup` layout, the proven yaw write, and the open HMD maths |
| `Docs/OpenCode/DeepSeek41Flash/DEFECTS.md` | The live defect register |
| `Docs/OpenCode/DeepSeek41Flash/DEPLOY.md` | Where the mod goes: the two game copies, `Release\ContagionWXR`, and the build-and-deploy script |
| `Docs/OpenCode/DeepSeek41Flash/SIBLING_PORTS.md` | Proven technique from XOPS-WXR, mirrors-edge-WXR and Interstellar-Marines-WXR |
| `Docs/OpenCode/DeepSeek41Flash/PLAN.md` | What to do next, in order, with the experiments that settle the unknowns |
| `Docs/general_wxr/` (rest) | The game-agnostic porting framework: the protocol, the staged plan, tools, tips |
| `Docs/ClaudeCode/` | **Archival.** A good audit of the Meta AI attempt, superseded by `Docs/OpenCode/DeepSeek41Flash/` |
| `Docs/MetaAI/` | **History only.** See below |

## Project-specific rules

These sit on top of the non-negotiables in `Docs/general_wxr/CLAUDE.md`, which
all apply and are not restated here. Full list in
`Docs/ClaudeCode/PROJECT_PLAN.md` §2.

- **No SDK-derived constant is trusted.** Contagion is **not** Source SDK 2007
  — it is a heavily modified Left 4 Dead 2 branch fork with Jolt, Wwise, CEF
  and Angelscript. Every vtable index, interface version, struct offset and
  global address must be derived from *these* binaries and confirmed by a log
  line from the running game. A number from any Source SDK header, or from
  `Docs/MetaAI/`, is a hypothesis — label it `[unverified]` until a log proves
  it. This is the project's defining risk.
- **The player's collision hull is never written.** Head position moves
  `CViewSetup::origin` and nothing else. No `SetAbsOrigin`, no hull or
  eye-offset changes. This keeps wall-peeking impossible.
- **`Docs/MetaAI/` is history, not truth.** Read it for design intent. Its
  completion percentages, offsets and interface layouts are wrong and must not
  be cited as evidence — the code there has never compiled, so nothing in it
  has ever executed.
- **`Docs/ClaudeCode/` is archival.** A careful audit of the Meta AI attempt,
  accurate about the code and about the blocker, but its engine-level
  conclusions have since been measured and partly overturned — see
  `Docs/OpenCode/DeepSeek41Flash/AUDIT.md`.
- **Mark provenance.** Use `[verified]` / `[code]` / `[spec]` / `[web]` /
  `[unverified]` on any non-obvious claim in the docs, as the existing
  `Docs/ClaudeCode/` files do.
- **The owner's game install is read-only to automation.** Reading
  `E:\Steam\steamapps\common\Contagion` to derive offsets is expected. Writing
  anything into it other than the mod's own deployed files is not.
- **Testing happens on a Quest 3 through WinlatorXR, by the owner.** Nothing is
  done until he has verified it on hardware. **One change per deploy.**
- **The build stamp is mandatory.** `__DATE__ __TIME__` compiled in, logged on
  line 1, and the deploy script refuses a binary older than the newest source
  file.

## Deploy targets

Full detail in `Docs/OpenCode/DeepSeek41Flash/DEPLOY.md`. Short version:

| Path | Role |
|---|---|
| `E:\Steam\steamapps\common\Contagion` | **PC test target**, native Windows, launched directly |
| `E:\tmp\Q3Mods\Contagion` | **Quest staging target**, a full game copy staged for the headset |
| `Release\ContagionWXR` | **mod-only build output** — gitignored; never the source of truth |

Mod files go at the **game root, beside `contagion.exe`** — not in `engine/` or
`contagion/bin/`. That is where the `version.dll` load chain finds them.

Shortcuts for all three live in `Release/`. The two game copies were
byte-identical on 2026-11-17; keep them that way, or a copy bug will read as a mod
bug.

## Container

**1024x768**, chosen for performance and simplicity. Note this is *below* the
Quest 3 panel resolution, so the runtime upscales rather than downsamples —
there is no free anti-aliasing at this size, and 2D art covers *more* of the
view rather than less. See `Docs/ClaudeCode/PROJECT_OVERVIEW.md` §5.
