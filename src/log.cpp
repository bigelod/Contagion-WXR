#include "log.h"

#include <bcrypt.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")

namespace cwxr {
namespace {

HANDLE        g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_cs;
bool          g_csReady = false;
char          g_path[MAX_PATH] = {0};
char          g_stamp[128] = {0};

const int kKeepLogs = 5;

void Lock()   { if (g_csReady) EnterCriticalSection(&g_cs); }
void Unlock() { if (g_csReady) LeaveCriticalSection(&g_cs); }

// Directory of the running executable. The game's own directory: writable on
// Windows, writable in the container, and the one place a human will look.
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

void JoinPath(char* out, size_t cap, const char* dir, const char* name) {
    if (dir[0] && dir[strlen(dir) - 1] == '\\') {
        _snprintf_s(out, cap, _TRUNCATE, "%s%s", dir, name);
    } else {
        _snprintf_s(out, cap, _TRUNCATE, "%s\\%s", dir, name);
    }
}

// Logs live in one fixed folder on both the PC and the headset, so a human
// never has to guess which game directory a run used. CWXR_LOGDIR overrides it;
// the smoke tests use that to keep a throwaway host out of the real folder.
const char kDefaultLogDir[] = "D:\\ContagionWXR\\Logs";

bool EnsureDirectory(const char* path) {
    if (!path || !path[0]) return false;

    char tmp[MAX_PATH];
    strncpy_s(tmp, sizeof(tmp), path, _TRUNCATE);

    // Create each component in turn. Skip the separator after a drive letter.
    for (size_t i = 1; tmp[i]; ++i) {
        if (tmp[i] != '\\' && tmp[i] != '/') continue;
        if (tmp[i - 1] == ':') continue;
        tmp[i] = 0;
        CreateDirectoryA(tmp, nullptr);
        tmp[i] = '\\';
    }
    if (CreateDirectoryA(tmp, nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool PreferredLogDir(char* out, size_t cap) {
    char env[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("CWXR_LOGDIR", env, sizeof(env)) > 0 && env[0]) {
        if (EnsureDirectory(env)) {
            strncpy_s(out, cap, env, _TRUNCATE);
            return true;
        }
    }
    if (EnsureDirectory(kDefaultLogDir)) {
        strncpy_s(out, cap, kDefaultLogDir, _TRUNCATE);
        return true;
    }
    return false;
}

// Delete all but the newest kKeepLogs files matching our prefix.
void PruneOldLogs(const char* dir) {
    // "contagion-wxr-2*.log" deliberately excludes the launcher's own logs,
    // which share the prefix but are not ours to delete.
    char pattern[MAX_PATH];
    JoinPath(pattern, sizeof(pattern), dir, "contagion-wxr-2*.log");

    struct Entry { FILETIME ft; char name[MAX_PATH]; };
    Entry found[64];
    int count = 0;

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= 64) break;
        found[count].ft = fd.ftLastWriteTime;
        strncpy_s(found[count].name, MAX_PATH, fd.cFileName, _TRUNCATE);
        ++count;
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    // Newest first: simple selection sort, count is small.
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (CompareFileTime(&found[j].ft, &found[i].ft) > 0) {
                Entry t = found[i]; found[i] = found[j]; found[j] = t;
            }
        }
    }
    for (int i = kKeepLogs; i < count; ++i) {
        char full[MAX_PATH];
        JoinPath(full, sizeof(full), dir, found[i].name);
        DeleteFileA(full);
    }
}

static bool NtOk(NTSTATUS s) { return s >= 0; }
bool Sha256File(const char* path, char* hexOut, size_t hexCap) {
    hexOut[0] = 0;

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    PUCHAR obj = nullptr;
    bool ok = false;

    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    DWORD objLen = 0, cb = 0, hashLen = 0, digestLen = 0;
    UCHAR digest[64];

    do {
        if (!NtOk(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) break;
        if (!NtOk(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0))) break;
        if (!NtOk(BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cb, 0))) break;
        if (objLen == 0 || hashLen > sizeof(digest)) break;

        obj = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, objLen);
        if (!obj) break;
        if (!NtOk(BCryptCreateHash(alg, &hash, obj, objLen, nullptr, 0, 0))) break;

        static BYTE buf[64 * 1024];
        DWORD read = 0;
        while (ReadFile(f, buf, sizeof(buf), &read, nullptr) && read > 0) {
            if (!NtOk(BCryptHashData(hash, buf, read, 0))) { ok = false; break; }
            ok = true;
        }
        if (!ok) break;
        if (!NtOk(BCryptFinishHash(hash, digest, hashLen, 0))) { ok = false; break; }
        ok = true;
        digestLen = hashLen;

        size_t pos = 0;
        for (DWORD i = 0; i < digestLen && pos + 2 < hexCap; ++i) {
            pos += _snprintf_s(hexOut + pos, hexCap - pos, _TRUNCATE, "%02X", digest[i]);
        }
    } while (false);

    if (hash) BCryptDestroyHash(hash);
    if (obj)  HeapFree(GetProcessHeap(), 0, obj);
    if (alg)  BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);

    if (!ok) hexOut[0] = 0;
    return ok && hexOut[0] != 0;
}

void WriteRaw(const char* text, int len) {
    if (g_file == INVALID_HANDLE_VALUE || len <= 0) return;
    DWORD written = 0;
    WriteFile(g_file, text, (DWORD)len, &written, nullptr);
    // Flushed per line: on a device with no debugger, a crash must not take the
    // last twenty lines with it. Volume is a few lines per second.
    FlushFileBuffers(g_file);
}

} // namespace

bool LogInit(HMODULE self, const char* buildStamp) {
    InitializeCriticalSection(&g_cs);
    g_csReady = true;

    if (buildStamp) strncpy_s(g_stamp, sizeof(g_stamp), buildStamp, _TRUNCATE);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char dir[MAX_PATH] = {0};
    if (!PreferredLogDir(dir, sizeof(dir))) {
        // D: not present or not writable: behave as before, beside the game.
        ExeDir(dir, sizeof(dir));
    }

    char name[MAX_PATH];
    _snprintf_s(name, sizeof(name), _TRUNCATE, "contagion-wxr-%04d%02d%02d-%02d%02d%02d.log",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    if (dir[0]) {
        JoinPath(g_path, sizeof(g_path), dir, name);
        // Prune *before* creating this run's file. Pruning afterwards could
        // delete the file we are writing: on device storage the timestamp
        // granularity is two seconds, so the new file can tie with the previous
        // run's and lose the "keep the newest five" sort while still open.
        PruneOldLogs(dir);
        // CREATE_ALWAYS: never append to a previous run's file, whatever the
        // filesystem thinks about creation times (DEFECTS.md D40).
        g_file = CreateFileA(g_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    if (g_file == INVALID_HANDLE_VALUE) {
        // Read-only install, or a directory we cannot write. TEMP always exists.
        char tmp[MAX_PATH] = {0};
        if (GetTempPathA(MAX_PATH, tmp) > 0) {
            JoinPath(g_path, sizeof(g_path), tmp, name);
            g_file = CreateFileA(g_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }

    if (g_file == INVALID_HANDLE_VALUE) {
        g_path[0] = 0;
        return false;
    }

    // Line 1: the build stamp. This must be the first thing in the file.
    Log("%s", g_stamp[0] ? g_stamp : "(no build stamp)");

    // Line 2: which copy loaded. Not academic - the proxy can be picked up from
    // a different directory than the one we think we deployed to.
    char mod[MAX_PATH] = {0};
    if (self && GetModuleFileNameA(self, mod, MAX_PATH)) {
        Log("module  : %s", mod);
    } else {
        Log("module  : (unknown)");
    }

    // Line 3: the module's own SHA-256, so the owner can compare four lines
    // against Release\ContagionWXR.manifest.txt and know the right binary is
    // running without trusting a filename or a timestamp.
    char hex[80] = {0};
    if (mod[0] && Sha256File(mod, hex, sizeof(hex))) {
        Log("sha256  : %s", hex);
    } else {
        Log("sha256  : (unavailable)");
    }

    char exe[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) Log("process : %s", exe);

    // Line 5: where this file is. Answers "which copy is this?" from the log
    // alone, including when the preferred folder was not available.
    Log("logfile : %s", g_path);

    return true;
}

void LogShutdown() {
    if (g_file != INVALID_HANDLE_VALUE) {
        Log("shutdown");
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    if (g_csReady) {
        g_csReady = false;
        DeleteCriticalSection(&g_cs);
    }
}

void Log(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE || !fmt) return;

    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[2200];
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] %s\r\n",
                        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);

    Lock();
    WriteRaw(line, n);
    Unlock();
}

const char* LogPath() { return g_path; }

} // namespace cwxr
