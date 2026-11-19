// d3d9hook.cpp - per-frame access to the game's own device.
//
// How the device is obtained, and why it is done this way.
//
// The game never hands its IDirect3DDevice9 to anything. Every attempt to find
// it after the fact failed on 2026-11-18: scanning memory found dispatch tables,
// and patching d3d9's shared IDirect3D9 vtable killed the game with 0xC0000005
// before the hook was ever entered (NIGHT_REPORT.md 2.3). Guessing at a table
// cannot be made safe, because an entry that takes no arguments and one that
// takes seven look identical until they are called.
//
// So we own the interface instead of searching for it:
//
//   1. MinHook d3d9.dll!Direct3DCreate9 (and ...Ex) before the game starts.
//   2. Return a thin wrapper of our own rather than the real object. Every
//      method forwards to the real one; CreateDevice/CreateDeviceEx are ours.
//   3. When the game calls CreateDevice through the wrapper we are handed the
//      real IDirect3DDevice9 in `out`, and patch that device's own vtable
//      entries for Present/BeginScene/EndScene.
//
// Nothing is patched before it is known to be a device, and QueryInterface
// returns the wrapper rather than the inner object - otherwise a QI for
// IDirect3D9Ex would hand the game an interface that bypasses us.
//
// The wrapper's methods are __stdcall, because COM methods are __stdcall on
// x86. The detours in the earlier builds were __fastcall (this in ECX), which
// cannot match a stdcall callee and is one more reason none of them fired
// cleanly.

#include <windows.h>
#include <d3d9.h>
#include <psapi.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

#include "d3d9hook.h"
#include "log.h"
#include "xrapi.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "psapi.lib")

namespace cwxr {
namespace {

const int kIdxCreateDevice = 16;   // IDirect3D9
const int kIdxPresent      = 17;   // IDirect3DDevice9
const int kIdxBeginScene   = 41;
const int kIdxEndScene     = 42;

const int kMaxDevices = 4;

typedef IDirect3D9* (__stdcall *Direct3DCreate9Fn)(UINT);
typedef HRESULT (__stdcall *Direct3DCreate9ExFn)(UINT, IDirect3D9Ex**);

typedef HRESULT (__stdcall *PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*,
                                       HWND, const RGNDATA*);
typedef HRESULT (__stdcall *EndSceneFn)(IDirect3DDevice9*);
typedef HRESULT (__stdcall *BeginSceneFn)(IDirect3DDevice9*);

Direct3DCreate9Fn   g_origCreate9   = nullptr;
Direct3DCreate9ExFn g_origCreate9Ex = nullptr;

volatile LONG g_present   = 0;
volatile LONG g_end       = 0;
volatile LONG g_begin     = 0;
volatile LONG g_hooked    = 0;
volatile LONG g_ctorHooked = 0;
volatile LONG g_ctorCalls = 0;
volatile LONG g_devices   = 0;
volatile LONG g_bbW       = 0;
volatile LONG g_bbH       = 0;

DWORD g_lastStats = 0;
DWORD g_lastRequestCheck = 0;
bool  g_loggedRtMismatch = false;
bool  g_loggedClearFail = false;
bool  g_loggedFirstPresent = false;
bool  g_loggedFault = false;

const int kSyncSize = 10;

void ExeDir(char* out, size_t cap) {
    out[0] = 0;
    char exe[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    for (int i = (int)n - 1; i >= 0; --i) {
        if (exe[i] == '\\' || exe[i] == '/') { exe[i] = 0; break; }
    }
    strncpy_s(out, cap, exe, _TRUNCATE);
}

bool ReadableRange(const void* p, size_t n) {
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

// ---------------------------------------------------------------------------
// Live vtable patching. The device's table belongs to that device, so an entry
// written here is only ever called with that device's own signature.
// ---------------------------------------------------------------------------
bool PatchSlot(void** vt, int idx, void* fn, void** oldOut) {
    if (!vt) return false;
    DWORD old = 0;
    void** slot = &vt[idx];
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        Log("d3d9        : VirtualProtect(slot %d) failed (%lu)", idx, GetLastError());
        return false;
    }
    if (oldOut && !*oldOut) *oldOut = *slot;
    *slot = fn;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// ---------------------------------------------------------------------------
// Sync pixel: a Clear of a 10x10 rect, not a draw. No state to save or restore.
// Echo HMD_SYNC exactly; draw nothing when no packet has arrived.
// ---------------------------------------------------------------------------
bool CurrentRtIsBackBuffer(IDirect3DDevice9* dev) {
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool same = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        same = (rt == bb);
    }
    if (rt) rt->Release();
    if (bb) bb->Release();
    return same;
}

void DrawSync(IDirect3DDevice9* dev) {
    XrFrame f;
    if (!Xr_GetFrame(&f)) return;

    if (!CurrentRtIsBackBuffer(dev)) {
        if (!g_loggedRtMismatch) {
            g_loggedRtMismatch = true;
            Log("d3d9        : render target 0 is not the back buffer at Present; "
                "sync pixel skipped (logged once)");
        }
        return;
    }

    const int r = f.sync & 0xFF;
    D3DRECT rect;
    rect.x1 = 0; rect.y1 = 0; rect.x2 = kSyncSize; rect.y2 = kSyncSize;

    HRESULT hr = dev->Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_XRGB(r, 0, 0), 1.0f, 0);
    if (FAILED(hr) && !g_loggedClearFail) {
        g_loggedClearFail = true;
        Log("d3d9        : Clear(sync) failed 0x%08X", (unsigned)hr);
    }
}

// ---------------------------------------------------------------------------
// Frame dump
// ---------------------------------------------------------------------------
void WriteBmp(const char* path, const BYTE* pixels, int width, int height, int pitch) {
    if (!pixels || width <= 0 || height <= 0) return;

    const int rowBytes = width * 4;
    const DWORD imageSize = (DWORD)rowBytes * (DWORD)height;
    const DWORD fileSize = 54 + imageSize;

    BYTE hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &fileSize, 4);
    DWORD off = 54; memcpy(hdr + 10, &off, 4);
    DWORD hdrSize = 40; memcpy(hdr + 14, &hdrSize, 4);
    LONG w = width, h = height;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    WORD planes = 1, bpp = 32;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &imageSize, 4);
    LONG ppm = 2835;
    memcpy(hdr + 38, &ppm, 4);
    memcpy(hdr + 42, &ppm, 4);

    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log("d3d9        : cannot write %s (%lu)", path, GetLastError());
        return;
    }

    DWORD written = 0;
    WriteFile(f, hdr, 54, &written, nullptr);
    for (int y = height - 1; y >= 0; --y) {
        WriteFile(f, pixels + (size_t)y * pitch, rowBytes, &written, nullptr);
    }
    CloseHandle(f);
}

void MaybeDump(IDirect3DDevice9* dev) {
    DWORD now = GetTickCount();
    if (now - g_lastRequestCheck < 500) return;
    g_lastRequestCheck = now;

    char dir[MAX_PATH] = {0};
    ExeDir(dir, sizeof(dir));
    if (!dir[0]) return;

    char req[MAX_PATH], out[MAX_PATH];
    _snprintf_s(req, sizeof(req), _TRUNCATE, "%s\\contagion-wxr-shot.request", dir);
    _snprintf_s(out, sizeof(out), _TRUNCATE, "%s\\contagion-wxr-shot.bmp", dir);
    if (GetFileAttributesA(req) == INVALID_FILE_ATTRIBUTES) return;

    DeleteFileA(req);

    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        Log("d3d9        : shot: GetBackBuffer failed");
        return;
    }

    D3DSURFACE_DESC desc;
    bb->GetDesc(&desc);

    IDirect3DSurface9* sys = nullptr;
    // The destination must match the source format or GetRenderTargetData
    // returns D3DERR_INVALIDCALL (measured 2026-11-18: the back buffer is
    // A8R8G8B8, and asking for X8R8G8B8 failed every time).
    HRESULT hr = dev->CreateOffscreenPlainSurface(desc.Width, desc.Height,
                                                  desc.Format, D3DPOOL_SYSTEMMEM,
                                                  &sys, nullptr);
    if (SUCCEEDED(hr)) hr = dev->GetRenderTargetData(bb, sys);

    // A multisampled back buffer cannot be copied directly (D3DERR_INVALIDCALL).
    // The front buffer is what the player sees and can always be captured, so
    // fall back to it: this dump exists to verify the sync pixel, not to be a
    // pristine screenshot.
    if (FAILED(hr)) {
        Log("d3d9        : shot: GetRenderTargetData 0x%08X (format %d, multisample %d/%d); "
            "trying front buffer",
            (unsigned)hr, (int)desc.Format, (int)desc.MultiSampleType, (int)desc.MultiSampleQuality);
        if (sys) {
            hr = dev->GetFrontBufferData(0, sys);
        }
    }

    if (SUCCEEDED(hr)) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            WriteBmp(out, (const BYTE*)lr.pBits, (int)desc.Width, (int)desc.Height, (int)lr.Pitch);
            sys->UnlockRect();
            Log("d3d9        : shot written %dx%d -> %s", (int)desc.Width, (int)desc.Height, out);
        }
    } else {
        Log("d3d9        : shot failed 0x%08X (multisampled back buffer?)", (unsigned)hr);
    }

    if (sys) sys->Release();
    bb->Release();
}

// ---------------------------------------------------------------------------
// Per-device state. The originals are per vtable because a device's table is
// its own: two devices can legitimately have different Present functions.
// ---------------------------------------------------------------------------
struct DeviceHook {
    void*        vt;
    PresentFn    present;
    BeginSceneFn begin;
    EndSceneFn   end;
    bool         cef;      // Chromium's helper window, not the game's frame
};

DeviceHook g_dev[kMaxDevices];
int g_devCount = 0;

DeviceHook* FindDevice(void* dev) {
    if (!dev) return nullptr;
    void** vt = nullptr;
    __try { vt = *(void***)dev; } __except (EXCEPTION_EXECUTE_HANDLER) { vt = nullptr; }
    if (!vt) return nullptr;
    for (int i = 0; i < g_devCount; ++i) {
        if (g_dev[i].vt == (void*)vt) return &g_dev[i];
    }
    return nullptr;
}

PresentFn    OriginalPresent(void* dev) { DeviceHook* d = FindDevice(dev); return d ? d->present : nullptr; }
BeginSceneFn OriginalBegin(void* dev)   { DeviceHook* d = FindDevice(dev); return d ? d->begin : nullptr; }
EndSceneFn   OriginalEnd(void* dev)     { DeviceHook* d = FindDevice(dev); return d ? d->end : nullptr; }

void LogStatsIfDue() {
    DWORD now = GetTickCount();
    if (g_lastStats == 0) { g_lastStats = now; return; }
    if (now - g_lastStats < 5000) return;
    g_lastStats = now;

    const unsigned p = (unsigned)g_present, e = (unsigned)g_end, b = (unsigned)g_begin;
    Log("d3d9        : present=%u begin=%u end=%u (end/present=%.2f) devices=%ld",
        p, b, e, p ? (double)e / (double)p : 0.0, g_devices);

    char dir[MAX_PATH] = {0};
    ExeDir(dir, sizeof(dir));
    if (dir[0]) {
        char req[MAX_PATH];
        _snprintf_s(req, sizeof(req), _TRUNCATE, "%s\\contagion-wxr-shot.request", dir);
        if (GetFileAttributesA(req) != INVALID_FILE_ATTRIBUTES) {
            Log("d3d9        : shot request present, will dump next Present");
        }
    }
}

// ---------------------------------------------------------------------------
// Device detours. __stdcall, matching COM on x86.
// ---------------------------------------------------------------------------
HRESULT __stdcall HookPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst,
                              HWND wnd, const RGNDATA* dirty) {
    InterlockedIncrement(&g_present);

    if (!g_loggedFirstPresent) {
        g_loggedFirstPresent = true;
        Log("d3d9        : first Present on device 0x%08X", (unsigned)(uintptr_t)dev);
    }

    // Our own work is guarded: a fault inside the mod must not take the game
    // down, and if one happens the log says so once.
    const DeviceHook* info = FindDevice(dev);
    if (!info || !info->cef) {
        __try {
            DrawSync(dev);
            MaybeDump(dev);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!g_loggedFault) {
                g_loggedFault = true;
                Log("d3d9        : exception 0x%08X in Present hook, suppressed",
                    GetExceptionCode());
            }
        }
    }

    LogStatsIfDue();

    PresentFn orig = OriginalPresent(dev);
    if (!orig) return D3DERR_INVALIDCALL;
    return orig(dev, src, dst, wnd, dirty);
}

HRESULT __stdcall HookEndScene(IDirect3DDevice9* dev) {
    InterlockedIncrement(&g_end);
    EndSceneFn orig = OriginalEnd(dev);
    if (!orig) return D3DERR_INVALIDCALL;
    return orig(dev);
}

HRESULT __stdcall HookBeginScene(IDirect3DDevice9* dev) {
    InterlockedIncrement(&g_begin);
    BeginSceneFn orig = OriginalBegin(dev);
    if (!orig) return D3DERR_INVALIDCALL;
    return orig(dev);
}

bool IsCefWindow(HWND hwnd) {
    if (!hwnd) return false;
    char cls[64] = {0};
    if (GetClassNameA(hwnd, cls, sizeof(cls)) == 0) return false;
    // Chromium's D3D9 presenter creates this class; the game's own window is
    // Valve001. Drawing on the helper is harmless, but a frame dump from it
    // would capture the menu rather than the map.
    return _stricmp(cls, "D3DProxyWindow") == 0;
}

// Called with the real device that the game just received from CreateDevice.
void HookDevice(IDirect3DDevice9* dev, const char* how, const D3DPRESENT_PARAMETERS* pp) {
    if (!dev) return;
    InterlockedIncrement(&g_devices);

    void** vt = nullptr;
    __try { vt = *(void***)dev; } __except (EXCEPTION_EXECUTE_HANDLER) { vt = nullptr; }
    if (!vt) {
        Log("d3d9        : %s returned a device (0x%08X) with no vtable",
            how, (unsigned)(uintptr_t)dev);
        return;
    }

    const HWND hwnd = pp ? pp->hDeviceWindow : nullptr;
    const bool cef = IsCefWindow(hwnd);

    Log("d3d9        : device 0x%08X from %s, vtable 0x%08X present=0x%08X%s",
        (unsigned)(uintptr_t)dev, how, (unsigned)(uintptr_t)vt,
        (unsigned)(uintptr_t)vt[kIdxPresent],
        cef ? " (Chromium helper)" : "");

    for (int i = 0; i < g_devCount; ++i) {
        if (g_dev[i].vt == (void*)vt) {
            Log("d3d9        : that vtable is already hooked");
            return;
        }
    }
    if (g_devCount >= kMaxDevices) {
        Log("d3d9        : device table full; Present not hooked");
        return;
    }

    // Record the originals before making any hook live: if Present fires
    // between the patch and the bookkeeping it must find its original, not a
    // null and a D3DERR_INVALIDCALL.
    void* oPresent = vt[kIdxPresent];
    void* oBegin   = vt[kIdxBeginScene];
    void* oEnd     = vt[kIdxEndScene];

    DeviceHook& d = g_dev[g_devCount];
    d.vt      = (void*)vt;
    d.present = (PresentFn)oPresent;
    d.begin   = (BeginSceneFn)oBegin;
    d.end     = (EndSceneFn)oEnd;
    d.cef     = cef;
    ++g_devCount;

    if (!PatchSlot(vt, kIdxPresent, (void*)&HookPresent, nullptr)) {
        --g_devCount;
        Log("d3d9        : could not patch Present");
        return;
    }
    PatchSlot(vt, kIdxBeginScene, (void*)&HookBeginScene, nullptr);
    PatchSlot(vt, kIdxEndScene, (void*)&HookEndScene, nullptr);

    InterlockedExchange(&g_hooked, 1);
    Log("d3d9        : Present hooked (original 0x%08X), begin 0x%08X, end 0x%08X",
        (unsigned)(uintptr_t)oPresent, (unsigned)(uintptr_t)oBegin, (unsigned)(uintptr_t)oEnd);
}

// ---------------------------------------------------------------------------
// The wrapped IDirect3D9.
//
// Layout is fixed because the forwarding thunks below address `inner` by
// offset: { vtable, inner, refs, innerIsEx }.
// ---------------------------------------------------------------------------
struct D3D9Wrapper {
    void**      vt;
    IDirect3D9* inner;
    volatile LONG refs;
    BOOL        innerIsEx;
};
static_assert(offsetof(D3D9Wrapper, inner) == 4, "the thunks assume inner at +4");

ULONG __stdcall WrapAddRef(D3D9Wrapper* self) {
    return (ULONG)InterlockedIncrement(&self->refs);
}

ULONG __stdcall WrapRelease(D3D9Wrapper* self) {
    const LONG n = InterlockedDecrement(&self->refs);
    if (n == 0) {
        if (self->inner) self->inner->Release();
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)n;
}

HRESULT __stdcall WrapQueryInterface(D3D9Wrapper* self, const IID& iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;

    // Never return the inner object for these: the game would then call
    // CreateDevice on something we do not control, which is the failure mode
    // that made the earlier attempts unusable.
    if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(IDirect3D9))) {
        *out = (void*)self;
        WrapAddRef(self);
        return S_OK;
    }
    if (IsEqualIID(iid, __uuidof(IDirect3D9Ex))) {
        if (!self->innerIsEx) return E_NOINTERFACE;
        *out = (void*)self;
        WrapAddRef(self);
        return S_OK;
    }

    // Anything else belongs to the real object and is none of our business.
    return self->inner->QueryInterface(iid, out);
}

HRESULT __stdcall WrapCreateDevice(D3D9Wrapper* self, UINT adapter, D3DDEVTYPE type,
                                   HWND hwnd, DWORD flags, D3DPRESENT_PARAMETERS* pp,
                                   IDirect3DDevice9** out) {
    Log("d3d9        : wrapper: CreateDevice(adapter=%u type=%d flags=0x%08X)",
        (unsigned)adapter, (int)type, (unsigned)flags);
    if (pp && ReadableRange(pp, sizeof(D3DPRESENT_PARAMETERS))) {
        g_bbW = (LONG)pp->BackBufferWidth;
        g_bbH = (LONG)pp->BackBufferHeight;
        Log("d3d9        : wrapper: windowed=%d %ux%u@%uHz hwnd=0x%08X format=%d depth=%d",
            (int)pp->Windowed, (unsigned)pp->BackBufferWidth, (unsigned)pp->BackBufferHeight,
            (unsigned)pp->FullScreen_RefreshRateInHz, (unsigned)(uintptr_t)pp->hDeviceWindow,
            (int)pp->BackBufferFormat, (int)pp->EnableAutoDepthStencil);
    }

    HRESULT hr = self->inner->CreateDevice(adapter, type, hwnd, flags, pp, out);
    if (SUCCEEDED(hr) && out && *out) {
        HookDevice(*out, "CreateDevice", pp);
    } else {
        Log("d3d9        : wrapper: CreateDevice failed 0x%08X", (unsigned)hr);
    }
    return hr;
}

HRESULT __stdcall WrapCreateDeviceEx(D3D9Wrapper* self, UINT adapter, D3DDEVTYPE type,
                                     HWND hwnd, DWORD flags, D3DPRESENT_PARAMETERS* pp,
                                     D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** out) {
    Log("d3d9        : wrapper: CreateDeviceEx(adapter=%u type=%d flags=0x%08X)",
        (unsigned)adapter, (int)type, (unsigned)flags);
    if (pp && ReadableRange(pp, sizeof(D3DPRESENT_PARAMETERS))) {
        g_bbW = (LONG)pp->BackBufferWidth;
        g_bbH = (LONG)pp->BackBufferHeight;
        Log("d3d9        : wrapper: windowed=%d %ux%u@%uHz hwnd=0x%08X format=%d depth=%d",
            (int)pp->Windowed, (unsigned)pp->BackBufferWidth, (unsigned)pp->BackBufferHeight,
            (unsigned)pp->FullScreen_RefreshRateInHz, (unsigned)(uintptr_t)pp->hDeviceWindow,
            (int)pp->BackBufferFormat, (int)pp->EnableAutoDepthStencil);
    }

    // Only reachable when the wrapper answered QI for IDirect3D9Ex, so this
    // must succeed; if it does not, say so rather than crash.
    IDirect3D9Ex* innerEx = nullptr;
    HRESULT hr = self->inner->QueryInterface(__uuidof(IDirect3D9Ex), (void**)&innerEx);
    if (FAILED(hr) || !innerEx) {
        Log("d3d9        : wrapper: CreateDeviceEx without an Ex inner (0x%08X)", (unsigned)hr);
        return D3DERR_INVALIDCALL;
    }

    hr = innerEx->CreateDeviceEx(adapter, type, hwnd, flags, pp, mode, out);
    innerEx->Release();
    if (SUCCEEDED(hr) && out && *out) {
        HookDevice((IDirect3DDevice9*)*out, "CreateDeviceEx", pp);
    } else {
        Log("d3d9        : wrapper: CreateDeviceEx failed 0x%08X", (unsigned)hr);
    }
    return hr;
}

// Forward one method to the real object. `this` is the first argument of a
// __stdcall COM method, so replacing stack slot +4 is enough; the jump means
// the real function returns straight to the original caller, which also
// removes the arguments as it always would.
#define WRAP_THUNK(n, off)                                                   \
    extern "C" __declspec(naked) void WrapThunk##n() {                       \
        __asm { mov eax, [esp + 4] }                                         \
        __asm { mov eax, [eax + 4] }                                         \
        __asm { mov [esp + 4], eax }                                         \
        __asm { mov eax, [eax] }                                             \
        __asm { jmp dword ptr [eax + off] }                                  \
    }

WRAP_THUNK(3, 12)  WRAP_THUNK(4, 16)  WRAP_THUNK(5, 20)  WRAP_THUNK(6, 24)
WRAP_THUNK(7, 28)  WRAP_THUNK(8, 32)  WRAP_THUNK(9, 36)  WRAP_THUNK(10, 40)
WRAP_THUNK(11, 44) WRAP_THUNK(12, 48) WRAP_THUNK(13, 52) WRAP_THUNK(14, 56)
WRAP_THUNK(15, 60) WRAP_THUNK(17, 68) WRAP_THUNK(18, 72) WRAP_THUNK(19, 76)
WRAP_THUNK(21, 84)

void* g_wrapVt[22] = {
    (void*)&WrapQueryInterface,  // 0
    (void*)&WrapAddRef,          // 1
    (void*)&WrapRelease,         // 2
    (void*)&WrapThunk3,          // 3  RegisterSoftwareDevice
    (void*)&WrapThunk4,          // 4  GetAdapterCount
    (void*)&WrapThunk5,          // 5  GetAdapterIdentifier
    (void*)&WrapThunk6,          // 6  GetAdapterModeCount
    (void*)&WrapThunk7,          // 7  EnumAdapterModes
    (void*)&WrapThunk8,          // 8  GetAdapterDisplayMode
    (void*)&WrapThunk9,          // 9  CheckDeviceType
    (void*)&WrapThunk10,         // 10 CheckDeviceFormat
    (void*)&WrapThunk11,         // 11 CheckDeviceMultiSampleType
    (void*)&WrapThunk12,         // 12 CheckDepthStencilMatch
    (void*)&WrapThunk13,         // 13 CheckDeviceFormatConversion
    (void*)&WrapThunk14,         // 14 GetDeviceCaps
    (void*)&WrapThunk15,         // 15 GetAdapterMonitor
    (void*)&WrapCreateDevice,    // 16 CreateDevice
    (void*)&WrapThunk17,         // 17 IDirect3D9Ex::GetAdapterModeCountEx
    (void*)&WrapThunk18,         // 18 EnumAdapterModesEx
    (void*)&WrapThunk19,         // 19 GetAdapterDisplayModeEx
    (void*)&WrapCreateDeviceEx,  // 20 CreateDeviceEx
    (void*)&WrapThunk21,         // 21 GetAdapterLUID
};

IDirect3D9* WrapD3D9(IDirect3D9* inner, const char* how, BOOL knownEx) {
    if (!inner) return nullptr;

    BOOL isEx = knownEx;
    if (!isEx) {
        IDirect3D9Ex* probe = nullptr;
        if (SUCCEEDED(inner->QueryInterface(__uuidof(IDirect3D9Ex), (void**)&probe)) && probe) {
            isEx = TRUE;
            probe->Release();
        }
    }

    D3D9Wrapper* w = (D3D9Wrapper*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                             sizeof(D3D9Wrapper));
    if (!w) {
        // Better to hand over the real object than to fail device creation.
        Log("d3d9        : %s: wrapper allocation failed; returning the real object", how);
        return inner;
    }
    w->vt        = (void**)g_wrapVt;
    w->inner     = inner;
    w->refs      = 1;
    w->innerIsEx = isEx;

    Log("d3d9        : %s -> real 0x%08X, wrapped 0x%08X (Ex=%d)",
        how, (unsigned)(uintptr_t)inner, (unsigned)(uintptr_t)w, (int)isEx);
    return (IDirect3D9*)w;
}

// ---------------------------------------------------------------------------
// Entry points.
// ---------------------------------------------------------------------------
IDirect3D9* __stdcall HookDirect3DCreate9(UINT sdk) {
    InterlockedIncrement(&g_ctorCalls);
    if (!g_origCreate9) return nullptr;

    IDirect3D9* real = g_origCreate9(sdk);
    if (!real) {
        Log("d3d9        : Direct3DCreate9 returned null");
        return nullptr;
    }
    return WrapD3D9(real, "Direct3DCreate9", FALSE);
}

HRESULT __stdcall HookDirect3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
    InterlockedIncrement(&g_ctorCalls);
    if (!g_origCreate9Ex) return D3DERR_INVALIDCALL;

    HRESULT hr = g_origCreate9Ex(sdk, out);
    if (FAILED(hr) || !out || !*out) {
        Log("d3d9        : Direct3DCreate9Ex failed 0x%08X", (unsigned)hr);
        return hr;
    }
    *out = (IDirect3D9Ex*)WrapD3D9((IDirect3D9*)*out, "Direct3DCreate9Ex", TRUE);
    return hr;
}

// Install before the game creates its device. Idempotent.
bool InstallCreate9Hooks() {
    if (InterlockedCompareExchange(&g_ctorHooked, 0, 0) == 1) return true;

    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) {
        // The hook has to exist before the game's first device, and the game
        // loads d3d9 lazily. Loading it here is deliberate: this runs in the
        // injected thread while the process is still suspended, so there is no
        // other thread to race with.
        d3d9 = LoadLibraryA("d3d9.dll");
        if (d3d9) Log("d3d9        : loaded d3d9.dll early at 0x%08X", (unsigned)(uintptr_t)d3d9);
    }
    if (!d3d9) return false;

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        Log("d3d9        : MH_Initialize failed (%d)", (int)st);
        return false;
    }

    void* fn = (void*)GetProcAddress(d3d9, "Direct3DCreate9");
    if (!fn) return false;

    if (MH_CreateHook(fn, (LPVOID)&HookDirect3DCreate9, (LPVOID*)&g_origCreate9) != MH_OK ||
        MH_EnableHook(fn) != MH_OK) {
        Log("d3d9        : MinHook failed on Direct3DCreate9");
        return false;
    }

    void* fnEx = (void*)GetProcAddress(d3d9, "Direct3DCreate9Ex");
    if (fnEx) {
        if (MH_CreateHook(fnEx, (LPVOID)&HookDirect3DCreate9Ex, (LPVOID*)&g_origCreate9Ex) == MH_OK &&
            MH_EnableHook(fnEx) == MH_OK) {
            Log("d3d9        : hooked Direct3DCreate9Ex at 0x%08X", (unsigned)(uintptr_t)fnEx);
        } else {
            Log("d3d9        : MinHook failed on Direct3DCreate9Ex (not fatal)");
        }
    }

    InterlockedExchange(&g_ctorHooked, 1);
    Log("d3d9        : hooked Direct3DCreate9 at 0x%08X (first byte 0x%02X)",
        (unsigned)(uintptr_t)fn, *(unsigned char*)fn);
    return true;
}

DWORD WINAPI WatcherThread(LPVOID) {
    for (int i = 0; i < 1200; ++i) {
        if (InstallCreate9Hooks()) return 0;
        Sleep(10);
    }
    Log("d3d9        : gave up waiting for d3d9.dll (no per-frame hook)");
    return 0;
}

} // namespace

bool D3d9_Init() {
    static bool tried = false;
    if (tried) return true;
    tried = true;

    if (InstallCreate9Hooks()) return true;

    HANDLE t = CreateThread(nullptr, 0, WatcherThread, nullptr, 0, nullptr);
    if (t) { CloseHandle(t); return true; }
    Log("d3d9        : could not start watcher thread");
    return false;
}

void D3d9_Shutdown() {}

void D3d9_Stats(unsigned* present, unsigned* endScene, unsigned* beginScene) {
    if (present)    *present = (unsigned)g_present;
    if (endScene)   *endScene = (unsigned)g_end;
    if (beginScene) *beginScene = (unsigned)g_begin;
}

bool D3d9_Hooked() { return g_hooked != 0; }

bool D3d9_BackBufferSize(int* width, int* height) {
    const LONG w = g_bbW, h = g_bbH;
    if (w <= 0 || h <= 0) return false;
    if (width)  *width  = (int)w;
    if (height) *height = (int)h;
    return true;
}

} // namespace cwxr
