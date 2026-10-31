
#include "voice_wheel.h"
#include "gameui.h"
#include <Windows.h>
#include <cmath>
#include <fstream>
#include <string>

VoiceOption g_voiceOptions[8] = {
    {"Help!","vocalize help"},
    {"Follow Me","vocalize followme"},
    {"Need Ammo","vocalize needammo"},
    {"Medic!","vocalize needhealth"},
    {"Thanks","vocalize thanks"},
    {"Sorry","vocalize sorry"},
    {"Zombie!","vocalize warnzombie"},
    {"Go! Go! Go!","vocalize go"},
};
const int g_voiceCount = 8;

static bool g_active = false;
static int g_selected = -1;
static DWORD g_holdStart = 0;
static bool g_wasCombo = false;

typedef void* (*CreateInterfaceFn)(const char*, int*);
class IVEngineClient{ public: virtual void _pad0()=0; virtual void ClientCmd(const char*)=0; };
static IVEngineClient* GetEngine(){
    static IVEngineClient* eng=nullptr; static bool tried=false;
    if(tried) return eng; tried=true;
    HMODULE h=GetModuleHandleA("engine.dll"); if(!h) return nullptr;
    auto fac=(CreateInterfaceFn)GetProcAddress(h,"CreateInterface");
    if(!fac) return nullptr;
    eng=(IVEngineClient*)fac("VEngineClient014",nullptr);
    if(!eng) eng=(IVEngineClient*)fac("VEngineClient013",nullptr);
    return eng;
}
static void SendCmd(const char* c){
    if(auto e=GetEngine()){ __try{ e->ClientCmd(c); }__except(EXCEPTION_EXECUTE_HANDLER){} }
}

void VoiceWheel_Init(){
    // Try load contagion_vr_voices.txt from game dir or Z:/tmp/xr/
    const char* paths[] = {"contagion_vr_voices.txt","Z:\\tmp\\xr\\voices.txt", nullptr};
    for(int pi=0; paths[pi]; ++pi){
        std::ifstream f(paths[pi]);
        if(!f) continue;
        std::string line;
        int idx=0;
        while(std::getline(f,line) && idx<8){
            if(line.empty() || line[0]=='#' || line[0]==';') continue;
            size_t eq = line.find('=');
            if(eq==std::string::npos) continue;
            std::string label = line.substr(0,eq);
            std::string cmd = line.substr(eq+1);
            // trim
            auto trim = [](std::string s){ s.erase(0,s.find_first_not_of(" \t\r\n")); s.erase(s.find_last_not_of(" \t\r\n")+1); return s; };
            label = trim(label); cmd = trim(cmd);
            if(label.empty()||cmd.empty()) continue;
            strncpy_s(g_voiceOptions[idx].label, label.c_str(), sizeof(g_voiceOptions[idx].label)-1);
            strncpy_s(g_voiceOptions[idx].command, cmd.c_str(), sizeof(g_voiceOptions[idx].command)-1);
            idx++;
        }
        break;
    }
}

static float ApplyDeadzone(float v, float dz){ if(fabs(v)<dz) return 0; return v; }

void VoiceWheel_Update(const XrFrame& fr, bool menuOpen){
    if(menuOpen){
        g_active=false; g_selected=-1; g_wasCombo=false; return;
    }
    bool combo = fr.lGrip && fr.lY; // Left grip + Y hold = voice wheel per your spec

    DWORD now = GetTickCount();

    if(combo && !g_wasCombo){
        // combo started
        g_holdStart = now;
        g_wasCombo = true;
    }

    if(combo){
        DWORD held = now - g_holdStart;
        if(held > 300){ // 300ms hold -> activate wheel (quick tap = phone handled elsewhere)
            g_active = true;
        }
        // selection via right thumbstick
        float rx = fr.rightStick.x;
        float ry = fr.rightStick.y;
        float mag = sqrtf(rx*rx + ry*ry);
        if(mag > 0.35f){
            // angle: 0 deg at top (up), clockwise
            float deg = atan2f(rx, ry) * 57.29578f; // ry up, rx right
            if(deg < 0) deg += 360.0f;
            int slice = (int)((deg + 22.5f)/45.0f) % 8;
            g_selected = slice;
        } else {
            // try right hand laser: use right hand forward vs HMD forward on yaw plane
            if(fr.right.valid && fr.hmd.valid){
                // forward vectors
                Vec3 rf = QuatRotate(fr.right.rot, {0,0,-1});
                Vec3 hf = QuatRotate(fr.hmd.rot, {0,0,-1});
                // project to horizontal plane (ignore Y up)
                float rYaw = atan2f(rf.x, -rf.z);
                float hYaw = atan2f(hf.x, -hf.z);
                float deltaYaw = rYaw - hYaw; // -PI to PI
                while(deltaYaw > 3.14159f) deltaYaw -= 6.28319f;
                while(deltaYaw < -3.14159f) deltaYaw += 6.28319f;
                float deg = deltaYaw * 57.29578f;
                // map -180..180 to 0..360 with 0 top? Let's map: 0 = forward (top of wheel is forward)
                // deltaYaw 0 = pointing forward -> select top slice 0
                // For wheel, top = 0, right = 90 etc. deltaYaw = 0 -> top
                float wheelDeg = deg + 180.0f; // shift? Let's try: forward 0 deg = top
                // Actually we want forward = 0, right = 90
                // Our earlier thumbstick mapping 0 = up (forward). So same.
                // deltaYaw 0 should be 0 deg
                wheelDeg = deg; // -180..180
                if(wheelDeg < 0) wheelDeg += 360.0f;
                // only select if hand is somewhat forward pointing away from body, not down
                if(fabs(deltaYaw) < 2.0f){ // within ~115 deg forward cone
                    int slice = (int)((wheelDeg + 22.5f)/45.0f) % 8;
                    g_selected = slice;
                }
            }
        }
    } else {
        if(g_active){
            // released -> trigger
            if(g_selected >=0 && g_selected < g_voiceCount){
                SendCmd(g_voiceOptions[g_selected].command);
            }
        }
        g_active = false;
        g_selected = -1;
        g_wasCombo = false;
    }
}

bool VoiceWheel_IsActive(){ return g_active; }
int VoiceWheel_GetSelected(){ return g_selected; }
bool VoiceWheel_ShouldBlockGameInput(){ return g_active; }
void VoiceWheel_Draw(){ /* actual draw done in d3d9 hook */ }
