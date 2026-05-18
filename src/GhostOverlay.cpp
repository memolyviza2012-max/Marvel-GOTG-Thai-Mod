// GhostOverlay.cpp
// External transparent Win32 overlay for Thai subtitle rendering
// Marvel's GOTG Thai Localization Mod — "Ghost Overlay" protocol
//
// Architecture:
//   - Detached thread creates a topmost, transparent, borderless Win32 window
//   - Dynamically finds the game HWND via EnumWindows + GetWindowThreadProcessId
//   - Tracks the game window using GetClientRect + ClientToScreen for exact sizing
//   - Renders Thai subtitles via GDI+ onto a 32-bit BGRA DIB section
//   - Displays via UpdateLayeredWindow (per-pixel alpha) — NOT color key
//   - Zero interference with Dawn Engine's DX12 rendering pipeline
//
// Hotfix (2026-05-15):
//   - STOP using FindWindow("UnrealWindow") — use EnumWindows + GetWindowThreadProcessId
//   - Auto-resize via GetClientRect + ClientToScreen on dynamically found game HWND
//   - Per-pixel alpha via UpdateLayeredWindow (DIB section) — NOT SetLayeredWindowAttributes
//   - Periodic SetWindowPos(HWND_TOPMOST) to fight DX12 borderless window layering
//
// Debug Mode (2026-05-15 v2):
//   - DEBUG_SOLID_RED: Disables ALL transparency. Paints a solid RED window with
//     plain GDI text "OVERLAY ACTIVE" to visually confirm the window exists,
//     tracks the game HWND, and stays topmost. No GDI+/font dependency.
//     Set to 0 to restore normal per-pixel alpha subtitle rendering.

// *** SET TO 1 FOR VISIBLE RED DEBUG OVERLAY, 0 FOR PRODUCTION ***
#define DEBUG_SOLID_RED 0

#include "GhostOverlay.h"
#include "TranslationHooks.h"
#include "sp/file.h"
#include "sp/str.h"

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

using namespace Gdiplus;

// ============================================================================
// GLOBALS
// ============================================================================

static sp::io::ps_ostream* g_im_debug = nullptr;

// Overlay window state
static HWND       g_overlay_hwnd = nullptr;
static HWND       g_game_hwnd = nullptr;
static HINSTANCE  g_overlay_hinstance = nullptr;
static HANDLE     g_overlay_thread = nullptr;
static std::atomic<bool> g_overlay_running = false;
static std::atomic<bool> g_overlay_ready = false;

// Subtitle state
static std::string g_current_subtitle;
static uint64_t    g_subtitle_set_time = 0;
static uint32_t    g_subtitle_duration_ms = 5000;
static std::mutex  g_subtitle_mutex;

// GDI+ state
static ULONG_PTR   g_gdiplus_token = 0;
static Gdiplus::Font*    g_thai_font = nullptr;
static Gdiplus::PrivateFontCollection g_font_collection;

// Per-pixel alpha rendering
// 32-bit BGRA DIB section — filled transparent, GDI+ draws text, then UpdateLayeredWindow
static HDC        g_mem_dc = nullptr;
static HBITMAP    g_mem_bmp = nullptr;
static HBITMAP    g_old_bmp = nullptr;
static uint8_t*   g_mem_bits = nullptr;  // raw BGRA pixel data
static int        g_mem_w = 0;
static int        g_mem_h = 0;

// ============================================================================
// ENUM WINDOWS CALLBACK — find windows belonging to our process
// ============================================================================

static HWND g_found_hwnd = nullptr;

static BOOL CALLBACK enum_windows_callback(HWND hwnd, LPARAM lParam)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == GetCurrentProcessId()) {
        RECT r;
        if (GetWindowRect(hwnd, &r)) {
            int w = r.right - r.left;
            int h = r.bottom - r.top;
            if (w >= 800 && h >= 600) {
                if (!IsWindowVisible(hwnd)) return TRUE;
                g_found_hwnd = hwnd;
                return FALSE;  // Stop enumeration
            }
        }
    }
    return TRUE;
}

// ============================================================================
// FIND GAME WINDOW — dynamic process-based detection
// ============================================================================

static HWND find_game_window_by_process()
{
    g_found_hwnd = nullptr;
    EnumWindows(enum_windows_callback, 0);

    if (g_found_hwnd) {
        if (g_im_debug) {
            char tmp[128];
            wsprintfA(tmp, "[GHOST] Found game HWND 0x%IX\n", (uint64_t)g_found_hwnd);
            g_im_debug->print(std::string(tmp));
        }
        return g_found_hwnd;
    }
    return nullptr;
}

// ============================================================================
// LOAD THAI FONT VIA GDI+ PrivateFontCollection
// ============================================================================

static bool load_thai_font(const wchar_t* font_path)
{
    Gdiplus::Status status = g_font_collection.AddFontFile(font_path);
    if (status != Gdiplus::Ok) {
        if (g_im_debug) g_im_debug->print("[GHOST] Failed to load font file\n");
        return false;
    }

    int family_count = g_font_collection.GetFamilyCount();
    if (family_count == 0) {
        if (g_im_debug) g_im_debug->print("[GHOST] Font collection has no families\n");
        return false;
    }

    int name_size = g_font_collection.GetFamilies(0, nullptr, &family_count);
    if (name_size <= 0) {
        if (g_im_debug) g_im_debug->print("[GHOST] Cannot get font families\n");
        return false;
    }

    Gdiplus::FontFamily* families = new Gdiplus::FontFamily[family_count];
    g_font_collection.GetFamilies(family_count, families, &family_count);

    float font_size = 36.0f;
    g_thai_font = new Gdiplus::Font(&families[0], font_size, FontStyleBold, UnitPoint);
    if (!g_thai_font || g_thai_font->GetLastStatus() != Gdiplus::Ok) {
        delete[] families;
        if (g_im_debug) g_im_debug->print("[GHOST] Failed to create font\n");
        return false;
    }

    wchar_t family_name[LF_FACESIZE];
    families[0].GetFamilyName(family_name);
    delete[] families;

    if (g_im_debug) {
        int narrow_len = WideCharToMultiByte(CP_UTF8, 0, family_name, -1, nullptr, 0, nullptr, nullptr);
        if (narrow_len > 0) {
            std::string name(narrow_len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, family_name, -1, &name[0], narrow_len, nullptr, nullptr);
            g_im_debug->print("[GHOST] Font loaded: " + name +
                " (" + std::to_string((int)font_size) + "pt)\n");
        }
    }

    return true;
}

// ============================================================================
// CREATE DIB SECTION for per-pixel alpha rendering
// ============================================================================

static bool ensure_dib_section(int w, int h)
{
    if (g_mem_bmp && g_mem_w == w && g_mem_h == h) return true;

    // Free old
    if (g_mem_dc && g_old_bmp) {
        SelectObject(g_mem_dc, g_old_bmp);
        g_old_bmp = nullptr;
    }
    if (g_mem_bmp) { DeleteObject(g_mem_bmp); g_mem_bmp = nullptr; }
    if (g_mem_dc) { DeleteDC(g_mem_dc); g_mem_dc = nullptr; }
    g_mem_bits = nullptr;

    HDC screen_dc = GetDC(nullptr);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_mem_bmp = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS,
        (void**)&g_mem_bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);

    if (!g_mem_bmp) {
        if (g_im_debug) g_im_debug->print("[GHOST] CreateDIBSection failed\n");
        return false;
    }

    g_mem_dc = CreateCompatibleDC(nullptr);
    g_old_bmp = (HBITMAP)SelectObject(g_mem_dc, g_mem_bmp);
    g_mem_w = w;
    g_mem_h = h;

    return true;
}

// ============================================================================
// RENDER FRAME onto DIB, then push via UpdateLayeredWindow
// ============================================================================

static void render_frame_to_overlay()
{
    if (!g_overlay_hwnd || !g_mem_dc || !g_mem_bits) return;

    int w = g_mem_w;
    int h = g_mem_h;
    if (w <= 0 || h <= 0) return;

    // 1. Clear entire DIB to fully transparent black (BGRA = 0,0,0,0)
    memset(g_mem_bits, 0, (size_t)w * h * 4);

    // 2. Create GDI+ Graphics on the DIB section
    Gdiplus::Graphics graphics(g_mem_dc);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

    // 3. Create system font — no external TTF dependency
    //    Leelawadee UI (Win10+) supports Thai natively; fallback to Tahoma
    Gdiplus::FontFamily family(L"Leelawadee UI");
    if (!family.IsAvailable()) {
        Gdiplus::FontFamily fam2(L"Tahoma");
        // Use Tahoma via a new font
        Gdiplus::Font fallback_font(L"Tahoma", 32.0f, FontStyleBold, UnitPixel);
        // We'll handle below
    }
    Gdiplus::Font subtitle_font(&family,
        family.IsAvailable() ? 36.0f : 32.0f,
        FontStyleBold, UnitPixel);

    // Small debug font
    Gdiplus::Font debug_font(L"Consolas", 18.0f, FontStyleBold, UnitPixel);

    // Brushes
    Gdiplus::SolidBrush white_brush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush black_brush(Gdiplus::Color(255, 0, 0, 0));
    Gdiplus::SolidBrush yellow_brush(Gdiplus::Color(255, 255, 255, 0));

    // ============================================================
    // 4. PERMANENT DEBUG TEXT — always visible at top-left
    //    If you can't see this, the DIB/Alpha pipeline is broken.
    // ============================================================
    {
        Gdiplus::PointF dbg_pos(10.0f, 10.0f);
        // Black outline for debug text
        for (float dx = -2; dx <= 2; dx += 2) {
            for (float dy = -2; dy <= 2; dy += 2) {
                if (dx == 0 && dy == 0) continue;
                Gdiplus::PointF p(dbg_pos.X + dx, dbg_pos.Y + dy);
                graphics.DrawString(L"OVERLAY TEST", -1, &debug_font, p, &black_brush);
            }
        }
        graphics.DrawString(L"OVERLAY TEST", -1, &debug_font, dbg_pos, &yellow_brush);
    }

    // ============================================================
    // 5. SUBTITLE TEXT — from g_current_subtitle
    // ============================================================
    std::string text;
    {
        std::lock_guard<std::mutex> lock(g_subtitle_mutex);
        if (!g_current_subtitle.empty()) {
            uint64_t elapsed = GetTickCount64() - g_subtitle_set_time;
            if (elapsed < g_subtitle_duration_ms) {
                text = g_current_subtitle;
            }
        }
    }

    if (!text.empty()) {
        // Convert UTF-8 → UTF-16
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
        if (wlen > 0) {
            std::wstring wtext(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &wtext[0], wlen);

            Gdiplus::StringFormat format;
            format.SetAlignment(StringAlignmentCenter);
            format.SetLineAlignment(StringAlignmentCenter);

            // Layout: bottom 20% of overlay
            float margin_pct = 0.20f;
            Gdiplus::RectF layout_rect(0.0f, (float)h * (1.0f - margin_pct),
                (float)w, (float)h * margin_pct);

            // Use the font we created (system font — guaranteed to exist)
            Gdiplus::Font* use_font = &subtitle_font;

            // Measure text bounds
            Gdiplus::RectF measure_rect;
            graphics.MeasureString(wtext.c_str(), -1, use_font, layout_rect, &format, &measure_rect);

            // Draw 8-directional black outline
            const float outline = 3.0f;
            float offsets[][2] = {
                {-outline, 0}, {outline, 0},
                {0, -outline}, {0, outline},
                {-outline, -outline}, {outline, -outline},
                {-outline, outline}, {outline, outline},
            };
            for (auto& off : offsets) {
                Gdiplus::RectF sr(measure_rect.X + off[0], measure_rect.Y + off[1],
                    measure_rect.Width, measure_rect.Height);
                graphics.DrawString(wtext.c_str(), -1, use_font, sr, &format, &black_brush);
            }

            // Draw white main text
            graphics.DrawString(wtext.c_str(), -1, use_font, measure_rect, &format, &white_brush);
        }
    }

    // ============================================================
    // 6. FIX PREMULTIPLIED ALPHA for UpdateLayeredWindow
    //    GDI+ draws with alpha=0 into our DIB. We must set alpha
    //    to 255 for every pixel that has any color data, because
    //    UpdateLayeredWindow expects premultiplied alpha.
    // ============================================================
    {
        int pixel_count = w * h;
        uint8_t* px = g_mem_bits;
        for (int i = 0; i < pixel_count; i++, px += 4) {
            uint8_t b = px[0], g = px[1], r = px[2];
            if (r > 0 || g > 0 || b > 0) {
                px[3] = 255;  // Fully opaque
            }
            // else: px[3] remains 0 = fully transparent
        }
    }

    // ============================================================
    // 7. Push to screen via UpdateLayeredWindow (per-pixel alpha)
    // ============================================================
    POINT src_pt = { 0, 0 };
    POINT dst_pt = { 0, 0 };
    SIZE sz = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    RECT overlay_rect;
    if (GetWindowRect(g_overlay_hwnd, &overlay_rect)) {
        dst_pt.x = overlay_rect.left;
        dst_pt.y = overlay_rect.top;
    }

    BOOL result = UpdateLayeredWindow(g_overlay_hwnd, nullptr, &dst_pt, &sz,
        g_mem_dc, &src_pt, 0, &bf, ULW_ALPHA);

    // Log failure once
    static bool logged_ulw_fail = false;
    if (!result && !logged_ulw_fail) {
        logged_ulw_fail = true;
        if (g_im_debug) {
            char tmp[128];
            wsprintfA(tmp, "[GHOST] UpdateLayeredWindow FAILED (err=%lu)\n", GetLastError());
            g_im_debug->print(std::string(tmp));
        }
    }
}

// ============================================================================
// OVERLAY WINDOW PROCEDURE — minimal, rendering is done in thread loop
// ============================================================================

static LRESULT CALLBACK overlay_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT:
#if DEBUG_SOLID_RED
        // DEBUG: Paint solid red background with plain GDI text
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Fill entire client area with RED
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH redBrush = CreateSolidBrush(RGB(220, 20, 20));
            FillRect(hdc, &rc, redBrush);
            DeleteObject(redBrush);

            // Draw debug text using plain GDI (no GDI+ needed)
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));

            // Use a large system font
            HFONT bigFont = CreateFontW(
                48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HFONT oldFont = (HFONT)SelectObject(hdc, bigFont);

            const wchar_t* line1 = L"OVERLAY ACTIVE";
            const wchar_t* line2 = L"Ghost Window is VISIBLE";

            // Build info string with game HWND
            wchar_t line3[128] = L"";
            wsprintfW(line3, L"Game HWND: 0x%IX", (UINT_PTR)g_game_hwnd);

            RECT textRect = rc;
            textRect.top = rc.top + 40;
            DrawTextW(hdc, line1, -1, &textRect, DT_CENTER | DT_SINGLELINE);
            textRect.top += 60;
            DrawTextW(hdc, line2, -1, &textRect, DT_CENTER | DT_SINGLELINE);
            textRect.top += 60;
            DrawTextW(hdc, line3, -1, &textRect, DT_CENTER | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            DeleteObject(bigFont);
            EndPaint(hwnd, &ps);
        }
        return 0;
#else
        // Production: Paint is handled entirely by UpdateLayeredWindow in the thread loop
        // Still call BeginPaint/EndPaint to validate the region
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
        }
        return 0;
#endif
    case WM_ERASEBKGND:
#if DEBUG_SOLID_RED
        return 0;  // Let WM_PAINT handle everything
#else
        return 1;  // Don't erase — UpdateLayeredWindow manages pixels
#endif
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// OVERLAY WINDOW CREATION
// ============================================================================

static bool create_overlay_window()
{
    const wchar_t* className = L"GOTG_GhostOverlay_Class";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = overlay_wndproc;
    wc.hInstance = g_overlay_hinstance;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
#if DEBUG_SOLID_RED
    // DEBUG: Use a red background brush so even without WM_PAINT we see something
    wc.hbrBackground = CreateSolidBrush(RGB(220, 20, 20));
#else
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
#endif
    wc.lpszClassName = className;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (g_im_debug) g_im_debug->print("[GHOST] Failed to register window class\n");
        return false;
    }

#if DEBUG_SOLID_RED
    // DEBUG: Create a normal opaque topmost popup — NO WS_EX_LAYERED
    // This guarantees visibility. WS_EX_LAYERED windows are invisible until
    // SetLayeredWindowAttributes or UpdateLayeredWindow is called.
    g_overlay_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        className,
        L"GOTG Ghost Overlay [DEBUG]",
        WS_POPUP | WS_VISIBLE,
        100, 100, 800, 200,
        nullptr, nullptr, g_overlay_hinstance, nullptr);
#else
    // Production: Create layered, transparent, toolwindow overlay
    // NO color key — use UpdateLayeredWindow for per-pixel alpha
    g_overlay_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        className,
        L"GOTG Ghost Overlay",
        WS_POPUP,
        0, 0, 1920, 1080,
        nullptr, nullptr, g_overlay_hinstance, nullptr);
#endif

    if (!g_overlay_hwnd) {
        DWORD err = GetLastError();
        if (g_im_debug) {
            char tmp[128];
            wsprintfA(tmp, "[GHOST] Failed to create overlay window (err=%lu)\n", err);
            g_im_debug->print(std::string(tmp));
        }
        return false;
    }

    // Show without activating — then force an immediate paint
    ShowWindow(g_overlay_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_overlay_hwnd);  // Forces immediate WM_PAINT dispatch

    if (g_im_debug) {
        char tmp[256];
        RECT wr;
        GetWindowRect(g_overlay_hwnd, &wr);
        wsprintfA(tmp, "[GHOST] Overlay window created at (%ld,%ld %ldx%ld) HWND=0x%IX\n",
            wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, (uint64_t)g_overlay_hwnd);
        g_im_debug->print(std::string(tmp));
#if DEBUG_SOLID_RED
        g_im_debug->print("[GHOST] *** DEBUG_SOLID_RED MODE — opaque red window, no transparency ***\n");
#else
        g_im_debug->print("[GHOST] Production mode (per-pixel alpha via UpdateLayeredWindow)\n");
#endif
    }
    return true;
}

// ============================================================================
// DYNAMIC OVERLAY POSITIONING
// ============================================================================

static void update_overlay_position()
{
    if (!g_overlay_hwnd) return;

    // If game HWND is invalid or lost, re-find it
    if (!g_game_hwnd || !IsWindow(g_game_hwnd)) {
        g_game_hwnd = find_game_window_by_process();
        if (!g_game_hwnd) return;
    }

    // Get the game window's client area dimensions
    RECT client_rect;
    if (!GetClientRect(g_game_hwnd, &client_rect)) return;

    // Convert client rect to screen coordinates for absolute positioning
    POINT top_left = { client_rect.left, client_rect.top };
    POINT bottom_right = { client_rect.right, client_rect.bottom };
    ClientToScreen(g_game_hwnd, &top_left);
    ClientToScreen(g_game_hwnd, &bottom_right);

    int w = bottom_right.x - top_left.x;
    int h = bottom_right.y - top_left.y;

    if (w <= 0 || h <= 0) return;

    // Resize and reposition overlay to match game client area exactly
    SetWindowPos(g_overlay_hwnd, HWND_TOPMOST,
        top_left.x, top_left.y, w, h,
        SWP_NOACTIVATE | SWP_NOREDRAW);

    // Ensure DIB section matches overlay size
    ensure_dib_section(w, h);
}

// ============================================================================
// OVERLAY THREAD
// ============================================================================

static DWORD WINAPI overlay_thread_proc(LPVOID param)
{
    if (g_im_debug) g_im_debug->print("[GHOST] Thread started\n");

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplus_token, &gdiplusInput, nullptr);

    // Find the game window via process enumeration
    for (int retry = 0; retry < 60 && g_overlay_running; retry++) {
        HWND hwnd = find_game_window_by_process();
        if (hwnd) {
            g_game_hwnd = hwnd;
            break;
        }
        Sleep(500);
    }

    if (!g_game_hwnd) {
        if (g_im_debug) g_im_debug->print("[GHOST] Game window not found after 30s\n");
        Gdiplus::GdiplusShutdown(g_gdiplus_token);
        return 1;
    }

    if (g_im_debug) {
        char tmp[128];
        wsprintfA(tmp, "[GHOST] Game HWND: 0x%IX\n", (uint64_t)g_game_hwnd);
        g_im_debug->print(std::string(tmp));
    }

    // Build font path from own module directory
    wchar_t exe_path[MAX_PATH] = L"";
    DWORD path_len = GetModuleFileNameW(g_overlay_hinstance, exe_path, MAX_PATH);
    if (path_len > 0) {
        wchar_t* lastSlash = wcsrchr(exe_path, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
    }

    std::wstring font_full_path = std::wstring(exe_path) + L"font_th.ttf";
    if (!load_thai_font(font_full_path.c_str())) {
        if (!load_thai_font(L"font_th.ttf")) {
            if (g_im_debug) g_im_debug->print("[GHOST] Font not found — overlay active without Thai glyphs\n");
        }
    }

    // Create the overlay window
    if (!create_overlay_window()) {
        Gdiplus::GdiplusShutdown(g_gdiplus_token);
        return 1;
    }

    // Do initial positioning
    update_overlay_position();

    g_overlay_ready = true;
    if (g_im_debug) g_im_debug->print("[GHOST] Overlay ready — tracking game window\n");

    // Main loop — ~30 FPS
    MSG msg = {};
    int frame_counter = 0;
    while (g_overlay_running) {
        // Pump Windows messages (non-blocking)
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_overlay_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_overlay_running) break;

        // Track and resize overlay to match game window
        update_overlay_position();

        // TOPMOST ENFORCEMENT — every 10 frames (~3/sec)
        if (g_overlay_hwnd && (frame_counter % 10 == 0)) {
            SetWindowPos(g_overlay_hwnd, HWND_TOPMOST,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

#if DEBUG_SOLID_RED
        // DEBUG: Force repaint every frame so we can see position updates
        if (g_overlay_hwnd) {
            InvalidateRect(g_overlay_hwnd, nullptr, TRUE);
        }
#else
        // Production: Render subtitle frame via UpdateLayeredWindow
        render_frame_to_overlay();
#endif

        // Verify game window still exists
        if (g_game_hwnd && !IsWindow(g_game_hwnd)) {
            g_game_hwnd = nullptr;
        }

        // Periodic debug logging
        if (g_im_debug && (frame_counter % 300 == 0)) {
            char tmp[256];
            wsprintfA(tmp, "[GHOST] Heartbeat frame=%d game=0x%IX overlay=0x%IX\n",
                frame_counter, (uint64_t)g_game_hwnd, (uint64_t)g_overlay_hwnd);
            g_im_debug->print(std::string(tmp));
        }

        frame_counter++;
        Sleep(33);  // ~30 FPS
    }

    // Cleanup
    if (g_mem_dc && g_old_bmp) {
        SelectObject(g_mem_dc, g_old_bmp);
        g_old_bmp = nullptr;
    }
    if (g_mem_bmp) { DeleteObject(g_mem_bmp); g_mem_bmp = nullptr; }
    if (g_mem_dc) { DeleteDC(g_mem_dc); g_mem_dc = nullptr; }
    g_mem_bits = nullptr;

    if (g_overlay_hwnd) {
        DestroyWindow(g_overlay_hwnd);
        g_overlay_hwnd = nullptr;
    }
    if (g_thai_font) { delete g_thai_font; g_thai_font = nullptr; }
    Gdiplus::GdiplusShutdown(g_gdiplus_token);

    if (g_im_debug) g_im_debug->print("[GHOST] Thread exiting\n");
    return 0;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool init_ghost_overlay()
{
    if (g_overlay_running) return true;

    g_overlay_hinstance = GetModuleHandleA("version.dll");
    if (!g_overlay_hinstance) g_overlay_hinstance = GetModuleHandleA(nullptr);

    g_overlay_running = true;

    g_overlay_thread = CreateThread(nullptr, 0, overlay_thread_proc, nullptr, 0, nullptr);
    if (!g_overlay_thread) {
        g_overlay_running = false;
        if (g_im_debug) g_im_debug->print("[GHOST] Failed to create overlay thread\n");
        return false;
    }

    if (g_im_debug) g_im_debug->print("[GHOST] Overlay thread spawned (detached)\n");
    return true;
}

void shutdown_ghost_overlay()
{
    g_overlay_running = false;

    if (g_overlay_hwnd) {
        PostMessageW(g_overlay_hwnd, WM_QUIT, 0, 0);
    }

    if (g_overlay_thread) {
        WaitForSingleObject(g_overlay_thread, 3000);
        CloseHandle(g_overlay_thread);
        g_overlay_thread = nullptr;
    }

    if (g_thai_font) { delete g_thai_font; g_thai_font = nullptr; }
}

void set_overlay_subtitle(const std::string& thai_text, uint32_t duration_ms)
{
    std::lock_guard<std::mutex> lock(g_subtitle_mutex);
    g_current_subtitle = thai_text;
    g_subtitle_set_time = GetTickCount64();
    g_subtitle_duration_ms = duration_ms;
}

void clear_overlay_subtitle()
{
    std::lock_guard<std::mutex> lock(g_subtitle_mutex);
    g_current_subtitle.clear();
}

bool is_overlay_initialized()
{
    return g_overlay_ready;
}

void set_overlay_debug(sp::io::ps_ostream* dbg)
{
    g_im_debug = dbg;
}