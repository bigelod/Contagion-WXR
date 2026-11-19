// ContagionWXR launcher - start Contagion with the mod's environment in place.
//
// Why this exists
// ---------------
// 1. The version.dll proxy only loads if Wine resolves "version.dll" to ours.
//    That needs WINEDLLOVERRIDES=version=n,b, and setting it in a container by
//    hand is easy to get wrong or to lose. The launcher sets it, every time.
// 2. On the Quest the game is a Steam install run through a Steam emulator
//    (Goldberg), so there is no Steam client to start it correctly. One entry
//    point that knows where the game is and how to start it is worth having.
// 3. It is the obvious place for an auto-updater later: everything about "what
//    should be running" is decided here, in one file, before the game starts.
//
// It does nothing clever: read an optional ini, set an environment variable,
// CreateProcess, wait, log. No injection, no patching - the DLL does that.
//
// Log: one file per run, beside the launcher. Same rules as the mod's log:
// line 1 is the build stamp, and nothing is assumed to be fresh.

#include <windows.h>
#include <psapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")

static const char kStamp[] = "ContagionWXR launcher built " __DATE__ " " __TIME__;

static HANDLE g_log = INVALID_HANDLE_VALUE;
static char   g_logPath[MAX_PATH] = {0};

static void ExeDir(char* out, size_t cap, const char* selfPath) {
    out[0] = 0;
    if (!selfPath || !selfPath[0]) return;
    strncpy_s(out, cap, selfPath, _TRUNCATE);
    for (int i = (int)strlen(out) - 1; i >= 0; --i) {
        if (out[i] == '\\' || out[i] == '/') { out[i] = 0; break; }
    }
}

static void LogOpen(const char* dir) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char name[MAX_PATH];
    _snprintf_s(name, sizeof(name), _TRUNCATE,
                "contagion-wxr-launcher-%04d%02d%02d-%02d%02d%02d.log",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    _snprintf_s(g_logPath, sizeof(g_logPath), _TRUNCATE, "%s\\%s", dir, name);
    g_log = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

static void Log(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
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
    DWORD written = 0;
    WriteFile(g_log, line, (DWORD)n, &written, nullptr);
    FlushFileBuffers(g_log);
}

// Minimal "key = value" reader: one key per call, never more than one file.
static bool ReadIniValue(const char* iniPath, const char* key, char* out, size_t cap) {
    out[0] = 0;
    HANDLE f = CreateFileA(iniPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    char buf[4096] = {0};
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(f);

    char* line = buf;
    while (line && *line) {
        char* nl = strpbrk(line, "\r\n");
        if (nl) { *nl = 0; }
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p && *p != ';' && *p != '#') {
            char* eq = strchr(p, '=');
            if (eq) {
                *eq = 0;
                char* k = p;
                char* end = eq - 1;
                while (end >= k && (*end == ' ' || *end == '\t')) *end-- = 0;
                char* v = eq + 1;
                while (*v == ' ' || *v == '\t') ++v;
                char* ve = v + strlen(v);
                while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
                if (_stricmp(k, key) == 0) {
                    strncpy_s(out, cap, v, _TRUNCATE);
                    return true;
                }
            }
        }
        line = nl ? nl + 1 : nullptr;
    }
    return false;
}

static bool FileExists(const char* p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------------------------------------------------------------------------
// Injection
//
// Why: measured four times on 2026-11-17/18, the game creates its IDirect3D9
// *before* a version.dll proxy's DllMain runs, so a creation hook installed
// there never fires and no per-frame hook is ever installed. Starting the
// process suspended and loading the mod before its entry point removes the race
// instead of racing harder.
//
// The module is loaded by full path, so when the game's own import of
// "version.dll" is resolved later the loader finds the same module and does not
// run DllMain twice.
//
// Everything here is best-effort: if any step fails the launcher says so, resumes
// the process, and the version.dll proxy still gets the mod in - later than we
// wanted, but working.
// ---------------------------------------------------------------------------
static bool InjectDll(HANDLE proc, const char* dllPath) {
    const SIZE_T len = strlen(dllPath) + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        Log("inject      : VirtualAllocEx failed (%lu)", GetLastError());
        return false;
    }

    if (!WriteProcessMemory(proc, remote, dllPath, len, nullptr)) {
        Log("inject      : WriteProcessMemory failed (%lu)", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return false;
    }

    // LoadLibraryA lives in kernel32, which is mapped in the target at some base
    // we do not know. Take the offset from our own copy and apply it to the
    // target's base, rather than assuming the two are equal.
    HMODULE localK32 = GetModuleHandleA("kernel32.dll");
    FARPROC localLoad = localK32 ? GetProcAddress(localK32, "LoadLibraryA") : nullptr;
    if (!localK32 || !localLoad) {
        Log("inject      : could not resolve LoadLibraryA locally");
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return false;
    }

    HMODULE remoteK32 = nullptr;
    HMODULE mods[256];
    DWORD needed = 0;
    DWORD enumerated = 0;
    if (EnumProcessModules(proc, mods, sizeof(mods), &needed)) {
        const DWORD count = needed / sizeof(HMODULE);
        enumerated = count;
        char name[MAX_PATH];
        for (DWORD i = 0; i < count && i < 256; ++i) {
            name[0] = 0;
            if (GetModuleBaseNameA(proc, mods[i], name, MAX_PATH) &&
                _stricmp(name, "kernel32.dll") == 0) {
                remoteK32 = mods[i];
                break;
            }
        }
    }
    Log("inject      : target has %lu module(s), remote kernel32 %s", enumerated,
        remoteK32 ? "found" : "not identified");
    if (!remoteK32) {
        Log("inject      : assuming kernel32 is mapped at the same base as ours");
        remoteK32 = localK32;
    }

    const SIZE_T offset = (SIZE_T)((BYTE*)localLoad - (BYTE*)localK32);
    LPTHREAD_START_ROUTINE start = (LPTHREAD_START_ROUTINE)((BYTE*)remoteK32 + offset);
    Log("inject      : remote kernel32 0x%08X, LoadLibraryA 0x%08X",
        (unsigned)(uintptr_t)remoteK32, (unsigned)(uintptr_t)start);

    HANDLE th = CreateRemoteThread(proc, nullptr, 0, start, remote, 0, nullptr);
    if (!th) {
        Log("inject      : CreateRemoteThread failed (%lu)", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return false;
    }

    DWORD wait = WaitForSingleObject(th, 15000);
    DWORD code = 0;
    GetExitCodeThread(th, &code);
    CloseHandle(th);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);

    if (wait != WAIT_OBJECT_0) {
        Log("inject      : remote thread did not finish (wait=%lu)", wait);
        return false;
    }
    Log("inject      : LoadLibraryA returned 0x%08X", code);
    return code != 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char self[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, self, MAX_PATH);

    char dir[MAX_PATH] = {0};
    ExeDir(dir, sizeof(dir), self);
    LogOpen(dir);

    Log("%s", kStamp);
    Log("launcher    : %s", self);
    Log("game dir    : %s", dir);

    char ini[MAX_PATH];
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%s\\contagion-wxr-launcher.ini", dir);

    // Which executable. Default: beside the launcher.
    char exeName[MAX_PATH] = "contagion.exe";
    char tmp[MAX_PATH];
    if (ReadIniValue(ini, "exe", tmp, sizeof(tmp)) && tmp[0]) {
        strncpy_s(exeName, sizeof(exeName), tmp, _TRUNCATE);
    }
    char exePath[MAX_PATH];
    if (exeName[0] == '\\' || (exeName[0] && exeName[1] == ':')) {
        strncpy_s(exePath, sizeof(exePath), exeName, _TRUNCATE);
    } else {
        _snprintf_s(exePath, sizeof(exePath), _TRUNCATE, "%s\\%s", dir, exeName);
    }

    if (!FileExists(exePath)) {
        Log("ERROR       : game executable not found: %s", exePath);
        Log("              put the launcher beside contagion.exe, or set exe= in");
        Log("              contagion-wxr-launcher.ini");
        if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
        return 2;
    }
    Log("executable  : %s", exePath);

    // Is the mod actually installed? Say so plainly; a silent no-mod launch is
    // the thing a player reports as "it does nothing".
    char dll[MAX_PATH], dllO[MAX_PATH];
    _snprintf_s(dll, sizeof(dll), _TRUNCATE, "%s\\version.dll", dir);
    _snprintf_s(dllO, sizeof(dllO), _TRUNCATE, "%s\\version_o.dll", dir);
    Log("mod         : version.dll %s, version_o.dll %s",
        FileExists(dll) ? "present" : "MISSING",
        FileExists(dllO) ? "present" : "MISSING");
    if (!FileExists(dll) || !FileExists(dllO)) {
        Log("WARNING     : the mod will not load without both files at the game root");
    }

    // WINEDLLOVERRIDES is what makes Wine resolve version.dll to ours. On real
    // Windows the variable is inert, so the same launcher works in both places.
    char overrides[512] = {0};
    DWORD n = GetEnvironmentVariableA("WINEDLLOVERRIDES", overrides, sizeof(overrides));
    if (n == 0 || n >= sizeof(overrides)) overrides[0] = 0;

    if (!strstr(overrides, "version=")) {
        char updated[512];
        if (overrides[0]) {
            _snprintf_s(updated, sizeof(updated), _TRUNCATE, "%s;version=n,b", overrides);
        } else {
            strncpy_s(updated, sizeof(updated), "version=n,b", _TRUNCATE);
        }
        SetEnvironmentVariableA("WINEDLLOVERRIDES", updated);
        Log("env         : WINEDLLOVERRIDES='%s' (was '%s')", updated, overrides[0] ? overrides : "");
    } else {
        Log("env         : WINEDLLOVERRIDES already set: '%s'", overrides);
    }

    // Arguments: ini first, then anything passed to the launcher.
    char args[1024] = {0};
    if (ReadIniValue(ini, "args", tmp, sizeof(tmp)) && tmp[0]) {
        strncpy_s(args, sizeof(args), tmp, _TRUNCATE);
    }
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                char narrow[512] = {0};
                WideCharToMultiByte(CP_ACP, 0, argv[i], -1, narrow, sizeof(narrow), nullptr, nullptr);
                if (strlen(args) + strlen(narrow) + 2 < sizeof(args)) {
                    if (args[0]) strncat_s(args, sizeof(args), " ", _TRUNCATE);
                    strncat_s(args, sizeof(args), narrow, _TRUNCATE);
                }
            }
            LocalFree(argv);
        }
    }
    Log("arguments   : %s", args[0] ? args : "(none)");

    // Optional: skip the wait. Waiting keeps the exit code, which is worth
    // having while this is still being debugged.
    char waitText[16] = "1";
    ReadIniValue(ini, "wait", waitText, sizeof(waitText));
    bool wait = !(waitText[0] == '0');

    // Injection is on by default: it is the difference between the mod being
    // present before the game creates its D3D device and not.
    char injectText[16] = "1";
    ReadIniValue(ini, "inject", injectText, sizeof(injectText));
    const bool inject = !(injectText[0] == '0');

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    char cmdline[2048];
    if (args[0]) {
        _snprintf_s(cmdline, sizeof(cmdline), _TRUNCATE, "\"%s\" %s", exePath, args);
    } else {
        _snprintf_s(cmdline, sizeof(cmdline), _TRUNCATE, "\"%s\"", exePath);
    }

    Log("starting    : %s", cmdline);

    DWORD flags = inject ? CREATE_SUSPENDED : 0;
    BOOL ok = CreateProcessA(exePath, cmdline, nullptr, nullptr, FALSE, flags, nullptr, dir, &si, &pi);
    if (!ok && inject) {
        Log("inject      : CreateProcess(CREATE_SUSPENDED) failed (%lu); retrying normally",
            GetLastError());
        ok = CreateProcessA(exePath, cmdline, nullptr, nullptr, FALSE, 0, nullptr, dir, &si, &pi);
        flags = 0;
    }
    if (!ok) {
        Log("ERROR       : CreateProcess failed (%lu)", GetLastError());
        if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
        return 3;
    }

    Log("started     : pid %lu%s", pi.dwProcessId,
        (flags & CREATE_SUSPENDED) ? " (suspended)" : "");

    if (flags & CREATE_SUSPENDED) {
        if (InjectDll(pi.hProcess, dll)) {
            Log("inject      : ok - the mod is in before the entry point");
        } else {
            Log("inject      : failed - the version.dll proxy is the fallback");
        }
        ResumeThread(pi.hThread);
        Log("resumed     : main thread");
    }

    CloseHandle(pi.hThread);

    DWORD exitCode = 0;
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        Log("exited      : code %lu (0x%08X)", exitCode, exitCode);
    } else {
        Log("not waiting : launcher will exit, game keeps running");
    }
    CloseHandle(pi.hProcess);

    if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    return (int)exitCode;
}
