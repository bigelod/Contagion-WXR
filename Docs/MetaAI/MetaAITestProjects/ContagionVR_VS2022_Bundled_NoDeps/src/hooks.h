
#pragma once
#include "math.h"
struct CUserCmd{ char pad0[12]; QAngle viewangles; };
struct CViewSetup{
    int x,y,w,h; bool ortho; float orthoLeft,orthoTop,orthoRight,orthoBottom;
    float fov; float fovViewmodel; float origin[3]; float angles[3];
    float zNear,zFar,zNearViewmodel,zFarViewmodel; float originViewmodel[3]; float anglesViewmodel[3]; float aspectRatio;
};
bool Hooks_Init();
void Hooks_Shutdown();
