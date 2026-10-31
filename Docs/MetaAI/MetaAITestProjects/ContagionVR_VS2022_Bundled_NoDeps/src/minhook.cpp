
#include "MinHook.h"
#include <windows.h>
#include <vector>
#include <mutex>

struct HookEntry {
    LPVOID target = nullptr;
    LPVOID detour = nullptr;
    LPVOID trampoline = nullptr;
    BYTE original[16]{};
    SIZE_T patchSize = 5;
    bool enabled = false;
    bool created = false;
};

static std::vector<HookEntry> g_hooks;
static std::mutex g_mtx;
static bool g_initialized = false;

static bool IsExecutable(LPVOID p){
    if(!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if(!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0
        || (mbi.Protect & PAGE_EXECUTE_READ) != 0;
}

MH_STATUS WINAPI MH_Initialize(VOID){
    std::lock_guard<std::mutex> l(g_mtx);
    if(g_initialized) return MH_ERROR_ALREADY_INITIALIZED;
    g_initialized = true;
    g_hooks.reserve(32);
    return MH_OK;
}

MH_STATUS WINAPI MH_Uninitialize(VOID){
    std::lock_guard<std::mutex> l(g_mtx);
    if(!g_initialized) return MH_ERROR_NOT_INITIALIZED;
    // Disable all
    for(auto &h : g_hooks){
        if(h.enabled && h.target){
            DWORD old;
            VirtualProtect(h.target, h.patchSize, PAGE_EXECUTE_READWRITE, &old);
            memcpy(h.target, h.original, h.patchSize);
            VirtualProtect(h.target, h.patchSize, old, &old);
            h.enabled = false;
        }
        if(h.trampoline){
            VirtualFree(h.trampoline, 0, MEM_RELEASE);
        }
    }
    g_hooks.clear();
    g_initialized = false;
    return MH_OK;
}

MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID *ppOriginal){
    if(!pTarget || !pDetour) return MH_ERROR_NOT_EXECUTABLE;
    std::lock_guard<std::mutex> l(g_mtx);
    if(!g_initialized) return MH_ERROR_NOT_INITIALIZED;

    for(auto &h : g_hooks){
        if(h.target == pTarget) return MH_ERROR_ALREADY_CREATED;
    }

    // Only support x86 5-byte JMP for simplicity - enough for Source engine vtables and D3D
    // Allocate trampoline: original 5 bytes + jmp back
    BYTE* tramp = (BYTE*)VirtualAlloc(nullptr, 16, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if(!tramp) return MH_ERROR_MEMORY_ALLOC;

    // Save original
    HookEntry e;
    e.target = pTarget;
    e.detour = pDetour;
    e.patchSize = 5;
    memcpy(e.original, pTarget, 5);

    // Build trampoline: original 5 + jmp to target+5
    memcpy(tramp, pTarget, 5);
    tramp[5] = 0xE9; // JMP rel32
    DWORD relBack = (DWORD)((BYTE*)pTarget + 5 - (tramp + 10));
    *(DWORD*)(tramp+6) = relBack;

    e.trampoline = tramp;
    e.created = true;
    e.enabled = false;

    if(ppOriginal) *ppOriginal = tramp;

    g_hooks.push_back(e);
    return MH_OK;
}

static MH_STATUS EnableHookEntry(HookEntry &h, bool enable){
    if(enable){
        if(h.enabled) return MH_ERROR_ENABLED;
        DWORD old;
        if(!VirtualProtect(h.target, h.patchSize, PAGE_EXECUTE_READWRITE, &old)) return MH_ERROR_MEMORY_PROTECT;
        BYTE jmp[5];
        jmp[0] = 0xE9;
        DWORD rel = (DWORD)((BYTE*)h.detour - (BYTE*)h.target - 5);
        *(DWORD*)(jmp+1) = rel;
        memcpy(h.target, jmp, 5);
        VirtualProtect(h.target, h.patchSize, old, &old);
        FlushInstructionCache(GetCurrentProcess(), h.target, h.patchSize);
        h.enabled = true;
        return MH_OK;
    } else {
        if(!h.enabled) return MH_ERROR_DISABLED;
        DWORD old;
        if(!VirtualProtect(h.target, h.patchSize, PAGE_EXECUTE_READWRITE, &old)) return MH_ERROR_MEMORY_PROTECT;
        memcpy(h.target, h.original, h.patchSize);
        VirtualProtect(h.target, h.patchSize, old, &old);
        FlushInstructionCache(GetCurrentProcess(), h.target, h.patchSize);
        h.enabled = false;
        return MH_OK;
    }
}

MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget){
    std::lock_guard<std::mutex> l(g_mtx);
    if(!g_initialized) return MH_ERROR_NOT_INITIALIZED;
    if(pTarget == MH_ALL_HOOKS){
        MH_STATUS last = MH_OK;
        for(auto &h : g_hooks){
            if(!h.enabled){
                MH_STATUS s = EnableHookEntry(h, true);
                if(s!=MH_OK) last = s;
            }
        }
        return last;
    }
    for(auto &h : g_hooks){
        if(h.target == pTarget){
            return EnableHookEntry(h, true);
        }
    }
    return MH_ERROR_NOT_CREATED;
}

MH_STATUS WINAPI MH_DisableHook(LPVOID pTarget){
    std::lock_guard<std::mutex> l(g_mtx);
    if(!g_initialized) return MH_ERROR_NOT_INITIALIZED;
    if(pTarget == MH_ALL_HOOKS){
        MH_STATUS last = MH_OK;
        for(auto &h : g_hooks){
            if(h.enabled){
                MH_STATUS s = EnableHookEntry(h, false);
                if(s!=MH_OK) last = s;
            }
        }
        return last;
    }
    for(auto &h : g_hooks){
        if(h.target == pTarget){
            return EnableHookEntry(h, false);
        }
    }
    return MH_ERROR_NOT_CREATED;
}

MH_STATUS WINAPI MH_RemoveHook(LPVOID pTarget){
    std::lock_guard<std::mutex> l(g_mtx);
    if(!g_initialized) return MH_ERROR_NOT_INITIALIZED;
    for(auto it=g_hooks.begin(); it!=g_hooks.end(); ++it){
        if(it->target==pTarget){
            if(it->enabled){
                EnableHookEntry(*it, false);
            }
            if(it->trampoline) VirtualFree(it->trampoline,0,MEM_RELEASE);
            g_hooks.erase(it);
            return MH_OK;
        }
    }
    return MH_ERROR_NOT_CREATED;
}

MH_STATUS WINAPI MH_CreateHookApi(LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID *ppOriginal){
    HMODULE h = GetModuleHandleW(pszModule);
    if(!h) return MH_ERROR_MODULE_NOT_FOUND;
    LPVOID p = (LPVOID)GetProcAddress(h, pszProcName);
    if(!p) return MH_ERROR_FUNCTION_NOT_FOUND;
    return MH_CreateHook(p, pDetour, ppOriginal);
}
MH_STATUS WINAPI MH_CreateHookApiEx(LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID *ppOriginal, LPVOID *ppTarget){
    HMODULE h = GetModuleHandleW(pszModule);
    if(!h) return MH_ERROR_MODULE_NOT_FOUND;
    LPVOID p = (LPVOID)GetProcAddress(h, pszProcName);
    if(!p) return MH_ERROR_FUNCTION_NOT_FOUND;
    if(ppTarget) *ppTarget = p;
    return MH_CreateHook(p, pDetour, ppOriginal);
}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID pTarget){ return MH_EnableHook(pTarget); }
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID pTarget){ return MH_DisableHook(pTarget); }
MH_STATUS WINAPI MH_ApplyQueued(VOID){ return MH_OK; }
const char* WINAPI MH_StatusToString(MH_STATUS status){
    switch(status){
        case MH_OK: return "MH_OK";
        case MH_ERROR_ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
        case MH_ERROR_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case MH_ERROR_ALREADY_CREATED: return "ALREADY_CREATED";
        case MH_ERROR_NOT_CREATED: return "NOT_CREATED";
        case MH_ERROR_ENABLED: return "ENABLED";
        case MH_ERROR_DISABLED: return "DISABLED";
        case MH_ERROR_NOT_EXECUTABLE: return "NOT_EXECUTABLE";
        case MH_ERROR_UNSUPPORTED_FUNCTION: return "UNSUPPORTED_FUNCTION";
        case MH_ERROR_MEMORY_ALLOC: return "MEMORY_ALLOC";
        case MH_ERROR_MEMORY_PROTECT: return "MEMORY_PROTECT";
        case MH_ERROR_MODULE_NOT_FOUND: return "MODULE_NOT_FOUND";
        case MH_ERROR_FUNCTION_NOT_FOUND: return "FUNCTION_NOT_FOUND";
        default: return "UNKNOWN";
    }
}
