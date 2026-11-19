// dllmain.cpp - milestone 2: prove the proxy loads and writes a log.
//
// Nothing here hooks the game. The deliverable is a file with a build stamp in
// it, written from inside contagion.exe. Everything else waits for that.

#include <windows.h>
#include <cstdlib>

#include "d3d9hook.h"
#include "camera.h"
#include "log.h"
#include "recon.h"
#include "xrapi.h"

// Logged as line 1 of every run. Defined in buildstamp.cpp, which the build
// script regenerates on every build so the stamp cannot outlive an incremental
// compile of this file.
extern const char kBuildStamp[];

namespace {

const DWORD kClientWaitMs    = 45000;  // how long to wait for client.dll
const DWORD kClientPollMs    = 250;
const DWORD kReconSettleMs   = 750;    // let the engine finish its own init

DWORD WINAPI InitThread(LPVOID) {
    cwxr::Log("init thread started");

    // The d3d9 Create9 hooks are already installed in DllMain. D3d9_Init is
    // idempotent, so this only covers the non-injected path.
    cwxr::D3d9_Init();

    // Then the XrApi client, so no packets are missed while we wait for the
    // game's own modules.
    if (!cwxr::Xr_Start()) {
        cwxr::Log("xr          : start failed");
    }

    // Poll rather than Sleep(1200): on a Quest 3 under Box86 the fixed sleeps in
    // the inherited code are simply wrong, and they are wrong differently on
    // every device (DEFECTS.md D32). Wait for the actual condition.
    //
    // CWXR_CLIENT_WAIT_MS exists so the same binary can be smoke-tested in a
    // host process that will never load client.dll.
    DWORD waitMs = kClientWaitMs;
    char env[32] = {0};
    if (GetEnvironmentVariableA("CWXR_CLIENT_WAIT_MS", env, sizeof(env)) > 0) {
        int v = atoi(env);
        if (v >= 0) waitMs = (DWORD)v;
    }

    DWORD waited = 0;
    while (waited < waitMs && !GetModuleHandleA("client.dll")) {
        Sleep(kClientPollMs);
        waited += kClientPollMs;
    }

    HMODULE client = GetModuleHandleA("client.dll");
    if (client) {
        cwxr::Log("client.dll  : 0x%08X after %lu ms", (unsigned)(uintptr_t)client, waited);
    } else {
        cwxr::Log("client.dll  : NOT LOADED after %lu ms - dumping anyway", waited);
    }

    Sleep(kReconSettleMs);

    // Milestone 6a. Off unless CWXR_CAMPROBE is set; it only observes.
    cwxr::Camera_ProbeInstall();

    cwxr::ReconDump();

    cwxr::Log("init thread done");
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);

        // Open the log immediately, still under the loader lock, because line 1
        // has to be the build stamp and nothing else may get there first.
        // Deliberately no LoadLibrary here: version_o.dll resolves through the
        // export forwarders on first call, so the loader-lock hazard in
        // DEFECTS.md D08 never arises.
        cwxr::LogInit(self, kBuildStamp);
        cwxr::Log("attach      : proxy version.dll loaded");

        // Install the d3d9 hooks here, synchronously, before returning.
        //
        // Measured 2026-11-18: the game creates its IDirect3D9 within ~2 s of
        // its entry point, and an init thread started from DllMain was 3 s late
        // - the hook installed, printed "first byte 0xE9", and was never called.
        // When the launcher injects, this runs while the game is still suspended,
        // so there is no concurrency to worry about: no other thread exists yet.
        cwxr::D3d9_Init();

        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) {
            CloseHandle(t);
        } else {
            cwxr::Log("attach      : CreateThread failed (%lu)", GetLastError());
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Do not log much here: the process is going away and other threads are
        // already being torn down. Stop the sockets (which also removes `vr`),
        // then close the log.
        cwxr::Xr_Stop();
        cwxr::LogShutdown();
    }
    return TRUE;
}
