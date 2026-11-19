// d3d9hook.h - per-frame access to the game's own D3D9 device.
//
// The device is captured rather than searched for: the mod hooks
// d3d9.dll!Direct3DCreate9, returns its own thin IDirect3D9 to the game, and
// therefore receives the real IDirect3DDevice9 in CreateDevice (PLAN.md 6,
// SIBLING_PORTS.md 2.4 / 3.1). Present/BeginScene/EndScene on that device are
// then hooked so drawing can happen once per frame.
#pragma once

namespace cwxr {

// Installs the Direct3DCreate9 hooks. Idempotent, and cheap enough to call from
// DllMain. Returns false only if d3d9.dll could not be reached or MinHook
// refused; the mod still works without a per-frame hook.
bool D3d9_Init();
void D3d9_Shutdown();

// Counts since start. The Present:EndScene ratio is the "is EndScene once per
// frame?" answer.
void D3d9_Stats(unsigned* present, unsigned* endScene, unsigned* beginScene);

// True once a real device has been captured and its Present hooked.
bool D3d9_Hooked();

// Back-buffer size from the game's D3DPRESENT_PARAMETERS, once a device has
// been created. False until then.
bool D3d9_BackBufferSize(int* width, int* height);

} // namespace cwxr
