#include "SteamABICompat.h"
#include "GameUtils.h"
#include "ImGuiHook.h"
#include "NetworkManager.h"
#include <chrono>
#include <windows.h>

namespace ViewportHelper {

static HWND g_hMainWnd = nullptr;

static uint8_t g_CurrentCursorType = 0;
static float g_LastBroadcastX = -1.0f;
static float g_LastBroadcastY = -1.0f;
static uint8_t g_LastBroadcastCursor = 255;


static void GetViewportInfo(const HWND hWnd, int &vpX, int &vpY, int &vpW, int &vpH) {
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

static void NormalizeCoordinates(const int x, const int y, const HWND hWnd, float &outX, float &outY) {
  int vX, vY, vW, vH;
  GetViewportInfo(hWnd, vX, vY, vW, vH);
  outX = (float)(x - vX) / (float)vW;
  outY = (float)(y - vY) / (float)vH;
}

static void DenormalizeCoordinates(const float normX, const float normY, const HWND hWnd, int &outX, int &outY) {
  int vX, vY, vW, vH;
  GetViewportInfo(hWnd, vX, vY, vW, vH);
  outX = vX + (int)(normX * vW);
  outY = vY + (int)(normY * vH);
}

static void Update() {
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
        nm.BroadcastPacket(PacketType::MouseMove, &data, sizeof(data), false);
      else
        nm.SendPacket(nm.GetHostID(), PacketType::MouseMove, &data,
                      sizeof(data));
      g_LastBroadcastX = normX;
      g_LastBroadcastY = normY;
      g_LastBroadcastCursor = g_CurrentCursorType;
    }
  }

}
} // namespace ViewportHelper
