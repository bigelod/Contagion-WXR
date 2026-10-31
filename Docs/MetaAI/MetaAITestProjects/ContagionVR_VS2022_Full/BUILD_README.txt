
Contagion VR - VS2022 Project - XrAPI 0.4 + Voice Wheel + FrameID Marker

SOLUTION: ContagionVR.sln (VS2022, v143, Win32)

BUILD STEPS:
1. Install DirectX SDK June 2010 (for d3dx9.lib)
2. Get MinHook:
   git clone https://github.com/TsudaKageyu/minhook
   Build Release Win32 -> MinHook.lib
   Copy include/MinHook.h to include/
   Copy lib MinHook.lib to libs/MinHook.lib
3. Open ContagionVR.sln
4. Release | Win32 -> Build -> outputs bin/Win32/Release/version.dll

FEATURES:
- XrAPI 0.4 listener 7872/7873, sender 7278, Z:/tmp/xr/version
- 6DOF, input block, mapping, voice wheel, red square FrameID marker

PROJECT GUID: 52DE53DB-BC3E-4DF5-8D78-3398F0BEFBA1
