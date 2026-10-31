
#include "wndproc.h"
#include "gameui.h"
#include "input.h"
#include <Windows.h>

static WNDPROC g_oldProc = nullptr;
static HWND g_hWnd = nullptr;

static LRESULT CALLBACK NewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    bool isMenu = IsPauseMenuOpen();
    if(!isMenu){
        // When in VR gameplay, block default mouse/kb except ESC
        if(Input_ShouldBlockKeyboardMouse()){
            switch(msg){
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                    if(wParam==VK_ESCAPE){
                        break; // allow ESC
                    }
                    return 0; // swallow
                case WM_CHAR:
                case WM_DEADCHAR:
                case WM_SYSCHAR:
                    return 0;
                case WM_MOUSEMOVE:
                case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
                case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
                case WM_MOUSEWHEEL:
                case WM_MOUSEHWHEEL:
                case WM_XBUTTONDOWN: case WM_XBUTTONUP:
                    return 0;
                case WM_INPUT:
                    return 0;
                default: break;
            }
        }
    }
    return CallWindowProc(g_oldProc, hWnd, msg, wParam, lParam);
}

static BOOL CALLBACK EnumWnd(HWND hWnd, LPARAM lp){
    DWORD pid; GetWindowThreadProcessId(hWnd, &pid);
    if(pid != GetCurrentProcessId()) return TRUE;
    char cls[64]; GetClassNameA(hWnd, cls, 64);
    // Source window class is Valve001
    if(strstr(cls,"Valve001")){
        g_hWnd = hWnd;
        return FALSE;
    }
    return TRUE;
}

void WndProcHook_Init(){
    // Find main window
    EnumWindows(EnumWnd, 0);
    if(!g_hWnd) g_hWnd = GetForegroundWindow();
    if(!g_hWnd) return;
    g_oldProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)NewWndProc);
}

void WndProcHook_Shutdown(){
    if(g_hWnd && g_oldProc){
        SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)g_oldProc);
    }
}
