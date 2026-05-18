// DllMain.cpp
// Marvel's GOTG Thai Localization Mod - DLL Entry Point
// Dawn Engine string interception via MinHook + version.dll proxy

#include <Windows.h>
#include <inttypes.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>

#include "../include/PatternScanner.h"
#include "../include/TranslationHooks.h"
// GhostOverlay removed — native font injection replaces external overlay
// #include "../include/GhostOverlay.h"
#include "../include/GOTG_Translation.h"
#include "sp/environment.h"
#include "sp/file.h"
#include "sp/str.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

const char* cfg_file = ".\\GOTG_Mod.ini";
sp::io::ps_ostream g_debug;
static sp::io::ps_ostream* g_debug_static = nullptr;

std::string g_log_file = ".\\GOTG_Mod.log";

// ============================================================================
// DLL EXPORTS (version.dll chain)
// ============================================================================

const char* lib_name = "version";
#define DLL_EXPORT_COUNT 15

LPCSTR import_names[DLL_EXPORT_COUNT] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA",
    "VerInstallFileW", "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"
};

HINSTANCE dll_instance = NULL;
HINSTANCE dll_chain_instance = NULL;

// ============================================================================
// GAME MEMORY INFO
// ============================================================================

uint64_t gotg_base = 0;
DWORD64 gotg_size = 0;

// ============================================================================
// EXPORTED VARIABLES — defined here, declared extern "C" in TranslationHooks.h
// ============================================================================
extern "C" {
    UINT_PTR export_locs[15]        = {};
    uint8_t  translations_enabled   = 1;
    uint8_t  debug_string_capture   = 0;

    void* prehook_inject_addr       = nullptr;
    void* prehook_ret               = nullptr;
    void* textlist_installer_func   = nullptr;
    void* get_mem_mgr_func          = nullptr;
    void* textlist_str_alloc_call_instruction = nullptr;
    void* textlist_str_alloc_func   = nullptr;
    void* loadingscreen_startsubs_func = nullptr;
    void* loadingscreen_startsubs_get_subs_data_instr = nullptr;
    void* str_eq_operator_func      = nullptr;
    void* uielement_playvid_func    = nullptr;
    void* uicredits_playvid_func    = nullptr;
    void* submgr_startsubs_get_subs_data_instr = nullptr;
    void* vidscreen_init_func       = nullptr;
    void* menuscreen_init_func      = nullptr;
    void* renderplayer_start_hook_addr = nullptr;
    void* ui_font_addr_hook_addr    = nullptr;
    void* ui_font_replace_hook_addr = nullptr;
    void* resid_record_mapping_func = nullptr;

    uint64_t textlist_res_id        = 0;
    uint32_t textlist_str_id        = 0;
    uint64_t video_res_id           = 0;
}

// ============================================================================
// INTERNAL FUNCTION PROTOTYPES
// ============================================================================

void load_original_dll();
int get_dll_chain();
void init_settings();
void init_debug();
DWORD WINAPI async_thread(LPVOID param);
void discover_offsets();
bool validate_game_version();
extern "C" int LoadSystemVersionDll(void);

// Shutdown flag for async monitoring thread
static volatile bool g_shutdown_requested = false;

// ============================================================================
// DLL MAIN ENTRY POINT
// ============================================================================

BOOL WINAPI DllMain(HINSTANCE hinst_dll, DWORD fdw_reason, LPVOID lpv_reserved)
{
    dll_instance = hinst_dll;

    if (fdw_reason == DLL_PROCESS_ATTACH)
    {
        // Load system version.dll immediately on attach
        LoadSystemVersionDll();

        // Set working directory to game retail folder
        SetCurrentDirectoryA(sp::env::lib_dir().c_str());

        // Validate configuration file exists
        std::ifstream cfg_check(cfg_file);
        if (!cfg_check.good())
        {
            std::string err_msg = std::string("GOTG Thai Mod: Configuration file not found:\n") + cfg_file + "\n\n";
            err_msg += "Current directory:\n" + std::filesystem::current_path().string() + "\n\n";
            err_msg += "Please copy GOTG_Mod.ini to the retail folder.";
            MessageBoxA(NULL, err_msg.c_str(), "ERROR", MB_OK | MB_SETFOREGROUND | MB_TOPMOST | MB_APPLMODAL);
            ExitProcess(SP_ERR_FILE_NOT_FOUND);
        }

        // Initialize debug logging
        init_debug();

        g_debug.print("\n");
        g_debug.print("+------------------------------------------+\n");
        g_debug.print("|  Marvel's GOTG Thai Translation Mod      |\n");
        g_debug.print("|  MinHook Detour Framework v1.0           |\n");
        g_debug.print("+------------------------------------------+\n");
        g_debug.print("Compiled: " __DATE__ "  " __TIME__ "\n\n");

        // Record attachment timestamp
        struct tm time_local;
        time_t time_now = std::time(NULL);
        localtime_s(&time_local, &time_now);
        std::ostringstream oss;
        oss << std::put_time(&time_local, "%Y-%m-%d %H:%M:%S");
        g_debug.print("Attached to process at " + oss.str() + "\n");

        // Get gotg.exe base address and size
        gotg_base = 0;
        gotg_size = 0;

        g_debug.print("Obtaining module base & size...\n");
        if (!get_module_size(GetModuleHandle(NULL), (LPVOID*)&gotg_base, &gotg_size))
        {
            g_debug.print("[ERROR] Failed to get module info\n");
            MessageBoxA(NULL, "GOTG Thai Mod: Failed to get module info", "ERROR", MB_OK);
            return FALSE;
        }

        g_debug.print("    Base:  0x" + sp::str::to_hex(gotg_base) + "\n");
        g_debug.print("    Size:  0x" + sp::str::to_hex(gotg_size) + " (" + std::to_string(gotg_size / (1024 * 1024)) + " MB)\n");

        // Validate game version before proceeding
        if (!validate_game_version())
        {
            g_debug.print("[ERROR] Unsupported game version\n");
            MessageBoxA(NULL, "GOTG Thai Mod: Unsupported game version", "ERROR", MB_OK);
            return FALSE;
        }

        // Load DLL chain (original version.dll)
        get_dll_chain();

        if (!dll_chain_instance)
            load_original_dll();

        if (!dll_chain_instance)
        {
            g_debug.print("[ERROR] Failed to load original version.dll\n");
            MessageBoxA(NULL, "GOTG Thai Mod: Failed to load version.dll", "ERROR", MB_OK);
            return FALSE;
        }

        // Load exported function addresses from real version.dll
        g_debug.print("Loading exported function addresses...\n");
        for (int i = 0; i < DLL_EXPORT_COUNT; i++)
        {
            export_locs[i] = (UINT_PTR)GetProcAddress(dll_chain_instance, import_names[i]);
            g_debug.print("    " + std::string(import_names[i]) + " @ 0x" + sp::str::to_hex(export_locs[i]) + "\n");
        }

        // Initialize settings and offsets
        g_debug.print("Initializing settings and discovering offsets...\n");
        init_settings();

        // Install MinHook detours for Dawn Engine string interception
        g_debug.print("Installing MinHook detours...");
        GotgHookStatus hook_status = install_all_hooks();
        if (hook_status != GotgHookStatus::OK)
        {
            g_debug.print("[ERROR] Hook installation failed (status: " + std::to_string((int)hook_status) + ")\n");
            g_debug.print("[ERROR] Translation system will not function\n");
            g_debug.print("[ERROR] Check GOTG_Mod.log for MinHook error details\n");
        }

        // Create async debug monitoring thread
        g_debug.print("Starting async monitoring thread...\n");
        HANDLE async_thread_handle = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&async_thread, 0, 0, 0);
        if (!async_thread_handle)
        {
            g_debug.print("[WARNING] Failed to create async thread\n");
        }

        // Ghost Overlay removed — native font injection via FT_New_Memory_Face
        // renders Thai text directly through the Dawn Engine's FreeType pipeline.
        // No external transparent window needed anymore.

        g_debug.print("\n[OK] Mod initialization complete\n");
        g_debug.print("    Translation dictionary: " + std::to_string(get_translation_count()) + " entries\n");
        g_debug.print("    Translations enabled: " + std::string(get_translations_enabled() ? "YES" : "NO") + "\n\n");
    }
    else if (fdw_reason == DLL_PROCESS_DETACH)
    {
        g_debug.print("Detaching from process...\n");
        g_shutdown_requested = true;  // signal monitoring thread to stop
        Sleep(200);                   // give it time to exit
        // shutdown_ghost_overlay();  // Overlay removed — native rendering used
        uninstall_all_hooks();
        if (dll_chain_instance) {
            FreeLibrary(dll_chain_instance);
            dll_chain_instance = NULL;
        }
        g_debug.print("[OK] Detached cleanly\n");
    }

    return TRUE;
}

// ============================================================================
// DEBUG INITIALIZATION
// ============================================================================

void init_debug()
{
    char cfg_str[MAX_PATH];

    // Get log file path from config
    GetPrivateProfileStringA("DLL", "LogFile", g_log_file.c_str(), cfg_str, MAX_PATH, cfg_file);
    g_log_file = cfg_str;

    g_debug.set_log_file(g_log_file);

    // Check if debug mode is enabled
    if (GetPrivateProfileIntA("DLL", "Debug", 0, cfg_file))
    {
        g_debug.start();
    }

    g_debug_static = &g_debug;
    // Wire the same debug stream into TranslationHooks
    set_translation_debug(&g_debug);
    // set_overlay_debug removed — overlay no longer used
}

// ============================================================================
// GAME VERSION VALIDATION
// ============================================================================

bool validate_game_version()
{
    std::string exe_path = sp::env::exe_path();
    std::string exe_name = sp::env::exe_name();

    g_debug.print("Executable: " + exe_name + "\n");

    // Verify we're attached to gotg.exe
    if (exe_name != "gotg.exe")
    {
        g_debug.print("[WARNING] Expected gotg.exe, found: " + exe_name + "\n");
        // Allow continue - might be renamed
    }

    // Calculate quick hash for version detection
    std::string exe_hash = calculate_file_md5(exe_path, 1048576); // First 1MB
    g_debug.print("Executable hash: " + exe_hash + "\n");

    // TODO: Add MD5 checks for known GOTG versions
    // Known Epic Games Store version hashes would go here

    return true; // Allow any version for now
}

// ============================================================================
// DLL CHAIN MANAGEMENT
// ============================================================================

void load_original_dll()
{
    g_debug.print("Loading original version.dll from system directory...\n");

    char buffer[MAX_PATH];
    GetSystemDirectoryA(buffer, MAX_PATH);
    strcat_s(buffer, (std::string("\\") + lib_name + ".dll").c_str());

    g_debug.print("    System path: " + std::string(buffer) + "\n");
    dll_chain_instance = LoadLibraryA(buffer);

    if (!dll_chain_instance)
    {
        g_debug.print("[ERROR] Failed to load system version.dll\n");
        MessageBoxA(NULL, "GOTG Thai Mod: Failed to load system version.dll", "ERROR", MB_OK);
        ExitProcess(SP_ERR_FILE_NOT_FOUND);
    }

    g_debug.print("[OK] Loaded original version.dll\n");
}

int get_dll_chain()
{
    g_debug.print("Checking DLL chain configuration...\n");

    char dll_chain_buffer[MAX_PATH];
    GetPrivateProfileStringA("DLL", "Chain", "", dll_chain_buffer, MAX_PATH, cfg_file);

    if (dll_chain_buffer[0] != '\0')
    {
        g_debug.print("    DLL Chain: \"" + std::string(dll_chain_buffer) + "\"\n");
        dll_chain_instance = LoadLibraryA(dll_chain_buffer);

        if (!dll_chain_instance)
        {
            g_debug.print("[WARNING] Failed to load chain DLL, will use system DLL\n");
            return 2;
        }
    }
    else
    {
        g_debug.print("    No DLL chain specified\n");
        return 1;
    }

    return 0;
}

// ============================================================================
// SETTINGS INITIALIZATION & OFFSET DISCOVERY
// ============================================================================

void init_settings()
{
    // Load mod settings from config file
    translations_enabled = (uint8_t)GetPrivateProfileIntA("Language", "EnableTranslation", 1, cfg_file);
    debug_string_capture = (uint8_t)GetPrivateProfileIntA("Game", "DebugStringCapture", 0, cfg_file);

    g_debug.print("    EnableTranslation: " + std::to_string(translations_enabled) + "\n");
    g_debug.print("    DebugStringCapture: " + std::to_string(debug_string_capture) + "\n");

    // Discover memory offsets via pattern scanning
    discover_offsets();

    // Load translation JSON file
    char json_path[MAX_PATH];
    GetPrivateProfileStringA("Language", "StringsJSON", "", json_path, MAX_PATH, cfg_file);
    load_translation_json(json_path);
    g_debug.print("    Translation JSON: " + std::string(json_path) + "\n");

    // Load custom UI font path if specified (stored separately, not as a JSON dict)
    char font_path[MAX_PATH];
    GetPrivateProfileStringA("Language", "UIFont", "", font_path, MAX_PATH, cfg_file);
    if (font_path[0] != '\0')
    {
        set_ui_font_path(font_path);
        g_debug.print("    Custom font: " + std::string(font_path) + "\n");
    }
}

// ============================================================================
// PATTERN SCANNING FOR OFFSET DISCOVERY
// ============================================================================

void discover_offsets()
{
    g_debug.print("Starting Dawn Engine offset discovery via pattern scanning...\n");

    if (gotg_base == 0 || gotg_size == 0)
    {
        g_debug.print("[ERROR] Invalid module base/size for scanning\n");
        return;
    }

    // Initialize all offsets to NULL
    prehook_inject_addr = nullptr;
    prehook_ret = nullptr;
    textlist_installer_func = nullptr;
    get_mem_mgr_func = nullptr;
    textlist_str_alloc_call_instruction = nullptr;
    textlist_str_alloc_func = nullptr;
    loadingscreen_startsubs_func = nullptr;
    loadingscreen_startsubs_get_subs_data_instr = nullptr;
    str_eq_operator_func = nullptr;
    uielement_playvid_func = nullptr;
    uicredits_playvid_func = nullptr;
    submgr_startsubs_get_subs_data_instr = nullptr;
    vidscreen_init_func = nullptr;
    menuscreen_init_func = nullptr;
    renderplayer_start_hook_addr = nullptr;
    ui_font_addr_hook_addr = nullptr;
    ui_font_replace_hook_addr = nullptr;
    resid_record_mapping_func = nullptr;

    // =========================================================================
    // DAWN ENGINE PATTERN DEFINITIONS
    // These byte patterns are typical signatures for string-related functions
    // =========================================================================

    struct ScanPattern {
        const char* name;
        uint8_t bytes[16];
        DWORD64 length;
        const char* mask;
    };

    // Dawn Engine common patterns
    // NOTE: These are APPROXIMATE - must be verified against actual gotg.exe
    ScanPattern patterns[] = {
        // String allocation entry (push rsi; mov rdi,rsi; call qword ptr [xxx])
        {
            "string_alloc_entry",
            { 0x48, 0x89, 0xFE, 0x48, 0x89, 0xD7, 0xE8 },
            7,
            "xx???xx"
        },
        // Memory manager retrieve (mov rax,[rcx+offset])
        {
            "mem_mgr_retrieve",
            { 0x48, 0x8B, 0x01, 0x48, 0x85, 0xC0 },
            6,
            "xx?xxx"
        },
        // UI text render preamble (sub rsp,xx; mov qword ptr [rsp+xx],rcx)
        {
            "ui_text_render",
            { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x4C, 0x24 },
            8,
            "????????"
        },
        // String equality check (test rax,rax; jz near)
        {
            "string_equality",
            { 0x48, 0x85, 0xC0, 0x74, 0x00, 0x48, 0x89 },
            7,
            "xxxx?xx"
        },
        // Loading screen subtitle setup (push rbx; sub rsp,xx)
        {
            "loading_subs",
            { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B },
            8,
            "??????xx"
        },
        // Font address retrieval
        {
            "font_addr",
            { 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9 },
            10,
            "xx????xxxx"
        }
    };

    int matches_found = 0;
    uint8_t* base = reinterpret_cast<uint8_t*>(gotg_base);

    for (const auto& p : patterns)
    {
        void* result = PatternScanner::find_pattern(
            base,
            gotg_size,
            p.bytes,
            p.length,
            p.mask
        );

        if (result)
        {
            g_debug.print("    [MATCH] " + std::string(p.name) + " @ 0x" + sp::str::to_hex((uint64_t)result) + "\n");

            // Assign to appropriate offset based on pattern name
            std::string name(p.name);
            if (name == "string_alloc_entry") {
                textlist_str_alloc_call_instruction = result;
                // Estimate textlist_str_alloc_func as nearby
                textlist_str_alloc_func = (void*)((uint64_t)result - 0x1000);
            }
            else if (name == "mem_mgr_retrieve") {
                get_mem_mgr_func = result;
            }
            else if (name == "ui_text_render") {
                uielement_playvid_func = result;
            }
            else if (name == "string_equality") {
                str_eq_operator_func = result;
            }
            else if (name == "loading_subs") {
                loadingscreen_startsubs_func = result;
            }
            else if (name == "font_addr") {
                ui_font_addr_hook_addr = result;
            }

            matches_found++;
        }
    }

    if (matches_found == 0)
    {
        g_debug.print("    [WARNING] No patterns matched!\n");
        g_debug.print("    [WARNING] Offsets remain as PLACEHOLDERS\n");
        g_debug.print("    [INFO] Manual scanning with x64dbg required\n");
    }
    else
    {
        g_debug.print("    [OK] Pattern scan complete: " + std::to_string(matches_found) + " matches\n");
    }

    // Print all discovered offsets
    g_debug.print("\n    Discovered offsets:\n");
    g_debug.print("        textlist_str_alloc_func:  0x" + sp::str::to_hex((uint64_t)textlist_str_alloc_func) + "\n");
    g_debug.print("        str_eq_operator_func:     0x" + sp::str::to_hex((uint64_t)str_eq_operator_func) + "\n");
    g_debug.print("        uielement_playvid_func:   0x" + sp::str::to_hex((uint64_t)uielement_playvid_func) + "\n");
    g_debug.print("        get_mem_mgr_func:         0x" + sp::str::to_hex((uint64_t)get_mem_mgr_func) + "\n");
    g_debug.print("        loadingscreen_startsubs_func: 0x" + sp::str::to_hex((uint64_t)loadingscreen_startsubs_func) + "\n");
    g_debug.print("        ui_font_addr_hook_addr:   0x" + sp::str::to_hex((uint64_t)ui_font_addr_hook_addr) + "\n");
}

// ============================================================================
// ASYNC MONITORING THREAD
// ============================================================================

DWORD WINAPI async_thread(LPVOID param)
{
    g_debug.print("[THREAD] Async monitoring thread started\n");

    int tick_count = 0;

    while (!g_shutdown_requested)
    {
        // Periodic status log every 10 seconds
        tick_count++;
        if (tick_count >= 100) // 100 * 100ms = 10 seconds
        {
            tick_count = 0;

            // Log current string IDs being processed
            if (debug_string_capture && textlist_str_id != 0)
            {
                g_debug.print("[MONITOR] Current string ID: 0x" + sp::str::to_hex(textlist_str_id) + "\n");
            }
        }

        Sleep(100);
    }

    g_debug.print("[THREAD] Async monitoring thread exiting\n");
    return 0;
}