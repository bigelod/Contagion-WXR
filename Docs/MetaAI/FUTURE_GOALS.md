# FUTURE_GOALS.md — Not Yet Started

## 1. Wrist-Attached Voice Wheel
- **Status:** Not started
- **Goal:** Instead of screen-space circle, attach voice wheel to left wrist (like VRChat menu). Render as world-space quad facing HMD, positioned `leftControllerPos + forward*0.15m`.
- **Tasks:**
  - Convert left controller pos/quat from XrAPI local to Source world (apply player yaw)
  - Render via `IVRenderView::Draw3DText` or D3D9 world quad + custom shader
  - Make it follow hand smoothly (lerp, not jitter)

## 2. Full 6DOF Weapon Visual
- **Status:** Not started
- **Goal:** Weapon doesn't just move with view, but has independent pos/rot lag from HMD (like BoneLab). 
- **Tasks:**
  - Hook `C_BaseViewModel::CalcViewModelView` or write to `viewmodel_offset` convars `viewmodel_offset_x/y/z`
  - Add lag filter: `weaponPos += (hmdDelta * 0.5 - weaponPos) * dt*10`
  - Handle clipping: pull weapon back when `trace` hits wall

## 3. Haptics Pulse Feedback
- **Status:** Not started (parsing only)
- **Goal:** Pulse Quest controllers when shooting / taking damage.
- **Tasks:**
  - Hook `FireBullets` / `TakeDamage` in client.dll, set `pendingHapticL/R = 1` for 50ms
  - Send via UDP reply: currently we send `0 0 ...` always, need to send `L_HAPTIC R_HAPTIC`
  - Tune duration/amplitude mapping from Source `Weapon_Shoot` events

## 4. Debug Colored Squares (Green/Blue)
- **Status:** Not started (only red exists)
- **Goal:** Like SixDOFinator, add additional markers for tracking latency.
- **Spec:**
  - Green 16x16 at top-right: `G = latencyMs & 0xFF` or `IN_JUMP` pressed flag
  - Blue 16x16 at bottom-left: `B = trackingLostFlag`
- **Tasks:**
  - Extend EndScene draw to 3 squares, make positions configurable via ini

## 5. Recenter Binding
- **Status:** Not started
- **Goal:** Long press both thumbsticks or Y+B → recenter HMD origin + yaw.
- **Tasks:**
  - Zero `hmdRecenterPos = currentHmdPos`, `hmdRecenterYaw = currentHmdYaw`
  - Save to `contagion_vr_config.ini`
  - Feedback: haptic + screen message `Recentered`

## 6. Auto Human/Zombie Profile Switch
- **Status:** Partially designed, not implemented
- **Goal:** Detect `m_iTeam` / `m_lifeState` to switch bindings (e.g., zombie has no flashlight, but pounce)
- **Tasks:**
  - Netvar scan for `DT_BasePlayer::m_iTeam` offset (use `client.dll` `RecvTable` dumper)
  - Poll every second, swap `InputProfile` struct
  - Voice wheel lines change per team

## 7. Stereo / Dual Eye Option
- **Status:** Not started
- **Goal:** Optional true stereo for users with powerful devices (e.g., ROG Ally via Winlator)
- **Tasks:**
  - When `MODE_3D=1`, render two viewports with IPD offset: `eyePos = headPos +/- rightVector*IPD/2`
  - Double `OverrideView` calls? Need to check if Source supports side-by-side
  - Config toggle `stereo=0/1` in ini

## 8. Smooth Locomotion Comfort Vignette
- **Status:** Not started
- **Goal:** Reduce motion sickness during stick movement.
- **Tasks:**
  - On stick move >0.1, draw dark vignette via D3D9 fullscreen quad with radial alpha
  - Fade in/out 200ms

## 9. Proper MinHook Full Implementation
- **Status:** Currently using simple 5-byte JMP
- **Goal:** Bundle full TsudaKageyu MinHook (HDE32/HDE64 disasm) for robustness on functions that start with short jumps
- **Tasks:**
  - Add `src/hde/hde32.c`, `hde64.c`, `buffer.c`, `hook.c`, `trampoline.c`
  - Test on functions with <5 byte prologue (e.g., `GetProcAddress` stubs)

## 10. Linux/Proton Build Path
- **Status:** Not started
- **Goal:** Make same dll work on Linux Proton (Contagion native Linux build) via `LD_PRELOAD` or Proton `version.dll` override
