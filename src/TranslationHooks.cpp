// TranslationHooks.cpp
// MinHook detour implementation for GOTG Thai Localization Mod
// Dawn Engine string interception via version.dll proxy
// Features:
//   1. StringAlloc hook — captures ALL strings registered with Dawn Engine pool
//   2. ZSubtitlesField::SetText hook — translation injection
//
// Hook targets:
//   - StringAlloc @ gotg_base + 0x14064BA24 (string registry allocator)
//   - ZSubtitlesField::SetText @ gotg_base + 0x1BFB5A0 (subtitle rendering)
//
// Safety measures implemented:
//   - Re-entrancy protection via thread_local guard (prevents Stack Overflow)
//   - Thread-safe global container via std::mutex
//   - SEH-wrapped pointer reads via __declspec(noinline) helpers
//   - Length + garbage filtering (skip file paths, short strings, binary junk)
//   - Win32 API file I/O in dump function (Loader Lock safe)

#include "../include/TranslationHooks.h"
// GhostOverlay removed — native font injection replaces external overlay
// #include "../include/GhostOverlay.h"
#include "../include/GOTG_Translation.h"
#include "../include/PatternScanner.h"
#include "sp/environment.h"
#include "sp/file.h"
#include "sp/str.h"
#include <fstream>
#include <string>
#include <string_view>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <Psapi.h>

// ============================================================================
// MINHOOK LIBRARY
// ============================================================================

#ifdef MINHOOK_AVAILABLE
#include <MinHook.h>

static const char* MH_STATUS_MESSAGE(MH_STATUS status)
{
    switch (status) {
    case MH_OK:                           return "OK";
    case MH_ERROR_ALREADY_INITIALIZED:    return "ERROR: MinHook already initialized";
    case MH_ERROR_NOT_INITIALIZED:        return "ERROR: MinHook not initialized";
    case MH_ERROR_ALREADY_CREATED:        return "ERROR: Hook already created";
    case MH_ERROR_NOT_CREATED:            return "ERROR: Hook not created";
    case MH_ERROR_ENABLED:                return "ERROR: Hook already enabled";
    case MH_ERROR_DISABLED:               return "ERROR: Hook already disabled";
    case MH_ERROR_NOT_EXECUTABLE:         return "ERROR: Target not executable";
    case MH_ERROR_UNSUPPORTED_FUNCTION:  return "ERROR: Unsupported function";
    case MH_ERROR_MEMORY_ALLOC:           return "ERROR: Memory allocation failed";
    case MH_ERROR_MEMORY_PROTECT:         return "ERROR: Memory protection failed";
    case MH_ERROR_MODULE_NOT_FOUND:       return "ERROR: Module not found";
    case MH_ERROR_FUNCTION_NOT_FOUND:     return "ERROR: Function not found";
    default:                              return "ERROR: Unknown MinHook status";
    }
}
#endif

// ============================================================================
// GLOBAL STATE
// ============================================================================

static std::unordered_map<std::string, std::string> g_translation_dict;
static bool g_json_loaded = false;
static bool g_translations_enabled = true;
static bool g_hooks_installed = false;
static int g_translation_count = 0;
static std::string g_ui_font_path;

// Original function pointers
static StringAlloc_t   g_orig_StringAlloc         = nullptr;
static StringEq_t      g_orig_StringEq            = nullptr;
static UITextRender_t  g_orig_UITextRender       = nullptr;
static GetMemMgr_t     g_orig_GetMemMgr           = nullptr;
static SubtitleLoad_t  g_orig_SubtitleLoad        = nullptr;
static ZSubtitlesField_SetText_t g_orig_ZSubtitlesField_SetText = nullptr;
static FT_New_Memory_Face_t g_orig_FT_New_Memory_Face = nullptr;
static ZTextBundle_TryGetText_t g_orig_ZTextBundle_TryGetText = nullptr;

typedef bool(__fastcall* ZTextBundle_TryGetText_Hash_t)(void* pThis, void* pRDX, void* pR8);
static ZTextBundle_TryGetText_Hash_t g_orig_ZTextBundle_TryGetText_Hash = nullptr;

typedef void(__fastcall* ZTextFieldEntity_OnSetText_t)(void* pThis, void* pZString);
static ZTextFieldEntity_OnSetText_t g_orig_ZTextFieldEntity_OnSetText = nullptr;

typedef void*(__fastcall* ZTextFieldEntity_GetText_t)(void* pRetBuffer, void* pThis);
static ZTextFieldEntity_GetText_t g_orig_ZTextFieldEntity_GetText = nullptr;

static int g_trygettext_call_count = 0;
static int g_trygettext_translate_count = 0;

// Saved memory manager pointer from StringAlloc — used by TryGetText to
// allocate translated strings through the engine's own allocator.
static void* g_saved_mem_mgr = nullptr;

// ============================================================================
// PERSISTENT STRING CACHE — heap-allocated, deduplicated Thai text copies
// Used by Hook 6/7 to provide stable pointers the engine can safely reference
// after our detour returns. Strings are allocated once per unique translation
// and never freed until process exit (bounded by dictionary size ~40K entries).
// ============================================================================

static std::unordered_map<std::string, const char*> g_persist_str_cache;
static std::mutex g_persist_str_mutex;

// Returns a heap-allocated copy of 'text' that is stable for the process lifetime.
// Deduplicates: repeated calls with the same text return the same pointer.
static const char* get_persistent_copy(const char* text, size_t len)
{
    std::string key(text, len);
    std::lock_guard<std::mutex> lock(g_persist_str_mutex);
    auto it = g_persist_str_cache.find(key);
    if (it != g_persist_str_cache.end()) return it->second;

    char* copy = new char[len + 1];
    memcpy(copy, text, len);
    copy[len] = '\0';
    g_persist_str_cache[key] = copy;
    return copy;
}

// ============================================================================
// MISSING KEYS CATCHER — logs untranslated text keys to a dedicated file
// ============================================================================

static std::unordered_set<std::string> g_missing_keys;     // Dedup set
static std::mutex g_missing_keys_mutex;                     // Thread safety
static HANDLE g_missing_keys_file = INVALID_HANDLE_VALUE;   // Win32 file handle
static bool g_missing_keys_file_opened = false;

// ============================================================================
// FONT SWAP STATE — Thai font injection via FT_New_Memory_Face hook
// ============================================================================

static unsigned char*  g_thai_font_buffer = nullptr;  // Thai TTF data loaded from disk
static long            g_thai_font_size   = 0;        // Size of the Thai TTF data
static bool            g_thai_font_loaded = false;    // True if font_th.ttf was loaded
static int             g_font_swap_count  = 0;        // Number of font swaps performed
static int             g_font_load_count  = 0;        // Total FT_New_Memory_Face calls

// ============================================================================
// ============================================================================// g_debug pointer — set by DllMain via set_translation_debug()
static sp::io::ps_ostream* g_debug = nullptr;

void set_translation_debug(sp::io::ps_ostream* dbg) { g_debug = dbg; }
void set_ui_font_path(const char* path) { if (path) g_ui_font_path = path; }

// ============================================================================
// STRING DUMP STATE — for GOTG_Full_Dump.json generation
// ============================================================================

// All unique strings captured from StringAlloc hook (unordered_set for dedup)
static std::unordered_set<std::string> g_captured_strings;
// Mutex to protect string capture set from concurrent access
static std::mutex g_string_capture_mutex;
// Flag: dump has been written to disk (prevent duplicate writes)
static bool g_dump_written = false;

// ============================================================================
// RE-ENTRANCY GUARD
// ============================================================================
// CRITICAL: Our hook intercepts StringAlloc — the engine's string allocator.
// Any C++ code inside our hook that allocates strings (std::string ctor,
// unordered_set::insert, logging, etc.) will CALL BACK INTO StringAlloc,
// triggering our detour AGAIN, causing infinite recursion → Stack Overflow.
//
// Solution: A thread_local flag that bypasses our logic if we're already
// executing inside the hook. Each thread gets its own copy, so there's no
// contention or need for atomic ops on this flag.
// ============================================================================

static thread_local bool tl_inside_hook = false;

// ============================================================================
// SAFE POINTER READ HELPERS — __declspec(noinline) to prevent SEH/EH
//                              unwind issues (C2712 avoidance)
// ============================================================================

// Safely read a C-string pointer — returns false if the pointer is bad
static bool __declspec(noinline) safe_read_cstring(const char* ptr, char* out_buf, size_t max_len)
{
    __try {
        // Probe the first byte
        volatile char probe = ptr[0];
        (void)probe;

        // Copy up to max_len-1 characters
        size_t i = 0;
        for (; i < max_len - 1; ++i) {
            char c = ptr[i];
            if (c == '\0') break;
            out_buf[i] = c;
        }
        out_buf[i] = '\0';
        return (i > 0);  // true if at least 1 char was read
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out_buf[0] = '\0';
        return false;
    }
}

// Safely read text_ptr from a ZString layout struct
static void __declspec(noinline) probe_ptr_to_raw_text(void* pZString, char** out_text)
{
    *out_text = nullptr;
    __try {
        ZStringLayout* z = (ZStringLayout*)pZString;
        *out_text = z->text_ptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *out_text = nullptr;
    }
}

// ============================================================================
// STRING QUALITY FILTER
// ============================================================================
// Filters out garbage, debug symbols, and binary junk that flows through
// the engine's string allocator. Only strings that pass these checks are
// captured into the dump.

static bool is_string_worth_capturing(const char* str, size_t len)
{
    // ---- Length filter ----
    // Strings shorter than 2 chars are almost never meaningful localization text
    if (len < 2) return false;

    // ---- File path / debug symbol filter ----
    const char* blacklist_substrings[] = {
        ".cpp", ".wwise", ".bnk", ".wem", ".dds", ".png", ".tga", ".jpg",
        ".obj", ".fbx", ".mesh", "\\\\", "0x", "::~"
    };

    for (const char* bl : blacklist_substrings) {
        size_t bl_len = 0;
        for (const char* p = bl; *p; ++p) ++bl_len;
        if (bl_len > len) continue;
        for (size_t i = 0; i <= len - bl_len; ++i) {
            bool match = true;
            for (size_t j = 0; j < bl_len; ++j) {
                char sc = str[i + j];
                char bc = bl[j];
                if (sc >= 'A' && sc <= 'Z') sc += 32;
                if (bc >= 'A' && bc <= 'Z') bc += 32;
                if (sc != bc) { match = false; break; }
            }
            if (match) return false;
        }
    }

    return true; // Relaxed readability filter
}

// ============================================================================
// HELPER: log_missing_key
// Logs untranslated keys to a dedicated file in real-time.
// Output format is JSON Lines: {"key": "..."}
// ============================================================================

static bool contains_thai_chars(const char* str, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        // Thai UTF-8 starts with E0 B8 or E0 B9
        if (i + 2 < len && (unsigned char)str[i] == 0xE0 && 
           ((unsigned char)str[i+1] == 0xB8 || (unsigned char)str[i+1] == 0xB9)) {
            return true;
        }
        // Also skip '?' spam if it's garbled encoding of Thai
        if (str[i] == '?') {
            size_t q_count = 0;
            for (size_t j = i; j < len && str[j] == '?'; ++j) q_count++;
            if (q_count >= 5) return true;
        }
    }
    return false;
}

static void log_missing_key(const char* text, size_t len)
{
    if (len < 2 || !is_string_worth_capturing(text, len)) return;
    if (contains_thai_chars(text, len)) return;

    std::lock_guard<std::mutex> lock(g_missing_keys_mutex);

    std::string key_str(text, len);
    if (g_missing_keys.find(key_str) == g_missing_keys.end()) {
        g_missing_keys.insert(key_str);

        if (!g_missing_keys_file_opened) {
            g_missing_keys_file = CreateFileA(
                ".\\GOTG_MissingKeys.log",
                FILE_APPEND_DATA, FILE_SHARE_READ,
                NULL, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_missing_keys_file != INVALID_HANDLE_VALUE) {
                g_missing_keys_file_opened = true;
            }
        }

        if (g_missing_keys_file != INVALID_HANDLE_VALUE) {
            char line_buf[8192];
            int pos = wsprintfA(line_buf, "{\"key\": \"");
            for (size_t i = 0; i < len && pos < 8100; ++i) {
                char c = text[i];
                if (c == '"') { line_buf[pos++] = '\\'; line_buf[pos++] = '"'; }
                else if (c == '\\') { line_buf[pos++] = '\\'; line_buf[pos++] = '\\'; }
                else if (c == '\n') { line_buf[pos++] = '\\'; line_buf[pos++] = 'n'; }
                else if (c == '\r') { /* skip */ }
                else { line_buf[pos++] = c; }
            }
            line_buf[pos++] = '"';
            line_buf[pos++] = '}';
            line_buf[pos++] = '\n';
            line_buf[pos] = '\0';

            DWORD written = 0;
            WriteFile(g_missing_keys_file, line_buf, (DWORD)pos, &written, NULL);
            FlushFileBuffers(g_missing_keys_file);
        }
    }
}

// ============================================================================
// JSON DUMP — writes captured strings to GOTG_Full_Dump.json
// ============================================================================
// IMPORTANT: This function uses Win32 APIs (CreateFile/WriteFile) instead of
// std::ofstream because it may be called during DLL_PROCESS_DETACH.
// During detach, the OS holds the Loader Lock, and std::ofstream's internal
// locale/CRT initialization can deadlock trying to acquire the same lock.

static void dump_all_strings_to_json()
{
    std::lock_guard<std::mutex> lock(g_string_capture_mutex);
    if (g_dump_written) return;
    g_dump_written = true;

    const char* out_path = ".\\GOTG_Full_Dump.json";

    // --- Use Win32 API for Loader Lock safety ---
    HANDLE hFile = CreateFileA(
        out_path,
        GENERIC_WRITE,
        0,                      // No sharing while writing
        NULL,
        CREATE_ALWAYS,          // Overwrite if exists
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        // Cannot open file — silently fail (no logging during detach either)
        return;
    }

    // Helper lambda: write a raw C-string to the file handle
    auto write_str = [&](const char* s) {
        DWORD len = 0;
        for (const char* p = s; *p; ++p) ++len;
        DWORD written = 0;
        WriteFile(hFile, s, len, &written, NULL);
    };

    // --- Build JSON output ---
    write_str("{\n");
    write_str("  \"_info\": \"Dawn Engine string registry dump - Marvel's GOTG Thai Mod\",\n");

    // Write total count
    {
        char count_buf[64];
        wsprintfA(count_buf, "  \"_total_strings\": %u,\n", (unsigned int)g_captured_strings.size());
        write_str(count_buf);
    }

    write_str("  \"strings\": {\n");

    // Convert set to a sorted vector for deterministic output
    std::vector<std::string> sorted_strings(g_captured_strings.begin(), g_captured_strings.end());
    std::sort(sorted_strings.begin(), sorted_strings.end());

    for (size_t i = 0; i < sorted_strings.size(); ++i) {
        const std::string& s = sorted_strings[i];

        // Build escaped string in a fixed buffer to avoid std::string alloc
        // where possible, but we need dynamic size for large strings
        write_str("    \"e");
        {
            char idx_buf[16];
            wsprintfA(idx_buf, "%u", (unsigned int)i);
            write_str(idx_buf);
        }
        write_str("\": \"");

        // Write escaped content character by character
        for (size_t ci = 0; ci < s.size(); ++ci) {
            char c = s[ci];
            switch (c) {
            case '"':  write_str("\\\""); break;
            case '\\': write_str("\\\\"); break;
            case '\n': write_str("\\n");  break;
            case '\r': write_str("\\r");  break;
            case '\t': write_str("\\t");  break;
            default: {
                char one[2] = { c, '\0' };
                WriteFile(hFile, one, 1, NULL, NULL);
                break;
            }
            }
        }

        write_str("\"");

        if (i + 1 < sorted_strings.size())
            write_str(",");
        write_str("\n");
    }

    write_str("  }\n");
    write_str("}\n");

    // Flush and close
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
}

// ============================================================================
// TRANSLATION DICTIONARY
// ============================================================================

// ==========================================================================
// ZERO-ALLOCATION FAST LOOKUP INDEX
// ==========================================================================
struct SVHash {
    size_t operator()(std::string_view sv) const {
        size_t hash = 14695981039346656037ULL;
        for (char c : sv) {
            hash ^= (size_t)(unsigned char)c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};
static std::unordered_map<std::string_view, const char*, SVHash> g_fast_lookup;

static void build_fast_lookup_index()
{
    g_fast_lookup.clear();
    g_fast_lookup.reserve(g_translation_dict.size() * 2);
    for (const auto& kv : g_translation_dict) {
        g_fast_lookup[std::string_view(kv.first)] = kv.second.c_str();
    }
}

void load_translation_json(const char* fpath)
{
    if (!fpath || fpath[0] == '\0') {
        g_translation_dict.clear();
        g_json_loaded = false;
        g_translation_count = 0;
        return;
    }

    try {
        std::ifstream f(fpath);
        if (!f.good()) {
            if (g_debug) g_debug->print("[ERROR] Cannot open translation JSON: " + std::string(fpath) + "\n");
            return;
        }

        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

        auto find_closing_quote = [&](size_t start) -> size_t {
            for (size_t p = start; p < content.size(); ++p) {
                if (content[p] == '\\') { ++p; continue; }
                if (content[p] == '"') return p;
            }
            return std::string::npos;
        };

        auto unescape_json = [](const std::string& s) -> std::string {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    char next = s[++i];
                    if (next == 'n') out.push_back('\n');
                    else if (next == 'r') out.push_back('\r');
                    else if (next == 't') out.push_back('\t');
                    else if (next == '"') out.push_back('"');
                    else if (next == '\\') out.push_back('\\');
                    else {
                        out.push_back('\\');
                        out.push_back(next);
                    }
                } else {
                    out.push_back(s[i]);
                }
            }
            return out;
        };

        size_t pos = 0;
        while (pos < content.size()) {
            size_t key_start = content.find('"', pos);
            if (key_start == std::string::npos) break;
            size_t key_end = find_closing_quote(key_start + 1);
            if (key_end == std::string::npos) break;

            size_t val_start = content.find('"', key_end + 1);
            if (val_start == std::string::npos) break;
            size_t val_end = find_closing_quote(val_start + 1);
            if (val_end == std::string::npos) break;

            std::string raw_key = content.substr(key_start + 1, key_end - key_start - 1);
            std::string raw_val = content.substr(val_start + 1, val_end - val_start - 1);
            
            std::string key = unescape_json(raw_key);
            std::string val = unescape_json(raw_val);

            if (!key.empty() && !val.empty() && key[0] != '_') {
                size_t k_start = 0;
                while (k_start < key.size() && (key[k_start] == ' ' || key[k_start] == '\n' || key[k_start] == '\r')) k_start++;
                size_t k_end = key.size();
                while (k_end > k_start && (key[k_end-1] == ' ' || key[k_end-1] == '\n' || key[k_end-1] == '\r')) k_end--;
                std::string trimmed_key = key.substr(k_start, k_end - k_start);
                
                if (g_translation_dict.find(trimmed_key) == g_translation_dict.end()) {
                    g_translation_dict[trimmed_key] = val;
                }
            }

            pos = val_end + 1;
        }

        g_translation_count = static_cast<int>(g_translation_dict.size());
        g_json_loaded = true;

        if (g_debug) {
            g_debug->print("[OK] Loaded " + std::to_string(g_translation_count) + " translations\n");
        }

        // Build zero-allocation fast lookup index
        build_fast_lookup_index();
        if (g_debug) {
            g_debug->print("[OK] Built fast lookup index: " + std::to_string(g_fast_lookup.size()) + " entries\n");
        }

    } catch (const std::exception& e) {
        if (g_debug) g_debug->print(std::string("[ERROR] JSON parse error: ") + e.what() + "\n");
        g_translation_dict.clear();
        g_json_loaded = false;
    }
}


// Buffer to hold string with re-attached whitespaces
static thread_local std::string tl_lookup_buf;

const char* lookup_translation(const char* original_str)
{
    if (!original_str || !g_translations_enabled || !g_json_loaded)
        return original_str;

    // Compute string_view — ZERO allocation
    std::string_view sv(original_str);
    
    if (sv.empty()) return original_str;

    // Trim leading/trailing whitespace without allocation
    size_t k_start = 0;
    while (k_start < sv.size() && (sv[k_start] == ' ' || sv[k_start] == '\n' || sv[k_start] == '\r')) k_start++;
    size_t k_end = sv.size();
    while (k_end > k_start && (sv[k_end-1] == ' ' || sv[k_end-1] == '\n' || sv[k_end-1] == '\r')) k_end--;
    
    if (k_start >= k_end) return original_str; // Only whitespace

    std::string_view trimmed = sv.substr(k_start, k_end - k_start);

    // Normalize \r\n -> \n  (engine sends CRLF, dict has LF only)
    bool has_cr = false;
    for (char c : trimmed) {
        if (c == '\r') { has_cr = true; break; }
    }

    static thread_local std::string tl_normalized;
    std::string_view lookup_sv = trimmed;
    if (has_cr) {
        tl_normalized.clear();
        tl_normalized.reserve(trimmed.size());
        for (char c : trimmed) {
            if (c != '\r') tl_normalized.push_back(c);
        }
        lookup_sv = tl_normalized;
    }
    
    // Fast lookup
    auto it = g_fast_lookup.find(lookup_sv);
    if (it != g_fast_lookup.end()) {
        // No padding case — return dict value directly
        if (k_start == 0 && k_end == sv.size()) {
            return it->second;
        }
        
        // Re-attach original leading/trailing whitespace (rare case)
        tl_lookup_buf.clear();
        tl_lookup_buf.append(sv.data(), k_start);
        tl_lookup_buf.append(it->second);
        tl_lookup_buf.append(sv.data() + k_end, sv.size() - k_end);
        return tl_lookup_buf.c_str();
    }
    
    return original_str;
}

int get_translation_count() { return g_translation_count; }
void set_translations_enabled(bool enabled) { g_translations_enabled = enabled; }
bool get_translations_enabled() { return g_translations_enabled; }

// ============================================================================
// DETOUR: StringAlloc — intercepts ALL string allocations in Dawn Engine
// Called every time the engine creates or registers a new string.
// This is our bulk-capture mechanism for the full localization database.
//
// SAFETY ARCHITECTURE:
//   1. Re-entrancy guard (thread_local)  — prevents infinite recursion
//   2. SEH safe read                     — survives bad pointers
//   3. Quality filter                    — skips junk/debug strings
//   4. Mutex-protected insertion         — thread-safe container access
// ============================================================================

const char* detour_string_alloc(void* mem_mgr, const char* original_str)
{


    if (!g_saved_mem_mgr) g_saved_mem_mgr = mem_mgr;
    if (g_orig_StringAlloc) {
        return g_orig_StringAlloc(mem_mgr, original_str);
    }
    return original_str;
}

// ============================================================================
// DETOUR: ZSubtitlesField::SetText — subtitle rendering + translation injection
// Signature: void __fastcall(void* pThis, void* pZString)
//
// With native font injection active, we pass the translated Thai string
// DIRECTLY into the engine's renderer. The Dawn Engine's FreeType pipeline
// now uses our Thai TTF, so it renders Thai glyphs natively.
// ============================================================================

void __fastcall detour_ZSubtitlesField_SetText(void* pThis, void* pZString)
{
    if (!pZString) {
        if (g_orig_ZSubtitlesField_SetText) {
            g_orig_ZSubtitlesField_SetText(pThis, pZString);
        }
        return;
    }

    // Safely read text_ptr from ZString layout
    char* raw_text = nullptr;
    probe_ptr_to_raw_text(pZString, &raw_text);

    if (!raw_text || !raw_text[0]) {
        if (g_orig_ZSubtitlesField_SetText) {
            g_orig_ZSubtitlesField_SetText(pThis, pZString);
        }
        return;
    }

    const char* english_text = raw_text;
    const char* translated = lookup_translation(english_text);

    if (translated != english_text) {
        // Thai translation found — inject directly into the engine's native renderer.
        // Since FT_New_Memory_Face has been hooked to load our Thai TTF,
        // the engine can now render Thai glyphs natively via FreeType.

        sp::io::ps_ostream* dbg = g_debug;
        if (dbg) dbg->print("[HOOK] Native render: " + std::string(english_text).substr(0, 60) + " -> " + std::string(translated).substr(0, 60) + "\n");

        // Build a ZStringLayout with the translated Thai text (UTF-8)
        // The Dawn Engine's string system uses UTF-8 internally
        size_t translated_len = 0;
        for (const char* p = translated; *p; ++p) ++translated_len;

        if (g_orig_ZSubtitlesField_SetText) {
            // Modify the existing ZString in-place to point to our translated text
            // This is safe because the engine will copy the string internally
            ZStringLayout thaiZString;
            memset(&thaiZString, 0, sizeof(thaiZString));
            thaiZString.text_ptr  = const_cast<char*>(translated);
            thaiZString.length    = (uint32_t)translated_len;
            thaiZString.capacity  = (uint32_t)translated_len;
            g_orig_ZSubtitlesField_SetText(pThis, &thaiZString);
        }
    } else {
        // No translation — let game render as-is (English or other)
        
        // MISSING KEY CATCHER: Subtitles might bypass TryGetText, so log them here!
        size_t key_len = 0;
        for (const char* p = english_text; *p; ++p) ++key_len;
        log_missing_key(english_text, key_len);

        if (g_orig_ZSubtitlesField_SetText) {
            g_orig_ZSubtitlesField_SetText(pThis, pZString);
        }
    }
}

// ============================================================================
// DETOUR: FT_New_Memory_Face — Font injection via buffer swap
// When the Dawn Engine calls FreeType to load a font from memory,
// we check if the font size matches known UI fonts and swap the
// buffer to our Thai font instead.
//
// Known UI font sizes (from headerlib analysis):
//   - helveticaneueltw1g-cn.ttf (Regular)
//   - helveticaneueltw1g-bdcn.ttf (Bold)
//   - hemi_head_bd_it.otf (Header)
//   - caveat-condensed.ttf (Handwritten)
//
// Strategy: Swap ALL font loads that look like UI fonts (reasonable size)
// to our Thai font. The game loads ~20 fonts total but we only need to
// replace the 4 Latin UI slots.
// ============================================================================

int __cdecl detour_FT_New_Memory_Face(void* library, const unsigned char* file_base,
                                       long file_size, long face_index, void** aface)
{
    g_font_load_count++;

    sp::io::ps_ostream* dbg = g_debug;

    // Log every font load attempt
    if (dbg) {
        char tmp[256];
        wsprintfA(tmp, "[FONT] FT_New_Memory_Face #%d: buffer=0x%IX size=%ld face_idx=%ld",
            g_font_load_count, (uint64_t)file_base, file_size, face_index);
        dbg->print(std::string(tmp) + "\n");
    }

    // Check if we should swap this font
    // Strategy: Swap fonts in the typical UI font size range (10KB - 2MB)
    // CJK fonts are typically 5-15MB so they won't be caught here
    // The Latin UI fonts (Helvetica Neue, Hemi Head, Caveat) are typically 30KB-500KB
    bool should_swap = false;

    if (g_thai_font_loaded && g_thai_font_buffer && g_thai_font_size > 0) {
        // Swap fonts that are in the Latin UI font size range
        // Exclude very small fonts (<10KB = debug/icon fonts)
        // Exclude very large fonts (>2MB = CJK fonts we don't want to replace)
        if (file_size >= 10000 && file_size <= 2000000) {
            should_swap = true;
            
            // FONT 4 CRASH FIX (Epic Games specific):
            // Font #4 (67848 bytes) crashes when swapped in Epic Games version
            if (file_size == 67848) {
                should_swap = false;
                if (dbg) dbg->print("[FONT] SKIPPING Font #4 swap to prevent crash on Epic Games.\n");
            }
        }
    }

    if (should_swap) {
        g_font_swap_count++;
        if (dbg) {
            char tmp[256];
            wsprintfA(tmp, "[FONT] *** SWAPPING font #%d (original %ld bytes) -> Thai font (%ld bytes)",
                g_font_swap_count, file_size, g_thai_font_size);
            dbg->print(std::string(tmp) + "\n");
        }

        // Call original FT_New_Memory_Face with our Thai font buffer instead!
        return g_orig_FT_New_Memory_Face(library, g_thai_font_buffer,
                                          g_thai_font_size, face_index, aface);
    }

    // No swap — call original with the engine's buffer
    return g_orig_FT_New_Memory_Face(library, file_base, file_size, face_index, aface);
}

// ============================================================================
// DETOUR: ZTextBundle::TryGetText — Universal text translation hook
// PDB Signature:
//   bool ZTextBundle::TryGetText(const ZString& key, ZString& outResult, bool) const
//   Mangled: ?TryGetText@ZTextBundle@@QEBA_NAEBVZString@@AEAV2@_N@Z
//
// This is the Dawn Engine's central text lookup function. Every piece of
// localized text in the game (menus, UI labels, item names, descriptions,
// codex entries, subtitles) flows through this function.
//
// Strategy:
//   1. Let the original function execute to get the English text
//   2. Read the output ZString to get the English text pointer
//   3. Look up Thai translation in our dictionary
//   4. If found, overwrite the output ZString with the Thai text
// ============================================================================

// SEH helpers for ZTextBundle::TryGetText (C2712 avoidance)
static __declspec(noinline) char* safe_read_zstring_ptr(void* pZString)
{
    __try {
        ZStringLayout* zs = (ZStringLayout*)pZString;
        return zs->text_ptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static __declspec(noinline) bool safe_overwrite_zstring_content(
    void* pZString, const char* new_text, uint32_t new_len)
{
    __try {
        ZStringLayout* zs = (ZStringLayout*)pZString;
        
        // Strategy 1: Copy Thai text INTO the existing engine buffer.
        // This preserves text_ptr so the engine's internal pointer tracking
        // remains intact. We ONLY overwrite the character data.
        if (zs->text_ptr && new_len <= zs->capacity) {
            for (uint32_t i = 0; i < new_len; ++i) {
                zs->text_ptr[i] = new_text[i];
            }
            zs->text_ptr[new_len] = '\0';
            zs->length = new_len;
            return true;
        }
        
        // Strategy 2: Buffer too small — replace pointer with persistent copy.
        // The persistent copy lives in our static cache and is never freed,
        // so it's safe for the engine to reference it indefinitely.
        zs->text_ptr = const_cast<char*>(new_text);  // new_text is already persistent
        zs->length = new_len;
        zs->capacity = new_len;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool __fastcall detour_ZTextBundle_TryGetText(void* pThis, void* pKey, void* pOutResult, bool fallback)
{
    bool result = g_orig_ZTextBundle_TryGetText(pThis, pKey, pOutResult, fallback);
    if (!result || !pOutResult) return result;

    ZStringLayout* zs = (ZStringLayout*)pOutResult;
    if (!zs || !zs->text_ptr || zs->length == 0 || zs->length > 8192)
        return result;

    std::string_view sv(zs->text_ptr, zs->length);
    
    bool has_cr = false;
    for (char c : sv) {
        if (c == '\r') { has_cr = true; break; }
    }
    
    std::string normalized;
    std::string_view lookup_key = sv;
    if (has_cr) {
        normalized.reserve(sv.size());
        for (char c : sv) {
            if (c != '\r') normalized.push_back(c);
        }
        lookup_key = normalized;
    }

    auto it = g_fast_lookup.find(lookup_key);
    if (it == g_fast_lookup.end()) {
        // DIAGNOSTIC: log ANY unmatched text > 50 chars
        if (zs->length > 50 && g_debug) {
            std::string msg = "[DIAG-TryGetText] MISS len=" + std::to_string(zs->length) + " text: ";
            uint32_t plen = (zs->length < 80) ? zs->length : 80;
            msg.append(zs->text_ptr, plen);
            msg += "\n";
            g_debug->print(msg);
        }
        return result;
    }

    // DIAGNOSTIC: log successful matches > 50 chars
    if (zs->length > 50 && g_debug) {
        std::string msg = "[DIAG-TryGetText] HIT! len=" + std::to_string(zs->length) + " text: ";
        uint32_t plen = (zs->length < 80) ? zs->length : 80;
        msg.append(zs->text_ptr, plen);
        msg += "\n";
        g_debug->print(msg);
    }

    const char* thai = it->second;
    uint32_t thai_len = 0;
    for (const char* p = thai; *p; ++p) ++thai_len;

    if (thai_len <= zs->capacity) {
        // Fits in buffer — overwrite in-place
        for (uint32_t i = 0; i < thai_len; ++i)
            zs->text_ptr[i] = thai[i];
        zs->text_ptr[thai_len] = '\0';
        zs->length = thai_len;
    } else {
        // CRASH VECTOR 2 FIX: Do NOT swap pointers for TryGetText!
        // The engine frees these buffers. If we swap it to our dictionary, it will crash when freed.
        // Fallback to English for strings that exceed buffer capacity.
    }
    return result;
}

// ============================================================================
// DETOUR 5: ZTextBundle::TryGetText (Hash-based overload)
// ============================================================================
bool __fastcall detour_ZTextBundle_TryGetText_Hash(void* pThis, void* pRDX, void* pOutResult)
{
    bool result = g_orig_ZTextBundle_TryGetText_Hash(pThis, pRDX, pOutResult);
    if (!result || !pOutResult) return result;

    ZStringLayout* zs = (ZStringLayout*)pOutResult;
    if (!zs || !zs->text_ptr || zs->length == 0 || zs->length > 8192)
        return result;

    std::string_view sv(zs->text_ptr, zs->length);
    
    bool has_cr = false;
    for (char c : sv) {
        if (c == '\r') { has_cr = true; break; }
    }
    
    std::string normalized;
    std::string_view lookup_key = sv;
    if (has_cr) {
        normalized.reserve(sv.size());
        for (char c : sv) {
            if (c != '\r') normalized.push_back(c);
        }
        lookup_key = normalized;
    }

    auto it = g_fast_lookup.find(lookup_key);
    if (it == g_fast_lookup.end()) return result;

    const char* thai = it->second;
    uint32_t thai_len = 0;
    for (const char* p = thai; *p; ++p) ++thai_len;

    if (thai_len <= zs->capacity) {
        for (uint32_t i = 0; i < thai_len; ++i)
            zs->text_ptr[i] = thai[i];
        zs->text_ptr[thai_len] = '\0';
        zs->length = thai_len;
    } else {
        // CRASH VECTOR 2 FIX: Do NOT swap pointers for TryGetText!
    }
    return result;
}
// ============================================================================
// DETOUR 6: ui::base::ZTextFieldEntity::OnSetText
// RVA: 0x00BF64D0
// Intercepts direct text assignments to UI elements (Menus, Codex, Headers)
//
// SAFETY: We create a STACK-LOCAL ZStringLayout with the Thai text and pass
// that to the original function. The engine copies text from the ZString
// internally, so it never takes ownership of our pointer. This is the same
// pattern used successfully by the ZSubtitlesField::SetText hook.
// ============================================================================
void __fastcall detour_ZTextFieldEntity_OnSetText(void* pThis, void* pZString)
{
    if (pZString && !tl_inside_hook) {
        char* out_text = safe_read_zstring_ptr(pZString);
        if (out_text && out_text[0]) {
            tl_inside_hook = true;
            char safe_buf[4096];
            if (safe_read_cstring(out_text, safe_buf, sizeof(safe_buf))) {
                // DIAGNOSTIC: log ALL text > 20 chars passing through OnSetText
                size_t slen = 0;
                for (const char* p = safe_buf; *p; ++p) ++slen;
                if (slen > 20 && g_debug) {
                    std::string preview = "[DIAG-OnSetText] len=" + std::to_string(slen) + " text: ";
                    size_t plen = (slen < 80) ? slen : 80;
                    preview.append(safe_buf, plen);
                    preview += "\n";
                    g_debug->print(preview);
                }
                const char* translated = lookup_translation(safe_buf);
                if (translated != safe_buf) {
                    // Build a fresh stack-local ZString with the Thai text
                    // The engine copies from this internally — never frees our pointer
                    size_t thai_len = 0;
                    for (const char* p = translated; *p; ++p) ++thai_len;

                    const char* stable = get_persistent_copy(translated, thai_len);

                    ZStringLayout thaiZString;
                    memset(&thaiZString, 0, sizeof(thaiZString));
                    thaiZString.text_ptr  = const_cast<char*>(stable);
                    thaiZString.length    = (uint32_t)thai_len;
                    thaiZString.capacity  = (uint32_t)thai_len;

                    tl_inside_hook = false;
                    g_orig_ZTextFieldEntity_OnSetText(pThis, &thaiZString);
                    return;
                } else {
                    size_t key_len = 0;
                    for (const char* p = safe_buf; *p; ++p) ++key_len;
                    log_missing_key(safe_buf, key_len);
                }
            }
            tl_inside_hook = false;
        }
    }

    // No translation — pass the original ZString through unchanged
    g_orig_ZTextFieldEntity_OnSetText(pThis, pZString);
}

// ============================================================================
// DETOUR 7: ui::base::ZTextFieldEntity::GetText
// RVA: 0x00BECEF0
// Returns ZString by value (caller passes pRetBuffer in RCX, pThis in RDX)
// Intercepts all text fetched by the UI framework for rendering!
//
// SAFETY: The caller reads from the return buffer AFTER this function returns,
// so the Thai text pointer must outlive this call. We use get_persistent_copy()
// which returns a heap-allocated, deduplicated pointer stable for the entire
// process lifetime. The engine's original text_ptr is NOT freed by this code
// path — we only swap the struct members, and the original allocation remains
// owned by the engine's internal ZTextBundle pool (which is never freed until
// the text bundle itself unloads).
// ============================================================================
void* __fastcall detour_ZTextFieldEntity_GetText(void* pRetBuffer, void* pThis)
{
    // Let original populate the return buffer first
    void* result = g_orig_ZTextFieldEntity_GetText(pRetBuffer, pThis);

    if (result && !tl_inside_hook) {
        char* out_text = safe_read_zstring_ptr(result);
        if (out_text && out_text[0]) {
            tl_inside_hook = true;
            char safe_buf[4096];
            if (safe_read_cstring(out_text, safe_buf, sizeof(safe_buf))) {
                // DIAGNOSTIC: log text > 20 chars through GetText
                {
                    size_t slen = 0;
                    for (const char* p = safe_buf; *p; ++p) ++slen;
                    if (slen > 20 && g_debug) {
                        std::string preview = "[DIAG-GetText] len=" + std::to_string(slen) + " text: ";
                        size_t plen = (slen < 80) ? slen : 80;
                        preview.append(safe_buf, plen);
                        preview += "\n";
                        g_debug->print(preview);
                    }
                }
                const char* translated = lookup_translation(safe_buf);
                if (translated != safe_buf) {
                    size_t thai_len = 0;
                    for (const char* p = translated; *p; ++p) ++thai_len;

                    // Use persistent cache — pointer is stable for entire session
                    const char* stable = get_persistent_copy(translated, thai_len);
                    safe_overwrite_zstring_content(result, stable, (uint32_t)thai_len);
                } else {
                    size_t key_len = 0;
                    for (const char* p = safe_buf; *p; ++p) ++key_len;
                    log_missing_key(safe_buf, key_len);
                }
            }
            tl_inside_hook = false;
        }
    }
    return result;
}

// ============================================================================
// MINHOOK INSTALLATION — seven active hooks
// ============================================================================

GotgHookStatus install_all_hooks()
{
    if (g_hooks_installed) {
        if (g_debug) g_debug->print("[WARNING] Hooks already installed\n");
        return GotgHookStatus::OK;
    }

#ifdef MINHOOK_AVAILABLE
    MH_STATUS status;

    // Initialize MinHook
    status = MH_Initialize();
    if (status != MH_OK) {
        if (g_debug) {
            g_debug->print("[ERROR] MinHook init failed: " + std::string(MH_STATUS_MESSAGE(status)) + "\n");
        }
        MessageBoxA(NULL, "GOTG Thai Mod: MinHook initialization failed.\n", "MinHook Error", MB_OK | MB_ICONERROR);
        return GotgHookStatus::ERR_MINHOOK_INIT;
    }
    if (g_debug) g_debug->print("[OK] MinHook initialized\n");

    // =========================================================================
    // HOOK 1: StringAlloc — DISABLED for isolation test
    // =========================================================================
    /*
    {
        void* target_addr = (void*)(gotg_base + 0x64CA24);
        if (g_debug) g_debug->print("[INFO] StringAlloc target: 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");

        status = MH_CreateHook(target_addr,
                               &detour_string_alloc,
                               reinterpret_cast<void**>(&g_orig_StringAlloc));
        if (status == MH_OK) {
            status = MH_EnableHook(target_addr);
            if (g_debug) g_debug->print("[OK] Hooked StringAlloc @ 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");
        }
    }
    */

    // =========================================================================
    // HOOK 2: ZSubtitlesField::SetText — subtitle rendering + translation
    // Offset: gotg_base + 0x1BFB5A0
    // =========================================================================
    {
        uint64_t rva = 0x1BFB5A0; // Epic Games
        if (gotg_file_size == 508186624) rva = 0x1BF5360; // Steam
        
        void* target_addr = (void*)(gotg_base + rva);
        if (g_debug) g_debug->print("[INFO] ZSubtitlesField::SetText target: 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");

        status = MH_CreateHook(target_addr,
                               &detour_ZSubtitlesField_SetText,
                               reinterpret_cast<void**>(&g_orig_ZSubtitlesField_SetText));
        if (status == MH_OK) {
            status = MH_EnableHook(target_addr);
            if (g_debug) g_debug->print("[OK] Hooked ZSubtitlesField::SetText @ 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");
        } else {
            if (g_debug) g_debug->print("[ERROR] ZSubtitlesField::SetText hook failed: " + std::string(MH_STATUS_MESSAGE(status)) + "\n");
        }
    }

    g_hooks_installed = true;

    // =========================================================================
    // HOOK 3: FT_New_Memory_Face — font injection for Thai UI rendering
    // RVA: 0x1DB69D30 (verified via gotg.pdb + DbgHelp symbol extraction)
    // This is the FreeType function that loads font faces from memory buffers.
    // =========================================================================
    {
        // Load Thai font file from disk into memory buffer
        if (!g_thai_font_loaded) {
            // Build path: same directory as gotg.exe
            wchar_t exe_dir[MAX_PATH] = L"";
            GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
            wchar_t* last_slash = wcsrchr(exe_dir, L'\\');
            if (last_slash) *(last_slash + 1) = L'\0';

            std::wstring font_path = std::wstring(exe_dir) + L"font_th.ttf";

            HANDLE hFont = CreateFileW(font_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFont != INVALID_HANDLE_VALUE) {
                DWORD font_file_size = GetFileSize(hFont, nullptr);
                if (font_file_size > 0 && font_file_size < 50 * 1024 * 1024) {
                    g_thai_font_buffer = new unsigned char[font_file_size];
                    DWORD bytes_read = 0;
                    if (ReadFile(hFont, g_thai_font_buffer, font_file_size, &bytes_read, nullptr)
                        && bytes_read == font_file_size) {
                        g_thai_font_size = (long)font_file_size;
                        g_thai_font_loaded = true;
                        if (g_debug) {
                            char tmp[256];
                            wsprintfA(tmp, "[FONT] Loaded font_th.ttf: %lu bytes\n", font_file_size);
                            g_debug->print(std::string(tmp));
                        }
                    } else {
                        delete[] g_thai_font_buffer;
                        g_thai_font_buffer = nullptr;
                        if (g_debug) g_debug->print("[FONT] Failed to read font_th.ttf\n");
                    }
                }
                CloseHandle(hFont);
            } else {
                if (g_debug) g_debug->print("[FONT] font_th.ttf not found in game directory\n");
            }
        }

        uint64_t rva = 0x1DB69D30; // Epic Games
        if (gotg_file_size == 508186624) rva = 0x1E644840; // Steam
        
        void* target_addr = (void*)(gotg_base + rva);
        if (g_debug) g_debug->print("[INFO] FT_New_Memory_Face target: 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");

        status = MH_CreateHook(target_addr,
                               &detour_FT_New_Memory_Face,
                               reinterpret_cast<void**>(&g_orig_FT_New_Memory_Face));
        if (status == MH_OK) {
            status = MH_EnableHook(target_addr);
            if (g_debug) g_debug->print("[OK] Hooked FT_New_Memory_Face @ 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");
        } else {
            if (g_debug) g_debug->print("[WARN] FT_New_Memory_Face hook failed: " + std::string(MH_STATUS_MESSAGE(status)) + "\n");
        }
    }

    // =========================================================================
    // HOOK 4/5: TryGetText — PASSTHROUGH (isolation test)
    // =========================================================================
    {
        uint64_t rva = 0x0060DAD0; // Epic Games
        if (gotg_file_size == 508186624) rva = 0x607B10; // Steam
        
        void* target_addr = (void*)(gotg_base + rva);
        status = MH_CreateHook(target_addr, &detour_ZTextBundle_TryGetText,
                               reinterpret_cast<void**>(&g_orig_ZTextBundle_TryGetText));
        if (status == MH_OK) {
            MH_EnableHook(target_addr);
            if (g_debug) g_debug->print("[OK] Hooked TryGetText @ 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");
        } else {
            if (g_debug) g_debug->print("[WARN] TryGetText hook FAILED: " + std::string(MH_STATUS_MESSAGE(status)) + "\n");
        }
    }
    {
        uint64_t rva = 0x60DD80; // Epic Games
        if (gotg_file_size == 508186624) rva = 0x607DC0; // Steam
        
        void* target_addr = (void*)(gotg_base + rva);
        status = MH_CreateHook(target_addr, &detour_ZTextBundle_TryGetText_Hash,
                               reinterpret_cast<void**>(&g_orig_ZTextBundle_TryGetText_Hash));
        if (status == MH_OK) {
            MH_EnableHook(target_addr);
            if (g_debug) g_debug->print("[OK] Hooked TryGetText_Hash @ 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");
        } else {
            if (g_debug) g_debug->print("[WARN] TryGetText_Hash hook FAILED: " + std::string(MH_STATUS_MESSAGE(status)) + "\n");
        }
    }

    // =========================================================================
    // HOOK 6: ui::base::ZTextFieldEntity::OnSetText (DISABLED - Crash Vector)
    // =========================================================================
    /*
    {
        uint64_t rva = 0xBF64D0; // Epic Games
        if (gotg_file_size == 508186624) rva = 0xBF0170; // Steam
        
        void* target_addr = (void*)(gotg_base + rva);
        status = MH_CreateHook(target_addr, &detour_ZTextFieldEntity_OnSetText,
                               reinterpret_cast<void**>(&g_orig_ZTextFieldEntity_OnSetText));
        if (status == MH_OK) {
            MH_EnableHook(target_addr);
            if (g_debug) g_debug->print("[OK] Hooked OnSetText @ 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");
        } else {
            if (g_debug) g_debug->print("[WARN] OnSetText hook failed: " + std::string(MH_STATUS_MESSAGE(status)) + "\n");
        }
    }
    */

    // =========================================================================
    // HOOK 7: ui::base::ZTextFieldEntity::GetText (DISABLED - Crash Vector)
    // RVA: 0x00BECEF0
    // =========================================================================
    /*
    {
        uint64_t rva = 0xBECEF0; // Epic Games
        if (gotg_file_size == 508186624) rva = 0xBE6B90; // Steam
        
        void* target_addr = (void*)(gotg_base + rva);
        status = MH_CreateHook(target_addr, &detour_ZTextFieldEntity_GetText,
                               reinterpret_cast<void**>(&g_orig_ZTextFieldEntity_GetText));
        if (status == MH_OK) {
            MH_EnableHook(target_addr);
            if (g_debug) g_debug->print("[OK] Hooked GetText @ 0x" + sp::str::to_hex((uint64_t)target_addr) + "\n");
        } else {
            if (g_debug) g_debug->print("[WARN] GetText hook failed: " + std::string(MH_STATUS_MESSAGE(status)) + "\n");
        }
    }
    */

    if (g_debug) g_debug->print("[OK] All Dawn Engine hooks installed\n");

    return GotgHookStatus::OK;

#else
    if (g_debug) {
        g_debug->print("[ERROR] MinHook not compiled — MINHOOK_AVAILABLE not defined\n");
    }
    return GotgHookStatus::ERR_MINHOOK_INIT;
#endif
}

GotgHookStatus uninstall_all_hooks()
{
#ifdef MINHOOK_AVAILABLE
    if (!g_hooks_installed) return GotgHookStatus::OK;

    // Disable hooks FIRST so no new calls enter our detours during teardown
    MH_DisableHook(MH_ALL_HOOKS);

    // Write any pending dump after hooks are disabled (safe — no new entries)
    dump_all_strings_to_json();

    // Close missing keys log file
    if (g_missing_keys_file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(g_missing_keys_file);
        CloseHandle(g_missing_keys_file);
        g_missing_keys_file = INVALID_HANDLE_VALUE;
        g_missing_keys_file_opened = false;
    }

    // Free Thai font buffer
    if (g_thai_font_buffer) {
        delete[] g_thai_font_buffer;
        g_thai_font_buffer = nullptr;
        g_thai_font_loaded = false;
    }

    MH_Uninitialize();
    g_hooks_installed = false;
#endif
    return GotgHookStatus::OK;
}

// ============================================================================
// UTILITIES
// ============================================================================

std::string calculate_file_md5(const std::string& fpath, size_t read_sz)
{
    std::ifstream f(fpath, std::ios::binary);
    if (!f.good()) return "";

    std::vector<unsigned char> buffer(read_sz);
    f.read(reinterpret_cast<char*>(buffer.data()), read_sz);
    size_t bytes_read = f.gcount();

    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < bytes_read; i++) {
        hash ^= buffer[i];
        hash *= 0x100000001b3ULL;
    }
    return sp::str::to_hex(hash);
}

BOOL get_module_size(HMODULE hmodule, LPVOID* lplp_base, PDWORD64 lpdw_size)
{
    if (!hmodule) return FALSE;
    MODULEINFO mi;
    if (!GetModuleInformation(GetCurrentProcess(), hmodule, &mi, sizeof(mi)))
        return FALSE;
    *lplp_base = mi.lpBaseOfDll;
    *lpdw_size = mi.SizeOfImage;
    return TRUE;
}

void install_pre_hook()
{
    if (g_debug) g_debug->print("[INFO] Pre-hook not implemented\n");
}

// Manual trigger for dump write — call this from DllMain or a hotkey
void trigger_string_dump()
{
    dump_all_strings_to_json();
}
