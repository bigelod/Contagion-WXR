// camera.h - milestone 6a/6b: the game's camera setup.
#pragma once

namespace cwxr {

// Installs the IViewRender vtable probe. Off unless one of the CWXR_CAM*
// variables is set, and it only observes unless CWXR_CAM_YAW is non-zero.
//
//   CWXR_CAMPROBE=1     log the first calls to every slot, with arguments
//   CWXR_CAM_SLOT=n     slot that receives the live CViewSetup (default 7)
//   CWXR_CAM_YAW=deg    add this yaw offset to the setup every frame
//
// The setup lives at g_pViewRender+4 and was identified on 2026-11-18: origin
// (-231, -98, -2795), yaw -123, fov 73.7 - plausible world values, not a
// guessed layout. See CAMERA.md.
void Camera_ProbeInstall();

} // namespace cwxr
