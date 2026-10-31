
#pragma once
#include <cmath>
#include <algorithm>

struct Vec2 { float x=0,y=0; };
struct Vec3 { float x=0,y=0,z=0; };
struct Quat { float x=0,y=0,z=0,w=1; };
struct QAngle { float pitch=0,yaw=0,roll=0; };

inline Vec3 operator-(Vec3 a, Vec3 b){ return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator+(Vec3 a, Vec3 b){ return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator*(Vec3 a, float s){ return {a.x*s, a.y*s, a.z*s}; }

struct XrPose { Vec3 pos; Quat rot; bool valid=false; };

struct XrFrame {
    XrPose left; Vec2 leftStick{};
    XrPose right; Vec2 rightStick{};
    XrPose hmd;
    float ipd=0.063f;
    float fovH=104.5f;
    float fovV=104.5f;
    int frameId=0;
    // buttons from boolStr
    bool lGrip=false,lMenu=false,lThumbPress=false,lThumbLeft=false,lThumbRight=false,lThumbUp=false,lThumbDown=false,lTrigger=false,lX=false,lY=false;
    bool rA=false,rB=false,rGrip=false,rThumbPress=false,rThumbLeft=false,rThumbRight=false,rThumbUp=false,rThumbDown=false,rTrigger=false;
    bool hasData=false;
    double lastUpdate=0;
    char hmdType[16]{"META"};
};

void SetXrFrame(const XrFrame& f);
XrFrame GetXrFrameCopy();
Vec3 GetHeadDeltaClamped();
void SetRecenter();

// math helpers
Quat QuatInv(Quat q);
Vec3 QuatRotate(Quat q, Vec3 v);
QAngle QuatToSourceAngles(Quat q);
Vec3 OpenXRToSourcePos(Vec3 v);
