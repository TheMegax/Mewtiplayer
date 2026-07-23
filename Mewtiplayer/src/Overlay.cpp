#include "Overlay.h"
#include "SteamABICompat.h"
#include "GameUtils.h"
#include "ImGuiHook.h"
#include "MewSQL.h"
#include "InputGhost.h"
#include "NetworkManager.h"
#include "hooks/AdventureBoxHooks.h"
#include "imgui.h"
#include "mew_ui_api.h"
#include <GL/gl.h>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <cstdarg>
#include <string>
#include <vector>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

struct RemoteCursor { // NOLINT(*-pro-type-member-init)
  float x, y;
  uint8_t type;
  std::chrono::steady_clock::time_point lastUpdate;
};

static std::deque<std::string> g_logLines;
static std::mutex g_logMutex;
static constexpr size_t MAX_LOG_LINES = 512;
static bool g_visible = false;
static char g_lobbyNameBuffer[128] = "Mewtiplayer Match";
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

void LoadCursorTextures() {
  if (g_TexturesLoaded)
    return;

  HMODULE hModule = GetModuleHandleA("Mewtiplayer.dll");
  if (!hModule) {
    // Fallback for internal testing if DLL name changes or injected differently
    hModule = GetModuleHandleA(nullptr);
  }

  // Load hotspots from resource
  std::map<std::string, ImVec2> hotspots;
  if (HRSRC hResHot = FindResourceA(hModule, "hotspots", RT_RCDATA)) {
    DWORD size = SizeofResource(hModule, hResHot);
    HGLOBAL hGlobal = LoadResource(hModule, hResHot);
    if (auto pData = (char *)LockResource(hGlobal)) {
      std::string content(pData, size);
      std::stringstream ss(content);
      std::string line;
      while (std::getline(ss, line)) {
        std::stringstream lss(line);
        std::string name, sx, sy;
        if (std::getline(lss, name, ',') && std::getline(lss, sx, ',') &&
            std::getline(lss, sy, ',')) {
          hotspots[name] = ImVec2(std::strtof(sx.c_str(), nullptr), std::strtof(sy.c_str(), nullptr));        }
      }
    }
  }

  for (uint8_t i = 0; i < 19; ++i) {
    if (HRSRC hRes = FindResourceA(hModule, g_CursorNames[i], RT_RCDATA)) {
      DWORD size = SizeofResource(hModule, hRes);
      HGLOBAL hGlobal = LoadResource(hModule, hRes);
      if (void *pData = LockResource(hGlobal)) {
        int w, h, channels;
        unsigned char *data = stbi_load_from_memory(
            static_cast<const unsigned char *>(pData), (int)size, &w, &h, &channels, 4);
        if (data) {
          GLuint tex;
          glGenTextures(1, &tex);
          glBindTexture(GL_TEXTURE_2D, tex);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, data);

          g_CursorTextures[i] = tex;
          g_CursorSizes[i] = ImVec2(static_cast<float>(w), static_cast<float>(h));
          if (hotspots.count(g_CursorNames[i])) {
            g_CursorHotspots[i] = hotspots[g_CursorNames[i]];
          } else {
            g_CursorHotspots[i] = ImVec2(0, 0);
          }

          // Register signature (CRC32 of first 4096 bytes)
          size_t bytes = static_cast<size_t>(w) * h * 4;
          if (bytes > 4096)
            bytes = 4096;
          uint32_t crc = GameUtils::CalculateCRC32(data, bytes);
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

void Overlay::UpdateRemoteCursor(const uint64_t steamID, const float x, const float y,
                                 const uint8_t type) {
  std::lock_guard<std::mutex> lock(g_cursorMutex);
  g_RemoteCursors[steamID] = {x, y, type, std::chrono::steady_clock::now()};
}

void Overlay::Log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogV(fmt, args);
  va_end(args);
}

void Overlay::LogV(const char *fmt, const va_list args) {
  char buf[2048];
  vsnprintf(buf, sizeof(buf), fmt, args);

  {
    std::lock_guard lock(g_logMutex);
    g_logLines.emplace_back(buf);
    while (g_logLines.size() > MAX_LOG_LINES)
      g_logLines.pop_front();
  }

  if (g_origMjLog) {
    g_origMjLog("Mewtiplayer", "%s", buf);
  }
}

static void __cdecl WrappedMjLog(const char *owner, const char *fmt, ...) {
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (owner && strcmp(owner, "Mewtiplayer") != 0) {
    Overlay::Log("[%s] %s", owner, buf);
  } else {
    Overlay::Log("%s", buf);
  }
}

void Overlay::ToggleVisible() { g_visible = !g_visible; }



static void RenderRemoteCursors() {
  std::lock_guard lock(g_cursorMutex);
  ImDrawList *drawList = ImGui::GetForegroundDrawList();
  // ReSharper disable once CppLocalVariableMayBeConst
  HWND hWnd = ImGuiHook::GetHWND();
  const auto now = std::chrono::steady_clock::now();

  for (auto it = g_RemoteCursors.begin(); it != g_RemoteCursors.end();) {
    const float elapsed =
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

      const float scale = ((float)vw / 1920.0f) * 0.5f;
      ImVec2 size = g_CursorSizes[it->second.type];
      ImVec2 hotspot = g_CursorHotspots[it->second.type];

      size.x *= scale;
      size.y *= scale;
      hotspot.x *= scale;
      hotspot.y *= scale;

      auto pos = ImVec2((float)px - hotspot.x, (float)py - hotspot.y);

      drawList->AddImage(
          g_CursorTextures[it->second.type], pos,
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

      const ImVec2 textSize = ImGui::CalcTextSize(name);
      auto textPos =
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

static void RenderLogTab() {
  ImGui::BeginChild("##log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                    false, ImGuiWindowFlags_HorizontalScrollbar);
  {
    std::lock_guard<std::mutex> lock(g_logMutex);
    for (const auto &line : g_logLines) {
      auto col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
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
}

static void RenderNetworkTab() {
  const CSteamID current = NetworkManager::Get().GetCurrentLobby();

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

    ImGui::Spacing();
    ImGui::Text("Lobby Members:");
    if (ImGui::BeginTable("##lobby_members", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Player Name");
      ImGui::TableSetupColumn("Cats in ButchBox");
      ImGui::TableHeadersRow();

      const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(current);
      for (int i = 0; i < numMembers; i++) {
        CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(current, i);
        const char* name = SteamFriends()->GetFriendPersonaName(member);
        const int catCount = NetworkManager::Get().GetLobbyMemberCatCount(member.ConvertToUint64());
        
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", name ? name : "Unknown Player");
        
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d cat(s)", catCount);
      }
      ImGui::EndTable();
    }
  } else {
    ImGui::Text("Status: Not in a lobby.");
  }

  // Modal for Lobby Creation
  if (ImGui::BeginPopupModal("Host Lobby Modal", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Enter a name for your lobby:");
    ImGui::InputText("##name", g_lobbyNameBuffer, sizeof(g_lobbyNameBuffer));
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
}

static void RenderRNGTab() {
  uint32_t state[8] = {};
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
  const auto s64 = (uint64_t *)&state[4];
  ImGui::Text("s[4-5]: 0x%016llX", s64[0]);
  ImGui::Text("s[6-7]: 0x%016llX", s64[1]);
  ImGui::Columns(1);

  ImGui::Text("Current RNG: %u", state[0]);

  ImGui::Separator();
  ImGui::TextWrapped("This displays the current 32-byte RNG state.");
}

static void RenderCombatTab() {
  auto &nm = NetworkManager::Get();
  const bool active = nm.IsCombatActive();
  const uint32_t activeNUID = nm.GetActiveNUID();

  ImGui::Text("Combat Mode: %s", active ? "ACTIVE" : "INACTIVE");
  if (active) {
    if (const Character *activeChar = nm.GetCharacter(activeNUID)) {
      ImGui::Text("Active Turn: %s (NUID: %u)", activeChar->name.to_utf8().c_str(), activeNUID);
    } else {
      ImGui::Text("Active Turn: NUID %u", activeNUID);
    }

    const uint64_t lastController = nm.GetLastControllingPlayer();
    if (lastController != 0) {
      const char *controllerName = SteamFriends()->GetFriendPersonaName(lastController);
      ImGui::Text("Last Controlling Player: %s (%llu)",
                  controllerName ? controllerName : "Unknown", lastController);
    } else {
      ImGui::Text("Last Controlling Player: None");
    }
  }

  ImGui::Separator();
  if (active) {
    ImGui::Text("Active Combat NUID Assignments:");

    if (ImGui::BeginTable("##nuids", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("NUID");
      ImGui::TableSetupColumn("Character");
      ImGui::TableSetupColumn("Owner");
      ImGui::TableSetupColumn("Status");
      ImGui::TableHeadersRow();

      const uint32_t nextNuid = nm.GetNextNuid();
      for (uint32_t nuid = 0; nuid < nextNuid; nuid++) {
        const Character *c = nm.GetCharacter(nuid);
        if (!c)
          continue;

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%u", nuid);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s", c->name.to_utf8().c_str());

        ImGui::TableSetColumnIndex(2);
        const uint64_t ownerID = nm.GetNUIDOwner(nuid);
        if (ownerID == 0) {
          ImGui::TextDisabled("Host (Default/NPC)");
        } else {
          const char *ownerName = SteamFriends()->GetFriendPersonaName(ownerID);
          ImGui::Text("%s", ownerName ? ownerName : "Unknown");
        }

        ImGui::TableSetColumnIndex(3);
        if (nm.IsHost()) {
          std::string comboLabel = "##assign_nuid_" + std::to_string(nuid);
          if (ImGui::BeginCombo(comboLabel.c_str(), "Assign...")) {
            CSteamID lobby = nm.GetCurrentLobby();
            if (lobby.IsValid()) {
              const int members = SteamMatchmaking()->GetNumLobbyMembers(lobby);
              for (int i = 0; i < members; i++) {
                CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i);
                const char *memberName = SteamFriends()->GetFriendPersonaName(member);
                if (ImGui::Selectable(memberName ? memberName : "Unknown Player")) {
                  nm.SyncNUIDOwnership(nuid, member.ConvertToUint64());
                }
              }
            }
            ImGui::EndCombo();
          }
          if (nuid == activeNUID) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "ACTIVE TURN");
          }
        } else {
          if (nuid == activeNUID) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "ACTIVE TURN");
          } else {
            ImGui::Text("-");
          }
        }
      }
      ImGui::EndTable();
    }
  }
}

static char g_csvPasteBuffer[4096] = "";

static void RenderActionManagerTab() {
  ImGui::Text("Recorded Actions:");
  ImGui::Separator();

  const auto &actions = NetworkManager::Get().GetRecordedActions();
  std::string csvData =
      "Type,ActorNUID,ActionType,AbilityName,TargetX,TargetY,Target2X,Target2Y,NX,NY,Anim,Force\n";
  for (const auto &[type, data] : actions) {
    char line[512];
    if (type == PacketType::TurnAction) {
      snprintf(line, sizeof(line), "ACTION,%u,%d,%s,%d,%d,%d,%d,0,0,0,0\n",
               data.action.actorNUID, data.action.actionType,
               data.action.abilityName, data.action.targetX,
               data.action.targetY, data.action.target2X,
               data.action.target2Y);
    } else if (type == PacketType::TurnFacing) {
      snprintf(line, sizeof(line), "FACING,%u,0,NULL,0,0,0,0,%d,%d,%d,%d\n",
               data.facing.actorNUID, data.facing.nx,
               data.facing.ny, data.facing.anim ? 1 : 0,
               data.facing.force ? 1 : 0);
    }
    csvData += line;
  }

  ImGui::InputTextMultiline("##csv_output", (char *)csvData.c_str(),
                            csvData.size() + 1,
                            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 8),
                            ImGuiInputTextFlags_ReadOnly);

  if (ImGui::Button("Copy to Clipboard")) {
    ImGui::SetClipboardText(csvData.c_str());
  }

  ImGui::Separator();
  ImGui::Text("Replay Actions from CSV:");

  ImGui::InputTextMultiline("##csv_input", g_csvPasteBuffer,
                            sizeof(g_csvPasteBuffer),
                            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 8));

  if (ImGui::Button("Replay CSV")) {
    std::stringstream ss(g_csvPasteBuffer);
    std::string line;
    // skip header if present
    bool firstLine = true;
    while (std::getline(ss, line)) {
      if (line.empty())
        continue;
      if (firstLine && line.find("ActorNUID") != std::string::npos) {
        firstLine = false;
        continue;
      }
      firstLine = false;

      char typeStr[32] = {};
      uint32_t actorNUID = 0;
      int32_t actionType = 0;
      char abilityName[64] = {};
      int32_t targetX = 0, targetY = 0, target2X = 0, target2Y = 0;
      int32_t nx = 0, ny = 0;
      int anim = 0, force = 0;

      const int parsed =
          sscanf(line.c_str(), "%31[^,],%u,%d,%63[^,],%d,%d,%d,%d,%d,%d,%d,%d", // NOLINT(*-err34-c)
                 typeStr, &actorNUID, &actionType,
                 abilityName, &targetX,
                 &targetY, &target2X,
                 &target2Y, &nx,
                 &ny, &anim, &force);

      if (parsed == 12) {
        ActionPacket pkt = {};
        if (strcmp(typeStr, "ACTION") == 0) {
          pkt.type = PacketType::TurnAction;
          pkt.data.action.actorNUID = actorNUID;
          pkt.data.action.actionType = actionType;
          strncpy_s(pkt.data.action.abilityName, abilityName, _TRUNCATE);
          pkt.data.action.targetX = targetX;
          pkt.data.action.targetY = targetY;
          pkt.data.action.target2X = target2X;
          pkt.data.action.target2Y = target2Y;
        } else if (strcmp(typeStr, "FACING") == 0) {
          pkt.type = PacketType::TurnFacing;
          pkt.data.facing.actorNUID = actorNUID;
          pkt.data.facing.nx = nx;
          pkt.data.facing.ny = ny;
          pkt.data.facing.anim = anim != 0;
          pkt.data.facing.force = force != 0;
        }
        NetworkManager::Get().EnqueueReplayAction(pkt);
      } else {
        Overlay::Log("[REPLAY] Failed to parse CSV line: %s (parsed %d)",
                     line.c_str(), parsed);
      }
    }
  }
}

static const char* g_departureModeNames[] = { "Shared Progress Only", "All Progress Combined" };

static bool IsSaveOnAdventure() {
  static bool cachedResult = false;
  static DWORD lastCheckTime = 0;
  const DWORD now = GetTickCount();

  if (now - lastCheckTime < 2000) {
    return cachedResult;
  }
  lastCheckTime = now;

  if (!MewSQL::SaveFileExists(CUSTOM_SAVE_NAME.c_str())) {
    cachedResult = false;
    return false;
  }

  // If a save injection/load is currently pending, bypass background query to avoid locks
  if (GameUtils::g_startCustomRunPending) {
    return cachedResult;
  }

  glaiel::SQLSaveFile* db = MewSQL::OpenSaveDatabase(CUSTOM_SAVE_NAME.c_str());
  if (!db) {
    cachedResult = false;
    return false;
  }
  const int64_t onAdventure = MewSQL::ReadIntFromDatabase(db, "on_adventure", 0);
  MewSQL::CloseSaveDatabase(db);
  cachedResult = onAdventure == 1;
  return cachedResult;
}

static void RenderRunTab() {
  ImGui::Text("Mewtiplayer Run");
  ImGui::Separator();

  ImGui::Text("Custom Run Configuration");
  ImGui::SliderInt("Team Size", &GameUtils::g_customTeamSize, 2, 8);
  ImGui::SliderInt("Difficulty Mod", &GameUtils::g_customDifficulty, 0, 10);
  ImGui::Combo("Departure Mode", &GameUtils::g_departureMode, g_departureModeNames, IM_ARRAYSIZE(g_departureModeNames));

  ImGui::Spacing();
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (NetworkManager::Get().IsHost()) {
      const int totalCats = NetworkManager::Get().GetTotalLobbyCatCount();
      const bool canDepart = (totalCats == GameUtils::g_customTeamSize);

      if (!canDepart) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Multiplayer Depart")) {
        NetworkManager::Get().BeginMultiplayerSave();
      }
      if (!canDepart) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(%d/%d cats placed)", totalCats, GameUtils::g_customTeamSize);
      }
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Waiting for host to depart...");
      ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Placed %d cat(s)", GetLocalButchBoxCatCount());
    }
  } else {
    if (ImGui::Button("Local Depart")) {
      GameUtils::g_oldDirector = GameUtils::GetMewDirectorSingleton();
      GameUtils::CreateMewtiplayerSave(CUSTOM_SAVE_NAME.c_str());
      GameUtils::g_startCustomRunPending = true;
      GameUtils::LoadSaveFile(CUSTOM_SAVE_NAME.c_str());
    }
  }

  ImGui::SameLine();

  // ReSharper disable once CppTooWideScope
  const bool canContinue = MewSQL::SaveFileExists(CUSTOM_SAVE_NAME.c_str()) && IsSaveOnAdventure();
  if (canContinue) {
    if (ImGui::Button("Continue Run")) {
      GameUtils::LoadSaveFile(CUSTOM_SAVE_NAME.c_str());
    }
  } else {
    ImGui::TextDisabled("Continue Run");
  }
}

struct ListedComponent {
  std::string roleName;
  std::vector<std::string> subcomponents;
};

struct ComponentTypeGroup {
  std::string typeName;
  std::vector<ListedComponent> instances;
};

struct ListedScene { // NOLINT(*-pro-type-member-init)
  std::string name;
  uint32_t entityCount;
  uint32_t componentCount;
  std::vector<ComponentTypeGroup> componentTypes;
};

static std::vector<ListedScene> g_listedScenes;
static int g_selectedSceneIndex = -1;

static void RenderScenesTab() {
  ImGui::Text("Active Scenes Manager");
  ImGui::Separator();

  // Draw List Scenes button
  if (ImGui::Button("List Current Scenes", ImVec2(180, 0))) {
    g_listedScenes.clear();
    g_selectedSceneIndex = -1;
    const auto scenes = GameUtils::GetCurrentScenes();
    for (const Scene *scene : scenes) {
      if (scene) {
        ListedScene ls;
        ls.name = scene->name.copy_to_native_string();
        ls.entityCount = scene->Entities.size();
        
        auto comps = GameUtils::GetSceneComponents(scene);
        ls.componentCount = comps.size();
        
        // Group components by type
        std::map<std::string, std::vector<ListedComponent>> compCount;
        for (const Component *c : comps) {
          ListedComponent lc;
          MsvcReleaseModeXString typeName = {};
          std::string tName = "Unknown";
          if (GameUtils::SafeGetComponentName(c, &typeName)) {
            tName = typeName.copy_to_native_string();
            GameUtils::FreeXString(typeName);
          }
          
          if (tName == "Button") {
              char roleBuf[256] = {};
              if (MewUI_GetButtonRoleName((Component*)c, roleBuf, sizeof(roleBuf))) {
                  lc.roleName = roleBuf;
              }
          }
          
          if (c->entity) {
              auto entityComps = GameUtils::GetEntityComponents(c->entity);
              for (const Component* ec : entityComps) {
                  if (ec != c) {
                      MsvcReleaseModeXString ecName = {};
                      if (GameUtils::SafeGetComponentName(ec, &ecName)) {
                          lc.subcomponents.push_back(ecName.copy_to_native_string());
                          GameUtils::FreeXString(ecName);
                      } else {
                          lc.subcomponents.push_back("Unknown");
                      }
                  }
              }
          }
          compCount[tName].push_back(lc);
        }
        for (auto &[fst, snd] : compCount) {
          ComponentTypeGroup group;
          group.typeName = fst;
          group.instances = std::move(snd);
          ls.componentTypes.push_back(std::move(group));
        }
        g_listedScenes.push_back(ls);
      }
    }
  }

  ImGui::Spacing();

  if (g_listedScenes.empty()) {
    ImGui::TextDisabled("No scenes listed. Click 'List Current Scenes' to query the game.");
  } else {
    // Left side: Table of scenes
    ImGui::BeginChild("##scenes_list_panel", ImVec2(320, 0), true);
    ImGui::Text("Scenes List:");
    ImGui::Separator();
    if (ImGui::BeginTable("##scenes_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Ents", ImGuiTableColumnFlags_WidthFixed, 40.0f);
      ImGui::TableSetupColumn("Comps", ImGuiTableColumnFlags_WidthFixed, 40.0f);
      ImGui::TableHeadersRow();

      for (int i = 0; i < (int)g_listedScenes.size(); i++) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool isSelected = g_selectedSceneIndex == i;
        if (ImGui::Selectable(g_listedScenes[i].name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
          g_selectedSceneIndex = i;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%u", g_listedScenes[i].entityCount);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%u", g_listedScenes[i].componentCount);
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right side: Scene Details
    ImGui::BeginChild("##scene_details_panel", ImVec2(0, 0), true);
    if (g_selectedSceneIndex >= 0 && g_selectedSceneIndex < (int)g_listedScenes.size()) {
      const auto &[name, entityCount, componentCount, componentTypes] = g_listedScenes[g_selectedSceneIndex];
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Scene: %s", name.c_str());
      ImGui::Separator();
      ImGui::Text("Entities: %u", entityCount);
      ImGui::Text("Components: %u", componentCount);
      ImGui::Spacing();
      ImGui::Text("Component Breakdown:");
      ImGui::Separator();
      
      ImGui::BeginChild("##comp_list", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
      for (const auto &group : componentTypes) {
        std::string groupLabel = group.typeName + " (" + std::to_string(group.instances.size()) + ")";
        if (ImGui::TreeNode(group.typeName.c_str(), "%s", groupLabel.c_str())) {
            int instanceIdx = 0;
            for (const auto &inst : group.instances) {
                std::string instLabel = "Instance " + std::to_string(instanceIdx++);
                if (!inst.roleName.empty()) {
                    instLabel += " - Role: " + inst.roleName;
                }
                if (inst.subcomponents.empty()) {
                    ImGui::BulletText("%s", instLabel.c_str());
                } else {
                    if (ImGui::TreeNode((void*)&inst, "%s", instLabel.c_str())) {
                        for (const auto &sub : inst.subcomponents) {
                            ImGui::BulletText("%s", sub.c_str());
                        }
                        ImGui::TreePop();
                    }
                }
            }
            ImGui::TreePop();
        }
      }
      ImGui::EndChild();
    } else {
      ImGui::TextDisabled("Select a scene from the list to view component details.");
    }
    ImGui::EndChild();
  }
}

static void InternalRender() {
  // Draw remote cursors
  RenderRemoteCursors();

  if (!g_visible)
    return;

  ImGui::SetNextWindowSize(ImVec2(680, 420), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::Begin("Mewtiplayer  |  F1 to hide", &g_visible,
               ImGuiWindowFlags_NoNav);

  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Logs")) {
      RenderLogTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Network")) {
      RenderNetworkTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Party & Ownership")) {
      ImGui::Text("Cat Ownership Assignments:");

      if (ImGui::BeginTable("##cats", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Cat Name");
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("Owner");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();

        auto &nm = NetworkManager::Get();
        const auto &cats = nm.GetDiscoveredCats();
        for (const auto &pair : cats) {
          const auto &cat = pair.second;
          ImGui::TableNextRow();

          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%s", cat.name.c_str());

          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%s", cat.className.c_str());

          ImGui::TableSetColumnIndex(2);
          const uint64_t ownerID = nm.GetCatOwner(cat.uid);
          if (ownerID == 0) {
            ImGui::TextDisabled("Unassigned");
          } else {
            const char *ownerName = SteamFriends()->GetFriendPersonaName(ownerID);
            ImGui::Text("%s", ownerName ? ownerName : "Unknown");
          }

          ImGui::TableSetColumnIndex(3);
          if (nm.IsHost()) {
            std::string comboLabel = "##assign_" + std::to_string(cat.uid);
            if (ImGui::BeginCombo(comboLabel.c_str(), "Assign...")) {
              CSteamID lobby = nm.GetCurrentLobby();
              if (lobby.IsValid()) {
                const int members = SteamMatchmaking()->GetNumLobbyMembers(lobby);
                for (int i = 0; i < members; i++) {
                  CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i);
                  const char *memberName = SteamFriends()->GetFriendPersonaName(member);
                  if (ImGui::Selectable(memberName ? memberName : "Unknown Player")) {
                    nm.SyncOwnership(cat.uid, member.ConvertToUint64());
                  }
                }
              }
              ImGui::EndCombo();
            }
          } else {
            ImGui::TextDisabled("Host Only");
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Combat")) {
      RenderCombatTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Action Manager")) {
      RenderActionManagerTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("RNG")) {
      RenderRNGTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Scenes")) {
      RenderScenesTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Run Manager")) {
      RenderRunTab();
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
        LoadCursorTextures();
        Log("[OK] Overlay active. F1=Menu");

        const char *cmdLine = GetCommandLineA();
        const char *posArg = strstr(cmdLine, "-pos ");
        if (posArg) {
          int x = 0, y = 0;
          if (sscanf(posArg + 5, "%d,%d", &x, &y) == 2) {
            HWND hWnd = ImGuiHook::GetHWND();
            if (hWnd) {
              SetWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
              Log("[INIT] Positioned window to (%d, %d)", x, y);
            }
          }
        }
      })) {
    mj->Log("Overlay", "[ERR] Failed to load ImGuiHook: %s",
            ImGuiHook::GetLastError().c_str());
  } else {
    mj->Log("Overlay", "[OK] Overlay initialized using Kiero.");
  }
}
