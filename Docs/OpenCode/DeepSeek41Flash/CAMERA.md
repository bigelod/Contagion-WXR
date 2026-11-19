# CAMERA.md — milestone 6a/6b: the game's camera, measured

Status: **the mechanism is proven on the PC, HMD integration is not done.**
Milestone 6a (find the method) is **done**; 6b (6DOF head tracking feels
locked) has its write path working, verified by rotating the rendered view at a
fixed position.

---

## 1. What the probe found

`g_pViewRender` is `client.dll + 0x7B5F98`, and the object it points at is:

```
slot 0x60B55F98 = 0x60B6F3B8
vtable          = 0x6097CC9C
class           = .?AVCTerrorViewRender@@
```

That confirms `BINARY_RECON.md` §8.2 behaviourally, in the running game, with the
RTTI name resolved from the same vtable the probe patched.

### 1.1 Slot 6 is called by the engine

| slot | caller (`ret`) | module | first argument |
|---|---|---|---|
| 6 | `0x6EC16579` | `engine.dll` | a stack struct with `0,0,1920,1080` at +0, but the rest is not a view setup |
| 7 | `0x60614CD9` | `client.dll` | **`g_pViewRender + 4`** — the live `CViewSetup` |

Slot 6 is the entry the engine uses (`IViewRender::RenderView`-shaped), but the
object it passes is not the live camera. Slot 7 is called every frame with the
setup that actually drives the picture, so **slot 7 is the write target** and the
default (`CWXR_CAM_SLOT=7`).

### 1.2 The layout, measured not assumed

Dumping slot 7's first argument in game (`02:51:23`):

```
[07] a1 @0x60B6F3BC: 00000000 00000000 00000780 00000438 00000000 00000000 ...
[07] a1 +0x20      : 00000000 42937AC6 4256AC08 C3674975 C2C410F4 C52EB5C0 ...
```

Decoded:

| offset | field | value |
|---|---|---|
| +0x00 | `x` | 0 |
| +0x04 | `y` | 0 |
| +0x08 | `width` | 1920 (`0x780`) |
| +0x0C | `height` | 1080 (`0x438`) |
| +0x24 | `fov` | 73.74° |
| +0x28 | `fovViewmodel` | 53.67° |
| +0x2C | `origin` | `(-231.3, -98.0, -2795.4)` |
| +0x38 | `angles` (pitch, yaw, roll) | `(0.0, -123.0, 0.0)` |

Those are world coordinates and a yaw, not a plausible-looking guess. The struct
starts at **`g_pViewRender + 4`** (the vptr occupies +0), and slot 7 receives a
pointer to it.

### 1.3 The write reaches the frame

Two screenshots in **one run, one position** (`origin (1172.0, -3000.0,
-2813.6)`), with the yaw offset changed through a control file between them:

| offset | setup yaw | result |
|---|---|---|
| 0° | 99.0 | looking at the police car and plaza |
| 60° | 159.0 | same position, view rotated 60°, brick wall now on the left |

`cam0.png` / `cam60.png` are the evidence. This is the milestone-6b write path
working end to end: **`CViewSetup::angles` is the camera the renderer uses.**

---

## 2. How the probe works

`src/camera.cpp`. It patches **the object's own vtable slots**, not the functions
they point at. That distinction was measured: function-hooking caught the same
base implementations being called on unrelated objects (`this=0xA5F3B910`, etc.),
which made the log ambiguous. The object's table is used by this object alone.

- Every slot is replaced with a naked thunk: `pushad`, log, apply, `popad`,
  `jmp original`. Registers, stack and arguments are untouched, so a method with
  any signature and any arity passes through unchanged.
- Only entries that are actually executable are patched (63 of 64; one is not).
- The first two calls per slot are logged with their first four stack arguments,
  plus 16 dwords from `a1`/`a2`.
- A fingerprint test looks for the back-buffer size (or 1024×768) anywhere in the
  first 0x60 bytes of each pointer argument and reports the offset where it was
  found. This is what found the layout instead of relying on an SDK.

### 2.1 Switches

| variable | effect |
|---|---|
| `CWXR_CAMPROBE=1` | install the probe and log the first calls |
| `CWXR_CAM_SLOT=n` | which slot receives the live setup (default **7**) |
| `CWXR_CAM_YAW=deg` | initial yaw offset applied every frame |
| `contagion-wxr-cam-yaw.txt` | a number, re-read every 500 ms, overrides the offset live |

All of them are off by default. The shipped build does not patch the camera.

The control file is what makes a camera test meaningful: two runs spawn at
different points, so comparing two runs compares two places. One run, one
position, two values is a controlled experiment.

### 2.2 Known limits of the write as it stands

- **The base yaw is captured once**, on the first applied frame. The write is
  `base + offset`, so if the player turns with the mouse the base does not
  follow. That is fine for a deterministic test and wrong for play; the real
  version must own the input yaw rather than add to a stale value.
- Only **yaw** is written. Pitch, roll, origin and recentring are not.
- The write happens in the slot-7 thunk, before the frame is built — good — but
  whether slot 7 is `RenderView` or a wrapper around it is not yet named. It does
  not matter for the write, and it will matter for the aim/camera split.
- `fov` read 73.7 during load and 106.3 in game. Both are plausible; it is not
  yet known whether +0x24 is `fov` or a different angle in this fork.

---

## 3. What is deliberately not done

Per `PLAN.md` §3.3, the order is orientation → position → recentre, with D10,
D11, D12 and D13 fixed first. None of those are fixed:

- **D10** — basis change and full-range yaw. The HMD quaternion is OpenXR
  (Y-up, right-handed); Source is Z-up with yaw about +Z. A sign or axis error
  here is a view that turns the wrong way or tumbles.
- **D11** — room-space translation rotated by the calibrated yaw only.
- **D12** — translation magnitude clamp.
- **D13** — roll must be written or the horizon does not tilt.

Writing the HMD pose into `angles` before those are settled would produce a
picture that moves but is wrong, which is worse for diagnosis than no picture.

## 4. The next step, concretely

1. Read `XrFrame` (already parsed, normalised) in the slot-7 thunk.
2. Convert the HMD quaternion to Source pitch/yaw/roll, **log the numbers for one
   session**, and compare them with the values the game itself reports at +0x38.
   That comparison is what fixes D10 without guessing: turn the head 90° and see
   whether the logged yaw moves 90° in the right direction.
3. Then write, with the aim (`CreateMove`) and the view (`RenderView`) kept
   separate, as §3.3 requires.
4. Run the PC test with the control file before every device deploy: if a yaw
   change does not move the picture on the PC, it will not on the Quest.

## 5. Reproducing the probe run

```
$env:CWXR_CAMPROBE='1'
.\tools\autotest.ps1 -RunSeconds 10 -BootSeconds 26 -LoadSeconds 30 -Sync 127
Remove-Item Env:CWXR_CAMPROBE
```

Look for `cam : g_pViewRender slot ... class .?AVCTerrorViewRender@@` and the
`[07] a1` dump. For the write test, start the game, enter a map, then write `0`
and `60` to `contagion-wxr-cam-yaw.txt` two seconds apart and screenshot each.
