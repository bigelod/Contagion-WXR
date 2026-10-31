
#include "math.h"
#include <mutex>
#include <cmath>

static std::mutex g_mtx;
static XrFrame g_frame;
static Vec3 g_hmdZero{0,0,0};
static bool g_hasZero=false;
static const float kMaxLeanUnits = 6.0f; // ~15cm anti peek
static const float kUnitsPerMeter = 39.37f;

void SetXrFrame(const XrFrame& f){
    std::lock_guard<std::mutex> l(g_mtx);
    g_frame = f;
    g_frame.hasData = true;
    if(!g_hasZero && f.hmd.valid){
        g_hmdZero = f.hmd.pos;
        g_hasZero = true;
    }
}
XrFrame GetXrFrameCopy(){
    std::lock_guard<std::mutex> l(g_mtx);
    return g_frame;
}
void SetRecenter(){
    std::lock_guard<std::mutex> l(g_mtx);
    if(g_frame.hmd.valid) g_hmdZero = g_frame.hmd.pos;
}
Vec3 GetHeadDeltaClamped(){
    std::lock_guard<std::mutex> l(g_mtx);
    if(!g_hasZero || !g_frame.hmd.valid) return {0,0,0};
    Vec3 d = {g_frame.hmd.pos.x - g_hmdZero.x,
              g_frame.hmd.pos.y - g_hmdZero.y,
              g_frame.hmd.pos.z - g_hmdZero.z};
    // OpenXR: +X right, +Y up, -Z forward -> Source: convert
    // Your earlier working: -d.z, -d.x, d.y
    Vec3 src = { -d.z, -d.x, d.y };
    src = src * kUnitsPerMeter;
    src.x = std::clamp(src.x, -kMaxLeanUnits, kMaxLeanUnits);
    src.y = std::clamp(src.y, -kMaxLeanUnits, kMaxLeanUnits);
    src.z = std::clamp(src.z, -kMaxLeanUnits, kMaxLeanUnits);
    return src;
}
Vec3 OpenXRToSourcePos(Vec3 v){ return { -v.z, -v.x, v.y }; }
Quat QuatInv(Quat q){ return {-q.x,-q.y,-q.z,q.w}; }
Vec3 QuatRotate(Quat q, Vec3 v){
    Quat vq{v.x,v.y,v.z,0};
    Quat t{ q.w*vq.x + q.y*vq.z - q.z*vq.y,
            q.w*vq.y + q.z*vq.x - q.x*vq.z,
            q.w*vq.z + q.x*vq.y - q.y*vq.x,
            -q.x*vq.x - q.y*vq.y - q.z*vq.z };
    Quat r{ t.x*q.w + t.w*-q.x + t.y*-q.z - t.z*-q.y,
            t.y*q.w + t.w*-q.y + t.z*-q.x - t.x*-q.z,
            t.z*q.w + t.w*-q.z + t.x*-q.y - t.y*-q.x,
            0 };
    return {r.x,r.y,r.z};
}
QAngle QuatToSourceAngles(Quat q){
    // Source engine: yaw Z, pitch Y, roll X
    float ys = 2.0f*(q.y*q.z + q.w*q.x);
    float yc = 1.0f - 2.0f*(q.x*q.x + q.y*q.y);
    float pitch = std::atan2(ys, yc);

    float ps = 2.0f*(q.w*q.y - q.z*q.x);
    ps = std::clamp(ps, -1.0f, 1.0f);
    float yaw = std::asin(ps);

    float rs = 2.0f*(q.x*q.y + q.w*q.z);
    float rc = 1.0f - 2.0f*(q.y*q.y + q.z*q.z);
    float roll = std::atan2(rs, rc);

    QAngle ang;
    ang.pitch = -pitch * 57.29578f;
    ang.yaw = yaw * 57.29578f;
    ang.roll = roll * 57.29578f;
    return ang;
}
