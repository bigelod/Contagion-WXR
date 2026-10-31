
Contagion VR - VS2022 Bundled Build - NO EXTERNAL DEPS NEEDED!

This version bundles MinHook implementation directly (simple x86 JMP hook)
so you don't need to download MinHook separately. Just need DirectX SDK for d3dx9.lib

SOLUTION: ContagionVR.sln
TOOLSET: v143 (VS2022), Platform Win32

BUILD:
1. Install DirectX SDK June 2010 for d3dx9.lib (or use NuGet)
2. Open ContagionVR.sln
3. Release | Win32 -> Build
4. Output: bin/Win32/Release/version.dll

FEATURES:
- XrAPI 0.4 (7872/7873 -> 7278, Z:/tmp/xr/version = 0.4)
- 6DOF (107B5F98, 38h/54h)
- Input block + full mapping you requested
- Voice wheel (Left Grip+Y hold) + laser pointer
- FrameID red square 16x16 top-left R=frameId&0xFF for WinlatorXR smoothness

If d3dx9.lib missing: you can replace D3D9 hook drawing with pure D3D9 DrawPrimitive (already mostly) and ID3DXLine/Font are only from d3dx9. You can keep d3d9.lib only and replace line/font with custom, but easiest is install DXSDK.

Project GUID: 75EF829B-8F95-4D28-8611-64EAFB055E18
