# BUG_FIXES.md — Short Bullet-Point Log

### 2026-04-30 Session

- **Fixed g_pClientMode false positive** — Pattern `54h` search returned `dword_1089D0CC` which was unrelated global. Switched to `38h` pattern `8B 0D ?? ?? ?? ?? 8B 01 FF 50 38` → correctly finds `dword_107B5F98` (user confirmed via IDA screenshot).

- **Fixed MinHook missing dependency build error** — Original zip expected external `MinHook.lib`. Created `src/minhook.cpp` simple 5-byte JMP trampoline implementing `MH_Initialize/CreateHook/EnableHook/DisableHook` API so project builds with no extra download. Also provided full-lib variant for users who have MinHook SDK.

- **Fixed WndProc subclass crash on second load** — Was calling `SetWindowLong` without saving old proc per-instance. Now saves `g_oldWndProc` static and restores on `DLL_PROCESS_DETACH`, checks `Valve001` class name before hooking.

- **Fixed UDP socket leak on map change** — `recvfrom` thread wasn't closed when `client.dll` reloaded on level change. Added `closesocket` in `Shutdown()` + `WSACleanup` guard with `g_bRunning = false`.

- **Fixed voice wheel stuck open** — Hold detection used `GetTickCount` without reset on release. Now uses `GripY_pressTime` and clears `voiceWheelActive` when both Grip+Y released, with 300ms threshold to distinguish tap vs hold.

- **Fixed FrameID square overwriting HUD** — Was drawing square with `D3DRS_ZENABLE=FALSE` but no `D3DRS_ALPHABLENDENABLE`. Square showed black border. Now sets `D3DFMT_A8R8G8B8` texture with solid color, disables blending, and draws after `BeginScene` before HUD to survive.

- **Fixed monocular FOV mismatch** — Was sending `FOVx=90 FOVy=90` hardcoded. Now reads `HMD_FOVx/FOVy` from XrAPI packet (last fields before FrameID) and forwards to WinlatorXR so warp matches game FOV.

- **Fixed input double-trigger** — Both WndProc block and CreateMove zeroing caused sprint to never trigger (Left Stick Click → `IN_SPEED` cleared by mouse zero). Separated: WndProc only blocks keyboard/mouse messages, CreateMove only zeroes `mousedx/y` and mouse wheel, not `buttons` except when voice wheel active.

- **Fixed version.dll proxy export ordinals** — Initial def file forwarded only `GetFileVersionInfoA`. Wine's loader also needs `GetFileVersionInfoSizeW`, `VerQueryValueW`. Added full export list via `GetProcAddress` dynamic forwarding in `DllMain` (proxy pattern) so `WINEDLLOVERRIDES=version=n,b` doesn't fail to load.

- **Fixed VS2022 project not showing bin output** — `OutDir` was `$(SolutionDir)bin\...` but `IntDir` defaulted to project folder causing file not found confusion. Set both `OutDir` and `IntDir` to `$(SolutionDir)bin\$(Platform)\$(Configuration)\` and `...\obj\`.

- **Fixed XrAPI bool string parsing off-by-one** — Example packet `FFFFFFFFFFFFFFFFFFF` (19 chars) was being read as 18 due to `META` suffix trim. Now strips trailing ` META` first, then takes last 19-char token before it as bool string.

- **Fixed 6DOF wall peek cheat** — Original code added HMD delta to both visual origin and player collision origin. Now only modifies `CViewSetup::origin` (visual), leaves `localPlayer->GetAbsOrigin()` untouched. Added comment `// visual only, no SetAbsOrigin`.
