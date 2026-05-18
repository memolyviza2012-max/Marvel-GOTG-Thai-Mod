// GOTG_Translation.h
// Main header for Marvel's GOTG Thai Localization Mod

#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

#define SP_ERR_FILE_NOT_FOUND 2

// Version info
#define GOTG_MOD_VERSION "1.0.0"
#define GOTG_MOD_AUTHOR "Thai Modding Community"

// Target game info
#define TARGET_GAME "Marvel's Guardians of the Galaxy"
#define TARGET_ENGINE "Dawn Engine (Similar to Deus Ex MD)"
#define TARGET_PLATFORM "Epic Games Store"

// Feature flags
#define SUPPORT_UNICODE 1
#define SUPPORT_FONT_INJECTION 1
#define SUPPORT_JSON_TRANSLATIONS 1
#define DEBUG_STRING_CAPTURE 0

// ============================================================================
// EXTERNALS — exported variables declared in DllMain.cpp
// All declared with extern "C" to avoid C++ name mangling
// ============================================================================
extern "C" {
    // gotg.exe base address — set during DLL_PROCESS_ATTACH
    extern uint64_t gotg_base;
}