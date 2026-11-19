#include <winsock2.h>
#include <ws2tcpip.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "xrapi.h"

#pragma comment(lib, "ws2_32.lib")

namespace cwxr {
namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
CRITICAL_SECTION g_cs;
bool             g_csReady = false;

SOCKET           g_sockIn  = INVALID_SOCKET;
SOCKET           g_sockOut = INVALID_SOCKET;
sockaddr_in      g_toOut;

volatile LONG    g_running = 0;
HANDLE           g_recvThread = nullptr;
HANDLE           g_sendThread = nullptr;

XrFrame          g_frame;          // newest accepted
bool             g_haveFrame = false;

unsigned         g_accepted = 0;
unsigned         g_rejected = 0;
unsigned         g_duplicates = 0;
int              g_lastAcceptedSync = -1;

int              g_modeVr = 1;     // 0 = off/flat, 1 = VR, 2 = screen mode
int              g_mode3d = 0;     // 0 = mono, 1 = SBS, 2 = AER
float            g_fovH = 104.5f;  // degrees; what we tell the runtime to paint
float            g_fovV = 104.5f;
int              g_hapL = 0;       // duration in frames, per hand
int              g_hapR = 0;

char             g_dir[MAX_PATH] = {0};
bool             g_onDevice = false;
bool             g_systemLogged = false;
bool             g_firstPacketLogged = false;

const int        kKeepMs      = 2000;   // handshake re-check period
const int        kSendPeriod  = 11;     // ms, ~90 Hz
const int        kStatsPeriod = 5000;   // ms

// ---------------------------------------------------------------------------
// Locale-independent number formatting.
//
// The outbound packet is whitespace-separated ASCII. A comma-decimal locale
// turns "0.500" into "0,500" and splits one field into two, shifting every
// field after it (DEFECTS.md D39). Never use %f for the wire.
// ---------------------------------------------------------------------------
int FmtInt(char* out, int v) {
    char tmp[16];
    int n = 0;
    bool neg = v < 0;
    unsigned u = neg ? (unsigned)(-(long long)v) : (unsigned)v;
    do { tmp[n++] = (char)('0' + (u % 10)); u /= 10; } while (u);
    int w = 0;
    if (neg) out[w++] = '-';
    while (n) out[w++] = tmp[--n];
    out[w] = 0;
    return w;
}

int FmtFixed2(char* out, float v) {
    if (!(v == v)) v = 0.0f;                    // NaN
    if (v > 100000.0f) v = 100000.0f;
    if (v < -100000.0f) v = -100000.0f;
    bool neg = v < 0.0f;
    if (neg) v = -v;
    int scaled = (int)(v * 100.0f + 0.5f);
    int ip = scaled / 100;
    int fp = scaled % 100;
    int w = 0;
    if (neg) out[w++] = '-';
    w += FmtInt(out + w, ip);
    out[w++] = '.';
    out[w++] = (char)('0' + (fp / 10));
    out[w++] = (char)('0' + (fp % 10));
    out[w] = 0;
    return w;
}

// ---------------------------------------------------------------------------
// Locale-independent parsing. Hand-rolled rather than strtod/atof, which honour
// the C locale and would read "0.210" as 0 on a comma-decimal machine.
// ---------------------------------------------------------------------------
void SkipSpace(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
}

bool ScanFloat(const char*& p, const char* end, float* out) {
    SkipSpace(p, end);
    const char* start = p;
    int sign = 1;
    if (p < end && (*p == '-' || *p == '+')) { if (*p == '-') sign = -1; ++p; }

    double val = 0.0;
    bool digits = false;
    while (p < end && *p >= '0' && *p <= '9') { val = val * 10.0 + (*p - '0'); ++p; digits = true; }
    if (p < end && *p == '.') {
        ++p;
        double frac = 0.1;
        while (p < end && *p >= '0' && *p <= '9') { val += (*p - '0') * frac; frac *= 0.1; ++p; digits = true; }
    }
    if (!digits) { p = start; return false; }

    if (p < end && (*p == 'e' || *p == 'E')) {
        const char* save = p;
        ++p;
        int esign = 1;
        if (p < end && (*p == '+' || *p == '-')) { if (*p == '-') esign = -1; ++p; }
        int exp = 0;
        bool expDigits = false;
        while (p < end && *p >= '0' && *p <= '9') { exp = exp * 10 + (*p - '0'); ++p; expDigits = true; }
        if (expDigits) val *= pow(10.0, esign * exp); else p = save;
    }

    *out = (float)(sign * val);
    return true;
}

bool ScanToken(const char*& p, const char* end, const char** tok, int* len) {
    SkipSpace(p, end);
    if (p >= end) return false;
    const char* s = p;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
    *tok = s;
    *len = (int)(p - s);
    return true;
}

bool NormaliseQuat(float& x, float& y, float& z, float& w) {
    float n2 = x * x + y * y + z * z + w * w;
    // Degenerate or absurd: reject rather than divide. Caller holds the last
    // good pose (freeze, never zero).
    if (!(n2 > 1e-6f) || !(n2 < 1e6f)) return false;
    float inv = 1.0f / sqrtf(n2);
    x *= inv; y *= inv; z *= inv; w *= inv;
    return true;
}

// Packet: client<N> <28 floats> <frame id> <bool string> [flags] [HMD]
bool ParsePacket(const char* buf, int len, XrFrame* f) {
    if (len < 32) return false;
    const char* p = buf;
    const char* end = buf + len;

    // Optional "client" prefix token, matched loosely ("client0", "client3"...).
    const char* tok = nullptr;
    int tlen = 0;
    const char* save = p;
    if (ScanToken(p, end, &tok, &tlen) && tlen >= 6 &&
        _strnicmp(tok, "client", 6) == 0) {
        // consumed
    } else {
        p = save;
    }

    float v[28];
    for (int i = 0; i < 28; ++i) {
        if (!ScanFloat(p, end, &v[i])) return false;
    }

    // HMD_SYNC is field 28 and is a float on the wire; accept "176" and "176.0"
    // alike. Parsing it as an int would leave ".0" behind and shift the bool
    // string into the wrong token.
    float syncF = 0.0f;
    if (!ScanFloat(p, end, &syncF)) return false;
    const int sync = (int)(syncF + 0.5f);

    const char* bools = nullptr;
    int blen = 0;
    if (!ScanToken(p, end, &bools, &blen) || blen < 19) return false;

    // 19 chars is the button string alone; 21 carries the immersive/SBS flags
    // in the last two (the runtime writes them as a separate token from 0.3, so
    // a parser that collects T/F and ignores whitespace sees 21).
    bool immersive = false, sbs = false;
    if (blen >= 21) {
        immersive = bools[19] == 'T';
        sbs       = bools[20] == 'T';
    } else if (blen == 19) {
        const char* flags = nullptr;
        int flen = 0;
        const char* after = p;
        if (ScanToken(p, end, &flags, &flen) && flen == 2 &&
            (flags[0] == 'T' || flags[0] == 'F') && (flags[1] == 'T' || flags[1] == 'F')) {
            immersive = flags[0] == 'T';
            sbs       = flags[1] == 'T';
        } else {
            p = after;
        }
    }

    XrFrame r;
    r.left.qx = v[0]; r.left.qy = v[1]; r.left.qz = v[2]; r.left.qw = v[3];
    r.lstickX = v[4]; r.lstickY = v[5];
    r.left.px = v[6]; r.left.py = v[7]; r.left.pz = v[8];
    r.right.qx = v[9]; r.right.qy = v[10]; r.right.qz = v[11]; r.right.qw = v[12];
    r.rstickX = v[13]; r.rstickY = v[14];
    r.right.px = v[15]; r.right.py = v[16]; r.right.pz = v[17];
    r.hmd.qx = v[18]; r.hmd.qy = v[19]; r.hmd.qz = v[20]; r.hmd.qw = v[21];
    r.hmd.px = v[22]; r.hmd.py = v[23]; r.hmd.pz = v[24];
    r.ipd  = v[25];
    r.fovX = v[26];
    r.fovY = v[27];
    r.sync = sync;
    r.immersive = immersive;
    r.sbs = sbs;
    r.buttons = 0;
    for (int i = 0; i < 19; ++i) {
        if (bools[i] == 'T') r.buttons |= (1u << i);
    }

    // Normalise every quaternion on ingest, unconditionally (general CLAUDE.md).
    r.left.valid  = NormaliseQuat(r.left.qx,  r.left.qy,  r.left.qz,  r.left.qw);
    r.right.valid = NormaliseQuat(r.right.qx, r.right.qy, r.right.qz, r.right.qw);
    r.hmd.valid   = NormaliseQuat(r.hmd.qx,   r.hmd.qy,   r.hmd.qz,   r.hmd.qw);
    if (!r.hmd.valid && !r.left.valid && !r.right.valid) return false;

    r.valid = true;
    *f = r;
    return true;
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------
bool DirExists(const char* p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void ResolveDir() {
    // Test the PARENT: Z:\ alone is not a device marker, and GetFileAttributes
    // on the root fails under Wine (SIBLING_PORTS.md 3.3).
    g_onDevice = DirExists("Z:\\tmp");
    if (g_onDevice) {
        strncpy_s(g_dir, sizeof(g_dir), "Z:\\tmp\\xr", _TRUNCATE);
    } else {
        strncpy_s(g_dir, sizeof(g_dir), "D:\\xrtemp", _TRUNCATE);
    }
}

bool WriteText(const char* path, const char* text) {
    // CREATE_ALWAYS and plain bytes: the runtime matches `version` with
    // startsWith, and a UTF-8 BOM makes "0.5" unrecognised, which yields total
    // silence rather than an error.
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(h, text, (DWORD)strlen(text), &written, nullptr);
    CloseHandle(h);
    return written == (DWORD)strlen(text);
}

void ReadSystem() {
    if (g_systemLogged) return;

    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\system", g_dir);

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    char buf[512] = {0};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;

    // Split into lines.
    char* lines[8] = {0};
    int nlines = 0;
    char* p = buf;
    while (*p && nlines < 8) {
        lines[nlines++] = p;
        char* nl = strpbrk(p, "\r\n");
        if (!nl) break;
        *nl = 0;
        p = nl + 1;
        while (*p == '\r' || *p == '\n') ++p;
    }

    // Do not latch a partial read: the runtime writes this at its own startup.
    if (nlines < 2 || !lines[0][0] || !lines[1][0]) return;

    char make[32] = {0}, product[64] = {0};
    strncpy_s(make, sizeof(make), lines[0], _TRUNCATE);
    strncpy_s(product, sizeof(product), lines[1], _TRUNCATE);

    const char* label = product;
    bool upsideDown = false;
    if (_stricmp(product, "EUREKA") == 0 || _stricmp(product, "PANTHER") == 0) { label = "Quest 3"; }
    else if (_stricmp(product, "HOLLYWOOD") == 0) { label = "Quest 2"; upsideDown = true; }
    else if (_stricmp(product, "SEACLIFF") == 0) { label = "Quest Pro"; upsideDown = true; }
    else if (_stricmp(product, "MONTEREY") == 0) { label = "Quest 1"; }

    Log("headset     : make='%s' product='%s' -> %s, hands %s",
        make, product, label, upsideDown ? "upside-down" : "upright");
    if (nlines < 7) Log("headset     : system has %d line(s), expected 7", nlines);

    g_systemLogged = true;
}

void TickHandshake() {
    // The runtime wipes this directory at its own startup, so a one-shot write
    // can be erased before it is read. Re-write cheaply; the writes are tiny.
    if (!DirExists(g_dir)) {
        CreateDirectoryA("D:\\xrtemp", nullptr);
        CreateDirectoryA("Z:\\tmp\\xr", nullptr);
        CreateDirectoryA(g_dir, nullptr);
    }

    char p[MAX_PATH];
    _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\version", g_dir);
    WriteText(p, "0.5\r\n");
    _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\vr", g_dir);
    WriteText(p, "VR\r\n");

    ReadSystem();
}

// ---------------------------------------------------------------------------
// Outbound packet: <Lhap> <Rhap> <MODE_VR> <MODE_3D> <FOVx> <FOVy>
// ---------------------------------------------------------------------------
void BuildOutPacket(char* out, int cap) {
    int mode3d = (g_modeVr == 1) ? g_mode3d : 0;   // clamp: never AER without VR

    char* w = out;
    char num[32];
    int n = FmtInt(num, g_hapL);    memcpy(w, num, n); w += n; *w++ = ' ';
    n = FmtInt(num, g_hapR);        memcpy(w, num, n); w += n; *w++ = ' ';
    n = FmtInt(num, g_modeVr);      memcpy(w, num, n); w += n; *w++ = ' ';
    n = FmtInt(num, mode3d);        memcpy(w, num, n); w += n; *w++ = ' ';
    n = FmtFixed2(num, g_fovH);     memcpy(w, num, n); w += n; *w++ = ' ';
    n = FmtFixed2(num, g_fovV);     memcpy(w, num, n); w += n;
    *w = 0;

    (void)cap;
}

// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------
char g_buf[4096];
char g_firstPacket[241];

DWORD WINAPI RecvThread(LPVOID) {
    sockaddr_in from;
    int fromLen = sizeof(from);

    while (InterlockedCompareExchange(&g_running, 1, 1) == 1) {
        fromLen = sizeof(from);
        int n = recvfrom(g_sockIn, g_buf, sizeof(g_buf) - 1, 0, (sockaddr*)&from, &fromLen);
        if (n <= 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) Sleep(1);
            else Sleep(10);
            continue;
        }
        g_buf[n] = 0;

        XrFrame f;
        if (!ParsePacket(g_buf, n, &f)) {
            EnterCriticalSection(&g_cs);
            ++g_rejected;
            LeaveCriticalSection(&g_cs);
            continue;
        }

        EnterCriticalSection(&g_cs);
        ++g_accepted;
        if (f.sync == g_lastAcceptedSync) ++g_duplicates;
        g_lastAcceptedSync = f.sync;
        f.serial = g_accepted;
        f.tickMs = GetTickCount();
        g_frame = f;
        g_haveFrame = true;
        bool first = !g_firstPacketLogged;
        if (first) {
            g_firstPacketLogged = true;
            strncpy_s(g_firstPacket, sizeof(g_firstPacket), g_buf, _TRUNCATE);
        }
        LeaveCriticalSection(&g_cs);

        if (first) {
            Log("xr first pkt: %s", g_firstPacket);
        }
    }
    return 0;
}

DWORD WINAPI SendThread(LPVOID) {
    DWORD lastKeep = GetTickCount();
    DWORD lastStats = lastKeep;
    DWORD lastPacket = 0;
    bool warnedSilent = false;

    while (InterlockedCompareExchange(&g_running, 1, 1) == 1) {
        char msg[128];
        BuildOutPacket(msg, sizeof(msg));
        if (g_sockOut != INVALID_SOCKET) {
            sendto(g_sockOut, msg, (int)strlen(msg), 0, (sockaddr*)&g_toOut, sizeof(g_toOut));
        }

        // Haptics are a duration in frames and the runtime only stores values
        // greater than zero, so a "stop" is a decay to zero, not a zero send.
        if (g_hapL > 0) --g_hapL;
        if (g_hapR > 0) --g_hapR;

        DWORD now = GetTickCount();
        if (now - lastKeep >= (DWORD)kKeepMs) {
            lastKeep = now;
            TickHandshake();
        }

        if (now - lastStats >= (DWORD)kStatsPeriod) {
            lastStats = now;

            unsigned acc, rej, dup;
            XrFrame f;
            bool have;
            EnterCriticalSection(&g_cs);
            acc = g_accepted; rej = g_rejected; dup = g_duplicates;
            f = g_frame; have = g_haveFrame;
            LeaveCriticalSection(&g_cs);

            if (acc != lastPacket) {
                lastPacket = acc;
                warnedSilent = false;
            }

            if (!have) {
                if (!warnedSilent) {
                    Log("xr          : no packets yet (rx=0, rejected=%u) - normal until the runtime is up, "
                        "and expected indefinitely at MODE_VR=0", rej);
                    warnedSilent = true;
                }
            } else {
                Log("xr          : rx=%u dup=%u rejected=%u sync=%d ipd=%.4f fov_in=%.2f/%.2f "
                    "hmd=(%.3f %.3f %.3f) q=(%.3f %.3f %.3f %.3f) btn=0x%05X flags=%c%c",
                    acc, dup, rej, f.sync, f.ipd, f.fovX, f.fovY,
                    f.hmd.px, f.hmd.py, f.hmd.pz,
                    f.hmd.qx, f.hmd.qy, f.hmd.qz, f.hmd.qw,
                    f.buttons, f.immersive ? 'T' : 'F', f.sbs ? 'T' : 'F');
            }
        }

        Sleep(kSendPeriod);
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool Xr_Start() {
    InitializeCriticalSection(&g_cs);
    g_csReady = true;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("xr          : WSAStartup failed");
        return false;
    }

    ResolveDir();
    CreateDirectoryA("D:\\xrtemp", nullptr);
    CreateDirectoryA("Z:\\tmp\\xr", nullptr);
    TickHandshake();

    Log("xr          : dir=%s (%s) version=0.5 vr=VR",
        g_dir, g_onDevice ? "device" : "desktop fallback");

    // One port only: from 0.4 the runtime sends the same packet to 7872 and
    // 7873, so binding both means handling every packet twice.
    g_sockIn = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sockIn == INVALID_SOCKET) {
        Log("xr          : socket() failed (%d)", WSAGetLastError());
        return false;
    }

    // Non-blocking receive: the loop drains naturally and keeps the newest.
    u_long nonblocking = 1;
    ioctlsocket(g_sockIn, FIONBIO, &nonblocking);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7872);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_sockIn, (sockaddr*)&addr, sizeof(addr)) != 0) {
        Log("xr          : bind 7872 failed (%d) - another client may hold it",
            WSAGetLastError());
        closesocket(g_sockIn);
        g_sockIn = INVALID_SOCKET;
        return false;
    }
    Log("xr          : listening on 7872");

    g_sockOut = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&g_toOut, 0, sizeof(g_toOut));
    g_toOut.sin_family = AF_INET;
    g_toOut.sin_port = htons(7278);
    g_toOut.sin_addr.s_addr = htonl(0x7F000001u);   // 127.0.0.1, no inet_addr

    InterlockedExchange(&g_running, 1);
    g_recvThread = CreateThread(nullptr, 0, RecvThread, nullptr, 0, nullptr);
    g_sendThread = CreateThread(nullptr, 0, SendThread, nullptr, 0, nullptr);
    return true;
}

void Xr_Stop() {
    if (InterlockedExchange(&g_running, 0) == 0) return;

    if (g_sockIn != INVALID_SOCKET) { closesocket(g_sockIn); g_sockIn = INVALID_SOCKET; }
    if (g_sockOut != INVALID_SOCKET) { closesocket(g_sockOut); g_sockOut = INVALID_SOCKET; }

    // `vr` is a live claim that this process wants VR; leaving it behind can
    // convince the runtime a dead process still wants it. `version` stays: it
    // describes the protocol this binary speaks.
    if (g_dir[0]) {
        char p[MAX_PATH];
        _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\vr", g_dir);
        DeleteFileA(p);
    }

    // Let the threads notice the closed sockets before the CRT tears down. Do
    // not join: this is called from DllMain.
    Sleep(100);
    WSACleanup();
}

bool Xr_GetFrame(XrFrame* out) {
    if (!out || !g_csReady) return false;
    EnterCriticalSection(&g_cs);
    bool have = g_haveFrame;
    if (have) *out = g_frame;
    LeaveCriticalSection(&g_cs);
    return have;
}

void Xr_Stats(unsigned* accepted, unsigned* rejected, unsigned* duplicates) {
    if (!g_csReady) return;
    EnterCriticalSection(&g_cs);
    if (accepted)   *accepted = g_accepted;
    if (rejected)   *rejected = g_rejected;
    if (duplicates) *duplicates = g_duplicates;
    LeaveCriticalSection(&g_cs);
}

void Xr_SetMode(int modeVr, int mode3d) {
    g_modeVr = modeVr;
    g_mode3d = (modeVr == 1) ? mode3d : 0;
}

void Xr_SetFovDegrees(float h, float v) {
    if (h > 1.0f) g_fovH = h;
    if (v > 1.0f) g_fovV = v;
}

void Xr_SetHaptics(int leftFrames, int rightFrames) {
    if (leftFrames > 0) g_hapL = (leftFrames > 100) ? 100 : leftFrames;
    if (rightFrames > 0) g_hapR = (rightFrames > 100) ? 100 : rightFrames;
}

bool Xr_PacketEverArrived() {
    if (!g_csReady) return false;
    EnterCriticalSection(&g_cs);
    bool have = g_haveFrame;
    LeaveCriticalSection(&g_cs);
    return have;
}

} // namespace cwxr
