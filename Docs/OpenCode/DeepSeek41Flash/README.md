# OpenCode / DeepSeek V4.1 Flash — notes for Contagion-WXR

**Written 2026-11-17.** This is the live documentation set for the project from
this point on. `Docs/MetaAI/` and `Docs/ClaudeCode/` are **archival**: readable
for history and design intent, not maintained, not current truth.

## What this session did

1. Read all of `Docs/MetaAI/` (5 documents plus the complete source of the
   bundled VS2022 variant).
2. Read all of `Docs/ClaudeCode/` (4 documents).
3. Read all 10 files of `Docs/general_wxr/` in the order its `README.md` gives.
4. Read the inherited C++ directly and checked its claims against the source.
5. Ran a **read-only** pass over the owner's install at
   `E:\Steam\steamapps\common\Contagion` — PE headers, import tables, export
   tables, interface version strings, RTTI type descriptors, embedded PDB
   paths, convar/bind text, and byte-pattern counts. Nothing was written to the
   install. No disassembler was used (see `BINARY_RECON.md` §1 for exactly what
   that limits).
6. Read three sibling ports for transferable technique — **XOPS-WXR**,
   **mirrors-edge-WXR** and **Interstellar-Marines-WXR** — including the
   readable C# of the last. See `SIBLING_PORTS.md`.

Step 5 is the part that produced new facts. Several long-standing assumptions
in both prior document sets are now settled one way or the other, including the
project's central one: **`0x107B5F98` is not `g_pClientMode`. It is
`g_pViewRender`**, proven by the error string at its assignment site. Details in
`BINARY_RECON.md` §8, consequences in `PLAN.md` §2.2 and §3.

## The one-paragraph verdict

Nothing builds and nothing runs, as ClaudeCode said, and the inherited code is
a non-compiling sketch — but it is a *better sketch than its numbers*, and the
numbers are now partly checkable rather than merely suspect. The Meta AI design
intent survives: `version.dll` proxy, in-process, 32-bit, the game's own D3D9,
mono, `MODE_VR = 0` in menus, visual-only lean, and an input mapping conceived
per hand. What does not survive is every hardcoded engine fact: the object the
hooks land on, the vtable indices, two of the three interface versions, the
console command names, the window class name, and the sync pixel's size,
location, timing and value. Two of those — the hook object and the command
names — were answered outright by reading the shipped binaries, and the answers
were sitting in a 9 KB human-readable `config.cfg` the whole time.

## Read in this order

| File | What it is |
|---|---|
| **[BINARY_RECON.md](BINARY_RECON.md)** | What the shipped binaries actually contain, with the method used. The verified base everything else rests on |
| **[RUNTIME_CONFIRMATIONS.md](RUNTIME_CONFIRMATIONS.md)** | What each in-game run proved, what it corrected, and what it retired |
| **[NIGHT_REPORT.md](NIGHT_REPORT.md)** | The overnight session: the device capture, the sync pixel, and the camera write, with the evidence |
| **[CAMERA.md](CAMERA.md)** | The camera, measured: `CTerrorViewRender`, the live `CViewSetup` layout, the proven write path, and what remains before HMD integration |
| **[AUDIT.md](AUDIT.md)** | Assessment of the Meta AI and ClaudeCode work: what holds, what I confirmed, what I corrected, what both missed |
| **[DEFECTS.md](DEFECTS.md)** | The live defect register, with status lines |
| **[DEPLOY.md](DEPLOY.md)** | Where the mod goes: the two game copies, `Release\ContagionWXR`, and the build-and-deploy script |
| **[SIBLING_PORTS.md](SIBLING_PORTS.md)** | What to copy from XOPS-WXR, mirrors-edge-WXR and Interstellar-Marines-WXR — proven technique, not game trivia |
| **[PLAN.md](PLAN.md)** | What to do next, in order, with the experiments that settle the remaining unknowns |

## Provenance tags

Following the convention already in use, plus one addition:

| Tag | Means |
|---|---|
| **[verified]** | established from the owner's install or by a check run on 2026-11-17 |
| **[bin]** | read out of the shipped binaries by string, export/import table, RTTI or byte-pattern analysis — but **not** by disassembly. Strong, not complete |
| **[code]** | read directly out of the inherited Meta AI source |
| **[spec]** | from `Docs/general_wxr/` or the XrApi documentation |
| **[web]** | from a public source, named inline |
| **[unverified]** | an assumption. Nothing load-bearing may rest on one without a named experiment |

Where a **[bin]** finding is load-bearing I say what would confirm it properly
(usually a log line from the running game — `PLAN.md` §2 is built around
exactly that).

## Standing rules inherited unchanged

Everything in `Docs/general_wxr/CLAUDE.md` and the project-specific rules in the
root `CLAUDE.md` still apply and are not restated here. In particular: the sync
pixel is not optional; the window must be borderless and filling, re-applied
every frame; normalise every quaternion on ingest; the collision hull is never
written; the build stamp is mandatory; one change per deploy; nothing is done
until the owner has verified it on the Quest 3.

## What this folder is not

- **Not a plan to rewrite the inherited code wholesale.** Its module split and
  its input design are worth more than its arithmetic.
- **Not authority on the game's internals.** Every address here is for *this
  build* and must be re-derived if the game updates. Record the build you
  derived it from; `BINARY_RECON.md` §2 does.
- **Not a device report.** Nothing in this folder has been run on a headset.
