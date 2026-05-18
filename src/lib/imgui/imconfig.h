// imconfig.h — ImGui user configuration for GOTG Thai Mod overlay
// This is a minimal config — we don't use stb headers (no image loading needed)

#pragma once

// Disable features we don't need to reduce binary size
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_DEMO_WINDOWS

// No image loading needed for subtitle overlay
#define IMGUI_DISABLE_STB_IMPLEMENTATION

// Use standard Win32 API for file dialogs (not needed but prevents link errors)
// No external library deps
