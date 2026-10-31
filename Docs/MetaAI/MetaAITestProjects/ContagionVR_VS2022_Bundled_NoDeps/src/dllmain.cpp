
#include <Windows.h>
#include "hooks.h"
#include "xrapi.h"

#pragma comment(linker, "/export:GetFileVersionInfoA=version_o.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=version_o.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:VerQueryValueA=version_o.VerQueryValueA")
#pragma comment(linker, "/export:GetFileVersionInfoExA=version_o.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=version_o.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:VerFindFileA=version_o.VerFindFileA")
#pragma comment(linker, "/export:VerInstallFileA=version_o.VerInstallFileA")
#pragma comment(linker, "/export:VerLanguageNameA=version_o.VerLanguageNameA")
#pragma comment(linker, "/export:VerQueryValueW=version_o.VerQueryValueW")
#pragma comment(linker, "/export:GetFileVersionInfoW=version_o.GetFileVersionInfoW")

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(hMod);
        LoadLibraryA("version_o.dll");
        CreateThread(nullptr,0,[](LPVOID)->DWORD{
            Sleep(1200);
            Xr_Init();
            Sleep(600);
            if(!Hooks_Init()){
                MessageBoxA(nullptr,"Hooks failed","Contagion XrAPI 0.4 VoiceWheel",0);
            }
            return 0;
        },nullptr,0,nullptr);
    }
    if(reason==DLL_PROCESS_DETACH){
        Hooks_Shutdown();
        Xr_Shutdown();
    }
    return TRUE;
}
