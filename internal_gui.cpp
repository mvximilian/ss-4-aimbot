#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <cstring>
#include "shared_config.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"
#include "third_party/imgui/backends/imgui_impl_dx11.h"
#include "third_party/minhook/include/MinHook.h"

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static HMODULE g_module{};
static SharedSettings* g_shared{};
static HANDLE g_mapping{};
static std::atomic<bool> g_stopping{false};

static PresentFn g_originalPresent{};
static ResizeBuffersFn g_originalResizeBuffers{};
static HWND g_game{};
static WNDPROC g_originalWndProc{};
static IDXGISwapChain* g_gameSwap{};
static ID3D11Device* g_device{};
static ID3D11DeviceContext* g_context{};
static ID3D11RenderTargetView* g_target{};
static bool g_imguiReady = false;
static bool g_menuOpen = true;
static bool g_insertWas = false;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void setStatus(const char* text) {
    if (!g_shared) return;
    strncpy_s(g_shared->status, sizeof(g_shared->status), text, _TRUNCATE);
}

static bool isInputMessage(UINT m) {
    switch (m) {
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR:
            return true;
        default:
            return false;
    }
}

static LRESULT CALLBACK hookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_menuOpen && g_imguiReady) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        ImGuiIO& io = ImGui::GetIO();
        if (isInputMessage(msg) && (io.WantCaptureMouse || io.WantCaptureKeyboard || msg == WM_CHAR))
            return 1;
    }
    return CallWindowProcW(g_originalWndProc, hwnd, msg, wp, lp);
}

static void destroyTarget() {
    if (g_target) { g_target->Release(); g_target = nullptr; }
}

static bool createTarget(IDXGISwapChain* swap) {
    destroyTarget();
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    HRESULT hr = g_device->CreateRenderTargetView(back, nullptr, &g_target);
    back->Release();
    return SUCCEEDED(hr) && g_target;
}

static bool initImGui(IDXGISwapChain* swap) {
    if (g_imguiReady) return true;

    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(swap->GetDesc(&sd)) || !sd.OutputWindow) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(sd.OutputWindow, &pid);
    if (pid != GetCurrentProcessId()) return false;

    ID3D11Device* device = nullptr;
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device)
        return false; // This build intentionally targets the game's DX11 renderer.

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    if (!context) { device->Release(); return false; }

    g_gameSwap = swap;
    g_game = sd.OutputWindow;
    g_device = device;
    g_context = context;
    if (!createTarget(swap)) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(g_game) || !ImGui_ImplDX11_Init(g_device, g_context))
        return false;

    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_game, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hookedWndProc)));
    if (!g_originalWndProc) return false;

    g_imguiReady = true;
    setStatus("Internal DX11 ImGui hook active. Insert toggles the menu.");
    return true;
}

static void drawMenu() {
    if (!g_shared) return;

    ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);
    ImGui::Begin("Sam4 - Internal Menu", &g_menuOpen, ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Injected DX11 renderer | Insert: show/hide | End: exit");
    ImGui::Separator();

    bool b;
    b = g_shared->aim != 0; if (ImGui::Checkbox("Enable aim (hold right mouse)", &b)) g_shared->aim = b;
    b = g_shared->wall != 0; if (ImGui::Checkbox("Wall check", &b)) g_shared->wall = b;
    b = g_shared->silent != 0; if (ImGui::Checkbox("Silent aim (experimental)", &b)) g_shared->silent = b;

    ImGui::SeparatorText("Targeting");
    float v = g_shared->fov; if (ImGui::SliderFloat("Aim FOV", &v, 1.f, 360.f, "%.0f deg")) g_shared->fov = v;
    v = g_shared->distance; if (ImGui::SliderFloat("Range", &v, 1.f, 2000.f, "%.0f u")) g_shared->distance = v;
    int pr = static_cast<int>(g_shared->priority);
    const char* names[] = {"Closest", "FOV", "Farthest", "Lowest health"};
    if (ImGui::Combo("Priority", &pr, names, 4)) g_shared->priority = pr;

    ImGui::SeparatorText("ESP");
    b = g_shared->esp != 0; if (ImGui::Checkbox("Box ESP", &b)) g_shared->esp = b;
    ImGui::SameLine();
    b = g_shared->tracers != 0; if (ImGui::Checkbox("Tracers", &b)) g_shared->tracers = b;
    b = g_shared->espStabilize != 0; if (ImGui::Checkbox("Stabilize ESP", &b)) g_shared->espStabilize = b;
    v = g_shared->cameraFov; if (ImGui::SliderFloat("ESP camera FOV", &v, 30.f, 150.f, "%.0f deg")) g_shared->cameraFov = v;

    ImGui::SeparatorText("Projectile prediction");
    b = g_shared->prediction != 0; if (ImGui::Checkbox("Enable prediction", &b)) g_shared->prediction = b;
    v = g_shared->projectileSpeed; if (ImGui::SliderFloat("Projectile speed", &v, 1.f, 500.f, "%.0f u/s")) g_shared->projectileSpeed = v;
    v = g_shared->projectileGravity; if (ImGui::SliderFloat("Drop compensation", &v, 0.f, 200.f, "%.0f u/s^2")) g_shared->projectileGravity = v;

    ImGui::SeparatorText("Third person");
    b = g_shared->modelFacing != 0; if (ImGui::Checkbox("Rotate model with silent aim", &b)) g_shared->modelFacing = b;
    b = g_shared->antiAim != 0; if (ImGui::Checkbox("Anti-aim model spin", &b)) g_shared->antiAim = b;
    v = g_shared->spinSpeed; if (ImGui::SliderFloat("Spin speed", &v, 0.f, 1440.f, "%.0f deg/s")) g_shared->spinSpeed = v;

    ImGui::SeparatorText("Status");
    ImGui::TextWrapped("%s", g_shared->status[0] ? g_shared->status : "Controller connected.");
    if (ImGui::Button("Exit controller")) g_shared->requestExit = 1;
    ImGui::End();
}

static HRESULT __stdcall hookedPresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags) {
    if (!g_stopping.load()) {
        bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (ins && !g_insertWas) g_menuOpen = !g_menuOpen;
        g_insertWas = ins;
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            if (g_shared) g_shared->requestExit = 1;
        }
        if (g_shared) g_shared->menuOpen = g_menuOpen ? 1 : 0;

        if (!g_imguiReady) initImGui(swap);

        if (g_imguiReady && swap == g_gameSwap && g_menuOpen && g_target) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            drawMenu();
            ImGui::Render();
            g_context->OMSetRenderTargets(1, &g_target, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }
    return g_originalPresent(swap, syncInterval, flags);
}

static HRESULT __stdcall hookedResizeBuffers(IDXGISwapChain* swap, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT swapFlags) {
    if (g_imguiReady && swap == g_gameSwap) destroyTarget();
    HRESULT hr = g_originalResizeBuffers(swap, count, width, height, format, swapFlags);
    if (SUCCEEDED(hr) && g_imguiReady && swap == g_gameSwap) createTarget(swap);
    return hr;
}

static LRESULT CALLBACK dummyWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcW(h, m, w, l);
}

static bool getDx11Vtable(void** present, void** resizeBuffers) {
    *present = nullptr; *resizeBuffers = nullptr;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = dummyWndProc;
    wc.hInstance = g_module;
    wc.lpszClassName = L"Sam4Dx11Probe";
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                               0, 0, 100, 100, nullptr, nullptr, g_module, nullptr);
    if (!wnd) return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL feature{};
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &sd, &swap, &device, &feature, &context);

    if (SUCCEEDED(hr) && swap) {
        void** vtable = *reinterpret_cast<void***>(swap);
        *present = vtable[8];
        *resizeBuffers = vtable[13];
    }

    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();
    DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, g_module);
    return *present && *resizeBuffers;
}

static void shutdownImGui() {
    if (g_originalWndProc && g_game && IsWindow(g_game)) {
        SetWindowLongPtrW(g_game, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
    }
    g_originalWndProc = nullptr;
    if (g_imguiReady) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    g_imguiReady = false;
    destroyTarget();
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
    g_gameSwap = nullptr;
    g_game = nullptr;
}

static void closeShared() {
    if (g_shared) { UnmapViewOfFile(g_shared); g_shared = nullptr; }
    if (g_mapping) { CloseHandle(g_mapping); g_mapping = nullptr; }
}

static DWORD WINAPI mainThread(void*) {
    wchar_t name[64];
    sharedName(GetCurrentProcessId(), name);
    for (int i = 0; i < 100 && !g_mapping; ++i) {
        g_mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
        if (!g_mapping) Sleep(100);
    }
    if (!g_mapping) { FreeLibraryAndExitThread(g_module, 0); return 0; }

    g_shared = static_cast<SharedSettings*>(MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedSettings)));
    if (!g_shared || g_shared->magic != SAM4_GUI_MAGIC) {
        closeShared(); FreeLibraryAndExitThread(g_module, 0); return 0;
    }

    void* present = nullptr;
    void* resizeBuffers = nullptr;
    if (!getDx11Vtable(&present, &resizeBuffers)) {
        setStatus("Internal GUI failed: could not create the DX11 probe device. Run the game with DX11.");
        closeShared(); FreeLibraryAndExitThread(g_module, 0); return 0;
    }

    if (MH_Initialize() != MH_OK ||
        MH_CreateHook(present, &hookedPresent, reinterpret_cast<void**>(&g_originalPresent)) != MH_OK ||
        MH_CreateHook(resizeBuffers, &hookedResizeBuffers, reinterpret_cast<void**>(&g_originalResizeBuffers)) != MH_OK ||
        MH_EnableHook(present) != MH_OK || MH_EnableHook(resizeBuffers) != MH_OK) {
        setStatus("Internal GUI failed: DXGI hook installation failed.");
        MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize();
        closeShared(); FreeLibraryAndExitThread(g_module, 0); return 0;
    }

    setStatus("Injected. Waiting for the game's DX11 Present call...");
    while (!g_shared->requestExit) Sleep(50);

    g_stopping.store(true);
    MH_DisableHook(MH_ALL_HOOKS);
    Sleep(50); // allow an in-flight Present to leave the detour before teardown
    shutdownImGui();
    MH_Uninitialize();
    closeShared();
    FreeLibraryAndExitThread(g_module, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = h;
        DisableThreadLibraryCalls(h);
        HANDLE t = CreateThread(nullptr, 0, mainThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
