#include "InputGhost.h"
#include "SteamABICompat.h"
#include "GameUtils.h"
#include "ImGuiHook.h"
#include "NetworkManager.h"
#include "imgui.h"
#include <chrono>
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

struct SDL_Surface {
  Uint32 flags;
  Uint32 format;
  int w;
  int h;
  int pitch;
  void *pixels;
  void *reserved;
};

typedef void *SDL_Cursor;
typedef SDL_Cursor *(*SDL_CreateColorCursor_t)(SDL_Surface *surface, int hot_x,
                                               int hot_y);
typedef bool (*SDL_SetCursor_t)(SDL_Cursor cursor);

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

static int g_BreakMouseX = 0;
static int g_BreakMouseY = 0;

static SDL_GetMouseState_t g_Original_SDL_GetMouseState = nullptr;
static SDL_GetMouseState_t g_Original_SDL_GetGlobalMouseState = nullptr;
static SDL_GetMouseState_t g_Original_SDL_GetRelativeMouseState = nullptr;
static SDL_PollEvent_t g_Original_SDL_PollEvent = nullptr;
static SDL_GetWindowFlags_t g_Original_SDL_GetWindowFlags = nullptr;
static SDL_CreateColorCursor_t g_Original_SDL_CreateColorCursor = nullptr;
static SDL_SetCursor_t g_Original_SDL_SetCursor = nullptr;

typedef void *(*SDL_GetFocus_t)();
static SDL_GetFocus_t g_Original_SDL_GetMouseFocus = nullptr;
static SDL_GetFocus_t g_Original_SDL_GetKeyboardFocus = nullptr;

static std::map<std::string, void **> g_ResolvedEntries;
static std::map<void *, uint8_t> g_CursorToType;
static std::map<uint32_t, uint8_t> g_SignatureToType;
static uint8_t g_CurrentCursorType = 0;
static float g_LastBroadcastX = -1.0f;
static float g_LastBroadcastY = -1.0f;
static uint8_t g_LastBroadcastCursor = 255;

void RegisterCursorSignature(const uint32_t crc, const uint8_t typeIndex) {
  g_SignatureToType[crc] = typeIndex;
}

void **ResolveTableEntry(const char *name) {
  if (g_ResolvedEntries.count(name))
    return g_ResolvedEntries[name];
  HMODULE hExe = GetModuleHandleA(nullptr);
  const auto stub = (uintptr_t)GetProcAddress(hExe, name);
  if (!stub)
    return nullptr;
  const auto code = (uint8_t *)stub;
  if (code[0] == 0x48 && code[1] == 0xFF && code[2] == 0x25) {
    const int32_t rel = *(int32_t *)(code + 3);
    const auto entry = (void **)(stub + 7 + rel);
    g_ResolvedEntries[name] = entry;
    return entry;
  }
  return nullptr;
}

bool IsSimActive() {
  if (g_LastSimX < 0)
    return false;

  // Check if hardware mouse broke the simulation
  // Only break if we are the foreground window (actively being played)
  // TODO: Need to remove this later, as the active ghost input is then suppose to take over
  if (GetForegroundWindow() == g_hMainWnd) {
    POINT pt;
    if (GetCursorPos(&pt)) {
      const int dx = pt.x - g_BreakMouseX;
      const int dy = pt.y - g_BreakMouseY;
      if ((dx * dx + dy * dy) > 100) { // > 10 pixels movement
        g_LastSimX = -1.0f;
        return false;
      }
    }
  }

  const auto now = std::chrono::steady_clock::now();
  // Persistent simulation as long as hardware mouse is still
  return std::chrono::duration<float>(now - g_LastSimTime).count() < 3600.0f;
}

struct alignas(8) Internal_SDL_Event {
  Uint32 type;
  Uint32 reserved;
  Uint64 timestamp;
  Uint8 data[112];
};

bool Hooked_SDL_SetCursor(const SDL_Cursor cursor) {
  return g_Original_SDL_SetCursor ? g_Original_SDL_SetCursor(cursor) : false;
}

bool IsSimulatedEvent(void *event) {
  if (!event)
    return false;
  return *(uint32_t *)((uint8_t *)event + 4) == 0x88008800;
}

SDL_Cursor *Hooked_SDL_CreateColorCursor(SDL_Surface *surface, const int hot_x,
                                         const int hot_y) {
  SDL_Cursor *res =
      g_Original_SDL_CreateColorCursor
          ? g_Original_SDL_CreateColorCursor(surface, hot_x, hot_y)
          : nullptr;
  if (res && surface && surface->pixels) {
    size_t bytes = (size_t)surface->w * surface->h * 4;
    if (bytes > 4096)
      bytes = 4096;
    const uint32_t crc = GameUtils::CalculateCRC32(surface->pixels, bytes);
    if (g_SignatureToType.count(crc)) {
      g_CursorToType[res] = g_SignatureToType[crc];
    } else {
      if (hot_x == 34 && hot_y == 7)
        g_CursorToType[res] = 0;
      else if (hot_x == 17 && hot_y == 58)
        g_CursorToType[res] = 1;
      else if (hot_x == 110 && hot_y == 58)
        g_CursorToType[res] = 2;
    }
  }
  return res;
}

bool Hooked_SDL_SetCursor(SDL_Cursor *cursor) {
  if (g_CursorToType.count(cursor)) {
    g_CurrentCursorType = g_CursorToType[cursor];
  }
  return g_Original_SDL_SetCursor ? g_Original_SDL_SetCursor(cursor) : false;
}

bool Hooked_SDL_PollEvent(void *event) {
  if (!g_Original_SDL_PollEvent)
    return false;
  const bool res = g_Original_SDL_PollEvent(event);
  if (res && event) {
    const uint32_t type = *(uint32_t *)event;

    if (!IsSimulatedEvent(event)) {
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
  const Uint32 flags =
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

void SetIsHost(const bool host) { g_IsHost = host; }

void SimulateMouseMove(const float normX, const float normY, const HWND hWnd) {
  int pixelX, pixelY;
  DenormalizeCoordinates(normX, normY, hWnd, pixelX, pixelY);
  const float prevX = g_SimX;
  const float prevY = g_SimY;
  g_SimX = (float)pixelX;
  g_SimY = (float)pixelY;
  g_LastSimX = normX;
  g_LastSimY = normY;
  g_LastSimTime = std::chrono::steady_clock::now();
  g_hMainWnd = hWnd;

  // Store current hardware position as the 'break' point
  POINT pt;
  if (GetCursorPos(&pt)) {
    g_BreakMouseX = pt.x;
    g_BreakMouseY = pt.y;
  }

  POINT spt = {pixelX, pixelY};
  ClientToScreen(hWnd, &spt);
  g_SimGlobalX = (float)spt.x;
  g_SimGlobalY = (float)spt.y;

  ApplyDynamicHook("SDL_GetMouseState",
    (void *)Hooked_SDL_GetMouseState,
    (void **)&g_Original_SDL_GetMouseState, true);
  ApplyDynamicHook("SDL_GetGlobalMouseState",
    (void *)Hooked_SDL_GetGlobalMouseState,
    (void **)&g_Original_SDL_GetGlobalMouseState, true);
  ApplyDynamicHook("SDL_GetRelativeMouseState",
    (void *)Hooked_SDL_GetRelativeMouseState,
    (void **)&g_Original_SDL_GetRelativeMouseState, true);
  ApplyDynamicHook("SDL_GetMouseFocus",
    (void *)Hooked_SDL_GetMouseFocus,
    (void **)&g_Original_SDL_GetMouseFocus, true);
  ApplyDynamicHook("SDL_GetKeyboardFocus",
    (void *)Hooked_SDL_GetKeyboardFocus,
    (void **)&g_Original_SDL_GetKeyboardFocus, true);
  ApplyDynamicHook("SDL_GetWindowFlags",
    (void *)Hooked_SDL_GetWindowFlags,
    (void **)&g_Original_SDL_GetWindowFlags, true);
  ApplyDynamicHook("SDL_PollEvent",
    (void *)Hooked_SDL_PollEvent,
    (void **)&g_Original_SDL_PollEvent, true);
  ApplyDynamicHook("SDL_CreateColorCursor",
    (void *)Hooked_SDL_CreateColorCursor,
    (void **)&g_Original_SDL_CreateColorCursor, true);
  ApplyDynamicHook("SDL_SetCursor",
    (void *)(SDL_SetCursor_t)Hooked_SDL_SetCursor,
    (void **)&g_Original_SDL_SetCursor, true);

  void **pPush = ResolveTableEntry("SDL_PushEvent");
  if (pPush && *pPush) {
    const auto SDL_PushEvent = (SDL_PushEvent_t)*pPush;
    Internal_SDL_Event ev = {};
    ev.type = 0x400; // SDL_EVENT_MOUSE_MOTION
    ev.reserved = 0x88008800;
    *(uint32_t *)(ev.data + 0) = g_ObservedWID; // windowID
    *(uint32_t *)(ev.data + 4) = 0;             // which
    *(uint32_t *)(ev.data + 8) = g_SimButtons;  // state
    *(float *)(ev.data + 12) = g_SimX;          // x
    *(float *)(ev.data + 16) = g_SimY;          // y
    *(float *)(ev.data + 20) = g_SimX - prevX;  // xrel
    *(float *)(ev.data + 24) = g_SimY - prevY;  // yrel
    SDL_PushEvent(&ev);
  }
}

void GetViewportInfo(const HWND hWnd, int &vpX, int &vpY, int &vpW, int &vpH) {
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
  constexpr float targetAspect = 16.0f / 9.0f;
  const float windowAspect = (float)w / (float)h;

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

void NormalizeCoordinates(const int x, const int y, const HWND hWnd, float &outX, float &outY) {
  int vX, vY, vW, vH;
  GetViewportInfo(hWnd, vX, vY, vW, vH);
  outX = (float)(x - vX) / (float)vW;
  outY = (float)(y - vY) / (float)vH;
}

void DenormalizeCoordinates(const float normX, const float normY, const HWND hWnd, int &outX,
                            int &outY) {
  int vX, vY, vW, vH;
  GetViewportInfo(hWnd, vX, vY, vW, vH);
  outX = vX + (int)(normX * vW);
  outY = vY + (int)(normY * vH);
}

void Update() {
  auto &nm = NetworkManager::Get();
  if (!nm.GetCurrentLobby().IsValid())
    return;
  const HWND hWnd = ImGuiHook::GetHWND();
  if (!hWnd)
    return;
  g_hMainWnd = hWnd;
  POINT pt;
  if (GetCursorPos(&pt) && ScreenToClient(hWnd, &pt)) {
    float normX, normY;
    NormalizeCoordinates(pt.x, pt.y, hWnd, normX, normY);
    if (abs(normX - g_LastBroadcastX) > 0.001f ||
        abs(normY - g_LastBroadcastY) > 0.001f ||
        g_CurrentCursorType != g_LastBroadcastCursor) {
      MouseMoveData data{};
      data.steamID = SteamUser()->GetSteamID().ConvertToUint64();
      data.x = normX;
      data.y = normY;
      data.cursorType = g_CurrentCursorType;
      if (nm.IsHost())
        nm.BroadcastPacket(PacketType::MouseMove, &data, sizeof(data), true);
      else
        nm.SendPacket(nm.GetHostID(), PacketType::MouseMove, &data,
                      sizeof(data));
      g_LastBroadcastX = normX;
      g_LastBroadcastY = normY;
      g_LastBroadcastCursor = g_CurrentCursorType;
    }
  }

}

bool IsCalibrated() { return g_HasObserved; }
void ResetCalibration() { g_HasObserved = false; }
} // namespace InputGhost
