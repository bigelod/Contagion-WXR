
#pragma once
#include "math.h"
#include "hooks.h"

enum class PlayerClass { Human, Zombie, Unknown };

// Call from hkCreateMove - overwrites cmd with VR input when in VR mode
void Input_Update(CUserCmd* cmd, const XrFrame& fr, bool isMenuOpen);

// Blocking
bool Input_ShouldBlockKeyboardMouse();
void Input_SetPlayerClass(PlayerClass c);
PlayerClass Input_DetectPlayerClass(); // tries to read from game, returns Unknown if not found

// Config
struct InputConfig {
    float moveDeadzone = 0.15f;
    float turnDeadzone = 0.15f;
    float turnSpeedDegPerSec = 90.0f; // smooth turn
    float forwardScale = 450.0f;
    float sideScale = 450.0f;
};
