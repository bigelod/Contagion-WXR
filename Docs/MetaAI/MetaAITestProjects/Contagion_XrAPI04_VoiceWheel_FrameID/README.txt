
Contagion 6DOF XrAPI 0.4 - WinlatorXR

BUILD: Win32 / Release / DLL named version.dll
Dependencies: MinHook (include), ws2_32.lib, Psapi.lib

INSTALL (WinlatorXR container):
- Copy Contagion game
- Place your built version.dll next to Contagion.exe
- Copy C:\windows\syswow64\version.dll -> Contagion/version_o.dll
- In container advanced env: WINEDLLOVERRIDES=version=n,b
- Launch with -insecure

XrAPI 0.4 spec implemented:
1. Listening thread 0.0.0.0:7872 fallback 7873 - parses:
   client0 lQuat(4) lStick(2) lPos(3) rQuat(4) rStick(2) rPos(3) hQuat(4) hPos(3) ipd fovH fovV frameId boolStr(19) hmdType
   Example: client0 0.210 0.290 -0.930 0.040 0.0 0.0 -0.008 -0.229 -0.173 0.100 -0.300 0.950 -0.080 0.0 0.0 0.154 -0.240 -0.140 0.150 -0.070 0.050 0.990 0.037 0.006 -0.017 0.0678 99.00 103.40 103 FFFFFFFFFFFFFFFFFFFFF META

2. File Z:/tmp/xr/version contains "0.4"

3. Sender thread to 127.0.0.1:7278 every ~11ms: "leftHap rightHap vrMode 0 fovH fovV"
   Example you gave: 0 0 0 0 104.5 104.5
   vrMode = GetDesiredVrMode() from GameUI011 / gameui_ingame_menu_active ConVar
   0 = flat menu (pause), 1 = VR gameplay

6DOF:
- OverrideView vt[14] = 38h at g_pClientMode dword_107B5F98 (your IDA result)
- CreateMove vt[21] = 54h
- Head delta clamped to 6 units anti-wallpeek
- Weapon visual offset from right hand relative to HMD

Haptics: Call Xr_SetHaptics(l,r) from game events later.
