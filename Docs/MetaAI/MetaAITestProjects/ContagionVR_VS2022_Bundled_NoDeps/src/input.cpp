
#include "input.h"
#include "gameui.h"
#include "math.h"
#include "voice_wheel.h"
#include <Windows.h>
#include <cmath>
#include <string>

typedef void* (*CreateInterfaceFn)(const char*, int*);
class IVEngineClient {
public:
    virtual void unused0()=0;
    virtual void ClientCmd(const char*)=0;
};

static IVEngineClient* GetEngine(){
    static IVEngineClient* eng=nullptr; static bool tried=false;
    if(tried) return eng; tried=true;
    HMODULE h=GetModuleHandleA("engine.dll"); if(!h) h=LoadLibraryA("engine.dll");
    if(!h) return nullptr;
    auto fac=(CreateInterfaceFn)GetProcAddress(h,"CreateInterface");
    if(!fac) return nullptr;
    eng=(IVEngineClient*)fac("VEngineClient014",nullptr);
    if(!eng) eng=(IVEngineClient*)fac("VEngineClient013",nullptr);
    return eng;
}
static void SendCmd(const char* cmd){
    if(auto e=GetEngine()){ __try{ e->ClientCmd(cmd); }__except(EXCEPTION_EXECUTE_HANDLER){} }
}

#define IN_ATTACK   (1<<0)
#define IN_JUMP     (1<<1)
#define IN_DUCK     (1<<2)
#define IN_FORWARD  (1<<3)
#define IN_BACK     (1<<4)
#define IN_USE      (1<<5)
#define IN_CANCEL   (1<<6)
#define IN_LEFT     (1<<7)
#define IN_RIGHT    (1<<8)
#define IN_MOVELEFT (1<<9)
#define IN_MOVERIGHT (1<<10)
#define IN_ATTACK2   (1<<11)
#define IN_RUN      (1<<12)
#define IN_RELOAD   (1<<13)
#define IN_ALT1     (1<<14)
#define IN_ALT2     (1<<15)
#define IN_SCORE    (1<<16)
#define IN_SPEED    (1<<17)
#define IN_WALK     (1<<18)
#define IN_ZOOM     (1<<19)

static PlayerClass g_class = PlayerClass::Human;
static InputConfig g_cfg;
static bool g_prevDropItem=false;
static bool g_prevDropAmmo=false;
static bool g_prevFlash=false;
static bool g_prevInvNext=false;
static bool g_prevInvPrev=false;
static bool g_prevPhoneTap=false;
static DWORD g_phoneHoldStart=0;

void Input_SetPlayerClass(PlayerClass c){ g_class=c; }
PlayerClass Input_DetectPlayerClass(){ return PlayerClass::Human; }
bool Input_ShouldBlockKeyboardMouse(){ return !IsPauseMenuOpen(); }
static float ApplyDeadzone(float v, float dz){ if(fabs(v) < dz) return 0; float sign = v>0?1:-1; return sign * (fabs(v)-dz)/(1.0f-dz); }

void Input_Update(CUserCmd* cmd, const XrFrame& fr, bool isMenuOpen){
    if(!cmd) return;
    if(isMenuOpen) return;
    if(!fr.hasData) return;
    if(VoiceWheel_ShouldBlockGameInput()) return;

    cmd->mousedx = 0;
    cmd->mousedy = 0;

    Vec2 left = fr.leftStick;
    float lx = ApplyDeadzone(left.x, g_cfg.moveDeadzone);
    float ly = ApplyDeadzone(left.y, g_cfg.moveDeadzone);
    cmd->forwardmove = ly * g_cfg.forwardScale;
    cmd->sidemove = lx * g_cfg.sideScale;

    Vec2 right = fr.rightStick;
    float rx = ApplyDeadzone(right.x, g_cfg.turnDeadzone);
    float ry = ApplyDeadzone(right.y, g_cfg.turnDeadzone);

    if(fabs(rx) > 0.01f){
        float turn = rx * g_cfg.turnSpeedDegPerSec * (1.0f/60.0f);
        cmd->viewangles.yaw += turn;
    }
    bool invNext = ry > 0.6f;
    bool invPrev = ry < -0.6f;
    if(invNext && !g_prevInvNext) SendCmd("inv_next");
    if(invPrev && !g_prevInvPrev) SendCmd("inv_prev");
    g_prevInvNext = invNext;
    g_prevInvPrev = invPrev;

    int buttons = 0;
    if(fr.rTrigger) buttons |= IN_ATTACK;
    if(fr.rGrip) buttons |= IN_JUMP;
    if(fr.lX) buttons |= IN_DUCK;

    PlayerClass pc = g_class;
    if(pc==PlayerClass::Human){
        if(fr.lThumbPress) buttons |= IN_SPEED;
        if(fr.lTrigger) buttons |= IN_ATTACK2;
        if(fr.rA) buttons |= IN_USE;
        if(fr.rB && !fr.lGrip) buttons |= IN_RELOAD; // don't reload when lGrip+R B is drop
        if(fr.lY && !fr.lGrip && !g_prevFlash){
            cmd->impulse = 100;
        }
        g_prevFlash = fr.lY;

        // Phone: quick tap Left Grip + Y (<300ms) = B
        bool combo = fr.lGrip && fr.lY;
        if(combo && !g_prevPhoneTap){
            g_phoneHoldStart = GetTickCount();
            g_prevPhoneTap = true;
        }
        if(!combo && g_prevPhoneTap){
            DWORD held = GetTickCount() - g_phoneHoldStart;
            if(held < 300){
                SendCmd("phone"); // B key
            }
            g_prevPhoneTap = false;
        }
        // Drop item: Left grip + Right B hold (G)
        if(fr.rB && fr.lGrip){
            if(!g_prevDropItem){ SendCmd("drop"); }
            g_prevDropItem = true;
        } else { g_prevDropItem = false; }
        // Drop ammo: Left grip + Right A hold (J)
        if(fr.rA && fr.lGrip){
            if(!g_prevDropAmmo){ SendCmd("dropammo"); }
            g_prevDropAmmo = true;
        } else { g_prevDropAmmo = false; }

    } else if(pc==PlayerClass::Zombie){
        if(fr.lThumbPress) buttons |= IN_SPEED;
        if(fr.lY) buttons |= IN_ATTACK2;
        if(fr.rA) buttons |= IN_USE;
        if(fr.rB) buttons |= IN_RELOAD;
    }

    cmd->buttons = buttons;
}
