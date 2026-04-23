#include "InputGhost.h"
#include "Overlay.h"
#include "external/kiero/minhook/include/MinHook.h"
#include "imgui.h"
#include "imgui_hook.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <windows.h>

// SDL3 Definitions for Polling Hooks and Direct Calls
typedef uint32_t Uint32;
typedef uint8_t Uint8;
typedef uint64_t Uint64;
typedef uint32_t SDL_WindowID;
typedef uint32_t SDL_MouseID;
typedef uint32_t SDL_EventType;
typedef uint32_t SDL_MouseButtonFlags;

typedef Uint32 (*SDL_GetMouseState_t)(float *x, float *y);
typedef bool (*SDL_PushEvent_t)(void *event);
typedef bool (*SDL_PollEvent_t)(void *event);
typedef void *(*SDL_GetMouseFocus_t)();
typedef SDL_WindowID (*SDL_GetWindowID_t)(void *window);
typedef Uint64 (*SDL_GetTicksNS_t)();

namespace InputGhost {
// Diagnostic Globals
static uint32_t g_ObservedType = 0;
static uint32_t g_ObservedWID = 0;
static uint64_t g_ObservedTS = 0;
static uint8_t g_ObservedBtn = 0;
static bool g_HasObserved = false;

uint32_t g_LastWindowID = 0;
bool g_LastPushResult = true;
static float g_LastSimX = -1.0f;
static float g_LastSimY = -1.0f;
static std::chrono::steady_clock::time_point g_LastSimTime;

typedef Uint32 (*SDL_GetWindowFlags_t)(void *window);
static SDL_GetWindowFlags_t g_Original_SDL_GetWindowFlags = nullptr;

static float g_SimX = 0;
static float g_SimY = 0;
static float g_SimGlobalX = 0;
static float g_SimGlobalY = 0;
static Uint32 g_SimButtons = 0;
static bool g_IsHost = false;
static void *g_MainSDLWindow = nullptr;
static HWND g_hMainWnd = nullptr;

static SDL_GetMouseState_t g_Original_SDL_GetMouseState = nullptr;
static SDL_GetMouseState_t g_Original_SDL_GetGlobalMouseState = nullptr;
static SDL_GetMouseState_t g_Original_SDL_GetRelativeMouseState = nullptr;
static SDL_PollEvent_t g_Original_SDL_PollEvent = nullptr;

typedef void *(*SDL_GetFocus_t)();
static SDL_GetFocus_t g_Original_SDL_GetMouseFocus = nullptr;
static SDL_GetFocus_t g_Original_SDL_GetKeyboardFocus = nullptr;

static std::map<std::string, void **> g_ResolvedEntries;

void **ResolveTableEntry(const char *name) {
  if (g_ResolvedEntries.count(name))
    return g_ResolvedEntries[name];
  HMODULE hExe = GetModuleHandleA(NULL);
  uintptr_t stub = (uintptr_t)GetProcAddress(hExe, name);
  if (!stub)
    return nullptr;
  uint8_t *code = (uint8_t *)stub;
  if (code[0] == 0x48 && code[1] == 0xFF && code[2] == 0x25) {
    int32_t rel = *(int32_t *)(code + 3);
    void **entry = (void **)(stub + 7 + rel);
    g_ResolvedEntries[name] = entry;
    return entry;
  }
  return nullptr;
}

bool IsLazyWrapper(void *ptr) {
  if (!ptr)
    return true;
  uint8_t *code = (uint8_t *)ptr;
  for (int i = 0; i < 24; i++) {
    if (code[i] == 0xE8)
      return true;
  }
  return false;
}

bool IsSimActive() {
  if (g_LastSimX < 0)
    return false;
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<float>(now - g_LastSimTime).count() < 0.15f;
}

bool Hooked_SDL_PollEvent(void *event) {
  if (!g_Original_SDL_PollEvent)
    return false;
  bool res = g_Original_SDL_PollEvent(event);
  if (res && event) {
    uint32_t type = *(uint32_t *)event;

    uint32_t FOCUS_LOST = 0x203;
    uint32_t MOUSE_LEAVE = 0x205;
    if (type == FOCUS_LOST || type == MOUSE_LEAVE) {
      return Hooked_SDL_PollEvent(event); // Skip and get next
    }

    if (!g_HasObserved) {
      if (type == 0x601 || type == 0x602 || type == 0x401 || type == 0x402) {
        g_ObservedType = type;
        g_ObservedWID = *(uint32_t *)((uint8_t *)event + 16);
        g_ObservedTS = *(uint64_t *)((uint8_t *)event + 8);
        g_ObservedBtn = *((uint8_t *)event + 24);
        g_HasObserved = true;
      }
    }
  }
  return res;
}

Uint32 Hooked_SDL_GetWindowFlags(void *window) {
  Uint32 flags =
      g_Original_SDL_GetWindowFlags ? g_Original_SDL_GetWindowFlags(window) : 0;
  // Force focus flags (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)
  // SDL3 flags: 0x01 = FULLSCREEN, 0x02 = OPENGL, 0x04 = HIDDEN, 0x08 =
  // BORDERLESS, 0x10 = RESIZABLE, 0x20 = MINIMIZED, 0x40 = MAXIMIZED, 0x80 =
  // MOUSE_GRABBED, 0x100 = INPUT_FOCUS, 0x200 = MOUSE_FOCUS...
  return flags | 0x100 | 0x200;
}

Uint32 Hooked_SDL_GetMouseState(float *x, float *y) {
  if (IsSimActive()) {
    if (x)
      *x = g_SimX;
    if (y)
      *y = g_SimY;
    return g_SimButtons;
  }
  if (g_Original_SDL_GetMouseState)
    return g_Original_SDL_GetMouseState(x, y);
  return 0;
}

Uint32 Hooked_SDL_GetGlobalMouseState(float *x, float *y) {
  if (IsSimActive()) {
    if (x)
      *x = g_SimGlobalX;
    if (y)
      *y = g_SimGlobalY;
    return g_SimButtons;
  }
  if (g_Original_SDL_GetGlobalMouseState)
    return g_Original_SDL_GetGlobalMouseState(x, y);
  return 0;
}

Uint32 Hooked_SDL_GetRelativeMouseState(float *x, float *y) {
  if (IsSimActive()) {
    if (x)
      *x = 0;
    if (y)
      *y = 0;
    return g_SimButtons;
  }
  if (g_Original_SDL_GetRelativeMouseState)
    return g_Original_SDL_GetRelativeMouseState(x, y);
  return 0;
}

void *Hooked_SDL_GetMouseFocus() {
  if (g_MainSDLWindow)
    return g_MainSDLWindow;
  return g_Original_SDL_GetMouseFocus ? g_Original_SDL_GetMouseFocus()
                                      : nullptr;
}

void *Hooked_SDL_GetKeyboardFocus() {
  if (g_MainSDLWindow)
    return g_MainSDLWindow;
  return g_Original_SDL_GetKeyboardFocus ? g_Original_SDL_GetKeyboardFocus()
                                         : nullptr;
}

void ApplyDynamicHook(const char *name, void *hookFunc, void **originalStore,
                      bool force = false) {
  void **entry = ResolveTableEntry(name);
  if (!entry)
    return;
  if (*entry == hookFunc)
    return;
  if (!force && IsLazyWrapper(*entry))
    return;
  DWORD oldProtect;
  if (VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &oldProtect)) {
    if (originalStore && !*originalStore)
      *originalStore = *entry;
    *entry = hookFunc;
    VirtualProtect(entry, sizeof(void *), oldProtect, &oldProtect);
  }
}

void SetIsHost(bool host) { g_IsHost = host; }

struct alignas(8) Internal_SDL_Event {
  Uint32 type;
  Uint32 reserved;
  Uint64 timestamp;
  Uint32 windowID;
  Uint32 which;
  Uint8 button;
  Uint8 down;
  Uint8 clicks;
  Uint8 padding1;
  float x;
  float y;
  Uint8 padding2[88];
};

void SimulateClick(UINT msg, float normX, float normY, HWND hWnd) {
  int pixelX, pixelY;
  DenormalizeCoordinates(normX, normY, hWnd, pixelX, pixelY);
  g_SimX = (float)pixelX;
  g_SimY = (float)pixelY;
  g_LastSimX = normX;
  g_LastSimY = normY;
  g_LastSimTime = std::chrono::steady_clock::now();
  g_hMainWnd = hWnd;

  POINT pt = {pixelX, pixelY};
  ClientToScreen(hWnd, &pt);
  g_SimGlobalX = (float)pt.x;
  g_SimGlobalY = (float)pt.y;

  ApplyDynamicHook("SDL_GetMouseState", (void *)Hooked_SDL_GetMouseState,
                   (void **)&g_Original_SDL_GetMouseState, true);
  ApplyDynamicHook("SDL_GetGlobalMouseState",
                   (void *)Hooked_SDL_GetGlobalMouseState,
                   (void **)&g_Original_SDL_GetGlobalMouseState, true);
  ApplyDynamicHook("SDL_GetRelativeMouseState",
                   (void *)Hooked_SDL_GetRelativeMouseState,
                   (void **)&g_Original_SDL_GetRelativeMouseState, true);

  // Background focus hooks
  ApplyDynamicHook("SDL_GetMouseFocus", (void *)Hooked_SDL_GetMouseFocus,
                   (void **)&g_Original_SDL_GetMouseFocus, true);
  ApplyDynamicHook("SDL_GetKeyboardFocus", (void *)Hooked_SDL_GetKeyboardFocus,
                   (void **)&g_Original_SDL_GetKeyboardFocus, true);
  ApplyDynamicHook("SDL_GetWindowFlags", (void *)Hooked_SDL_GetWindowFlags,
                   (void **)&g_Original_SDL_GetWindowFlags, true);

  void **pPush = ResolveTableEntry("SDL_PushEvent");
  void **pFocus = ResolveTableEntry("SDL_GetMouseFocus");
  void **pWID = ResolveTableEntry("SDL_GetWindowID");
  void **pTicks = ResolveTableEntry("SDL_GetTicksNS");

  if (pPush && *pPush) {
    SDL_PushEvent_t _SDL_PushEvent = (SDL_PushEvent_t)*pPush;
    SDL_GetMouseFocus_t _SDL_GetMouseFocus =
        pFocus ? (SDL_GetMouseFocus_t)*pFocus : nullptr;
    SDL_GetWindowID_t _SDL_GetWindowID =
        pWID ? (SDL_GetWindowID_t)*pWID : nullptr;
    SDL_GetTicksNS_t _SDL_GetTicksNS =
        pTicks ? (SDL_GetTicksNS_t)*pTicks : nullptr;

    void *focus = _SDL_GetMouseFocus ? _SDL_GetMouseFocus() : nullptr;
    if (focus)
      g_MainSDLWindow = focus;

    SDL_WindowID wid =
        (focus && _SDL_GetWindowID) ? _SDL_GetWindowID(focus) : 0;
    if (g_ObservedWID != 0)
      wid = g_ObservedWID;
    g_LastWindowID = wid;

    Internal_SDL_Event ev = {0};
    uint32_t baseType = g_HasObserved ? (g_ObservedType & ~1) : 0x600;

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
      ev.type = (msg == WM_LBUTTONDOWN) ? baseType + 1 : baseType + 2;
      ev.button = 1;
      ev.down = (msg == WM_LBUTTONDOWN ? 1 : 0);
      g_SimButtons = (msg == WM_LBUTTONDOWN ? 1 : 0);
    } else if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
      ev.type = (msg == WM_RBUTTONDOWN) ? baseType + 1 : baseType + 2;
      ev.button = 3;
      ev.down = (msg == WM_RBUTTONDOWN ? 1 : 0);
      g_SimButtons = (msg == WM_RBUTTONDOWN ? 4 : 0);
    }

    if (ev.type != 0) {
      ev.timestamp = _SDL_GetTicksNS ? _SDL_GetTicksNS() : 0;
      ev.windowID = wid;
      ev.clicks = 1;
      ev.x = g_SimX;
      ev.y = g_SimY;
      _SDL_PushEvent(&ev);
    }
  }
}

void GetViewportInfo(HWND hWnd, int &vpX, int &vpY, int &vpW, int &vpH) {
  RECT rect;
  if (!GetClientRect(hWnd, &rect)) {
    vpX = 0;
    vpY = 0;
    vpW = 1;
    vpH = 1;
    return;
  }
  int w = rect.right - rect.left, h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0) {
    vpX = 0;
    vpY = 0;
    vpW = 1;
    vpH = 1;
    return;
  }
  float targetAspect = 16.0f / 9.0f, windowAspect = (float)w / (float)h;
  if (windowAspect > targetAspect) {
    vpW = (int)(h * targetAspect);
    vpH = h;
    vpX = (w - vpW) / 2;
    vpY = 0;
  } else {
    vpW = w;
    vpH = (int)(w / targetAspect);
    vpX = 0;
    vpY = (h - vpH) / 2;
  }
}

void NormalizeCoordinates(int x, int y, HWND hWnd, float &outX, float &outY) {
  int vX, vY, vW, vH;
  GetViewportInfo(hWnd, vX, vY, vW, vH);
  outX = (float)(x - vX) / (float)vW;
  outY = (float)(y - vY) / (float)vH;
}

void DenormalizeCoordinates(float normX, float normY, HWND hWnd, int &outX,
                            int &outY) {
  int vX, vY, vW, vH;
  GetViewportInfo(hWnd, vX, vY, vW, vH);
  outX = vX + (int)(normX * vW);
  outY = vY + (int)(normY * vH);
}

bool IsSimulated() { return false; }
void RenderDebug() {
  static bool forceHook = false;
  ApplyDynamicHook("SDL_PollEvent", (void *)Hooked_SDL_PollEvent,
                   (void **)&g_Original_SDL_PollEvent, forceHook);

  if (g_LastSimX >= 0) {
    HWND hWnd = ImGuiHook::GetHWND();
    if (!hWnd)
      return;
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - g_LastSimTime).count();
    int x, y;
    DenormalizeCoordinates(g_LastSimX, g_LastSimY, hWnd, x, y);
    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImVec2 pos = {(float)x, (float)y};
    float alpha = 1.0f - (elapsed / 2.0f);
    if (alpha > 0) {
      ImU32 color = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 1.0f, alpha));
      drawList->AddCircleFilled(pos, 6.0f, color);
      drawList->AddCircle(pos, 8.0f, color, 0, 2.0f);
    }
  }
}

bool IsCalibrated() { return g_HasObserved; }
void ResetCalibration() { g_HasObserved = false; }
} // namespace InputGhost
