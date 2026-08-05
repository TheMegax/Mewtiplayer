#include "ImGuiHook.h"
#include "ChatManager.h"
#include "SteamABICompat.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "external/kiero/kiero.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"
#include <windows.h>
#include <GL/gl.h>

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
static bool g_initImGui = false;
static HWND g_hWnd = nullptr;
static MewjectorAPI *g_mj = nullptr;

// Render function variables
static std::function<void()> g_renderMain = []() {};
static std::function<void()> g_extraInit = []() {};

// Last error status
static std::string g_lastError;

// WndProc callback ImGui handler
static LRESULT CALLBACK ImGui_WndProc(const HWND hWnd, const UINT uMsg, WPARAM wParam,
                                      const LPARAM lParam) {
  if (uMsg == WM_KEYDOWN && wParam == VK_F1) {
    if (!(lParam & (1 << 30))) {
      Overlay::ToggleVisible();
    }
    return 1;
  }

  if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
    return true;

  // Block keyboard input passing through to the game while typing in chat
  const bool isKeyboardMsg = uMsg == WM_KEYDOWN || uMsg == WM_KEYUP ||
                             uMsg == WM_CHAR || uMsg == WM_SYSKEYDOWN ||
                             uMsg == WM_SYSKEYUP || uMsg == WM_UNICHAR;
  if (isKeyboardMsg && ChatManager::Get().IsTyping()) {
    return 1;
  }

  // Mouse event sync
  const bool isSimulated = (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP ||
                      uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP ||
                      uMsg == WM_MOUSEMOVE) &&
                     ((wParam & 0xFF00) == 0x8800);

  if (isSimulated)
    wParam &= ~0xFF00;

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

  if (!wglGetCurrentContext()) {
    g_lastError = "No current OpenGL context";
    return false;
  }

  // Hook WndProc
  g_WndProc_o =
      (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)ImGui_WndProc);
  if (!g_WndProc_o) {
    g_lastError = "Failed to hook WndProc";
    return false;
  }

  IMGUI_CHECKVERSION();
  if (!ImGui::CreateContext()) {
    g_lastError = "Failed to create ImGui context";
    return false;
  }

  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  if (!ImGui_ImplWin32_Init(g_hWnd)) {
    g_lastError = "Failed to init ImGui_ImplWin32";
    return false;
  }

  if (!ImGui_ImplOpenGL3_Init(nullptr)) {
    g_lastError = "Failed to init ImGui_ImplOpenGL3";
    return false;
  }

  g_extraInit();
  g_initImGui = true;

  if (g_mj) {
    GLint fbo = 0;
    glGetIntegerv(0x8CA6 /*GL_FRAMEBUFFER_BINDING*/, &fbo);
    Overlay::Log("[ImGuiHook] Initialized in game context %p on HWND %p FBO=%d",
                 wglGetCurrentContext(), g_hWnd, fbo);
  }

  return true;
}

// Generic ImGui renderer
static void Render_ImGui(const HDC hDc) {
  if (!g_initImGui)
    return;

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  g_renderMain();

  ImGui::Render();
  ImDrawData *drawData = ImGui::GetDrawData();
  ImGui_ImplOpenGL3_RenderDrawData(drawData);
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

  if (kiero::init(kiero::RenderType::OpenGL) != kiero::Status::Success) {
    g_lastError = "Kiero init failed";
    return false;
  }

  const auto target = (void *)GetProcAddress(GetModuleHandleA("opengl32.dll"),
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
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext()) {
      ImGui::DestroyContext();
    }
    g_initImGui = false;
  }

  kiero::shutdown();
}

std::string GetLastError() { return g_lastError; }

HWND GetHWND() { return g_hWnd; }
} // namespace ImGuiHook
