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
| `Docs/ClaudeCode/PROJECT_OVERVIEW.md` | The goal, the target as measured, the path chosen and the paths rejected |
| `Docs/ClaudeCode/CURRENT_STATUS.md` | Where the port actually stands, and the current blocker |
| `Docs/ClaudeCode/BUG_FIXES.md` | Numbered defect register with status lines |
| `Docs/ClaudeCode/PROJECT_PLAN.md` | Milestones, feature specs, risks |
| `Docs/general_wxr/` (rest) | The game-agnostic porting framework: the protocol, the staged plan, tools, tips |
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

## Container

**1024x768**, chosen for performance and simplicity. Note this is *below* the
Quest 3 panel resolution, so the runtime upscales rather than downsamples —
there is no free anti-aliasing at this size, and 2D art covers *more* of the
view rather than less. See `Docs/ClaudeCode/PROJECT_OVERVIEW.md` §5.
