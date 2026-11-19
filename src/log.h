// log.h - the only diagnostic channel we have on a standalone headset.
//
// Rules this implements, from Docs/OpenCode/DeepSeek41Flash/PLAN.md 2.1:
//   * one file per run, not a fixed name (NTFS tunnelling can append runs)
//   * line 1 is the build stamp, before anything else
//   * line 2 is the module that loaded, line 3 its own SHA-256
//   * flushed per line, so a crash keeps everything up to the crash
#pragma once

#include <windows.h>

namespace cwxr {

// Opens the log. Must be called from DllMain, before any other output.
// `self` is this DLL's module handle; used for the module path and self-hash.
bool LogInit(HMODULE self, const char* buildStamp);

void LogShutdown();

// printf-style. Safe to call before LogInit (output is dropped).
void Log(const char* fmt, ...);

// The file we are writing, or "" if logging failed.
const char* LogPath();

} // namespace cwxr
