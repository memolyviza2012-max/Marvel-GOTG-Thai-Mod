// TranslationHooks.h
// MinHook detour framework for Marvel's GOTG Thai Localization Mod
// Dawn Engine string interception via DLL chain (version.dll)

#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

// Forward declare ps_ostream so TranslationHooks.h does not pull in all sp
// headers
namespace sp {
namespace io {
class ps_ostream;
}
} // namespace sp

// MinHook feature detection
#ifdef MINHOOK_AVAILABLE
#include <MinHook.h>
#endif

#ifdef GOTGTRANSLATION_EXPORTS
#define GOTG_API __declspec(dllexport)
#else
#define GOTG_API __declspec(dllimport)
#endif

// ============================================================================
// EXTERNAL LINKAGE — Exported variables defined in DllMain.cpp
// All declared with extern "C" so C++ name mangling does not break linking
// ============================================================================
extern "C" {
// version.dll export locations (for chaining)
GOTG_API extern UINT_PTR export_locs[15];

// Mod enable/disable flags
GOTG_API extern uint8_t translations_enabled;
GOTG_API extern uint8_t debug_string_capture;

// Dawn Engine memory addresses (discovered via pattern scanning)
GOTG_API extern void *prehook_inject_addr;
GOTG_API extern void *prehook_ret;
GOTG_API extern void *textlist_installer_func;
GOTG_API extern void *get_mem_mgr_func;
GOTG_API extern void *textlist_str_alloc_call_instruction;
GOTG_API extern void *textlist_str_alloc_func;
GOTG_API extern void *loadingscreen_startsubs_func;
GOTG_API extern void *loadingscreen_startsubs_get_subs_data_instr;
GOTG_API extern void *str_eq_operator_func;
GOTG_API extern void *uielement_playvid_func;
GOTG_API extern void *uicredits_playvid_func;
GOTG_API extern void *submgr_startsubs_get_subs_data_instr;
GOTG_API extern void *vidscreen_init_func;
GOTG_API extern void *menuscreen_init_func;
GOTG_API extern void *renderplayer_start_hook_addr;
GOTG_API extern void *ui_font_addr_hook_addr;
GOTG_API extern void *ui_font_replace_hook_addr;
GOTG_API extern void *resid_record_mapping_func;

// gotg.exe base address — set by DllMain.cpp during DLL_PROCESS_ATTACH
GOTG_API extern uint64_t gotg_base;
GOTG_API extern uint64_t gotg_file_size;

// Runtime state tracking
GOTG_API extern uint64_t textlist_res_id;
GOTG_API extern uint32_t textlist_str_id;
GOTG_API extern uint64_t video_res_id;
}

// ============================================================================
// TARGET FUNCTION SIGNATURES — Dawn Engine string rendering
// ============================================================================

// String allocation function signature
typedef const char *(__thiscall *StringAlloc_t)(void *, const char *);

// String equality/comparison function
typedef bool(__thiscall *StringEq_t)(const char *, const char *);

// UI text rendering function
typedef void(__thiscall *UITextRender_t)(void *, const char *);

// Memory manager retrieval function
typedef void *(__thiscall *GetMemMgr_t)(void);

// Subtitle loading function
typedef void(__thiscall *SubtitleLoad_t)(uint64_t, uint32_t);

// String registry dump — called from StringAlloc detour
typedef void(__thiscall *StringAllocCallback_t)(const char* original_str, const char* pool_str);

// ZSubtitlesField::SetText — verified at offset 0x1BFB5A0 from gotg.exe base
// Signature: void __fastcall(void* pThis, void* pZString)
typedef void(__fastcall *ZSubtitlesField_SetText_t)(void*, void*);

// FT_New_Memory_Face — FreeType function that loads fonts from memory buffers
// Signature: FT_Error FT_New_Memory_Face(FT_Library library, const unsigned char* file_base,
//                                        long file_size, long face_index, FT_Face* aface)
// RVA: 0x1DB69D30 (verified via PDB symbols)
typedef int(__cdecl *FT_New_Memory_Face_t)(void* library, const unsigned char* file_base,
                                            long file_size, long face_index, void** aface);

// ZTextBundle::TryGetText — Universal text lookup function (Dawn Engine localization)
// Overload 1 (RVA: 0x0060DAD0, 680 bytes):
//   bool ZTextBundle::TryGetText(const ZString& key, ZString& outResult, bool fallback) const
// Overload 2 (RVA: 0x0060DD80, 275 bytes):
//   bool ZTextBundle::TryGetText(ZCaseInsensitiveHashedString hashKey, ZString& outResult) const
//
// Calling convention: __thiscall (x64 __fastcall with this in RCX)
// Signature verified via PDB mangled name: ?TryGetText@ZTextBundle@@QEBA_NAEBVZString@@AEAV2@_N@Z
typedef bool(__fastcall *ZTextBundle_TryGetText_t)(void* pThis, void* pKey, void* pOutResult, bool fallback);

// ZString memory layout (Dawn Engine)
// Offset 0x00: char* text_ptr
// Offset 0x08: uint32_t length (chars, not bytes)
// Offset 0x0C: uint32_t capacity
struct ZStringLayout {
    char*   text_ptr;    // 0x00
    uint32_t length;    // 0x08
    uint32_t capacity;  // 0x0C
    // Padding out to 32 bytes for safety
    uint8_t  _reserved[16];
};


// ============================================================================
// HOOK MANAGEMENT
// ============================================================================

// Hook status enumeration
enum class GotgHookStatus : int {
  OK = 0,
  ERR_MINHOOK_INIT = -1,
  ERR_MINHOOK_CREATE = -2,
  ERR_MINHOOK_ENABLE = -3,
  ERR_INVALID_TARGET = -4,
  ERR_NO_PATTERN = -5
};

// Install all MinHook detours
GotgHookStatus install_all_hooks();

// Remove all MinHook detours
GotgHookStatus uninstall_all_hooks();

// Enable/disable translation system at runtime
void set_translations_enabled(bool enabled);
bool get_translations_enabled();

// ============================================================================
// DETOUR FUNCTIONS — Dawn Engine hooks
// ============================================================================

const char *detour_string_alloc(void *mem_mgr, const char *original_str);
bool detour_string_eq(const char *str1, const char *str2);
void *detour_get_mem_mgr();
void detour_subtitle_load(uint64_t res_id, uint32_t str_id);
void __fastcall detour_ZSubtitlesField_SetText(void *pThis, void *pZString);
int __cdecl detour_FT_New_Memory_Face(void* library, const unsigned char* file_base,
                                       long file_size, long face_index, void** aface);
bool __fastcall detour_ZTextBundle_TryGetText(void* pThis, void* pKey, void* pOutResult, bool fallback);

// Trigger string dump to JSON on demand
void trigger_string_dump();

// ============================================================================
// TRANSLATION DICTIONARY
// ============================================================================

void load_translation_json(const char *fpath);
const char *lookup_translation(const char *original_str);
int get_translation_count();

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

std::string calculate_file_md5(const std::string &fpath,
                               size_t read_sz = 1048576);
BOOL get_module_size(HMODULE hmodule, LPVOID *lplp_base, PDWORD64 lpdw_size);
void install_pre_hook();

// Wire debug stream from DllMain into TranslationHooks (call after init_debug)
void set_translation_debug(sp::io::ps_ostream *dbg);

// Set the custom UI font path (called from DllMain init_settings)
void set_ui_font_path(const char *path);