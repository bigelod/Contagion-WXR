// xrapi.h - the XrApi 0.5 client.
//
// Protocol: Docs/general_wxr/XRAPI_V0_4_DESC.md. Handshake paths are the owner's
// decision of 2026-11-17 (see DEPLOY.md 5):
//
//     Z:\tmp exists -> Z:\tmp\xr      (device)
//     otherwise     -> D:\xrtemp      (desktop test)
//
// Nothing here touches the game. It owns two sockets and a few files, and hands
// out the newest parsed frame.
#pragma once

#include <windows.h>

namespace cwxr {

struct XrPose {
    float px, py, pz;      // metres, OpenXR: +X right, +Y up, -Z forward
    float qx, qy, qz, qw;  // normalised on ingest
    bool  valid;
};

struct XrFrame {
    XrPose  left, right, hmd;
    float   lstickX, lstickY, rstickX, rstickY;
    float   ipd;           // metres (float 25)
    float   fovX, fovY;    // inbound floats 26/27, exactly as received
    int     sync;          // HMD_SYNC, the pose's identity
    unsigned buttons;      // bit i = bool-string index i
    bool    immersive, sbs;// flags token: chars 19 and 20
    bool    valid;
    unsigned serial;       // our own count of accepted packets
    DWORD   tickMs;        // GetTickCount when accepted
};

bool Xr_Start();
void Xr_Stop();

// Copies the newest accepted frame. Returns false if none has arrived yet.
bool Xr_GetFrame(XrFrame* out);

// How many packets have been parsed, and how many were rejected as malformed.
void Xr_Stats(unsigned* accepted, unsigned* rejected, unsigned* duplicates);

// Outbound state. Mode is clamped: MODE_3D is forced to 0 unless MODE_VR == 1,
// because MODE_VR=0 + AER breaks WinlatorXR (owner, 2026-11-17).
void Xr_SetMode(int modeVr, int mode3d);
void Xr_SetFovDegrees(float h, float v);
void Xr_SetHaptics(int leftFrames, int rightFrames);

bool Xr_PacketEverArrived();

} // namespace cwxr
