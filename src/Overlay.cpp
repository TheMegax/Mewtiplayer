#include "Overlay.h"
#include "GameUtils.h"
#include "NetworkManager.h"
#include "imgui.h"
#include "imgui_hook.h"
#include <deque>
#include <mutex>
#include <stdarg.h>
#include <string>
#include <windows.h>

static std::deque<std::string> g_logLines;
static std::mutex g_logMutex;
static const size_t MAX_LOG_LINES = 512;
static bool g_visible = true;
static char g_lobbyNameBuffer[128] = "Multigenics Match";

static MJ_fn_Log g_origMjLog = nullptr;

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
