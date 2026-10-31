
#include "gameui.h"
#include <Windows.h>

typedef void* (*CreateInterfaceFn)(const char*, int*);

class IGameUI {
public:
    virtual void ActivateGameUI()=0;
    virtual void ActivateDemoUI()=0;
    virtual void ActivateMPStatsUI()=0;
    virtual bool HasExclusiveInput()=0;
    virtual void RunFrame()=0;
    virtual void ConnectToServer(const char*, int,int,int)=0;
    virtual void DisconnectFromServer()=0;
    virtual void HideGameUI()=0;
    virtual bool IsGameUIActive()=0;
};

class ConVar{ public: virtual ~ConVar(){}; bool GetBool(); };
class ICvar{ public: virtual void* _pad()=0; virtual ConVar* FindVar(const char*)=0; };

bool ConVar::GetBool(){
    void** vt = *(void***)this;
    typedef bool(__thiscall* Fn)(void*);
    __try{
        auto fn = (Fn)vt[13]; // SDK2007 GetBool index
        return fn(this);
    }__except(EXCEPTION_EXECUTE_HANDLER){ return false; }
}

static IGameUI* GetGameUI(){
    static IGameUI* s=nullptr; static bool tried=false;
    if(tried) return s; tried=true;
    HMODULE h=GetModuleHandleA("GameUI.dll"); if(!h) h=LoadLibraryA("GameUI.dll");
    if(!h) return nullptr;
    auto fac=(CreateInterfaceFn)GetProcAddress(h,"CreateInterface");
    if(!fac) return nullptr;
    s=(IGameUI*)fac("GameUI011",nullptr);
    if(!s) s=(IGameUI*)fac("GameUI010",nullptr);
    if(!s) s=(IGameUI*)fac("GameUI009",nullptr);
    return s;
}
static ConVar* FindVar(const char* name){
    static ICvar* cvar=nullptr; static bool tried=false;
    if(!tried){ tried=true;
        HMODULE h=GetModuleHandleA("materialsystem.dll"); if(!h) h=GetModuleHandleA("vstdlib.dll"); if(!h) h=GetModuleHandleA("engine.dll");
        if(h){ auto fac=(CreateInterfaceFn)GetProcAddress(h,"CreateInterface"); if(fac){ cvar=(ICvar*)fac("VEngineCvar004",nullptr); if(!cvar) cvar=(ICvar*)fac("VEngineCvar007",nullptr);} }
    }
    if(!cvar) return nullptr;
    __try{ return cvar->FindVar(name);}__except(EXCEPTION_EXECUTE_HANDLER){ return nullptr; }
}

bool IsPauseMenuOpen(){
    if(auto g=GetGameUI()){ __try{ if(g->IsGameUIActive()) return true; }__except(EXCEPTION_EXECUTE_HANDLER){} }
    if(auto cv=FindVar("gameui_ingame_menu_active")){ __try{ if(cv->GetBool()) return true; }__except(EXCEPTION_EXECUTE_HANDLER){} }
    if(auto cv=FindVar("gameui_paused")){ __try{ if(cv->GetBool()) return true; }__except(EXCEPTION_EXECUTE_HANDLER){} }
    return false;
}
int GetDesiredVrMode(){ return IsPauseMenuOpen() ? 0 : 1; }
