#include "InputGhost.h"
#include "NetworkManager.h"
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
typedef uint16_t Uint16;
typedef uint64_t Uint64;
typedef int16_t Sint16;
typedef uint32_t SDL_WindowID;
typedef uint32_t SDL_MouseID;
typedef uint32_t SDL_EventType;
typedef uint32_t SDL_MouseButtonFlags;
typedef uint32_t SDL_JoystickID;

typedef Uint32 (*SDL_GetMouseState_t)(float *x, float *y);
typedef bool (*SDL_PushEvent_t)(void *event);
typedef bool (*SDL_PollEvent_t)(void *event);
typedef void *(*SDL_GetFocus_t)();
typedef SDL_WindowID (*SDL_GetWindowID_t)(void *window);
typedef Uint64 (*SDL_GetTicksNS_t)();
typedef Uint32 (*SDL_GetWindowFlags_t)(void *window);

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
static SDL_GetWindowFlags_t g_Original_SDL_GetWindowFlags = nullptr;

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

bool IsSimActive() {
  if (g_LastSimX < 0)
    return false;
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration<float>(now - g_LastSimTime).count() < 0.15f;
}

struct alignas(8) Internal_SDL_Event {
  Uint32 type;
  Uint32 reserved;
  Uint64 timestamp;
  Uint8 data[112];
};

bool IsSimulatedEvent(void *event) {
  if (!event)
    return false;
  return *(uint32_t *)((uint8_t *)event + 4) == 0x88008800;
}

bool Hooked_SDL_PollEvent(void *event) {
  if (!g_Original_SDL_PollEvent)
    return false;
  bool res = g_Original_SDL_PollEvent(event);
  if (res && event) {
    uint32_t type = *(uint32_t *)event;

    // SDL3 Focus/Window events range
    if (type == 0x203 || type == 0x205) {
      return Hooked_SDL_PollEvent(event);
    }

    // Handle generic input sync in PollEvent (Keyboard/Gamepad)
    if (!IsSimulatedEvent(event)) {
      auto &nm = NetworkManager::Get();
      bool imguiKeys = false;
      if (ImGui::GetCurrentContext())
        imguiKeys = ImGui::GetIO().WantCaptureKeyboard;

      // Keyboard: 0x300 (Down), 0x301 (Up)
      if ((type == 0x300 || type == 0x301) && !imguiKeys) {
        KeyEventData data;
        data.type = type;
        // SDL3 KeyboardEvent Offsets: windowID=16, which=20, scancode=24,
        // keycode=28, mod=32, raw=34, down=36, repeat=37
        data.scancode = *(uint32_t *)((uint8_t *)event + 24);
        data.keycode = *(uint32_t *)((uint8_t *)event + 28);
        data.mod = *(uint16_t *)((uint8_t *)event + 32);
        data.down = *((uint8_t *)event + 36);
        data.repeat = *((uint8_t *)event + 37);

        if (nm.IsHost()) {
          nm.BroadcastPacket(PacketType::KeyEvent, &data, sizeof(data), true);
        } else if (nm.GetCurrentLobby().IsValid()) {
          nm.SendPacket(nm.GetHostID(), PacketType::KeyEvent, &data,
                        sizeof(data));
          return Hooked_SDL_PollEvent(event);
        }
      }
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
  return flags | 0x100 | 0x200; // Force focus
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
  DWORD oldProtect;
  if (VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &oldProtect)) {
    if (originalStore && !*originalStore)
      *originalStore = *entry;
    *entry = hookFunc;
    VirtualProtect(entry, sizeof(void *), oldProtect, &oldProtect);
  }
}

void SetIsHost(bool host) { g_IsHost = host; }

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
  ApplyDynamicHook("SDL_GetMouseFocus", (void *)Hooked_SDL_GetMouseFocus,
                   (void **)&g_Original_SDL_GetMouseFocus, true);
  ApplyDynamicHook("SDL_GetKeyboardFocus", (void *)Hooked_SDL_GetKeyboardFocus,
                   (void **)&g_Original_SDL_GetKeyboardFocus, true);
  ApplyDynamicHook("SDL_GetWindowFlags", (void *)Hooked_SDL_GetWindowFlags,
                   (void **)&g_Original_SDL_GetWindowFlags, true);
  ApplyDynamicHook("SDL_PollEvent", (void *)Hooked_SDL_PollEvent,
                   (void **)&g_Original_SDL_PollEvent, true);

  void **pPush = ResolveTableEntry("SDL_PushEvent");
  if (pPush && *pPush) {
    SDL_PushEvent_t _SDL_PushEvent = (SDL_PushEvent_t)*pPush;
    Internal_SDL_Event ev = {0};
    ev.reserved = 0x88008800; // Mark as simulated
    uint32_t baseType = g_HasObserved ? (g_ObservedType & ~1) : 0x600;

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
      ev.type = (msg == WM_LBUTTONDOWN) ? baseType + 1 : baseType + 2;
      ev.data[8] = 1;                               // button at offset 16+8=24
      ev.data[9] = (msg == WM_LBUTTONDOWN ? 1 : 0); // down at offset 16+9=25
      g_SimButtons = (msg == WM_LBUTTONDOWN ? 1 : 0);
    } else if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
      ev.type = (msg == WM_RBUTTONDOWN) ? baseType + 1 : baseType + 2;
      ev.data[8] = 3;                               // button at offset 24
      ev.data[9] = (msg == WM_RBUTTONDOWN ? 1 : 0); // down at offset 25
      g_SimButtons = (msg == WM_RBUTTONDOWN ? 4 : 0);
    }

    if (ev.type != 0) {
      *(uint32_t *)(ev.data + 0) = g_ObservedWID; // windowID at offset 16
      *(float *)(ev.data + 12) = g_SimX;          // x at offset 28
      *(float *)(ev.data + 16) = g_SimY;          // y at offset 32
      _SDL_PushEvent(&ev);
    }
  }
}

void SimulateKeyEvent(uint32_t type, uint32_t keycode, uint32_t scancode,
                      uint16_t mod, uint8_t down, uint8_t repeat) {
  void **pPush = ResolveTableEntry("SDL_PushEvent");
  if (pPush && *pPush) {
    SDL_PushEvent_t _SDL_PushEvent = (SDL_PushEvent_t)*pPush;
    Internal_SDL_Event ev = {0};
    ev.type = type;
    ev.reserved = 0x88008800;
    // SDL3 KeyboardEvent Offsets relative to data (offset 16):
    // windowID=0, which=4, scancode=8, keycode=12, mod=16, raw=18, down=20,
    // repeat=21
    *(uint32_t *)(ev.data + 0) = g_ObservedWID;
    *(uint32_t *)(ev.data + 8) = scancode;
    *(uint32_t *)(ev.data + 12) = keycode;
    *(uint16_t *)(ev.data + 16) = mod;
    *(uint8_t *)(ev.data + 20) = down;
    *(uint8_t *)(ev.data + 21) = repeat;
    _SDL_PushEvent(&ev);
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

void RenderDebug() {
  static bool forceHook = true;
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
