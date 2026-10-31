
#include "hooks.h"
#include "pattern.h"
#include "math.h"
#include "gameui.h"
#include "input.h"
#include "wndproc.h"
#include "xrapi.h"
#include "voice_wheel.h"
#include "d3d9_hook.h"
#include <MinHook.h>
#include <Windows.h>

typedef bool(__thiscall* CreateMoveFn)(void*, float, CUserCmd*);
typedef void(__thiscall* OverrideViewFn)(void*, CViewSetup*);

static CreateMoveFn oCreateMove=nullptr;
static OverrideViewFn oOverrideView=nullptr;
static void* g_pClientMode=nullptr;

bool __fastcall hkCreateMove(void* ecx, void* edx, float sample, CUserCmd* cmd){
    bool ret = oCreateMove(ecx, sample, cmd);
    if(!cmd) return ret;

    XrFrame fr = GetXrFrameCopy();
    bool isMenu = IsPauseMenuOpen();

    VoiceWheel_Update(fr, isMenu);

    if(VoiceWheel_ShouldBlockGameInput()){
        // Block all shooting while wheel open
        cmd->buttons = 0;
        cmd->forwardmove = 0;
        cmd->sidemove = 0;
        cmd->mousedx = 0;
        cmd->mousedy = 0;
        return ret;
    }

    if(!isMenu && fr.hasData && fr.hmd.valid){
        QAngle ang = QuatToSourceAngles(fr.hmd.rot);
        cmd->viewangles.yaw = ang.yaw;
        cmd->viewangles.pitch = ang.pitch;
    }

    Input_Update(cmd, fr, isMenu);

    return ret;
}
void __fastcall hkOverrideView(void* ecx, void* edx, CViewSetup* setup){
    oOverrideView(ecx, setup);
    if(!setup) return;
    if(IsPauseMenuOpen()) return;
    if(VoiceWheel_IsActive()) return; // don't lean while in wheel
    Vec3 lean = GetHeadDeltaClamped();
    setup->origin[0] += lean.x;
    setup->origin[1] += lean.y;
    setup->origin[2] += lean.z;

    XrFrame fr = GetXrFrameCopy();
    if(fr.hasData && fr.right.valid && fr.hmd.valid){
        Vec3 delta = { fr.right.pos.x - fr.hmd.pos.x, fr.right.pos.y - fr.hmd.pos.y, fr.right.pos.z - fr.hmd.pos.z };
        Quat inv = QuatInv(fr.hmd.rot);
        Vec3 local = QuatRotate(inv, delta);
        Vec3 src = OpenXRToSourcePos(local);
        src = src * 39.37f;
        setup->originViewmodel[0] += src.x * 0.15f;
        setup->originViewmodel[1] += src.y * 0.15f;
        setup->originViewmodel[2] += src.z * 0.15f;
    }
}

bool Hooks_Init(){
    MH_Initialize();
    HMODULE hClient = GetModuleHandleA("client.dll");
    if(!hClient) return false;

    uintptr_t pat = PatternScan(hClient, "8B 0D ?? ?? ?? ?? 8B 01 FF 50 38");
    if(!pat) pat = PatternScan(hClient, "8B 0D ?? ?? ?? ?? 8B 01 FF 50 ??");
    if(!pat) return false;
    uintptr_t pAddr = *(uintptr_t*)(pat+2);
    g_pClientMode = *(void**)pAddr;
    if(!g_pClientMode) return false;

    void** vt = *(void***)g_pClientMode;
    void* ov = vt[14];
    void* cm = vt[21];

    if(MH_CreateHook(ov, &hkOverrideView, (void**)&oOverrideView)!=MH_OK) return false;
    if(MH_CreateHook(cm, &hkCreateMove, (void**)&oCreateMove)!=MH_OK) return false;

    MH_EnableHook(ov);
    MH_EnableHook(cm);

    VoiceWheel_Init();
    D3D9Hook_Init();
    WndProcHook_Init();
    return true;
}
void Hooks_Shutdown(){
    D3D9Hook_Shutdown();
    WndProcHook_Shutdown();
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}
