// GhostOverlay.h
// External transparent Win32 overlay for Thai subtitle rendering
// Marvel's GOTG Thai Localization Mod — "Ghost Overlay" protocol
// Zero interference with the game's DirectX 12 render pipeline

#pragma once

#include <Windows.h>
#include <string>
#include <cstdint>

// Forward declare
namespace sp { namespace io { class ps_ostream; } }

// Initialize the ghost overlay (spawns detached thread, creates transparent window)
bool init_ghost_overlay();

// Shutdown the ghost overlay (destroys window, terminates thread)
void shutdown_ghost_overlay();

// Set the current subtitle text (called from ZSubtitlesField detour)
void set_overlay_subtitle(const std::string& thai_text, uint32_t duration_ms = 5000);

// Clear the current subtitle
void clear_overlay_subtitle();

// Check if overlay is initialized
bool is_overlay_initialized();

// Set debug stream
void set_overlay_debug(sp::io::ps_ostream* dbg);
