# CURRENT_PROGRESS.md — In-Progress Features

## 1. OverrideView Visual 6DOF Hook
- **Status:** 80% — Code written, pattern scan finds `g_pClientMode = 0x107B5F98`, vtable 14 hooked, delta clamping to 6 units implemented. **Not yet tested live in Contagion.exe inside Winlator**
- **Remaining:**
  - Verify `CViewSetup` layout matches SDK2007 (origin at +0x?, angles +?)
  - Confirm 6 units = ~15cm feels right (may need 4 or 8)
  - Test wall collision still blocks visual lean (traceray check)
  - Hook not hitting? May need to find `ClientModeNormal::OverrideView` subclass instead of base

## 2. CreateMove Logical Aim Hook
- **Status:** 75% — VTable 21 hooked, `viewangles` overwritten from HMD yaw/pitch, `mousedx/mousedy` zeroed. **Not tested for aim drift**
- **Remaining:**
  - Yaw wrapping (clamp -180..180) to avoid spin
  - Pitch clamp -89..89 to prevent flip
  - Verify `CUserCmd` size (Contagion may have extra fields vs SDK2007)
  - Test multiplayer: does server anti-cheat kick for large angle changes per tick?

## 3. XrAPI 0.4 Networking
- **Status:** 90% — UDP listener on 7872/7873, sender on 7278, parsing logic ported from C# example. Version file `Z:/tmp/xr/version` write.
- **Remaining:**
  - Handle packet loss / out-of-order FrameID (use latest)
  - Test string parsing with real WinlatorXR 0.4 (some builds send extra spaces)
  - Confirm `T/F` bool string length 19 maps to your 19 bools list

## 4. Input Blocking + Remapping
- **Status:** 70% — WndProc subclass of `Valve001`, swallow keys except ESC, full mapping table defined per user request (Left Grip+Y, Right Trigger etc.)
- **Remaining:**
  - Contagion specific `IN_` flags: need `client.dll` `in_buttons.h` dump to confirm `IN_SPEED`, `IN_DUCK` values still same
  - Console commands `phone`, `drop`, `dropammo`, `vocalize` are placeholders — need `find` command output from in-game console
  - DirectInput bypass: Source may use `m_pInputSystem` raw input, WndProc alone may not block. Fallback is zeroing moves in CreateMove (already done) but still needs test

## 5. Voice Wheel UI
- **Status:** 60% — D3D9 EndScene hook via dummy device, `ID3DXLine` circle + font labels, config file `contagion_vr_voices.txt` parsing, hold detection 300ms for Left Grip+Y
- **Remaining:**
  - Laser pointer selection math from right hand quat → screen ray (currently using thumbstick angle only)
  - Text rendering: `D3DXCreateFont` may fail if device not `D3DDEVTYPE_HAL`, need fallback
  - Device Reset hook: on `Reset` we release line/font, recreate after — implemented but not tested
  - `vocalize` EventID list: need real Contagion `vocalize` help output

## 6. FrameID Red Square
- **Status:** 85% — 16x16 top-left (0,0) drawn in EndScene, `R = frameID & 0xFF`, `G=0 B=0 A=255`, matches SixDOFinator/HWXR spec
- **Remaining:**
  - Verify Android `glReadPixels` reads back our square (may be overwritten by Steam overlay or HUD)
  - Test smoothness improvement in WinlatorXR with marker enabled vs disabled

## 7. VS2022 Build System
- **Status:** 90% — `ContagionVR.sln`, `v143`, Win32 Release, `bin/Win32/Release/version.dll` output, `d3d9.lib d3dx9.lib ws2_32.lib` linking. Two variants: external MinHook lib and bundled simple JMP.
- **Remaining:**
  - User reports build not showing — may be IntDir issue (now fixed to `$(SolutionDir)bin/.../obj/`)
  - `d3dx9.lib` dependency: June 2010 SDK not on fresh VS. Bundled version still needs it for `ID3DXLine/Font`. Could replace with pure D3D9 primitives to make zero-dep
  - `version_o.dll` copy step: need post-build event `copy %WINDIR%\SysWOW64\version.dll $(OutDir)version_o.dll` documented

## 8. GameUI Pause Detection
- **Status:** 65% — String search for `GameUI011`, `gameui_ingame_menu_active` ConVar at `unk_107C3888`, constructor `sub_100516F0`. Logic `if IsGameUIActive() send MODE_VR=0` implemented.
- **Remaining:**
  - Confirm `GameUI011` interface actually exists in Contagion's `GameUI.dll` (some mods use `GameUI010`)
  - Test transition: when menu opens mid-game, does mouse become free? Need to release WndProc block temporarily
  - Fallback: also check `engine->IsInGame()` / `IsDrawingLoadingImage()`
