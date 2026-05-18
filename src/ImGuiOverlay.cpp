// ImGuiOverlay.cpp
// DirectX 12 hooking + ImGui overlay for subtitle rendering
// Marvel's GOTG Thai Localization Mod — "ImGui Overlay" protocol
//
// Architecture:
//   1. Hook CreateSwapChainForHwnd from dxgi.dll (MinHook)
//   2. After swap chain creation → hook IDXGISwapChain::Present via VMT
//   3. On first Present → init ImGui with DX12 backend
//   4. Each frame → render subtitle overlay at bottom-center

#include "ImGuiOverlay.h"
#include "TranslationHooks.h"
#include "sp/str.h"
#include "sp/file.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ---- ImGui ----
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

// ---- MinHook ----
#ifdef MINHOOK_AVAILABLE
#include <MinHook.h>
#endif

// ============================================================================
// GLOBALS
// ============================================================================

// Debug stream — set by DllMain via set_translation_debug
static sp::io::ps_ostream* g_im_debug = nullptr;

void set_overlay_debug(sp::io::ps_ostream* dbg){
    g_im_debug = dbg;
}

// ImGui state
static bool   g_imgui_initialized = false;
static bool   g_overlay_initialized = false;
static bool   g_overlay_shutting_down = false;

// DX12 device + swap chain handles
static ID3D12Device*           g_d3d12_device = nullptr;
static ID3D12CommandQueue*     g_d3d12_command_queue = nullptr;
static IDXGISwapChain3*        g_swap_chain3 = nullptr;
static HWND                     g_game_hwnd = nullptr;

// ImGui DX12 resources
static ID3D12DescriptorHeap*   g_d3d12_srv_heap = nullptr;
static ID3D12DescriptorHeap*   g_d3d12_rtv_heap = nullptr;
static int                      g_d3d12_frame_index = 0;
static ID3D12CommandAllocator* g_d3d12_command_allocator = nullptr;
static ID3D12GraphicsCommandList* g_d3d12_command_list = nullptr;
static ID3D12Fence*            g_d3d12_fence = nullptr;
static HANDLE                   g_d3d12_fence_event = nullptr;
static uint64_t                 g_d3d12_fence_value = 0;

// Swap chain buffer count
static UINT    g_buffer_count = 0;
static DXGI_SWAP_CHAIN_DESC1  g_swapchain_desc = {};

// Subtitle state
static std::string g_current_subtitle;
static uint64_t    g_subtitle_set_time = 0;
static uint32_t    g_subtitle_duration_ms = 5000;
static CRITICAL_SECTION g_subtitle_lock;

// Original function pointers (VMT hooks)
typedef HRESULT (WINAPI* CreateSwapChainForHwnd_t)(
    IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
static CreateSwapChainForHwnd_t g_orig_CreateSwapChainForHwnd = nullptr;

typedef HRESULT (STDMETHODCALLTYPE* Present_t)(IDXGISwapChain3*, UINT, UINT);
static Present_t  g_orig_Present = nullptr;
static PVOID      g_present_vtable_orig = nullptr;  // original vtable entry

// ============================================================================
// DX12 UTILITY FUNCTIONS
// ============================================================================

static void wait_for_gpu()
{
    if (!g_d3d12_command_queue || !g_d3d12_fence) return;
    g_d3d12_fence_value++;
    g_d3d12_command_queue->Signal(g_d3d12_fence, g_d3d12_fence_value);
    if (g_d3d12_fence->GetCompletedValue() < g_d3d12_fence_value) {
        g_d3d12_fence->SetEventOnCompletion(g_d3d12_fence_value, g_d3d12_fence_event);
        WaitForSingleObject(g_d3d12_fence_event, INFINITE);
    }
}

static void cleanup_dx12_resources()
{
    wait_for_gpu();

    if (g_d3d12_rtv_heap) { g_d3d12_rtv_heap->Release(); g_d3d12_rtv_heap = nullptr; }
    if (g_d3d12_srv_heap) { g_d3d12_srv_heap->Release(); g_d3d12_srv_heap = nullptr; }

    if (g_d3d12_fence_event) { CloseHandle(g_d3d12_fence_event); g_d3d12_fence_event = nullptr; }
    if (g_d3d12_fence) { g_d3d12_fence->Release(); g_d3d12_fence = nullptr; }
    if (g_d3d12_command_allocator) { g_d3d12_command_allocator->Release(); g_d3d12_command_allocator = nullptr; }
    if (g_d3d12_command_list) { g_d3d12_command_list->Release(); g_d3d12_command_list = nullptr; }
}

// ============================================================================
// IMGUI INIT + FONT LOADING
// ============================================================================

static bool init_imgui()
{
    if (g_imgui_initialized) return true;

    if (!g_d3d12_device || !g_swap_chain3 || !g_game_hwnd) {
        return false;
    }

    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style: transparent window, white text
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_Border]   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Win32 backend init
    if (!ImGui_ImplWin32_Init(g_game_hwnd)) {
        if (g_im_debug) g_im_debug->print("[IMGUI] Win32 backend init failed\n");
        ImGui::DestroyContext();
        return false;
    }

    // Setup DX12 resources
    // SRV heap for ImGui font texture
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_d3d12_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_d3d12_srv_heap)))) {
        if (g_im_debug) g_im_debug->print("[IMGUI] SRV heap creation failed\n");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // Command allocator + list
    if (FAILED(g_d3d12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&g_d3d12_command_allocator)))) {
        if (g_im_debug) g_im_debug->print("[IMGUI] Command allocator creation failed\n");
        g_d3d12_srv_heap->Release(); g_d3d12_srv_heap = nullptr;
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    if (FAILED(g_d3d12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_d3d12_command_allocator, nullptr, IID_PPV_ARGS(&g_d3d12_command_list)))) {
        if (g_im_debug) g_im_debug->print("[IMGUI] Command list creation failed\n");
        g_d3d12_command_allocator->Release(); g_d3d12_command_allocator = nullptr;
        g_d3d12_srv_heap->Release(); g_d3d12_srv_heap = nullptr;
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    g_d3d12_command_list->Close();

    // Fence for GPU sync
    if (FAILED(g_d3d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_d3d12_fence)))) {
        if (g_im_debug) g_im_debug->print("[IMGUI] Fence creation failed\n");
        g_d3d12_command_list->Release(); g_d3d12_command_list = nullptr;
        g_d3d12_command_allocator->Release(); g_d3d12_command_allocator = nullptr;
        g_d3d12_srv_heap->Release(); g_d3d12_srv_heap = nullptr;
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    g_d3d12_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // DX12 backend init (v1.91 API — individual parameters, no struct)
    D3D12_CPU_DESCRIPTOR_HANDLE font_cpu = g_d3d12_srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE font_gpu = g_d3d12_srv_heap->GetGPUDescriptorHandleForHeapStart();

    if (!ImGui_ImplDX12_Init(g_d3d12_device, g_buffer_count,
                              g_swapchain_desc.Format,
                              g_d3d12_srv_heap,
                              font_cpu, font_gpu)) {
        if (g_im_debug) g_im_debug->print("[IMGUI] DX12 backend init failed\n");
        if (g_d3d12_fence_event) { CloseHandle(g_d3d12_fence_event); g_d3d12_fence_event = nullptr; }
        if (g_d3d12_fence) { g_d3d12_fence->Release(); g_d3d12_fence = nullptr; }
        g_d3d12_command_list->Release(); g_d3d12_command_list = nullptr;
        g_d3d12_command_allocator->Release(); g_d3d12_command_allocator = nullptr;
        g_d3d12_srv_heap->Release(); g_d3d12_srv_heap = nullptr;
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // Load Thai font from game directory
    {
        char dll_path[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(nullptr, dll_path, MAX_PATH)) {
            char* lastSlash = nullptr;
            for (char* p = dll_path; *p; ++p) {
                if (*p == '\\' || *p == '/') lastSlash = p;
            }
            if (lastSlash) *lastSlash = '\0';
        }

        std::string font_path = std::string(dll_path) + "\\font_th.ttf";
        float font_size = 32.0f;

        io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_size, nullptr,
                                     io.Fonts->GetGlyphRangesThai());

        if (g_im_debug) g_im_debug->print("[IMGUI] Font loaded: " + font_path + "\n");
    }

    g_imgui_initialized = true;
    if (g_im_debug) g_im_debug->print("[IMGUI] Init OK — Thai subtitle overlay ready\n");
    return true;
}

// ============================================================================
// RENDER SUBTITLE OVERLAY
// ============================================================================

static void render_subtitle_overlay()
{
    if (!g_imgui_initialized) return;

    std::string subtitle_text;
    {
        EnterCriticalSection(&g_subtitle_lock);

        if (!g_current_subtitle.empty()) {
            uint64_t now = GetTickCount64();
            uint64_t elapsed = now - g_subtitle_set_time;
            if (elapsed < g_subtitle_duration_ms) {
                subtitle_text = g_current_subtitle;
            } else {
                g_current_subtitle.clear();
            }
        }

        LeaveCriticalSection(&g_subtitle_lock);
    }

    if (subtitle_text.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_width = io.DisplaySize.x * 0.85f;
    float win_height = 100.0f;
    float win_x = (io.DisplaySize.x - win_width) * 0.5f;
    float win_y = io.DisplaySize.y * 0.82f;

    ImGui::SetNextWindowPos(ImVec2(win_x, win_y));
    ImGui::SetNextWindowSize(ImVec2(win_width, win_height));

    ImGui::Begin("##SubtitleOverlay", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings);

    float text_width = ImGui::CalcTextSize(subtitle_text.c_str()).x;
    float text_x = (win_width - text_width) * 0.5f;
    if (text_x < 0) text_x = 0;
    ImGui::SetCursorPosX(text_x);

    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::TextUnformatted(subtitle_text.c_str());
    ImGui::PopStyleColor();

    ImGui::End();
}

// ============================================================================
// DETOUR: IDXGISwapChain::Present
// ============================================================================

static HRESULT STDMETHODCALLTYPE detour_Present(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
{
    if (!g_imgui_initialized) {
        init_imgui();
    }

    if (g_imgui_initialized) {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        render_subtitle_overlay();

        ImGui::Render();

        // Reset allocator and command list for this frame
        g_d3d12_command_allocator->Reset();
        g_d3d12_command_list->Reset(g_d3d12_command_allocator, nullptr);

        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_d3d12_command_list);
    }

    return g_orig_Present(pSwapChain, SyncInterval, Flags);
}

// ============================================================================
// DETOUR: CreateSwapChainForHwnd
// ============================================================================

static HRESULT WINAPI detour_CreateSwapChainForHwnd(
    IDXGIFactory2* pFactory,
    IUnknown* pDevice,
    HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain)
{
    HRESULT hr = g_orig_CreateSwapChainForHwnd(pFactory, pDevice, hWnd,
        pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);

    if (SUCCEEDED(hr) && *ppSwapChain && !g_swap_chain3) {
        (*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&g_swap_chain3));
        g_game_hwnd = hWnd;
        memcpy(&g_swapchain_desc, pDesc, sizeof(*pDesc));
        g_buffer_count = pDesc->BufferCount;
        if (g_buffer_count < 2) g_buffer_count = 2;
        if (g_buffer_count > 3) g_buffer_count = 3;

        if (pDevice) {
            pDevice->QueryInterface(IID_PPV_ARGS(&g_d3d12_device));
        }

        if (g_d3d12_device) {
            D3D12_COMMAND_QUEUE_DESC qDesc = {};
            qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            qDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            qDesc.NodeMask = 0;
            g_d3d12_device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&g_d3d12_command_queue));
        }

        if (g_im_debug) {
            g_im_debug->print("[DX12] SwapChain: " + std::to_string(pDesc->Width) + "x" +
                std::to_string(pDesc->Height) + " HWND=0x" + sp::str::to_hex((uint64_t)hWnd) + "\n");
        }

        if (g_swap_chain3) {
            PVOID* vtable = *(PVOID**)g_swap_chain3;
            g_present_vtable_orig = vtable[8];
            g_orig_Present = (Present_t)g_present_vtable_orig;

            DWORD old_protect;
            VirtualProtect(&vtable[8], sizeof(PVOID), PAGE_READWRITE, &old_protect);
            vtable[8] = &detour_Present;
            VirtualProtect(&vtable[8], sizeof(PVOID), old_protect, &old_protect);

            if (g_im_debug) g_im_debug->print("[DX12] Present hook installed via VMT\n");
        }
    }

    return hr;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool init_imgui_overlay()
{
    if (g_overlay_initialized) return true;

    InitializeCriticalSection(&g_subtitle_lock);

    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    if (!dxgi) {
        DeleteCriticalSection(&g_subtitle_lock);
        return false;
    }

#ifdef MINHOOK_AVAILABLE
    void* create_swapchain_addr = (void*)GetProcAddress(dxgi, "CreateSwapChainForHwnd");
    if (!create_swapchain_addr) {
        DeleteCriticalSection(&g_subtitle_lock);
        return false;
    }

    MH_STATUS mh = MH_CreateHook(create_swapchain_addr,
                                  &detour_CreateSwapChainForHwnd,
                                  reinterpret_cast<void**>(&g_orig_CreateSwapChainForHwnd));
    if (mh != MH_OK) {
        DeleteCriticalSection(&g_subtitle_lock);
        return false;
    }

    MH_EnableHook(create_swapchain_addr);
#endif

    g_overlay_initialized = true;
    if (g_im_debug) g_im_debug->print("[OVERLAY] DX12 hook system initialized\n");
    return true;
}

void shutdown_imgui_overlay()
{
    g_overlay_shutting_down = true;

    if (g_imgui_initialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imgui_initialized = false;
    }

    if (g_swap_chain3 && g_present_vtable_orig) {
        PVOID* vtable = *(PVOID**)g_swap_chain3;
        DWORD old_protect;
        VirtualProtect(&vtable[8], sizeof(PVOID), PAGE_READWRITE, &old_protect);
        vtable[8] = g_present_vtable_orig;
        VirtualProtect(&vtable[8], sizeof(PVOID), old_protect, &old_protect);
    }

    cleanup_dx12_resources();

    if (g_d3d12_command_queue) { g_d3d12_command_queue->Release(); g_d3d12_command_queue = nullptr; }
    if (g_swap_chain3) { g_swap_chain3->Release(); g_swap_chain3 = nullptr; }
    if (g_d3d12_device) { g_d3d12_device->Release(); g_d3d12_device = nullptr; }

    DeleteCriticalSection(&g_subtitle_lock);
    g_overlay_initialized = false;
}

void set_overlay_subtitle(const std::string& thai_text, uint32_t duration_ms)
{
    EnterCriticalSection(&g_subtitle_lock);
    g_current_subtitle = thai_text;
    g_subtitle_set_time = GetTickCount64();
    g_subtitle_duration_ms = duration_ms;
    LeaveCriticalSection(&g_subtitle_lock);
}

void clear_overlay_subtitle()
{
    EnterCriticalSection(&g_subtitle_lock);
    g_current_subtitle.clear();
    LeaveCriticalSection(&g_subtitle_lock);
}

const std::string& get_overlay_subtitle()
{
    return g_current_subtitle;
}

bool is_overlay_initialized()
{
    return g_overlay_initialized;
}
