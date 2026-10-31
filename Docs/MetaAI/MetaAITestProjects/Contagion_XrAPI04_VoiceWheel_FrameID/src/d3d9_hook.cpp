
#include "d3d9_hook.h"
#include "voice_wheel.h"
#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <MinHook.h>
#include <cmath>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

typedef HRESULT(__stdcall* EndSceneFn)(IDirect3DDevice9*);
typedef HRESULT(__stdcall* ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static EndSceneFn oEndScene = nullptr;
static ResetFn oReset = nullptr;
static IDirect3DDevice9* g_pDevice = nullptr;
static ID3DXLine* g_pLine = nullptr;
static ID3DXFont* g_pFont = nullptr;
static ID3DXFont* g_pFontSmall = nullptr;
static bool g_bInit = false;

static HRESULT __stdcall hkReset(IDirect3DDevice9* pDev, D3DPRESENT_PARAMETERS* pParams){
    if(g_pLine){ g_pLine->OnLostDevice(); }
    if(g_pFont){ g_pFont->OnLostDevice(); }
    if(g_pFontSmall){ g_pFontSmall->OnLostDevice(); }
    HRESULT hr = oReset(pDev, pParams);
    if(SUCCEEDED(hr)){
        if(g_pLine) g_pLine->OnResetDevice();
        if(g_pFont) g_pFont->OnResetDevice();
        if(g_pFontSmall) g_pFontSmall->OnResetDevice();
    }
    return hr;
}

static void DrawCircle(IDirect3DDevice9* dev, float cx, float cy, float r, D3DCOLOR col, float thickness=2.0f){
    if(!g_pLine) return;
    const int segs = 64;
    D3DXVECTOR2 pts[65];
    for(int i=0;i<=segs;i++){
        float a = (float)i/segs * 6.28319f;
        pts[i] = D3DXVECTOR2(cx + cosf(a)*r, cy + sinf(a)*r);
    }
    g_pLine->SetWidth(thickness);
    g_pLine->Draw(pts, segs+1, col);
}

static void DrawLine(IDirect3DDevice9* dev, float x1,float y1,float x2,float y2, D3DCOLOR col, float w=2.0f){
    if(!g_pLine) return;
    D3DXVECTOR2 pts[2] = { D3DXVECTOR2(x1,y1), D3DXVECTOR2(x2,y2) };
    g_pLine->SetWidth(w);
    g_pLine->Draw(pts,2,col);
}

static void DrawFilledSlice(IDirect3DDevice9* dev, float cx, float cy, float r, float a1, float a2, D3DCOLOR col){
    struct VERT { float x,y,z,rhw; D3DCOLOR c; };
    const int steps = 16;
    VERT verts[32];
    verts[0] = {cx,cy,0,1,col};
    for(int i=0;i<=steps;i++){
        float t = (float)i/steps;
        float a = a1 + (a2-a1)*t;
        verts[i+1] = {cx + cosf(a)*r, cy + sinf(a)*r, 0,1,col};
    }
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetTexture(0,nullptr);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, steps, verts, sizeof(VERT));
}

static void DrawText(ID3DXFont* font, float x, float y, D3DCOLOR col, const char* txt, bool centered=true){
    if(!font) return;
    RECT rc; rc.left = (LONG)(x - (centered?200:0)); rc.top=(LONG)(y-10); rc.right=rc.left+400; rc.bottom=rc.top+20;
    font->DrawTextA(nullptr, txt, -1, &rc, DT_CENTER|DT_NOCLIP, col);
}

static HRESULT __stdcall hkEndScene(IDirect3DDevice9* pDev){
    if(!g_bInit){
        g_pDevice = pDev;
        D3DXCreateLine(pDev, &g_pLine);
        D3DXCreateFontA(pDev, 22, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Arial", &g_pFont);
        D3DXCreateFontA(pDev, 16, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Arial", &g_pFontSmall);
        g_bInit = true;
    }

    if(VoiceWheel_IsActive()){
        D3DVIEWPORT9 vp; pDev->GetViewport(&vp);
        float cx = vp.Width * 0.5f;
        float cy = vp.Height * 0.5f;
        float r = 180.0f;
        float inner = 45.0f;

        int sel = VoiceWheel_GetSelected();

        // dim background
        DrawFilledSlice(pDev, cx, cy, vp.Width, 0, 6.28319f, D3DCOLOR_ARGB(80,0,0,0));

        // slices background
        for(int i=0;i<8;i++){
            float a1 = (i*45.0f - 90.0f - 22.5f) * 3.14159f/180.0f;
            float a2 = (i*45.0f - 90.0f + 22.5f) * 3.14159f/180.0f;
            D3DCOLOR col = (i==sel) ? D3DCOLOR_ARGB(160, 60,180,255) : D3DCOLOR_ARGB(90, 40,40,40);
            DrawFilledSlice(pDev, cx, cy, r, a1, a2, col);
        }

        // circles
        DrawCircle(pDev, cx, cy, r, D3DCOLOR_ARGB(200,200,200,200), 2.5f);
        DrawCircle(pDev, cx, cy, inner, D3DCOLOR_ARGB(200,200,200,200), 2.0f);

        // lines
        for(int i=0;i<8;i++){
            float a = (i*45.0f - 90.0f - 22.5f) * 3.14159f/180.0f;
            float x1 = cx + cosf(a)*inner;
            float y1 = cy + sinf(a)*inner;
            float x2 = cx + cosf(a)*r;
            float y2 = cy + sinf(a)*r;
            DrawLine(pDev, x1,y1,x2,y2, D3DCOLOR_ARGB(180,200,200,200), 2.0f);
        }

        // labels + laser dot
        extern VoiceOption g_voiceOptions[8];
        for(int i=0;i<8;i++){
            float a = (i*45.0f - 90.0f) * 3.14159f/180.0f;
            float lx = cx + cosf(a)*(r*0.68f);
            float ly = cy + sinf(a)*(r*0.68f);
            D3DCOLOR txtCol = (i==sel) ? D3DCOLOR_ARGB(255,255,255,0) : D3DCOLOR_ARGB(220,220,220,220);
            DrawText(g_pFont, lx, ly, txtCol, g_voiceOptions[i].label, true);
            // command small
            if(i==sel){
                DrawText(g_pFontSmall, lx, ly+18, D3DCOLOR_ARGB(180,200,200,200), g_voiceOptions[i].command, true);
            }
        }

        // center instruction
        DrawText(g_pFontSmall, cx, cy-6, D3DCOLOR_ARGB(200,255,255,255), "Voice Wheel - Right stick or point right hand", true);
        if(sel>=0) DrawText(g_pFontSmall, cx, cy+8, D3DCOLOR_ARGB(200,255,255,0), "Release Left Grip+Y to vocalize", true);
        else DrawText(g_pFontSmall, cx, cy+8, D3DCOLOR_ARGB(150,255,255,255), "Point to select", true);
    }

    return oEndScene(pDev);
}

void D3D9Hook_Init(){
    // Create dummy device to get vtable
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if(!pD3D) return;
    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAP_EFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetDesktopWindow();
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* pDummy = nullptr;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummy);
    if(FAILED(hr)){
        hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummy);
    }
    if(!pDummy){
        pD3D->Release();
        return;
    }
    void** vtable = *(void***)pDummy;
    void* endScene = vtable[42];
    void* reset = vtable[16];

    MH_CreateHook(endScene, &hkEndScene, (void**)&oEndScene);
    MH_CreateHook(reset, &hkReset, (void**)&oReset);
    MH_EnableHook(endScene);
    MH_EnableHook(reset);

    pDummy->Release();
    pD3D->Release();
}

void D3D9Hook_Shutdown(){
    if(g_pLine){ g_pLine->Release(); g_pLine=nullptr; }
    if(g_pFont){ g_pFont->Release(); g_pFont=nullptr; }
    if(g_pFontSmall){ g_pFontSmall->Release(); g_pFontSmall=nullptr; }
}
