// ImGuiOverlay.h
// DirectX 12 hooking + ImGui overlay for subtitle rendering
// Marvel's GOTG Thai Localization Mod — "ImGui Overlay" protocol

#pragma once

#include <Windows.h>
#include <string>
#include <cstdint>

namespace sp { namespace io { class ps_ostream; } }

// Initialize the ImGui overlay system (hooking DXGI swap chain, loading fonts)
bool init_imgui_overlay();

// Shutdown the ImGui overlay system
void shutdown_imgui_overlay();

// Set the current subtitle text (called from ZSubtitlesField detour)
void set_overlay_subtitle(const std::string& thai_text, uint32_t duration_ms = 5000);

// Clear the current subtitle
void clear_overlay_subtitle();

// Get the current subtitle text (for debugging)
const std::string& get_overlay_subtitle();

// Check if overlay system is initialized
bool is_overlay_initialized();

// Set debug stream (called from DllMain)
void set_overlay_debug(sp::io::ps_ostream* dbg);
