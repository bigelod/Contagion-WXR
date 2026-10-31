# PROJECT_PLAN.md — Contagion VR 6DOF Injector for WinlatorXR

## 1. High-Level Goal
Inject seated/standing 6DOF head position + rotation from a Meta Quest (WinlatorXR / Wine-on-Android) into **Contagion** (32-bit Source SDK 2007) without modifying game binaries. Achieve:

- Visual-only head lean (no collider change → no wall-peek cheat → multiplayer-safe)
- Logical aim driven by headset yaw/pitch via `CreateMove`
- Monocular VR for performance (single eye render, ~2x cheaper than stereo)
- Full controller input remapping from XrAPI 0.4 to Source inputs
- Flat mouse menu when pause UI is open, VR when in-game

## 2. Architecture

```
Quest (WinlatorXR) -> XrAPI 0.4 -> UDP localhost:7872/7873 ASCII string
                          |
                          v
            version.dll proxy (WINEDLLOVERRIDES=version=n,b)
            Loads version_o.dll (syswow64 version.dll) + our hooks
                          |
        +-----------------+------------------+
        |                 |                  |
   OverrideView     CreateMove Hook    D3D9 EndScene Hook
   (visual lean)   (logical aim)      (voice wheel + FrameID marker)
        |                 |                  |
        +--------+--------+                  |
                 |                           |
            UDP reply 127.0.0.1:7278 -> "L_HAPTIC R_HAPTIC MODE_VR MODE_3D FOVX FOVY"
            File Z:/tmp/xr/version = "0.4"
```

### XrAPI 0.4 Wire Format (Real, from WinlatorXR example)
Client -> Host on `0.0.0.0:7872` fallback `7873`:
```
client0 L_Qx Qy Qz Qw LStickX Y Lx Ly Lz R_Qx Qy Qz Qw RStickX Y Rx Ry Rz HMD_Qx Qy Qz Qw HMD_X Y Z IPD FOVx FOVy FrameID T/F boolString[19] META
Example: client0 0.210 0.290 -0.930 0.012 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.5 0.1 -0.2 0.06 90 90 1234 T FTF... META
```

Host -> Client on `127.0.0.1:7278`:
```
"L_HAPTIC R_HAPTIC MODE_VR MODE_3D FOVX FOVY" e.g. "0 0 1 0 90 90" = monocular VR
                                                        "0 0 0 0 90 90" = flat menu
```

### Hooking Strategy

**1. Visual 6DOF (OverrideView)**
- Interface: `ClientModeShared` from `client.dll`
- Global: `g_pClientMode` = `dword_107B5F98` (found via IDA: pattern `8B 0D ?? ?? ?? ?? 8B 01 FF 50 38` → xref dword, index 14 = 0x38)
- VTable index: 14 `OverrideView(CViewSetup*)`
- Logic: Extract HMD local delta = `HMD_pos - HMD_recenterOrigin`. Clamp to 6 units (~15cm in Source). Add to `view->origin`. Keep `view->angles` from HMD for visual, but do NOT modify player collision hull.
- Also apply 6DOF to viewmodel via `viewmodel_offset` proxy if available.

**2. Logical Aim (CreateMove)**
- VTable index: 21 `CreateMove(float frametime, CUserCmd*)` → pattern `50 54` in IDA, dword search `dword_1089D0CC` was false positive, `107B5F98` is correct
- Set `cmd->viewangles = HMD_yaw/pitch + recenterYaw`. Zero `mousedx/mousedy` to kill mouse drift.

**3. Pause / Menu Detection**
- `GameUI011` interface string in `GameUI.dll` → `IsGameUIActive()` bool
- ConVars `gameui_ingame_menu_active`, `gameui_paused` found via strings view, constructor `sub_100516F0`, object `unk_107C3888`
- When `IsGameUIActive()` true → send `MODE_VR=0` so WinlatorXR shows flat mouse cursor, user can click menu.

**4. Input Blocking**
- Subclass `Valve001` WndProc (main Source window). Swallow all `WM_KEYDOWN/UP`, `WM_MOUSEMOVE` except ESC.
- In CreateMove, zero mouse deltas and override `forwardmove/sidemove` from left stick.

## 3. Input Mapping (Per User Request)

**Left Hand:**
- Stick → WASD (`forwardmove`, `sidemove`)
- Grip = modifier
- Grip + Y tap (<300ms) → phone (B) → `ClientCmd("phone")`
- Grip + Y hold (>300ms) → open voice wheel, right thumbstick selects slice, release triggers `vocalize <Event>`
- X → crouch (CTRL) → `IN_DUCK`
- Left Trigger → melee (V) → `+melee` or `impulse` placeholder
- Grip+B hold → drop (G)
- Grip+A hold → dropammo (J)
- Y → flashlight (F / impulse 100)
- Stick click → sprint/blending `IN_SPEED`

**Right Hand:**
- Trigger → `IN_ATTACK`
- Grip → `IN_JUMP`
- A → `IN_USE` (E)
- B → `IN_RELOAD` (R)
- Thumbstick up/down → `inv_next/prev` (mouse wheel)
- Thumbstick left/right → smooth turn (adds yaw)

**Human vs Zombie Profiles:** Auto-detect team via `GetTeamNumber()` or `m_iTeam` netprop, swap bindings if needed.

## 4. Voice Wheel

- Hold Left Grip+Y >300ms → `voiceWheelActive = true`
- Right thumbstick angle → 8 slices: help, followme, needammo, groupup, etc. Config in `contagion_vr_voices.txt`
- Right hand laser pointer alternative: raycast from right controller pos/quat
- Release → `engine->ClientCmd("vocalize <EventID>")`
- Render: D3D9 `EndScene` hook → `ID3DXLine` circle + `ID3DXFont` labels. Requires `d3dx9.lib` (June 2010 SDK) or custom line renderer.

## 5. Performance / VR Smoothness

- Monocular mode: Tell WinlatorXR `MODE_3D=0` so it doesn't do double-eye warp. We render one eye full-res.
- FrameID marker: 16x16 solid square top-left (0,0)-(16,16) with `R = frameID & 0xFF`, `G=0 B=0 A=255`. Android side `glReadPixels` detects duplicate frames (HWXR/SixDOFinator technique) and interpolates/ATW.

## 6. Build / Load

- VS2022 v143, Win32 Release, `version.dll` proxy → forwards to `version_o.dll` (real syswow64 version.dll copy)
- Exports: `GetFileVersionInfoA/W`, `GetFileVersionInfoSizeA/W`, `VerQueryValueA/W` etc. proxied via `GetProcAddress`
- MinHook bundled simple 5-byte JMP trampoline (enough for x86 vtable + EndScene)
- Launch: `WINEDLLOVERRIDES=version=n,b Contagion.exe -insecure -windowed -novid`
- Linux path `Z:/tmp/xr/version` created by WinlatorXR Wine prefix

## 7. Safety / Anti-Cheat

- `-insecure` disables VAC for Contagion (community game). No collider change = can't peek walls.
- All hooks client-side, no memory writes to server.
- Seated/standing still only, no room-scale movement beyond 15cm clamp.

## 8. IDA Workflow Used

1. Strings view: `IsGameUIActive`, `GameUI011`, `gameui_ingame_menu_active`
2. Xref to `GameUI.dll`: `sub_100516F0` registers ConVar, object at `unk_107C3888`
3. `client.dll`: Search byte pattern `8B 0D ?? ?? ?? ?? 8B 01 FF 50 38` → gives `g_pClientMode` dword `107B5F98` (38h search) vs `1089D0CC` (54h false)
4. Verify vtable: `[g_pClientMode] + 0x38 = OverrideView`, `+0x54 = CreateMove`
