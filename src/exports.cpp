// exports.cpp - the proxy's export table, as linker export forwarders.
//
// Nothing is implemented here. Each name is forwarded to version_o.dll (a copy
// of the real 32-bit version.dll renamed and placed beside this file). The
// loader resolves a forwarder on first call, which means:
//
//   * version_o.dll is loaded by the loader, not by our DllMain - no LoadLibrary
//     under the loader lock (DEFECTS.md D08);
//   * the target's real calling convention and stack cleanup are used, so there
//     is no signature to get wrong. On x86 a wrong __stdcall prototype would
//     corrupt the caller's stack, and GetFileVersionInfoByHandle has no public
//     prototype to copy;
//   * we never manufacture a return value for a call we did not implement.
//
// The list must equal the export set of the real DLL. See version.def's comment
// for the two failure modes; tools/check-exports.ps1 compares them.
//
// Note: a DEF-file forwarder (`name=version_o.name`) is the same mechanism and
// reads better, but link.exe resolves it as an alias to an internal symbol
// instead of a forwarder with this toolset. The /export: directive does not have
// that problem.

#pragma comment(linker, "/export:GetFileVersionInfoA=version_o.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=version_o.GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA=version_o.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=version_o.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=version_o.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=version_o.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=version_o.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=version_o.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=version_o.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA=version_o.VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=version_o.VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=version_o.VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=version_o.VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=version_o.VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=version_o.VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=version_o.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=version_o.VerQueryValueW")
