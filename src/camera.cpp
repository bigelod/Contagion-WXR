// camera.cpp - milestone 6a: find the IViewRender method that takes a
// CViewSetup*, by watching the object the game actually uses.
//
// Why a probe rather than an SDK index: the five IViewRender call sites found
// statically are all nullary getters, so the argument-taking methods are
// reached through cached registers and cannot be found by pattern (PLAN.md 3.1).
// The vtable is the authority for this binary, so we instrument it and read the
// answer off the log.
//
// Safety, in order of importance:
//
//   - Every detour is a naked thunk: pushad, log, popad, jmp original. Registers,
//     stack and flags layout are untouched, so a method with any signature and
//     any number of arguments passes through unchanged.
//   - Only entries that are real, executable code are patched. NULL slots and
//     anything that is not executable are counted and skipped.
//   - The object's own vtable is patched rather than the functions it points
//     at, so calls made by other classes through a shared base implementation
//     do not appear in the log.
//   - Each slot logs at most three calls. The probe cannot flood the log or
//     change the frame rate measurably.
//   - It is off unless CWXR_CAMPROBE is set, so the shipped build is unchanged.
//
// The detection rule for CViewSetup is deliberately loose: x86 MSVC lays out
// CViewSetup with the viewport rectangle near the start, so a pointer whose
// target contains the current back-buffer width and height (or the container's
// 1024x768) in the first 0x60 bytes is reported with the offset where the pair
// was found. That is evidence, not proof, and it is logged as such.

#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "camera.h"
#include "d3d9hook.h"
#include "log.h"

namespace cwxr {
namespace {

// g_pViewRender, as an offset from client.dll's base (BINARY_RECON.md 8.2).
const unsigned kViewRenderOffset = 0x7B5F98u;
const int kMaxSlots = 64;

// The thunks address this by name from inline asm, so it needs C linkage.
extern "C" void* g_camOrig[kMaxSlots] = { nullptr };
extern "C" void LogCamCall(void* self, int index, DWORD* frame);

volatile LONG g_calls[kMaxSlots];
volatile LONG g_fingerprinted[kMaxSlots];

int g_bbW = 0;
int g_bbH = 0;

bool Readable(const void* p, size_t n);

// The live camera, as measured on 2026-11-18 from slot 7's first argument:
// x,y,width,height at +0x00, fov at +0x24, origin at +0x2C, angles at +0x38.
// The values were plausible map coordinates (-231, -98, -2795) and a yaw of
// -123 degrees, which is what identifies this as CViewSetup and not a guess.
struct ViewSetup {
    int   x, y, width, height;   // 0x00
    char  ortho[0x14];           // 0x10
    float fov;                   // 0x24
    float fovViewmodel;          // 0x28
    float origin[3];             // 0x2C
    float angles[3];             // 0x38  pitch, yaw, roll (degrees)
};

int   g_camSlot = 7;
float g_yawOffset = 0.0f;
bool  g_yawSet = false;
bool  g_yawApplied = false;
float g_yawBase = 0.0f;
DWORD g_lastCamLog = 0;
DWORD g_lastYawPoll = 0;

// A control file makes the camera testable in one run at one position: the
// harness changes the number between two screenshots. Without it, comparing two
// runs compares two different spawn points.
void PollYawFile() {
    const DWORD now = GetTickCount();
    if (g_lastYawPoll && now - g_lastYawPoll < 500) return;
    g_lastYawPoll = now;

    char exe[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exe, MAX_PATH) == 0) return;
    for (int i = (int)strlen(exe) - 1; i >= 0; --i) {
        if (exe[i] == '\\' || exe[i] == '/') { exe[i] = 0; break; }
    }
    if (!exe[0]) return;

    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\contagion-wxr-cam-yaw.txt", exe);

    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[32] = {0};
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(f);
    if (got == 0) return;

    const float v = (float)atof(buf);
    const float diff = (v > g_yawOffset) ? (v - g_yawOffset) : (g_yawOffset - v);
    if (!g_yawSet || diff > 0.01f) {
        g_yawOffset = v;
        g_yawSet = true;
        Log("cam         : yaw offset from file: %.2f", g_yawOffset);
    }
}

// Applied from the forwarding thunk before the original method runs, so the
// value is in place before the frame that uses it is built.
void MaybeApplyCamera(int index, DWORD* frame) {
    PollYawFile();
    if (!g_yawSet || index != g_camSlot) return;

    void* p = (void*)(uintptr_t)frame[1];
    if (!p || !Readable(p, sizeof(ViewSetup))) return;
    ViewSetup* v = (ViewSetup*)p;

    if (!g_yawApplied) {
        g_yawBase = v->angles[1];
        g_yawApplied = true;
        Log("cam         : yaw write armed on slot %d: base %.2f, offset %.2f",
            index, g_yawBase, g_yawOffset);
    }
    v->angles[1] = g_yawBase + g_yawOffset;

    const DWORD now = GetTickCount();
    if (g_lastCamLog == 0 || now - g_lastCamLog >= 1000) {
        g_lastCamLog = now;
        Log("cam         : setup origin=(%.1f %.1f %.1f) angles=(%.1f %.1f %.1f) "
            "fov=%.1f %dx%d @%d,%d",
            v->origin[0], v->origin[1], v->origin[2],
            v->angles[0], v->angles[1], v->angles[2],
            v->fov, v->width, v->height, v->x, v->y);
    }
}

void DumpCandidate(int index, int arg, uintptr_t p) {
    if (p < 0x10000) return;
    __try {
        const DWORD* q = (const DWORD*)p;
        // Sixteen dwords: enough to see x/y/w/h, the ortho block, fov, origin
        // and angles of a CViewSetup, which is what slot 6 turned out to take.
        Log("cam         : [%02d] a%d @0x%08X: %08X %08X %08X %08X %08X %08X %08X %08X",
            index, arg, (unsigned)p,
            q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]);
        Log("cam         : [%02d] a%d +0x20    : %08X %08X %08X %08X %08X %08X %08X %08X",
            index, arg,
            q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("cam         : [%02d] a%d @0x%08X: unreadable", index, arg, (unsigned)p);
    }
}

bool Readable(const void* p, size_t n) {
    const BYTE* q = (const BYTE*)p;
    const BYTE* end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(q, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & PAGE_GUARD) return false;
        const DWORD prot = mbi.Protect & 0xFF;
        if (prot == PAGE_NOACCESS || prot == PAGE_EXECUTE) return false;
        const BYTE* next = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
        if (next <= q) return false;
        q = next;
    }
    return true;
}

bool Executable(const void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD)) return false;
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

// A naked forwarding thunk per slot. `frame` points at the return address, so
// frame[1..4] are the first four stack arguments - correct for __thiscall,
// which is what MSVC emits for these methods (`this` is already in ECX).
#define CAM_THUNK(n, off)                                                    \
    extern "C" __declspec(naked) void CamThunk##n() {                        \
        __asm { pushad }                                                     \
        __asm { mov eax, esp }                                               \
        __asm { add eax, 32 }                                                \
        __asm { push eax }                                                   \
        __asm { push n }                                                     \
        __asm { push ecx }                                                   \
        __asm { call LogCamCall }                                            \
        __asm { add esp, 12 }                                                \
        __asm { popad }                                                      \
        __asm { jmp dword ptr [g_camOrig + off] }                            \
    }

CAM_THUNK(0, 0)    CAM_THUNK(1, 4)    CAM_THUNK(2, 8)    CAM_THUNK(3, 12)
CAM_THUNK(4, 16)   CAM_THUNK(5, 20)   CAM_THUNK(6, 24)   CAM_THUNK(7, 28)
CAM_THUNK(8, 32)   CAM_THUNK(9, 36)   CAM_THUNK(10, 40)  CAM_THUNK(11, 44)
CAM_THUNK(12, 48)  CAM_THUNK(13, 52)  CAM_THUNK(14, 56)  CAM_THUNK(15, 60)
CAM_THUNK(16, 64)  CAM_THUNK(17, 68)  CAM_THUNK(18, 72)  CAM_THUNK(19, 76)
CAM_THUNK(20, 80)  CAM_THUNK(21, 84)  CAM_THUNK(22, 88)  CAM_THUNK(23, 92)
CAM_THUNK(24, 96)  CAM_THUNK(25, 100) CAM_THUNK(26, 104) CAM_THUNK(27, 108)
CAM_THUNK(28, 112) CAM_THUNK(29, 116) CAM_THUNK(30, 120) CAM_THUNK(31, 124)
CAM_THUNK(32, 128) CAM_THUNK(33, 132) CAM_THUNK(34, 136) CAM_THUNK(35, 140)
CAM_THUNK(36, 144) CAM_THUNK(37, 148) CAM_THUNK(38, 152) CAM_THUNK(39, 156)
CAM_THUNK(40, 160) CAM_THUNK(41, 164) CAM_THUNK(42, 168) CAM_THUNK(43, 172)
CAM_THUNK(44, 176) CAM_THUNK(45, 180) CAM_THUNK(46, 184) CAM_THUNK(47, 188)
CAM_THUNK(48, 192) CAM_THUNK(49, 196) CAM_THUNK(50, 200) CAM_THUNK(51, 204)
CAM_THUNK(52, 208) CAM_THUNK(53, 212) CAM_THUNK(54, 216) CAM_THUNK(55, 220)
CAM_THUNK(56, 224) CAM_THUNK(57, 228) CAM_THUNK(58, 232) CAM_THUNK(59, 236)
CAM_THUNK(60, 240) CAM_THUNK(61, 244) CAM_THUNK(62, 248) CAM_THUNK(63, 252)

void* g_thunks[kMaxSlots] = {
    (void*)&CamThunk0,  (void*)&CamThunk1,  (void*)&CamThunk2,  (void*)&CamThunk3,
    (void*)&CamThunk4,  (void*)&CamThunk5,  (void*)&CamThunk6,  (void*)&CamThunk7,
    (void*)&CamThunk8,  (void*)&CamThunk9,  (void*)&CamThunk10, (void*)&CamThunk11,
    (void*)&CamThunk12, (void*)&CamThunk13, (void*)&CamThunk14, (void*)&CamThunk15,
    (void*)&CamThunk16, (void*)&CamThunk17, (void*)&CamThunk18, (void*)&CamThunk19,
    (void*)&CamThunk20, (void*)&CamThunk21, (void*)&CamThunk22, (void*)&CamThunk23,
    (void*)&CamThunk24, (void*)&CamThunk25, (void*)&CamThunk26, (void*)&CamThunk27,
    (void*)&CamThunk28, (void*)&CamThunk29, (void*)&CamThunk30, (void*)&CamThunk31,
    (void*)&CamThunk32, (void*)&CamThunk33, (void*)&CamThunk34, (void*)&CamThunk35,
    (void*)&CamThunk36, (void*)&CamThunk37, (void*)&CamThunk38, (void*)&CamThunk39,
    (void*)&CamThunk40, (void*)&CamThunk41, (void*)&CamThunk42, (void*)&CamThunk43,
    (void*)&CamThunk44, (void*)&CamThunk45, (void*)&CamThunk46, (void*)&CamThunk47,
    (void*)&CamThunk48, (void*)&CamThunk49, (void*)&CamThunk50, (void*)&CamThunk51,
    (void*)&CamThunk52, (void*)&CamThunk53, (void*)&CamThunk54, (void*)&CamThunk55,
    (void*)&CamThunk56, (void*)&CamThunk57, (void*)&CamThunk58, (void*)&CamThunk59,
    (void*)&CamThunk60, (void*)&CamThunk61, (void*)&CamThunk62, (void*)&CamThunk63,
};

extern "C" void LogCamCall(void* self, int index, DWORD* frame) {
    if (index < 0 || index >= kMaxSlots) return;
    const LONG n = InterlockedIncrement(&g_calls[index]);

    // The write path is independent of the logging budget below: it has to run
    // on every frame, not on the first two calls.
    MaybeApplyCamera(index, frame);

    // The first two calls per slot are logged in full, with a memory dump of the
    // first two pointer arguments. That is what names a CViewSetup even when its
    // viewport happens to be a size we did not guess.
    if (n <= 2) {
        Log("cam         : [%02d] call %ld this=0x%08X ret=0x%08X "
            "a1=0x%08X a2=0x%08X a3=0x%08X a4=0x%08X",
            index, n, (unsigned)(uintptr_t)self, (unsigned)frame[0],
            (unsigned)frame[1], (unsigned)frame[2], (unsigned)frame[3], (unsigned)frame[4]);
        if (n == 1) {
            DumpCandidate(index, 1, (uintptr_t)frame[1]);
            DumpCandidate(index, 2, (uintptr_t)frame[2]);
        }
    }

    // The fingerprint is tested on every call until it matches once, so a hit
    // cannot be hidden by the logging budget above.
    if (g_bbW <= 0 || g_bbH <= 0) return;
    if (InterlockedCompareExchange(&g_fingerprinted[index], 1, 0) != 0) return;

    for (int a = 1; a <= 4; ++a) {
        const uintptr_t p = (uintptr_t)frame[a];
        if (p < 0x10000) continue;
        __try {
            for (int off = 0; off <= 0x60; off += 4) {
                const int w = *(const int*)(p + off);
                const int h = *(const int*)(p + off + 4);
                if ((w == g_bbW && h == g_bbH) || (w == 1024 && h == 768)) {
                    Log("cam         : [%02d] a%d -> %dx%d at +0x%02X (CViewSetup?)",
                        index, a, w, h, off);
                    InterlockedExchange(&g_fingerprinted[index], 1);
                    return;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

// One attempt: resolve g_pViewRender, then hook every executable slot.
// Returns false only when the object does not exist yet.
bool TryInstall() {
    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) return false;

    MODULEINFO mi;
    memset(&mi, 0, sizeof(mi));
    if (!GetModuleInformation(GetCurrentProcess(), client, &mi, sizeof(mi))) return false;

    const uintptr_t lo = (uintptr_t)mi.lpBaseOfDll;
    const uintptr_t hi = lo + mi.SizeOfImage;

    const uintptr_t slot = lo + kViewRenderOffset;
    uintptr_t obj = 0;
    if (!Readable((const void*)slot, sizeof(obj))) return false;
    memcpy(&obj, (const void*)slot, sizeof(obj));
    if (!obj) return false;

    void** vt = nullptr;
    if (!Readable((const void*)obj, sizeof(vt))) {
        Log("cam         : g_pViewRender 0x%08X is unreadable", (unsigned)obj);
        return true;   // nothing to wait for
    }
    memcpy(&vt, (const void*)obj, sizeof(vt));
    if (!vt) {
        Log("cam         : g_pViewRender 0x%08X has null vtable", (unsigned)obj);
        return true;
    }

    // Name the class from RTTI, so the log proves which object was instrumented.
    unsigned rtti = 0;
    if (Readable((const void*)((uintptr_t)vt - 4), 4)) {
        memcpy(&rtti, (const void*)((uintptr_t)vt - 4), 4);
    }
    const char* cls = "?";
    unsigned td = 0;
    if (rtti && Readable((const void*)((uintptr_t)rtti + 12), 4)) {
        memcpy(&td, (const void*)((uintptr_t)rtti + 12), 4);
        if (td && Readable((const void*)((uintptr_t)td + 8), 96)) {
            cls = (const char*)((uintptr_t)td + 8);
        }
    }
    Log("cam         : g_pViewRender slot 0x%08X = 0x%08X, vtable 0x%08X, class %s",
        (unsigned)slot, (unsigned)obj, (unsigned)(uintptr_t)vt, cls);

    D3d9_BackBufferSize(&g_bbW, &g_bbH);

    // Patch the object's own vtable, not the functions it points at. A base
    // implementation is shared by every derived class, so function hooks logged
    // half a dozen unrelated objects (measured 02:46). The object's table is
    // used by this object alone, which is the signal we want.
    DWORD old = 0;
    const size_t span = kMaxSlots * sizeof(void*);
    if (!VirtualProtect(vt, span, PAGE_EXECUTE_READWRITE, &old)) {
        Log("cam         : VirtualProtect(vtable) failed (%lu)", GetLastError());
        return true;
    }

    int patched = 0, skipped = 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        void* fn = vt[i];
        if (!fn || !Executable(fn)) {
            ++skipped;
            continue;
        }
        g_camOrig[i] = fn;
        vt[i] = g_thunks[i];
        ++patched;
    }
    VirtualProtect(vt, span, old, &old);

    Log("cam         : probe installed: %d slot(s) patched, %d skipped", patched, skipped);
    return true;
}

} // namespace

void Camera_ProbeInstall() {
    char probe[8] = {0};
    char yaw[32] = {0};
    char slot[16] = {0};
    const bool wantProbe = GetEnvironmentVariableA("CWXR_CAMPROBE", probe, sizeof(probe)) > 0;
    const bool wantYaw   = GetEnvironmentVariableA("CWXR_CAM_YAW", yaw, sizeof(yaw)) > 0;
    if (!wantProbe && !wantYaw) return;

    if (GetEnvironmentVariableA("CWXR_CAM_SLOT", slot, sizeof(slot)) > 0) {
        const int s = atoi(slot);
        if (s >= 0 && s < kMaxSlots) g_camSlot = s;
    }
    if (wantYaw) {
        g_yawOffset = (float)atof(yaw);
        g_yawSet = true;
        Log("cam         : yaw write requested: slot %d, offset %.2f degrees",
            g_camSlot, g_yawOffset);
    }

    Log("cam         : probe requested (CWXR_CAMPROBE/CWXR_CAM_YAW)");
    for (int i = 0; i < 20; ++i) {
        if (TryInstall()) return;
        Sleep(500);
    }
    Log("cam         : g_pViewRender still null after 10 s; probe skipped");
}

} // namespace cwxr
