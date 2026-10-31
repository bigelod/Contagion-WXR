
#pragma once
#include "math.h"

struct VoiceOption {
    char label[48];
    char command[96];
};

extern VoiceOption g_voiceOptions[8];
extern const int g_voiceCount;

void VoiceWheel_Init();
void VoiceWheel_Update(const XrFrame& fr, bool menuOpen);
bool VoiceWheel_IsActive();
int VoiceWheel_GetSelected();
void VoiceWheel_Draw(); // called from D3D hook - does nothing if not active, actual draw done in d3d hook

// For input.cpp to check blocking
bool VoiceWheel_ShouldBlockGameInput();
