#pragma once
#include <windows.h>
#include <cstdio>

static constexpr LONG SAM4_GUI_MAGIC = 0x53414D34; // SAM4
static constexpr LONG SAM4_GUI_VERSION = 1;

struct SharedSettings {
    LONG magic = SAM4_GUI_MAGIC;
    LONG version = SAM4_GUI_VERSION;
    volatile LONG aim = 1;
    volatile LONG esp = 1;
    volatile LONG tracers = 0;
    volatile LONG espStabilize = 1;
    volatile LONG wall = 1;
    volatile LONG silent = 0;
    volatile LONG modelFacing = 0;
    volatile LONG antiAim = 0;
    volatile LONG prediction = 0;
    volatile LONG priority = 1;
    volatile LONG menuOpen = 0;
    volatile LONG requestExit = 0;
    volatile float fov = 40.f;
    volatile float distance = 100.f;
    volatile float cameraFov = 120.f;
    volatile float spinSpeed = 180.f;
    volatile float projectileSpeed = 80.f;
    volatile float projectileGravity = 0.f;
    char status[384]{};
};

inline void sharedName(DWORD pid, wchar_t (&out)[64]) {
    swprintf_s(out, L"Local\\Sam4AimShared_%lu", static_cast<unsigned long>(pid));
}
