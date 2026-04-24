#include "Overlay.h"
#include "GameUtils.h"
#include "ImGuiHook.h"
#include "InputGhost.h"
#include "NetworkManager.h"
#include "imgui.h"
#include <GL/gl.h>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdarg.h>
#include <string>
#include <vector>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

struct RemoteCursor {
  float x, y;
  uint8_t type;
  std::chrono::steady_clock::time_point lastUpdate;
};

static std::deque<std::string> g_logLines;
static std::mutex g_logMutex;
static const size_t MAX_LOG_LINES = 512;
static bool g_visible = true;
static char g_lobbyNameBuffer[128] = "Multigenics Match";
static MJ_fn_Log g_origMjLog = nullptr;

static std::map<uint64_t, RemoteCursor> g_RemoteCursors;
static std::mutex g_cursorMutex;

static std::map<uint8_t, GLuint> g_CursorTextures;
static std::map<uint8_t, ImVec2> g_CursorHotspots;
static std::map<uint8_t, ImVec2> g_CursorSizes;
static bool g_TexturesLoaded = false;

// TODO: Currently useless! Only the default cursor is used.
const char *g_CursorNames[] = {
    "default",         "grab",       "grabr",
    "pet_frame1",      "pet_frame2", "pet_frame3",
    "pet_frame4",      "attack",     "attack_hastargets",
    "btn_over",        "examine",    "heal",
    "heal_hastargets", "invalid",    "move",
    "move_hastargets", "question",   "spell",
    "spell_hastargets"};

// Simple CRC32 to match InputGhost
uint32_t Overlay_CRC32(const void *data, size_t n_bytes) {
  uint32_t crc = 0xFFFFFFFF;
  const uint8_t *p = (const uint8_t *)data;
  while (n_bytes--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++)
      crc = (crc >> 1) ^ (-(int32_t)(crc & 1) & 0xEDB88320);
  }
  return ~crc;
}

void LoadCursorTextures() {
  if (g_TexturesLoaded)
    return;

  HMODULE hModule = GetModuleHandleA("Multigenics.dll");
  if (!hModule) {
    // Fallback for internal testing if DLL name changes or injected differently
    hModule = GetModuleHandleA(NULL);
  }

  // Load hotspots from resource
  std::map<std::string, ImVec2> hotspots;
  HRSRC hResHot = FindResourceA(hModule, "hotspots", RT_RCDATA);
  if (hResHot) {
    DWORD size = SizeofResource(hModule, hResHot);
    HGLOBAL hGlobal = LoadResource(hModule, hResHot);
    char *pData = (char *)LockResource(hGlobal);
    if (pData) {
      std::string content(pData, size);
      std::stringstream ss(content);
      std::string line;
      while (std::getline(ss, line)) {
        std::stringstream lss(line);
        std::string name, sx, sy;
        if (std::getline(lss, name, ',') && std::getline(lss, sx, ',') &&
            std::getline(lss, sy, ',')) {
          hotspots[name] =
              ImVec2((float)atof(sx.c_str()), (float)atof(sy.c_str()));
        }
      }
    }
  }

  for (uint8_t i = 0; i < 19; ++i) {
    HRSRC hRes = FindResourceA(hModule, g_CursorNames[i], RT_RCDATA);
    if (hRes) {
      DWORD size = SizeofResource(hModule, hRes);
      HGLOBAL hGlobal = LoadResource(hModule, hRes);
      void *pData = LockResource(hGlobal);
      if (pData) {
        int w, h, channels;
        unsigned char *data = stbi_load_from_memory(
            (const unsigned char *)pData, size, &w, &h, &channels, 4);
        if (data) {
          GLuint tex;
          glGenTextures(1, &tex);
          glBindTexture(GL_TEXTURE_2D, tex);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, data);

          g_CursorTextures[i] = tex;
          g_CursorSizes[i] = ImVec2((float)w, (float)h);
          if (hotspots.count(g_CursorNames[i])) {
            g_CursorHotspots[i] = hotspots[g_CursorNames[i]];
          } else {
            g_CursorHotspots[i] = ImVec2(0, 0);
          }

          // Register signature (CRC32 of first 4096 bytes)
          size_t bytes = (size_t)w * h * 4;
          if (bytes > 4096)
            bytes = 4096;
          uint32_t crc = Overlay_CRC32(data, bytes);
          InputGhost::RegisterCursorSignature(crc, i);

          stbi_image_free(data);
        }
      }
    } else {
      Overlay::Log("[ERR] Failed to find resource: %s", g_CursorNames[i]);
    }
  }
  g_TexturesLoaded = true;
}

void Overlay::UpdateRemoteCursor(uint64_t steamID, float x, float y,
                                 uint8_t type) {
  std::lock_guard<std::mutex> lock(g_cursorMutex);
  g_RemoteCursors[steamID] = {x, y, type, std::chrono::steady_clock::now()};
}

void Overlay::Log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogV(fmt, args);
  va_end(args);
}

void Overlay::LogV(const char *fmt, va_list args) {
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, args);

  std::lock_guard<std::mutex> lock(g_logMutex);
  g_logLines.push_back(std::string(buf));
  while (g_logLines.size() > MAX_LOG_LINES)
    g_logLines.pop_front();
}

static void __cdecl WrappedMjLog(const char *owner, const char *fmt, ...) {
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (owner && strcmp(owner, "Multigenics") != 0) {
    Overlay::Log("[%s] %s", owner, buf);
  } else {
    Overlay::Log("%s", buf);
  }
}

void Overlay::ToggleVisible() { g_visible = !g_visible; }

static void InternalRender() {
  LoadCursorTextures();

  InputGhost::RenderDebug();

  // Draw remote cursors
  {
    std::lock_guard<std::mutex> lock(g_cursorMutex);
    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    HWND hWnd = ImGuiHook::GetHWND();
    auto now = std::chrono::steady_clock::now();

    for (auto it = g_RemoteCursors.begin(); it != g_RemoteCursors.end();) {
      float elapsed =
          std::chrono::duration<float>(now - it->second.lastUpdate).count();
      if (elapsed > 3600.0f) { // 1 hour timeout
        it = g_RemoteCursors.erase(it);
        continue;
      }

      if (g_CursorTextures.count(it->second.type)) {
        int px, py;
        int vx, vy, vw, vh;
        InputGhost::GetViewportInfo(hWnd, vx, vy, vw, vh);
        InputGhost::DenormalizeCoordinates(it->second.x, it->second.y, hWnd, px,
                                           py);

        float scale = ((float)vw / 1920.0f) * 0.5f;
        ImVec2 size = g_CursorSizes[it->second.type];
        ImVec2 hotspot = g_CursorHotspots[it->second.type];

        size.x *= scale;
        size.y *= scale;
        hotspot.x *= scale;
        hotspot.y *= scale;

        ImVec2 pos = ImVec2((float)px - hotspot.x, (float)py - hotspot.y);

        drawList->AddImage(
            (ImTextureID)(uintptr_t)g_CursorTextures[it->second.type], pos,
            ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, 128));

        // Draw player name above cursor
        const char *steamName = SteamFriends()->GetFriendPersonaName(it->first);
        char name[64];
        if (steamName && steamName[0]) {
          snprintf(name, sizeof(name), "%s", steamName);
        } else {
          snprintf(name, sizeof(name), "Player %llu", it->first % 1000);
        }

        ImVec2 textSize = ImGui::CalcTextSize(name);
        ImVec2 textPos =
            ImVec2(pos.x + (size.x * 0.5f) - (textSize.x * 0.5f), pos.y - 15);

        // Draw Outline
        drawList->AddText(ImVec2(textPos.x - 1, textPos.y),
                          IM_COL32(0, 0, 0, 255), name);
        drawList->AddText(ImVec2(textPos.x + 1, textPos.y),
                          IM_COL32(0, 0, 0, 255), name);
        drawList->AddText(ImVec2(textPos.x, textPos.y - 1),
                          IM_COL32(0, 0, 0, 255), name);
        drawList->AddText(ImVec2(textPos.x, textPos.y + 1),
                          IM_COL32(0, 0, 0, 255), name);

        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), name);
      }
      ++it;
    }
  }

  if (!g_visible)
    return;

  ImGui::SetNextWindowSize(ImVec2(680, 320), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::Begin("Multigenics  |  F1 to hide", &g_visible,
               ImGuiWindowFlags_NoNav);

  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Logs")) {
      ImGui::BeginChild("##log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                        false, ImGuiWindowFlags_HorizontalScrollbar);
      {
        std::lock_guard<std::mutex> lock(g_logMutex);
        for (const auto &line : g_logLines) {
          ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
          if (line.find("[WARN]") != std::string::npos)
            col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
          else if (line.find("[ERR]") != std::string::npos)
            col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
          else if (line.find("[OK]") != std::string::npos)
            col = ImVec4(0.4f, 1.0f, 0.5f, 1.0f);
          ImGui::TextColored(col, "%s", line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
          ImGui::SetScrollHereY(1.0f);
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Network")) {
      CSteamID current = NetworkManager::Get().GetCurrentLobby();

      if (current.IsValid()) {
        if (ImGui::Button("Leave Lobby")) {
          NetworkManager::Get().LeaveLobby();
        }
      } else {
        if (ImGui::Button("Host Lobby")) {
          ImGui::OpenPopup("Host Lobby Modal");
        }
      }

      ImGui::SameLine();
      if (ImGui::Button("Refresh List")) {
        NetworkManager::Get().RefreshLobbyList();
      }

      if (current.IsValid()) {
        ImGui::Text("Current Lobby: %llu", current.ConvertToUint64());
      } else {
        ImGui::Text("Status: Not in a lobby.");
      }

      // Modal for Lobby Creation
      if (ImGui::BeginPopupModal("Host Lobby Modal", NULL,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter a name for your lobby:");
        ImGui::InputText("##name", g_lobbyNameBuffer,
                         sizeof(g_lobbyNameBuffer));
        ImGui::Separator();

        if (ImGui::Button("Create", ImVec2(120, 0))) {
          NetworkManager::Get().HostLobby(g_lobbyNameBuffer);
          ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      ImGui::Separator();
      ImGui::Text("Available Lobbies:");

      if (ImGui::BeginTable("##lobbies", 3,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Players");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();

        const auto &lobbies = NetworkManager::Get().GetLobbyList();
        for (const auto &lobby : lobbies) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%s", lobby.name.c_str());

          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%d/%d", lobby.memberCount, lobby.maxMembers);

          ImGui::TableSetColumnIndex(2);
          if (lobby.id == current) {
            ImGui::TextDisabled("Joined");
          } else {
            std::string label =
                "Join##" + std::to_string(lobby.id.ConvertToUint64());
            if (ImGui::Button(label.c_str())) {
              NetworkManager::Get().JoinLobby(lobby.id);
            }
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("RNG")) {
      uint32_t state[8] = {0};
      GameUtils::GetRNGState(state);

      ImGui::Text("Xoshiro256 State (TLS +0x178):");
      ImGui::Separator();

      // Display the 32-byte state
      ImGui::Columns(2, "##rng_cols", false);
      ImGui::Text("s[0]: 0x%08X", state[0]);
      ImGui::Text("s[1]: 0x%08X", state[1]);
      ImGui::Text("s[2]: 0x%08X", state[2]);
      ImGui::Text("s[3]: 0x%08X", state[3]);
      ImGui::NextColumn();
      uint64_t *s64 = (uint64_t *)&state[4];
      ImGui::Text("s[4-5]: 0x%016llX", s64[0]);
      ImGui::Text("s[6-7]: 0x%016llX", s64[1]);
      ImGui::Columns(1);

      ImGui::Text("Current RNG: %u", state[0]);

      ImGui::Separator();
      ImGui::TextWrapped("This displays the current 32-byte RNG state.");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Input")) {
      ImGui::Text("Calibration Status:");
      if (InputGhost::IsCalibrated()) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "READY: Calibrated and active.");
        if (ImGui::Button("Reset Calibration")) {
          InputGhost::ResetCalibration();
        }
      } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1),
                           "WAITING: Click in the game window to calibrate.");
      }

      ImGui::Separator();
      ImGui::Text("Test Injection:");
      if (ImGui::Button("Trigger Test Click (Center)")) {
        HWND hWnd = ImGuiHook::GetHWND();
        if (hWnd) {
          InputGhost::SimulateClick(WM_LBUTTONDOWN, 0.5f, 0.5f, hWnd);
          InputGhost::SimulateClick(WM_LBUTTONUP, 0.5f, 0.5f, hWnd);
        }
      }
      ImGui::TextWrapped("This will inject a click at the center of the screen "
                         "to verify your current calibration.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

void Overlay::Setup(MewjectorAPI *mj) {
  if (mj && mj->Log && mj->Log != WrappedMjLog) {
    g_origMjLog = mj->Log;
    mj->Log = WrappedMjLog;
  }

  if (!ImGuiHook::Load(mj, InternalRender, []() {
        Overlay::Log("[OK] Overlay active. F1=Menu");
      })) {
    mj->Log("Overlay", "[ERR] Failed to load ImGuiHook: %s",
            ImGuiHook::GetLastError().c_str());
  } else {
    mj->Log("Overlay", "[OK] Overlay initialized using Kiero.");
  }
}
