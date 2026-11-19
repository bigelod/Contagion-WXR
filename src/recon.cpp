#include "recon.h"
#include "log.h"

#include <windows.h>
#include <psapi.h>
#include <cstdio>

#pragma comment(lib, "psapi.lib")

namespace cwxr {
namespace {

// ---------------------------------------------------------------------------
// Memory probing. VirtualQuery rather than SEH: no __try, so this file stays
// free of the "cannot use __try in a function that requires unwinding" trap,
// and a wild pointer costs a log line instead of a crash.
// ---------------------------------------------------------------------------
bool IsReadable(const void* p, size_t n) {
    if (!p || n == 0) return false;
    const BYTE* q = (const BYTE*)p;
    const BYTE* end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(q, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & PAGE_GUARD) return false;
        DWORD prot = mbi.Protect & 0xFF;
        if (prot == PAGE_NOACCESS || prot == PAGE_EXECUTE) return false;
        const BYTE* next = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
        if (next <= q) return false;
        q = next;
    }
    return true;
}

bool ReadU32(const void* p, unsigned* out) {
    if (!IsReadable(p, 4)) return false;
    *out = *(const unsigned*)p;
    return true;
}

// ---------------------------------------------------------------------------
// Handshake directory. Owner's decision, 2026-11-17:
//   Z:\tmp exists -> Z:\tmp\xr     (real hardware)
//   otherwise     -> D:\xrtemp     (desktop test)
// The test is on the PARENT: Z:\ alone is not a device marker, and under Wine
// GetFileAttributesA("Z:\\") is unreliable (SIBLING_PORTS.md 3.3).
// ---------------------------------------------------------------------------
void ReconHandshake() {
    Log("-- handshake --");
    DWORD ztmp = GetFileAttributesA("Z:\\tmp");
    bool onDevice = (ztmp != INVALID_FILE_ATTRIBUTES) && (ztmp & FILE_ATTRIBUTE_DIRECTORY);
    Log("Z:\\tmp exists : %s", onDevice ? "yes (device)" : "no (desktop fallback)");

    const char* dir = onDevice ? "Z:\\tmp\\xr" : "D:\\xrtemp";
    CreateDirectoryA("D:\\xrtemp", nullptr);           // harmless on device
    CreateDirectoryA("Z:\\tmp\\xr", nullptr);
    DWORD attrs = GetFileAttributesA(dir);
    Log("chosen dir   : %s (%s)", dir,
        (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            ? "present" : "could not create");

    const char* systemPath = onDevice ? "Z:\\tmp\\xr\\system" : "D:\\xrtemp\\system";
    DWORD sysAttrs = GetFileAttributesA(systemPath);
    Log("system file  : %s", (sysAttrs != INVALID_FILE_ATTRIBUTES) ? "present" : "absent");
}

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------
void ReconEnvironment() {
    Log("-- environment --");
    Log("screen       : %d x %d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    const char* wine = nullptr;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32) {
        typedef const char* (__cdecl *WineGetVersionFn)();
        WineGetVersionFn fn = (WineGetVersionFn)GetProcAddress(k32, "wine_get_version");
        if (fn) wine = fn();
    }
    Log("wine         : %s", wine ? wine : "no");

    char win[MAX_PATH] = {0};
    if (GetWindowsDirectoryA(win, MAX_PATH)) Log("windir       : %s", win);
}

// ---------------------------------------------------------------------------
// Windows belonging to this process. The inherited WndProc hook looks for a
// class containing "Valve001", which exists in none of the shipped binaries
// (BINARY_RECON.md 6). This is how we find out what to look for instead.
// ---------------------------------------------------------------------------
BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;

    char cls[128] = {0};
    char title[160] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));

    RECT rc = {0};
    GetClientRect(hwnd, &rc);

    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
    LONG_PTR ex    = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    bool visible   = IsWindowVisible(hwnd) != 0;

    Log("window       : hwnd=0x%08X class='%s' visible=%d client=%dx%d style=0x%08X ex=0x%08X title='%s'",
        (unsigned)(uintptr_t)hwnd, cls, visible ? 1 : 0,
        (int)(rc.right - rc.left), (int)(rc.bottom - rc.top),
        (unsigned)style, (unsigned)ex, title);
    return TRUE;
}

void ReconWindows() {
    Log("-- windows in this process --");
    EnumWindows(EnumWndProc, 0);

    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        char cls[128] = {0};
        GetClassNameA(fg, cls, sizeof(cls));
        Log("foreground   : hwnd=0x%08X class='%s' ours=%s",
            (unsigned)(uintptr_t)fg, cls, (pid == GetCurrentProcessId()) ? "yes" : "no");
    }
}

// ---------------------------------------------------------------------------
// Loaded modules
// ---------------------------------------------------------------------------
void ReconModules() {
    Log("-- modules --");
    HMODULE mods[256];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        Log("EnumProcessModules failed (%lu)", GetLastError());
        return;
    }
    DWORD count = needed / sizeof(HMODULE);
    if (count > 256) count = 256;
    for (DWORD i = 0; i < count; ++i) {
        char path[MAX_PATH] = {0};
        MODULEINFO mi = {0};
        GetModuleFileNameExA(GetCurrentProcess(), mods[i], path, MAX_PATH);
        GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi));
        Log("module       : 0x%08X size=0x%08X %s",
            (unsigned)(uintptr_t)mi.lpBaseOfDll, (unsigned)mi.SizeOfImage, path);
    }
}

// ---------------------------------------------------------------------------
// Interfaces. CreateInterface with a wrong name returns null and is harmless;
// with a right name it proves the interface exists in this build.
// ---------------------------------------------------------------------------
typedef void* (__cdecl *CreateInterfaceFn)(const char* name, int* returnCode);

CreateInterfaceFn GetFactory(const char* module) {
    HMODULE h = GetModuleHandleA(module);
    if (!h) return nullptr;
    return (CreateInterfaceFn)GetProcAddress(h, "CreateInterface");
}

void ProbeInterface(const char* module, const char* iface) {
    CreateInterfaceFn fn = GetFactory(module);
    if (!fn) {
        Log("iface        : %-22s in %-14s module not loaded", iface, module);
        return;
    }
    int rc = -1;
    void* p = fn(iface, &rc);
    Log("iface        : %-22s in %-14s -> 0x%08X (rc=%d)",
        iface, module, (unsigned)(uintptr_t)p, rc);
}

void ReconInterfaces() {
    Log("-- interfaces (verified strings: BINARY_RECON.md 4) --");

    // The inherited code asks for VEngineClient014 / VEngineCvar004, neither of
    // which exists. These are the names measured in the shipped binaries.
    //
    // Run 1 corrected two module guesses: VEngineRenderView013 and
    // VGUI_Surface031 returned null in client.dll, and VEngineCvar007 only
    // answered in vstdlib.dll. Probe the module that registers them.
    ProbeInterface("client.dll",        "VClient016");
    ProbeInterface("engine.dll",        "VEngineRenderView013");
    ProbeInterface("vguimatsurface.dll","VGUI_Surface031");
    ProbeInterface("vstdlib.dll",       "VEngineCvar007");
    ProbeInterface("engine.dll",        "VEngineClient013");
    ProbeInterface("GameUI.dll",        "GameUI011");
    ProbeInterface("vstdlib.dll",       "VEngineModel016");
    ProbeInterface("client.dll",        "VEngineRenderView013");  // null expected; keep as proof

    // g_pCVar is an exported global in every module of interest, so ICvar does
    // not need to be guessed from a version string.
    const char* mods[] = { "client.dll", "engine.dll", "vstdlib.dll", "GameUI.dll" };
    for (int i = 0; i < 4; ++i) {
        HMODULE h = GetModuleHandleA(mods[i]);
        if (!h) continue;
        void** pp = (void**)GetProcAddress(h, "g_pCVar");
        unsigned value = 0;
        if (pp && ReadU32(pp, &value)) {
            Log("g_pCVar      : %-14s = 0x%08X", mods[i], value);
        }
    }
}

// ---------------------------------------------------------------------------
// The two anchor globals from BINARY_RECON.md 8.
//
// They are recorded as absolute VAs for a client.dll loaded at its preferred
// base 0x10000000. If the module was relocated, the VA is wrong - so compute
// from the real base and log both. That is the difference between a finding
// and a rumour.
// ---------------------------------------------------------------------------
const unsigned kClientPreferredBase = 0x10000000u;

struct Anchor {
    unsigned offset;
    const char* label;
};

const Anchor kAnchors[] = {
    { 0x7B5F98u, "0x107B5F98 (identified: g_pViewRender)" },
    { 0x89D0CCu, "0x1089D0CC (unidentified; six calls at vtable index 21)" },
    { 0x6B1F88u, "0x106B1F88 (unidentified; first match of the 38h pattern)" },
};

void DumpAnchor(HMODULE client, const Anchor& a) {
    uintptr_t base = (uintptr_t)client;
    uintptr_t slot = base + a.offset;

    Log("anchor       : %s", a.label);
    Log("  slot       : base+0x%X = 0x%08X (base=0x%08X, preferred=0x%08X%s)",
        a.offset, (unsigned)slot, (unsigned)base, kClientPreferredBase,
        (base == kClientPreferredBase) ? "" : ", RELOCATED");

    unsigned obj = 0;
    if (!ReadU32((const void*)slot, &obj)) {
        Log("  value      : unreadable");
        return;
    }
    Log("  value      : 0x%08X%s", obj, obj ? "" : " (null - not constructed yet)");
    if (!obj) return;

    unsigned vt = 0;
    if (!ReadU32((const void*)obj, &vt) || !vt) {
        Log("  vtable     : unreadable");
        return;
    }
    Log("  vtable     : 0x%08X", vt);

    // x86 MSVC puts the RTTI complete-object-locator pointer immediately before
    // the first virtual function. Resolving it names the class without a
    // disassembler: COL -> pTypeDescriptor -> mangled name. Run 1 proved this
    // works on this binary and named all three anchors.
    unsigned rtti = 0;
    if (ReadU32((const void*)((uintptr_t)vt - 4), &rtti) && rtti) {
        Log("  rtti loc   : 0x%08X", rtti);

        unsigned td = 0;
        if (ReadU32((const void*)((uintptr_t)rtti + 12), &td) && td) {
            // TypeDescriptor: vfptr, spare, then the mangled name.
            const char* name = (const char*)(uintptr_t)(td + 8);
            if (IsReadable(name, 96)) {
                char buf[128];
                int i = 0;
                while (i < 96 && name[i]) { buf[i] = name[i]; ++i; }
                buf[i] = 0;
                Log("  class      : %s", buf);
            }
        }

        // Class hierarchy: CHD { sig, attrs, count, pBaseClassArray }
        unsigned chd = 0;
        if (ReadU32((const void*)((uintptr_t)rtti + 16), &chd) && chd) {
            unsigned count = 0, arr = 0;
            if (ReadU32((const void*)(uintptr_t)(chd + 8), &count) &&
                ReadU32((const void*)(uintptr_t)(chd + 12), &arr) && count && count < 16) {
                for (unsigned bi = 0; bi < count && bi < 8; ++bi) {
                    unsigned bcd = 0;
                    if (!ReadU32((const void*)(uintptr_t)(arr + bi * 4), &bcd) || !bcd) break;
                    unsigned btd = 0;
                    if (!ReadU32((const void*)(uintptr_t)bcd, &btd) || !btd) break;
                    const char* bn = (const char*)(uintptr_t)(btd + 8);
                    if (!IsReadable(bn, 96)) break;
                    char buf[128];
                    int i = 0;
                    while (i < 96 && bn[i]) { buf[i] = bn[i]; ++i; }
                    buf[i] = 0;
                    Log("    base[%u]  : %s", bi, buf);
                }
            }
        }
    }

    // 64 entries, and keep going past an unreadable dword: run 1's dump stopped
    // at 28 while call sites clearly reference indices up to 34, because the
    // vtable crosses a page boundary.
    char line[1024];
    int pos = 0;
    for (int i = 0; i < 64; ++i) {
        unsigned fn = 0;
        if (!ReadU32((const void*)((uintptr_t)vt + i * 4), &fn)) {
            pos += _snprintf_s(line + pos, sizeof(line) - pos, _TRUNCATE, "%02d:?? ", i);
        } else {
            pos += _snprintf_s(line + pos, sizeof(line) - pos, _TRUNCATE, "%02d:0x%08X ", i, fn);
        }
        if (pos > 900) {
            Log("  entries    : %s", line);
            pos = 0;
        }
    }
    if (pos > 0) Log("  entries    : %s", line);
}

void ReconAnchors() {
    Log("-- client.dll anchors --");
    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) {
        Log("client.dll not loaded yet");
        return;
    }
    for (int i = 0; i < (int)(sizeof(kAnchors) / sizeof(kAnchors[0])); ++i) {
        DumpAnchor(client, kAnchors[i]);
    }
}

} // namespace

void ReconDump() {
    Log("================ recon dump ================");
    ReconEnvironment();
    ReconHandshake();
    ReconWindows();
    ReconModules();
    ReconInterfaces();
    ReconAnchors();
    Log("================ end recon =================");
}

} // namespace cwxr
