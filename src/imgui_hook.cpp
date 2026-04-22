#include "imgui_hook.h"
#include "Overlay.h"
#include "external/kiero/kiero.h"
#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "imgui_impl_win32.h"
#include <windows.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace ImGuiHook {
typedef BOOL(WINAPI *wglSwapBuffers_t)(HDC hDc);

// Original functions variables
static WNDPROC g_WndProc_o = nullptr;
static wglSwapBuffers_t g_wglSwapBuffers_o = nullptr;

// Global variables
static HGLRC g_wglContext = nullptr;
static bool g_initImGui = false;
static HWND g_hWnd = nullptr;
static MewjectorAPI *g_mj = nullptr;

// Render function variables
static std::function<void()> g_renderMain = []() {};
static std::function<void()> g_extraInit = []() {};

// Last error status
static std::string g_lastError;

// WndProc callback ImGui handler
static LRESULT CALLBACK ImGui_WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam) {
  if (uMsg == WM_KEYDOWN && wParam == VK_F1) {
    if (!(lParam & (1 << 30))) {
      Overlay::ToggleVisible();
    }
    return 1; // Handled
  }

  if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
    return true;

  return CallWindowProc(g_WndProc_o, hWnd, uMsg, wParam, lParam);
}

// Initialisation for ImGui
static bool Init_ImGui(const HDC hDc) {
  if (g_initImGui)
    return true;

  g_hWnd = WindowFromDC(hDc);
  if (!g_hWnd) {
    g_lastError = "Failed to get window handle from HDC";
    return false;
  }

  // Create our own context to avoid conflicts with game's state/profile
  g_wglContext = wglCreateContext(hDc);
  if (!g_wglContext) {
    g_lastError = "Failed to create OpenGL context";
    return false;
  }

  auto o_WglContext = wglGetCurrentContext();
  if (!wglMakeCurrent(hDc, g_wglContext)) {
    g_lastError = "Failed to make our OpenGL context current";
    return false;
  }

  // Hook WndProc
  g_WndProc_o =
      (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)ImGui_WndProc);
  if (!g_WndProc_o) {
    g_lastError = "Failed to hook WndProc";
    wglMakeCurrent(hDc, o_WglContext);
    return false;
  }

  // Initialize ImGui
  IMGUI_CHECKVERSION();
  if (!ImGui::CreateContext()) {
    g_lastError = "Failed to create ImGui context";
    wglMakeCurrent(hDc, o_WglContext);
    return false;
  }

  if (!ImGui_ImplWin32_Init(g_hWnd)) {
    g_lastError = "Failed to init ImGui_ImplWin32";
    wglMakeCurrent(hDc, o_WglContext);
    return false;
  }

  if (!ImGui_ImplOpenGL2_Init()) {
    g_lastError = "Failed to init ImGui_ImplOpenGL2";
    wglMakeCurrent(hDc, o_WglContext);
    return false;
  }

  g_extraInit();
  g_initImGui = true;

  if (g_mj)
    Overlay::Log("[ImGuiHook] ImGui initialized successfully on HWND %p with "
                 "private context %p",
                 g_hWnd, g_wglContext);

  wglMakeCurrent(hDc, o_WglContext);
  return true;
}

// Generic ImGui renderer
static void Render_ImGui(const HDC hDc) {
  if (!g_initImGui)
    return;

  auto o_WglContext = wglGetCurrentContext();
  if (!wglMakeCurrent(hDc, g_wglContext))
    return;

  ImGui_ImplOpenGL2_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  g_renderMain();

  ImGui::Render();
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

  wglMakeCurrent(hDc, o_WglContext);
}

// Hooked wglSwapBuffers function
static BOOL WINAPI wglSwapBuffers_h(const HDC hDc) {
  if (Init_ImGui(hDc)) {
    Render_ImGui(hDc);
  }
  return g_wglSwapBuffers_o(hDc);
}

bool Load(MewjectorAPI *mj, const std::function<void()> &render,
          const std::function<void()> &init) {
  g_mj = mj;
  g_renderMain = render;
  g_extraInit = init;

  // Initialize kiero just to find the render type and potentially methods
  if (kiero::init(kiero::RenderType::OpenGL) != kiero::Status::Success) {
    g_lastError = "Kiero init failed";
    return false;
  }

  void *target = (void *)GetProcAddress(GetModuleHandleA("opengl32.dll"),
                                        "wglSwapBuffers");
  if (!target) {
    g_lastError = "wglSwapBuffers not found";
    return false;
  }

  if (g_mj)
    Overlay::Log("[ImGuiHook] Hooking wglSwapBuffers via Kiero at %p", target);

  if (kiero::bind(target, (void **)&g_wglSwapBuffers_o,
                  (void *)wglSwapBuffers_h) != kiero::Status::Success) {
    g_lastError = "Kiero bind failed";
    return false;
  }

  return true;
}

void Unload() {
  if (g_hWnd && g_WndProc_o) {
    SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)g_WndProc_o);
  }

  if (g_initImGui) {
    // We need to be in our context to shutdown
    HDC hDc = GetDC(g_hWnd);
    auto o_WglContext = wglGetCurrentContext();
    if (wglMakeCurrent(hDc, g_wglContext)) {
      ImGui_ImplOpenGL2_Shutdown();
      ImGui_ImplWin32_Shutdown();
      if (ImGui::GetCurrentContext()) {
        ImGui::DestroyContext();
      }
      wglMakeCurrent(hDc, o_WglContext);
    }
    ReleaseDC(g_hWnd, hDc);

    if (g_wglContext) {
      wglDeleteContext(g_wglContext);
      g_wglContext = nullptr;
    }
    g_initImGui = false;
  }

  kiero::shutdown();
}

std::string GetLastError() { return g_lastError; }
} // namespace ImGuiHook
