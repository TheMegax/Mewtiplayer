// ReSharper disable CppMemberFunctionMayBeStatic
#include "NetworkManager.h"
#include "ModState.h"
#include "SteamABICompat.h"
#include "GameUtils.h"
#include "ImGuiHook.h"
#include "InputGhost.h"
#include "Overlay.h"
#include "mewjector.h"
#include "MewSQL.h"
#include "hooks/AdventureBoxHooks.h"
#include "hooks/ClassChooserHooks.h"
#include "hooks/StorageHooks.h"
#include "hooks/SaveHooks.h"
#include <cstring>
#include <algorithm>

using FaceDirection_t = void(__fastcall *)(void *character,
                                          uint64_t target_packed,
                                          bool play_animation, bool force);
extern FaceDirection_t g_origFaceDirection;

void NetworkManager::Init(MewjectorAPI *mj, const char *modID) {
  m_mj = mj;
  m_ModID = modID;
  m_AutoJoinStartTime = GetTickCount64();
  m_LastAutoJoinAttempt = 0;
  m_AutoJoinFinished = false;
  if (SteamAPI_Init()) {
    Overlay::Log("[NETWORK] SteamAPI initialized successfully!");
  } else {
    Overlay::Log("[NETWORK] SteamAPI failed to initialize. Make sure Steam is running.");
  }
}

void NetworkManager::Update() {
  SteamAPI_RunCallbacks();
  ReceivePackets();

  if (g_modState.autoJoin && !m_AutoJoinFinished && !m_CurrentLobby.IsValid()) {
    ULONGLONG now = GetTickCount64();
    if (m_AutoJoinStartTime == 0) {
      m_AutoJoinStartTime = now;
    }

    if (now - m_AutoJoinStartTime > 60000) {
      Overlay::Log("[NETWORK] Auto join timed out after 60 seconds.");
      m_AutoJoinFinished = true;
    } else if (now - m_LastAutoJoinAttempt >= 5000) {
      m_LastAutoJoinAttempt = now;
      Overlay::Log("[NETWORK] Auto join attempting to find and join lobby (elapsed: %llu s)...", (now - m_AutoJoinStartTime) / 1000);
      JoinAnyLobby();
    }
  }

  static int frameCount = 0;
  if (m_CurrentLobby.IsValid()) {
    frameCount++;
    if (frameCount >= 120) {
      frameCount = 0;
      SendLocalCatCount();
    }
  }
}

bool NetworkManager::SendPacket(const CSteamID target, const PacketType type,
                                const void *data, const uint32_t size) {
  PacketHeader header;
  header.type = type;
  header.length = size;

  std::vector<uint8_t> buffer(sizeof(PacketHeader) + size);
  memcpy(buffer.data(), &header, sizeof(PacketHeader));
  if (size > 0 && data)
    memcpy(buffer.data() + sizeof(PacketHeader), data, size);

  return SteamNetworking()->SendP2PPacket(
      target, buffer.data(), (uint32)buffer.size(), k_EP2PSendUnreliable);
}

void NetworkManager::ReceivePackets() {
  uint32 packetSize;
  while (SteamNetworking()->IsP2PPacketAvailable(&packetSize)) {
    std::vector<uint8_t> buffer(packetSize);
    CSteamID remoteID;
    if (!SteamNetworking()->ReadP2PPacket(buffer.data(), packetSize,
                            &packetSize, &remoteID))
      continue;

    if (packetSize < sizeof(PacketHeader))
      continue;

    const auto hdr = (PacketHeader *)buffer.data();
    if (hdr->magic1 != 'M' || hdr->magic2 != 'G')
      continue;

    const void *payload = buffer.data() + sizeof(PacketHeader);
    const uint32_t payloadLen = hdr->length;

    switch (hdr->type) {
    case PacketType::Ping:
      Overlay::Log("[NETWORK] Received Ping from %llu", remoteID.ConvertToUint64());
      break;
    case PacketType::Handshake:
      HandleHandshake(remoteID);
      break;
    case PacketType::RNGSync:
      HandleRNGSync(payload, payloadLen);
      break;
    case PacketType::MouseMove:
      HandleMouseMove(payload, payloadLen);
      break;
    case PacketType::CatOwnershipSync:
      HandleCatOwnershipSync(payload, payloadLen);
      break;
    case PacketType::CombatStart:
      StartCombat();
      break;
    case PacketType::CombatEnd:
      EndCombat();
      break;
    case PacketType::TurnAction:
      HandleTurnAction(remoteID, payload, payloadLen);
      break;
    case PacketType::TurnFacing:
      HandleTurnFacing(remoteID, payload, payloadLen);
      break;
    case PacketType::SaveCatRequest:
      HandleSaveCatRequest(remoteID);
      break;
    case PacketType::SaveCatResponse:
      HandleSaveCatResponse(remoteID, payload, payloadLen);
      break;
    case PacketType::SaveFileTransfer:
      HandleSaveFileTransfer(remoteID, payload, payloadLen);
      break;
    case PacketType::SaveFileAck:
      HandleSaveFileAck(remoteID);
      break;
    case PacketType::SaveLoadSignal:
      HandleSaveLoadSignal(payload, payloadLen);
      break;
    case PacketType::ButchBoxCatCountSync:
      HandleButchBoxCatCountSync(remoteID, payload, payloadLen);
      break;
    case PacketType::CollarSync:
      HandleCollarSync(payload, payloadLen);
      break;
    case PacketType::LobbyReady:
      HandleLobbyReady(payload, payloadLen);
      break;
    case PacketType::LobbyProceed:
      HandleLobbyProceed();
      break;
    case PacketType::StorageItemSync:
      HandleStorageItemSync(payload, payloadLen);
      break;
    case PacketType::MapNodeSync:
      HandleMapNodeSync(payload, payloadLen);
      break;
    case PacketType::ActSelectSync:
      HandleActSelectSync(payload, payloadLen);
      break;
    case PacketType::MapInventoryOpen:
      HandleMapInventoryOpen(payload, payloadLen);
      break;
    case PacketType::MapInventoryClose:
      HandleMapInventoryClose(payload, payloadLen);
      break;
    case PacketType::LevelUpSelectOption:
      HandleLevelUpSelectOption(payload, payloadLen);
      break;
    case PacketType::LevelUpReroll:
      HandleLevelUpReroll(payload, payloadLen);
      break;
    case PacketType::AbilityReplace:
      HandleAbilityReplace(payload, payloadLen);
      break;
    case PacketType::WorldEventSelectOption:
      HandleWorldEventSelectOption(payload, payloadLen);
      break;
    case PacketType::WorldEventSelectCat:
      HandleWorldEventSelectCat(payload, payloadLen);
      break;
    case PacketType::WorldEventClickEnd:
      HandleWorldEventClickEnd(payload, payloadLen);
      break;
    case PacketType::ShopBuyItem:
      HandleShopBuyItem(payload, payloadLen);
      break;
    case PacketType::ShopExitButton:
      HandleShopExitButton(payload, payloadLen);
      break;

    case PacketType::ShopChestClick:
      HandleShopChestClick(payload, payloadLen);
      break;
    default:
      Overlay::Log("[NETWORK] Received unknown packet type %u from %llu", hdr->type,
                   remoteID.ConvertToUint64());
      break;
    }
  }
}

void NetworkManager::HandleHandshake(const CSteamID remoteID) {
  Overlay::Log("[NETWORK] Received Handshake from %llu", remoteID.ConvertToUint64());
}

void NetworkManager::HandleRNGSync(const void *data, const uint32_t length) {
  if (length == 32) {
    GameUtils::SetRNGState(data);
    Overlay::Log("[NETWORK] RNG state synchronized with host.");
  } else {
    Overlay::Log("[NETWORK] [ERR] Received invalid RNGSync packet (length %u)", length);
  }
}

void NetworkManager::HandleMouseMove(const void *data, const uint32_t length) {
  if (length == sizeof(MouseMoveData)) {
    const MouseMoveData *move = (MouseMoveData *)data;
    if (IsHost()) {
      BroadcastPacket(PacketType::MouseMove, move, sizeof(MouseMoveData), true);
    }

    const uint64_t actualSender = move->steamID;

    // Only show the ghost cursor and name tag if it's NOT the local player
    if (actualSender != SteamUser()->GetSteamID().ConvertToUint64()) {
      // Only simulate the mouse moving around if we are NOT focused
      // AND it's that player's turn (or everyone can move outside combat).
      if (GetForegroundWindow() != ImGuiHook::GetHWND() &&
          !IsInputBlocked(actualSender)) {
        InputGhost::SimulateMouseMove(move->x, move->y, ImGuiHook::GetHWND());
      }
      Overlay::UpdateRemoteCursor(actualSender, move->x, move->y,
                                  move->cursorType);
    }
  }
}

void NetworkManager::HandleCatOwnershipSync(const void *data, const uint32_t length) {
  if (length == sizeof(CatOwnershipData)) {
    const auto sync = (const CatOwnershipData *)data;
    m_catOwnership[sync->catUID] = sync->ownerSteamID;
    UpdateNUIDOwnership();
    const char *name = SteamFriends()->GetFriendPersonaName(sync->ownerSteamID);
    Overlay::Log("[NETWORK] Ownership Sync: Cat %lld is now owned by %s", sync->catUID,
                 name ? name : "Unknown");
  }
}

void NetworkManager::UpdateNUIDOwnership() {
  for (const auto &[character, nuid] : m_charToNuid) {
    if (!character)
      continue;

    uint64_t ownerSteamID = 0;

    if (character->persistentChar) {
      const int64_t sqlKey = character->persistentChar->sql_key;
      const auto it = m_catOwnership.find(sqlKey);
      if (it != m_catOwnership.end()) {
        ownerSteamID = it->second;
      }
    }

    if (ownerSteamID == 0 && character->persistentChar) {
      const int64_t catID = character->persistentChar->catID;
      extern std::map<int64_t, uint64_t> g_catIdToOwnerSteamID;
      const auto it = g_catIdToOwnerSteamID.find(catID);
      if (it != g_catIdToOwnerSteamID.end()) {
        ownerSteamID = it->second;
      }
    }

    if (ownerSteamID != 0) {
      m_nuidOwnership[nuid] = ownerSteamID;
    }
  }
}

bool NetworkManager::IsInputBlocked(const uint64_t steamID) {
  if (!m_CurrentLobby.IsValid())
    return false;
  if (!m_combatActive)
    return false;

  // If no active cat, block everyone but the host as a failsafe
  if (m_activeNUID == 0xFFFFFFFF) {
    return steamID != GetHostID().ConvertToUint64();
  }

  const auto it = m_nuidOwnership.find(m_activeNUID);
  if (it == m_nuidOwnership.end() || it->second == 0) {
    return steamID != GetHostID().ConvertToUint64();
  }

  return steamID != it->second;
}

void NetworkManager::SyncOwnership(const int64_t uid, const uint64_t steamID) {
  if (!IsHost())
    return;

  CatOwnershipData data{};
  data.catUID = uid;
  data.ownerSteamID = steamID;

  m_catOwnership[uid] = steamID;
  UpdateNUIDOwnership();

  BroadcastPacket(PacketType::CatOwnershipSync, &data, sizeof(data),
                  false); // Include self to update map
}

void NetworkManager::SetActiveNUID(const uint32_t nuid) {
  if (nuid != 0xFFFFFFFF) {
    m_combatActive = true;
    const auto it = m_nuidOwnership.find(nuid);
    if (it != m_nuidOwnership.end() && it->second != 0) {
      m_lastControllingPlayer = it->second;
    }
  }
  m_activeNUID = nuid;
}

void NetworkManager::RegisterCat(const int64_t uid, const char *name,
                                 const char *className) {
  if (uid == -1)
    return;

  CatInfo info;
  info.uid = uid;
  info.name = name ? name : "Unknown";
  info.className = className ? className : "Collarless";
  m_discoveredCats[uid] = info;
}

void NetworkManager::StartCombat() {
  if (!m_combatActive) {
    m_combatActive = true;
    if (IsHost()) {
      BroadcastPacket(PacketType::CombatStart, nullptr, 0, true);
      Overlay::Log("[NETWORK] Combat Started (Host)");
    } else {
      Overlay::Log("[NETWORK] Combat Started (Client)");
    }
  }
}

void NetworkManager::EndCombat() {
  if (m_combatActive) {
    Overlay::Log("[NETWORK] Combat ended, unblocking input.");
    m_combatActive = false;
    m_activeNUID = 0xFFFFFFFF;
    if (IsHost()) {
      BroadcastPacket(PacketType::CombatEnd, nullptr, 0, true);
    }
  }
}

uint64_t NetworkManager::GetCatOwner(const int64_t uid) {
  const auto it = m_catOwnership.find(uid);
  if (it != m_catOwnership.end())
    return it->second;
  return 0;
}

uint64_t NetworkManager::GetNUIDOwner(const uint32_t nuid) const {
  const auto it = m_nuidOwnership.find(nuid);
  if (it != m_nuidOwnership.end())
    return it->second;
  return 0;
}

void NetworkManager::HostLobby(const char *lobbyName) {
  m_PendingLobbyName = lobbyName;
  Overlay::Log("[NETWORK] Creating Steam Lobby '%s'...", lobbyName);
  const SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, 4);
  m_LobbyCreatedCallResult.Set(call, this, &NetworkManager::OnLobbyCreated);
}

void NetworkManager::LeaveLobby() {
  if (m_CurrentLobby.IsValid()) {
    Overlay::Log("[NETWORK] Leaving lobby %llu...", m_CurrentLobby.ConvertToUint64());
    SteamMatchmaking()->LeaveLobby(m_CurrentLobby);
    m_CurrentLobby.Clear();
    m_combatActive = false;
    m_activeNUID = 0xFFFFFFFF;
    m_catOwnership.clear();
    m_discoveredCats.clear();
    m_lobbyMemberCatCounts.clear();
    extern void ResetLobbyReadyStates();
    ResetLobbyReadyStates();
    RefreshLobbyList();
  }
}

void NetworkManager::JoinLobby(const CSteamID lobbyID) {
  Overlay::Log("[NETWORK] Joining lobby %llu...", lobbyID.ConvertToUint64());
  const SteamAPICall_t call = SteamMatchmaking()->JoinLobby(lobbyID);
  m_LobbyEnterCallResult.Set(call, this, &NetworkManager::OnLobbyEnter);
}

void NetworkManager::JoinAnyLobby() {
  Overlay::Log("[NETWORK] Searching for lobbies...");
  m_AutoJoinSearch = true;
  RefreshLobbyList();
}

void NetworkManager::RefreshLobbyList() {
  SteamMatchmaking()->AddRequestLobbyListDistanceFilter(
      k_ELobbyDistanceFilterWorldwide);
  SteamMatchmaking()->AddRequestLobbyListStringFilter(
      "mewtiplayer", m_ModID.c_str(), k_ELobbyComparisonEqual);
  const SteamAPICall_t call = SteamMatchmaking()->RequestLobbyList();
  m_LobbyMatchListCallResult.Set(call, this, &NetworkManager::OnLobbyMatchList);
}

bool NetworkManager::IsHost() const {
  if (!m_CurrentLobby.IsValid())
    return false;
  return SteamMatchmaking()->GetLobbyOwner(m_CurrentLobby) ==
         SteamUser()->GetSteamID();
}

CSteamID NetworkManager::GetHostID() const {
  if (!m_CurrentLobby.IsValid())
    return {};
  return SteamMatchmaking()->GetLobbyOwner(m_CurrentLobby);
}

void NetworkManager::BroadcastPacket(const PacketType type, const void *data,
                                     const uint32_t size, const bool excludeSelf) {
  if (!m_CurrentLobby.IsValid())
    return;
  const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  const CSteamID myID = SteamUser()->GetSteamID();
  for (int i = 0; i < numMembers; i++) {
    CSteamID member =
        SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    if (excludeSelf && member == myID)
      continue;
    SendPacket(member, type, data, size);
  }
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void NetworkManager::OnLobbyCreated(LobbyCreated_t *pCallback, const bool bIOFailure) {
  if (bIOFailure || pCallback->m_eResult != k_EResultOK) {
    Overlay::Log("[NETWORK] [ERR] Failed to create lobby (Result: %d)", pCallback->m_eResult);
    return;
  }
  m_CurrentLobby = CSteamID(pCallback->m_ulSteamIDLobby);
  m_combatActive = false;
  m_activeNUID = 0xFFFFFFFF;
  m_catOwnership.clear();
  m_discoveredCats.clear();
  extern void ResetLobbyReadyStates();
  ResetLobbyReadyStates();

  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "name",
                                   m_PendingLobbyName.c_str());
  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "mewtiplayer",
                                   m_ModID.c_str());
  Overlay::Log("[NETWORK] [OK] Lobby created: %llu", m_CurrentLobby.ConvertToUint64());
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void NetworkManager::OnLobbyEnter(LobbyEnter_t *pCallback, const bool bIOFailure) {
  if (bIOFailure || pCallback->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
    Overlay::Log("[NETWORK] [ERR] Failed to join lobby (Response: %d)",
                 pCallback->m_EChatRoomEnterResponse);
    return;
  }
  m_CurrentLobby = CSteamID(pCallback->m_ulSteamIDLobby);
  m_combatActive = false;
  m_activeNUID = 0xFFFFFFFF;
  m_catOwnership.clear();
  m_discoveredCats.clear();
  m_lobbyMemberCatCounts.clear();
  extern void ResetLobbyReadyStates();
  ResetLobbyReadyStates();

  Overlay::Log("[OK] Joined lobby: %llu", m_CurrentLobby.ConvertToUint64());

  if (g_modState.autoJoin) {
    m_AutoJoinFinished = true;
  }

  // Send handshake to host
  SendPacket(GetHostID(), PacketType::Handshake, nullptr, 0);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void NetworkManager::OnLobbyMatchList(LobbyMatchList_t *pCallback,
                                      bool bIOFailure) {
  m_LobbyList.clear();
  for (uint32 i = 0; i < pCallback->m_nLobbiesMatching; i++) {
    const CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i); // NOLINT(*-narrowing-conversions)
    LobbyInfo info;
    info.id = lobbyID;
    const char *name = SteamMatchmaking()->GetLobbyData(lobbyID, "name");
    info.name = name ? name : "Unknown Lobby";
    info.memberCount = SteamMatchmaking()->GetNumLobbyMembers(lobbyID);
    info.maxMembers = SteamMatchmaking()->GetLobbyMemberLimit(lobbyID);
    m_LobbyList.push_back(info);
  }
  Overlay::Log("Found %d lobbies.", static_cast<int>(m_LobbyList.size()));

  if (m_AutoJoinSearch && !m_LobbyList.empty()) {
    JoinLobby(m_LobbyList[0].id);
  }
  m_AutoJoinSearch = false;
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void NetworkManager::OnP2PSessionRequest(P2PSessionRequest_t *pParam) {
  SteamNetworking()->AcceptP2PSessionWithUser(pParam->m_steamIDRemote);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void NetworkManager::OnGameLobbyJoinRequested(GameLobbyJoinRequested_t *pParam) {
  JoinLobby(pParam->m_steamIDLobby);
}

void NetworkManager::HandleTurnAction(CSteamID remoteID, const void *data,
                                      const uint32_t length) {
  if (length != sizeof(TurnActionPacket))
    return;

  const auto *pkt = (const TurnActionPacket *)data;
  Overlay::Log("[NET] Queuing TurnAction: Type=%d NUID=%u Ability=%s",
               pkt->actionType, pkt->actorNUID, pkt->abilityName);

  if (pkt->isPassive) {
    Overlay::Log("[NET] Forcing Passive Trigger: NUID=%u Ability=%s",
                 pkt->actorNUID, pkt->abilityName);
    
    if (Character *c = NetworkManager::Get().GetCharacter(pkt->actorNUID)) {
      TurnAction turnAction{};
      turnAction.type = pkt->actionType;
      turnAction.targetX = pkt->targetX;
      turnAction.targetY = pkt->targetY;
      turnAction.target2X = pkt->target2X;
      turnAction.target2Y = pkt->target2Y;
      turnAction.unk_28 = pkt->unk_28;
      turnAction.unk_2C = pkt->unk_2C;
      turnAction.flag_30 = pkt->flag_30;
      turnAction.flag_31 = pkt->flag_31;
      turnAction.flag_32 = pkt->flag_32;
      turnAction.flag_33 = pkt->flag_33;
      turnAction.flag_34 = pkt->flag_34;
      turnAction.flag_35 = pkt->flag_35;
      turnAction.flag_36 = pkt->flag_36;
      turnAction.actor = c;
      
      Component *comp = GameUtils::FindCharacterPassive(c, pkt->abilityName);
      if (comp) {
          turnAction.ability = reinterpret_cast<Ability *>(comp);
      } else {
          turnAction.ability = GameUtils::FindCharacterAbility(c, pkt->abilityName);
      }
      
      if (turnAction.ability) {
          GameUtils::SetRNGState(pkt->rngState);
          ParaboxAPI::ForceAbilityTrigger(turnAction.ability, &turnAction);
      } else {
          Overlay::Log("[NET] Failed to find passive/ability: %s", pkt->abilityName);
      }
    }
  } else {
    ActionPacket action{};
    action.type = PacketType::TurnAction;
    action.data.action = *pkt;

    extern std::deque<ActionPacket> g_pendingInjections;
    g_pendingInjections.push_back(action);
  }
}

void NetworkManager::HandleTurnFacing(CSteamID remoteID, const void *data,
                                      const uint32_t length) {
  if (length != sizeof(TurnFacingPacket))
    return;

  const auto pkt = (const TurnFacingPacket *)data;
  Overlay::Log("[NET] Received TurnFacing: NUID=%u Target=(%d,%d)",
               pkt->actorNUID, pkt->nx, pkt->ny);

  ActionPacket action{};
  action.type = PacketType::TurnFacing;
  action.data.facing = *pkt;

  extern std::deque<ActionPacket> g_pendingInjections; // NOLINT(*-redundant-declaration)
  g_pendingInjections.push_back(action);

  // If not replaying, apply it immediately
  if (m_pendingReplays.empty()) {
    if (Character *c = GetCharacter(pkt->actorNUID)) {
      const uint64_t packed = (uint64_t)pkt->nx | (static_cast<uint64_t>(pkt->ny) << 32);
      ParaboxAPI::ForceFaceDirection(c, packed, pkt->anim, pkt->force);
    }
  }
}

void NetworkManager::RecordAction(const ActionPacket &pkt) {
  m_recordedActions.push_back(pkt);
}

void NetworkManager::ClearRecordedActions() {
  m_recordedActions.clear();
  m_lastFacing.clear();
}

const std::vector<ActionPacket> &NetworkManager::GetRecordedActions() const {
  return m_recordedActions;
}

void NetworkManager::EnqueueReplayAction(const ActionPacket &pkt) {
  extern std::deque<ActionPacket> g_pendingInjections; // NOLINT(*-redundant-declaration)
  g_pendingInjections.push_back(pkt);
  if (pkt.type == PacketType::TurnAction) {
    Overlay::Log("[REPLAY] Queued action: %s", pkt.data.action.abilityName);
  } else {
    Overlay::Log("[REPLAY] Queued facing: (%d, %d)", pkt.data.facing.nx, pkt.data.facing.ny);
  }
}
void NetworkManager::InitializeEntityMapping() {
  ResetEntityMapping();

  auto fighters = GameUtils::GetFighters();
  // Entities at the start of combat are loaded in *always* in the same order,
  // allowing us to use NUIDs for networking.
  // Thanks, Tyler <3

  Overlay::Log("NUID: Initializing mapping for %zu fighters", fighters.size());

  for (Character *c : fighters) {
    if (!c)
      continue;

    uint32_t nuid = m_nextNuid++;
    m_charToNuid[c] = nuid;
    m_nuidToChar[nuid] = c;

    Overlay::Log("NUID: Map [%d] -> Character %p (%s)", nuid, c,
                 c->name.to_utf8().c_str());
  }

  UpdateNUIDOwnership();
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
uint32_t NetworkManager::GetNUID(Character *character) {
  auto it = m_charToNuid.find(character);
  if (it == m_charToNuid.end()) {
    UpdateDynamicEntities();
    it = m_charToNuid.find(character);
  }
  if (it != m_charToNuid.end()) {
    return it->second;
  }
  return 0xFFFFFFFF; // Invalid
}

Character *NetworkManager::GetCharacter(const uint32_t nuid) {
  const auto it = m_nuidToChar.find(nuid);
  if (it != m_nuidToChar.end()) {
    return it->second;
  }
  return nullptr;
}

void NetworkManager::UpdateDynamicEntities() {
  auto all = GameUtils::GetFighters();
  for (Character *c : all) {
    if (!c)
      continue;
    if (m_charToNuid.find(c) == m_charToNuid.end()) {
      uint32_t nuid = m_nextNuid++;
      m_charToNuid[c] = nuid;
      m_nuidToChar[nuid] = c;

      if (m_lastControllingPlayer != 0) {
        m_nuidOwnership[nuid] = m_lastControllingPlayer;
        Overlay::Log("NUID: Auto assigned dynamic NUID %u to player %llu", nuid, m_lastControllingPlayer);
      }

      Overlay::Log("NUID: Dynamic Map [%d] -> Character %p (%s)", nuid, c,
                   c->name.to_utf8().c_str());
    }
  }

  UpdateNUIDOwnership();
}

void NetworkManager::ResetEntityMapping() {
  m_charToNuid.clear();
  m_nuidToChar.clear();
  m_nextNuid = 0;
  m_nuidOwnership.clear();
  m_lastControllingPlayer = 0;
}

bool NetworkManager::SendPacketReliable(const CSteamID target, const PacketType type, const void *data, const uint32_t size) {
  PacketHeader header;
  header.type = type;
  header.length = size;

  std::vector<uint8_t> buffer(sizeof(PacketHeader) + size);
  memcpy(buffer.data(), &header, sizeof(PacketHeader));
  if (size > 0 && data)
    memcpy(buffer.data() + sizeof(PacketHeader), data, size);

  return SteamNetworking()->SendP2PPacket(
      target, buffer.data(), (uint32)buffer.size(), k_EP2PSendReliable);
}

void NetworkManager::SendChunkedData(CSteamID target, const PacketType type, const uint8_t* data, const uint32_t totalSize, const uint32_t transferId) {
  constexpr uint32_t CHUNK_PAYLOAD_SIZE = 100 * 1024; // 100 KB chunks
  const uint32_t totalChunks = totalSize == 0 ? 1 : (totalSize + CHUNK_PAYLOAD_SIZE - 1) / CHUNK_PAYLOAD_SIZE;

  Overlay::Log("[SAVE] Starting chunked transfer ID %u, size %u, chunks %u to %llu",
               transferId, totalSize, totalChunks, target.ConvertToUint64());

  for (uint32_t i = 0; i < totalChunks; ++i) {
    const uint32_t offset = i * CHUNK_PAYLOAD_SIZE;
    uint32_t size = 0;
    if (totalSize > 0) {
      size = std::min(CHUNK_PAYLOAD_SIZE, totalSize - offset);
    }

    std::vector<uint8_t> chunkBuffer(sizeof(ChunkedTransferHeader) + size);
    const auto header = (ChunkedTransferHeader*)chunkBuffer.data();
    header->transferId = transferId;
    header->chunkIndex = i;
    header->totalChunks = totalChunks;
    header->totalSize = totalSize;
    header->chunkSize = size;
    header->chunkOffset = offset;

    if (size > 0 && data) {
      memcpy(chunkBuffer.data() + sizeof(ChunkedTransferHeader), data + offset, size);
    }

    if (!SendPacketReliable(target, type, chunkBuffer.data(), chunkBuffer.size())) {
      Overlay::Log("[ERR] Failed to send chunk %u of %u to %llu", i, totalChunks, target.ConvertToUint64());
    }
  }
}

void NetworkManager::BeginMultiplayerSave() {
  if (!IsHost()) {
    Overlay::Log("[ERR] Only host can begin multiplayer save.");
    return;
  }
  if (!m_CurrentLobby.IsValid()) {
    Overlay::Log("[ERR] Not in a lobby.");
    return;
  }

  Overlay::Log("[SAVE] Initiating multiplayer save sync protocol...");

  const int totalCats = GetTotalLobbyCatCount();
  if (totalCats != GameUtils::g_customTeamSize) {
    Overlay::Log("[ERR] Cannot depart: total cat count (%d) must match configured Team Size (%d)!", totalCats, GameUtils::g_customTeamSize);
    return;
  }

  m_saveSyncState = SaveSyncState::WaitingForCatResponses;
  m_collectedCatBlobs.clear();
  m_collectedUnlocksBlobs.clear();
  m_collectedInventoryBlobs.clear();
  m_collectedMapFlags.clear();
  m_pendingCatResponseFrom.clear();
  m_clientTransfers.clear();

  // Gather lobby members (excluding host)
  const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  const CSteamID myID = SteamUser()->GetSteamID();
  for (int i = 0; i < numMembers; i++) {
    CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    if (member != myID) {
      m_pendingCatResponseFrom.insert(member.ConvertToUint64());
    }
  }

  // Read host's own cats from their active save file
  const MewDirector* director = GameUtils::GetMewDirectorSingleton();
  void* activeDb = director ? director->sqlSaveFile.db : nullptr;

  // Get host's ButchBox cat keys
  const std::vector<int64_t> hostKeys = GetButchBoxCatKeys();
  Overlay::Log("[SAVE] Host found %zu cats in their ButchBox.", hostKeys.size());

  if (activeDb) {
    glaiel::SQLSaveFile tempDb = {};
    tempDb.db = activeDb;

    for (const int64_t key : hostKeys) {
      ParaboxAPI::Array<uint8_t> blob = MewSQL::ReadBlobFromDatabase(&tempDb, "cats", key);
      if (!blob.empty()) {
        const size_t blobSize = blob.size();
        PendingCatBlob pending;
        pending.senderSteamID = myID.ConvertToUint64();
        pending.sqlKey = key;
        
        std::vector<uint8_t> stdBlob(blob.begin(), blob.end());
        pending.data = std::move(stdBlob);
        pending.originalAge = GetButchBoxCatAge(key);
        m_collectedCatBlobs.push_back(std::move(pending));
        Overlay::Log("[SAVE] Gathered host cat key %lld (size %zu, age %d)", key, blobSize, pending.originalAge);
      } else {
        Overlay::Log("[ERR] Failed to read blob for host cat key %lld", key);
      }
    }

    ParaboxAPI::Array<uint8_t> unlocksBlob = MewSQL::ReadBlobFromDatabaseStr(&tempDb, "files", "unlocks");
    m_collectedUnlocksBlobs.push_back(unlocksBlob.to_vector());

    ParaboxAPI::Array<uint8_t> invBlob = MewSQL::ReadBlobFromDatabaseStr(&tempDb, "files", "inventory_storage");
    m_collectedInventoryBlobs.push_back(invBlob.to_vector());

    std::vector<std::string> hostFlags;
    const auto hostFlagsMap = MewSQL::QueryMapFlags(&tempDb);
    for (const auto& flag : hostFlagsMap) {
        if (flag.value == 1 && flag.key.to_string() != "mapflag_TutorialUnlocked" && flag.key.to_string() != "mapflag_TutorialDone") {
            hostFlags.push_back(flag.key.to_string());
        }
    }
    m_collectedMapFlags.push_back(std::move(hostFlags));

  } else {
    Overlay::Log("[ERR] Host active save database connection is null!");
  }

  // 3. Send SaveCatRequest to all clients
  if (!m_pendingCatResponseFrom.empty()) {
    constexpr uint32_t requestVal = 0;
    for (const uint64_t clientID : m_pendingCatResponseFrom) {
      SendPacketReliable(CSteamID(clientID), PacketType::SaveCatRequest, &requestVal, sizeof(requestVal));
      Overlay::Log("[SAVE] Requested cats from client %llu", clientID);
    }
  } else {
    Overlay::Log("[SAVE] No clients in lobby. Building save file directly.");
    BuildAndDistributeSave();
  }
}

SaveSyncState NetworkManager::GetSaveSyncState() const { return m_saveSyncState; }

void NetworkManager::HandleSaveCatRequest(const CSteamID remoteID) {
  Overlay::Log("[SAVE] Received SaveCatRequest from host %llu", remoteID.ConvertToUint64());

  const MewDirector* director = GameUtils::GetMewDirectorSingleton();
  void* activeDb = director ? director->sqlSaveFile.db : nullptr;

  const std::vector<int64_t> clientKeys = GetButchBoxCatKeys();
  Overlay::Log("[SAVE] Client found %zu cats in ButchBox.", clientKeys.size());

  std::vector<uint8_t> responseBuffer;
  responseBuffer.resize(sizeof(CatResponseHeader)); // Reserve header

  const CSteamID myID = SteamUser()->GetSteamID();

  if (activeDb) {
    uint32_t numCats = 0;
    glaiel::SQLSaveFile tempDb = {};
    tempDb.db = activeDb;

    for (const int64_t key : clientKeys) {
      ParaboxAPI::Array<uint8_t> blob = MewSQL::ReadBlobFromDatabase(&tempDb, "cats", key);
      if (!blob.empty()) {
        CatBlobHeader header = {};
        header.senderSteamID = myID.ConvertToUint64();
        header.sqlKey = key;
        header.blobSize = blob.size();
        header.originalAge = GetButchBoxCatAge(key);

        // Append header
        const size_t oldSize = responseBuffer.size();
        responseBuffer.resize(oldSize + sizeof(CatBlobHeader) + blob.size());
        memcpy(responseBuffer.data() + oldSize, &header, sizeof(CatBlobHeader));
        memcpy(responseBuffer.data() + oldSize + sizeof(CatBlobHeader), blob.data, blob.size());

        numCats++;
        Overlay::Log("[SAVE] Added cat key %lld (size %zu, age %d) to response buffer.", key, blob.size(), header.originalAge);
      } else {
        Overlay::Log("[ERR] Client failed to read blob for cat key %lld", key);
      }
    }

    ParaboxAPI::Array<uint8_t> unlocksBlob = MewSQL::ReadBlobFromDatabaseStr(&tempDb, "files", "unlocks");
    ParaboxAPI::Array<uint8_t> invBlob = MewSQL::ReadBlobFromDatabaseStr(&tempDb, "files", "inventory_storage");

    std::string mapFlagsStr;
    auto clientFlagsMap = MewSQL::QueryMapFlags(&tempDb);
    for (const auto& pair : clientFlagsMap) {
        if (pair.value == 1 && pair.key.to_string() != "mapflag_TutorialUnlocked" && pair.key.to_string() != "mapflag_TutorialDone") {
            mapFlagsStr += pair.key.to_string();
            mapFlagsStr += '\0';
        }
    }

    auto* hdr = (CatResponseHeader*)responseBuffer.data();
    hdr->numCats = numCats;
    hdr->unlocksSize = unlocksBlob.size();
    hdr->inventorySize = invBlob.size();
    hdr->mapFlagsSize = mapFlagsStr.size();

    const size_t offset = responseBuffer.size();
    responseBuffer.resize(offset + unlocksBlob.size() + invBlob.size() + mapFlagsStr.size());

    if (!unlocksBlob.empty()) {
        memcpy(responseBuffer.data() + offset, unlocksBlob.data, unlocksBlob.size());
    }
    if (!invBlob.empty()) {
        memcpy(responseBuffer.data() + offset + unlocksBlob.size(), invBlob.data, invBlob.size());
    }
    if (!mapFlagsStr.empty()) {
        memcpy(responseBuffer.data() + offset + unlocksBlob.size() + invBlob.size(), mapFlagsStr.c_str(), mapFlagsStr.size());
    }

  } else {
    Overlay::Log("[ERR] Client active save database connection is null!");
  }

  // Send chunked response back to host
  const uint32_t transferId = m_nextTransferId++;
  SendChunkedData(remoteID, PacketType::SaveCatResponse, responseBuffer.data(), responseBuffer.size(), transferId);
}

void NetworkManager::HandleSaveCatResponse(const CSteamID remoteID, const void *data, const uint32_t length) {
  if (length < sizeof(ChunkedTransferHeader)) {
    Overlay::Log("[ERR] SaveCatResponse length too small: %u", length);
    return;
  }

  const auto chunkHdr = (const ChunkedTransferHeader*)data;
  const uint64_t senderID = remoteID.ConvertToUint64();

  // Find or create transfer state for this client
  auto&[transferId, expectedChunks, receivedChunks, buffer, chunkTracker] = m_clientTransfers[senderID];
  if (transferId != chunkHdr->transferId) {
    transferId = chunkHdr->transferId;
    buffer.resize(chunkHdr->totalSize);
    expectedChunks = chunkHdr->totalChunks;
    receivedChunks = 0;
    chunkTracker.assign(chunkHdr->totalChunks, false);
    Overlay::Log("[SAVE] Initialized cat response transfer ID %u from %llu, expected size %u, chunks %u",
                 chunkHdr->transferId, senderID, chunkHdr->totalSize, chunkHdr->totalChunks);
  }

  if (chunkHdr->chunkIndex < chunkTracker.size() && !chunkTracker[chunkHdr->chunkIndex]) {
    chunkTracker[chunkHdr->chunkIndex] = true;
    receivedChunks++;

    if (chunkHdr->chunkSize > 0) {
      memcpy(buffer.data() + chunkHdr->chunkOffset, (const uint8_t*)data + sizeof(ChunkedTransferHeader), chunkHdr->chunkSize);
    }

    if (receivedChunks == expectedChunks) {
      Overlay::Log("[SAVE] Completed cat response transfer from client %llu. Reassembling...", senderID);

      // Parse cats from reassembled buffer
      uint32_t offset = sizeof(CatResponseHeader);
      const uint32_t bufferSize = buffer.size();
      uint32_t parsedCatsCount = 0;
      const auto* respHdr = (CatResponseHeader*)buffer.data();

      for (uint32_t i = 0; i < respHdr->numCats; i++) {
        if (offset + sizeof(CatBlobHeader) > bufferSize) break;
        const auto catHdr = (const CatBlobHeader*)(buffer.data() + offset);
        if (offset + sizeof(CatBlobHeader) + catHdr->blobSize > bufferSize) {
          Overlay::Log("[ERR] Corrupt cat response buffer from %llu: blob size goes out of bounds.", senderID);
          break;
        }

        PendingCatBlob pending;
        pending.senderSteamID = catHdr->senderSteamID;
        pending.sqlKey = catHdr->sqlKey;
        pending.originalAge = catHdr->originalAge;
        pending.data.resize(catHdr->blobSize);
        memcpy(pending.data.data(), buffer.data() + offset + sizeof(CatBlobHeader), catHdr->blobSize);

        m_collectedCatBlobs.push_back(std::move(pending));
        parsedCatsCount++;

        offset += sizeof(CatBlobHeader) + catHdr->blobSize;
      }

      if (offset + respHdr->unlocksSize <= bufferSize) {
        std::vector<uint8_t> unlocksBlob(buffer.data() + offset, buffer.data() + offset + respHdr->unlocksSize);
        m_collectedUnlocksBlobs.push_back(unlocksBlob);
        offset += respHdr->unlocksSize;
      }

      if (offset + respHdr->inventorySize <= bufferSize) {
        std::vector<uint8_t> invBlob(buffer.data() + offset, buffer.data() + offset + respHdr->inventorySize);
        m_collectedInventoryBlobs.push_back(invBlob);
        offset += respHdr->inventorySize;
      }

      std::vector<std::string> flags;
      uint32_t flagsEnd = offset + respHdr->mapFlagsSize;
      while (offset < flagsEnd && offset < bufferSize) {
          std::string f((const char*)(buffer.data() + offset));
          flags.push_back(f);
          offset += f.length() + 1;
      }
      m_collectedMapFlags.push_back(flags);

      Overlay::Log("[SAVE] Successfully parsed %u cats and extra data from client %llu.", parsedCatsCount, senderID);
      m_pendingCatResponseFrom.erase(senderID);

      // If all client responses have been received, build and distribute save file!
      if (m_pendingCatResponseFrom.empty()) {
        Overlay::Log("[SAVE] All client cat responses received. Proceeding to build save.");
        BuildAndDistributeSave();
      }
    }
  }
}

void NetworkManager::BuildAndDistributeSave() {
  if (m_collectedCatBlobs.size() != GameUtils::g_customTeamSize) {
    Overlay::Log("[ERR] Aborting depart: gathered %zu cats, but configured Team Size is %d!",
                 m_collectedCatBlobs.size(), GameUtils::g_customTeamSize);
    m_saveSyncState = SaveSyncState::Idle;
    return;
  }

  Overlay::Log("[SAVE] Building synchronized %s...", CUSTOM_SAVE_NAME.c_str());

  // Create multiplayer save file
  GameUtils::CreateMewtiplayerSave(CUSTOM_SAVE_NAME.c_str());

  // Open it and write all collected cat blobs, and merge extra data
  if (glaiel::SQLSaveFile* db = MewSQL::OpenSaveDatabase(CUSTOM_SAVE_NAME.c_str())) {
    {
      std::vector<ParaboxAPI::Array<ParaboxAPI::String>> convertedFlags;
      for (const auto& flags : m_collectedMapFlags) {
        std::vector<ParaboxAPI::String> inner;
        for (const auto& f : flags) inner.push_back(ParaboxAPI::MakeString(f));
        convertedFlags.push_back(ParaboxAPI::MakeArray(inner));
      }
      GameUtils::MergeMapFlags(db, ParaboxAPI::MakeArray(convertedFlags));
    }

    {
      std::vector<ParaboxAPI::Array<uint8_t>> convertedUnlocks;
      for (const auto& blob : m_collectedUnlocksBlobs) convertedUnlocks.push_back(ParaboxAPI::MakeArray(blob));
      GameUtils::MergeUnlocksBlobs(db, ParaboxAPI::MakeArray(convertedUnlocks));
    }

    {
      std::vector<ParaboxAPI::Array<uint8_t>> convertedInv;
      for (const auto& blob : m_collectedInventoryBlobs) convertedInv.push_back(ParaboxAPI::MakeArray(blob));
      GameUtils::MergeInventoryBlobs(db, ParaboxAPI::MakeArray(convertedInv));
    }

    Overlay::Log("[SAVE] Inserting %zu cats into %s...", m_collectedCatBlobs.size(), CUSTOM_SAVE_NAME.c_str());

    for (size_t i = 0; i < m_collectedCatBlobs.size(); ++i) {
      const auto& cat = m_collectedCatBlobs[i];

      // Convert bytes to Hex
      std::string hexStr;
      hexStr.reserve(cat.data.size() * 2);
      static constexpr char hexChars[] = "0123456789ABCDEF";
      for (const uint8_t b : cat.data) {
        hexStr.push_back(hexChars[b >> 4]);
        hexStr.push_back(hexChars[b & 0x0F]);
      }

      std::string query = "INSERT OR REPLACE INTO cats VALUES (" + std::to_string(i + 1) + ", X'" + hexStr + "');";
      MewSQL::ExecSQLOnDatabase(db, query.c_str());

      // Write original age to properties table
      // We'll use it later to restore their original age once we send them back to their original saves
      std::string ageQuery = "INSERT OR REPLACE INTO properties VALUES ('cat_original_age_" + std::to_string(i + 1) + "', " + std::to_string(cat.originalAge) + ");";
      MewSQL::ExecSQLOnDatabase(db, ageQuery.c_str());

      std::string ownerQuery = "INSERT OR REPLACE INTO properties VALUES ('cat_owner_steamid_" +
                               std::to_string(i + 1) + "', " + std::to_string(cat.senderSteamID) + ");";
      MewSQL::ExecSQLOnDatabase(db, ownerQuery.c_str());
      Overlay::Log("[SAVE] Wrote original age %d for cat %zu into properties", cat.originalAge, i + 1);
    }

    // Add house storage upgrades to accommodate the merged inventory
    MewSQL::ExecSQLOnDatabase(db, "INSERT OR REPLACE INTO properties VALUES ('house_storage_upgrades', 5000);");

    MewSQL::CloseSaveDatabase(db);
    Overlay::Log("[SAVE] Finished inserting cats into %s.", CUSTOM_SAVE_NAME.c_str());
  } else {
    Overlay::Log("[ERR] Failed to open %s database for inserting cats!", CUSTOM_SAVE_NAME.c_str());
  }

  // Read raw bytes of the newly built save file to send to clients
  ParaboxAPI::Array<uint8_t> saveBytesArray = MewSQL::ReadSaveFileRaw(CUSTOM_SAVE_NAME.c_str());
  const std::vector<uint8_t> saveBytes = saveBytesArray.to_vector();
  if (saveBytes.empty()) {
    Overlay::Log("[ERR] Synchronized %s is empty or cannot be read!", CUSTOM_SAVE_NAME.c_str());
    return;
  }
  Overlay::Log("[SAVE] Synchronized save size: %zu bytes", saveBytes.size());

  // Send save file to all clients in the lobby
  m_saveSyncState = SaveSyncState::SendingSaveFile;
  m_pendingAcksFrom.clear();

  const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  const CSteamID myID = SteamUser()->GetSteamID();
  const uint32_t transferId = m_nextTransferId++;

  for (int i = 0; i < numMembers; i++) {
    CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    if (member != myID) {
      m_pendingAcksFrom.insert(member.ConvertToUint64());
      SendChunkedData(member, PacketType::SaveFileTransfer, saveBytes.data(), saveBytes.size(), transferId);
      Overlay::Log("[SAVE] Sent save file transfer to client %llu", member.ConvertToUint64());
    }
  }

  if (!m_pendingAcksFrom.empty()) {
    m_saveSyncState = SaveSyncState::WaitingForAcks;
  } else {
    // No clients to wait for, host can load immediately
    Overlay::Log("[SAVE] No clients to wait for. Sending Load Signal directly.");
    m_saveSyncState = SaveSyncState::Ready;

    SaveLoadSignalPacket packet = {};
    packet.teamSize = GameUtils::g_customTeamSize;
    packet.difficulty = GameUtils::g_customDifficulty;
    packet.collarIndex = 4;

    std::string collarStr;
    for (size_t i = 0; i < GameUtils::g_customCollarClasses.size(); ++i) {
        collarStr += GameUtils::g_customCollarClasses[i].c_str();
        if (i < GameUtils::g_customCollarClasses.size() - 1) {
            collarStr += ", ";
        }
    }
    strncpy(packet.customCollars, collarStr.c_str(), sizeof(packet.customCollars) - 1);

    HandleSaveLoadSignal(&packet, sizeof(packet));
  }
}

void NetworkManager::HandleSaveFileTransfer(const CSteamID remoteID, const void *data, const uint32_t length) {
  if (length < sizeof(ChunkedTransferHeader)) {
    Overlay::Log("[ERR] SaveFileTransfer length too small: %u", length);
    return;
  }

  const auto chunkHdr = (const ChunkedTransferHeader*)data;

  if (m_clientSaveTransferId != chunkHdr->transferId) {
    m_clientSaveTransferId = chunkHdr->transferId;
    m_receivedSaveBuffer.resize(chunkHdr->totalSize);
    m_expectedSaveSize = chunkHdr->totalSize;
    m_expectedSaveChunks = chunkHdr->totalChunks;
    m_receivedSaveChunks = 0;
    m_receivedSaveChunkTracker.assign(chunkHdr->totalChunks, false);
    m_saveSyncState = SaveSyncState::ReceivingSaveFile;
    Overlay::Log("[SAVE] Client: Initialized save file transfer ID %u, size %u, chunks %u",
                 chunkHdr->transferId, chunkHdr->totalSize, chunkHdr->totalChunks);
  }

  if (chunkHdr->chunkIndex < m_receivedSaveChunkTracker.size() && !m_receivedSaveChunkTracker[chunkHdr->chunkIndex]) {
    m_receivedSaveChunkTracker[chunkHdr->chunkIndex] = true;
    m_receivedSaveChunks++;

    if (chunkHdr->chunkSize > 0) {
      memcpy(m_receivedSaveBuffer.data() + chunkHdr->chunkOffset, (const uint8_t*)data + sizeof(ChunkedTransferHeader), chunkHdr->chunkSize);
    }

    if (m_receivedSaveChunks == m_expectedSaveChunks) {
      Overlay::Log("[SAVE] Client: Completed save file transfer. Writing to %s...", CUSTOM_SAVE_NAME.c_str());

      MewSQL::DeleteSaveFile(CUSTOM_SAVE_NAME.c_str());

      if (MewSQL::WriteSaveFileRaw(CUSTOM_SAVE_NAME.c_str(), m_receivedSaveBuffer.data(), m_receivedSaveBuffer.size())) {
        Overlay::Log("[SAVE] Client: Successfully wrote %s. Sending Ack to host.", CUSTOM_SAVE_NAME.c_str());

        constexpr uint32_t ackVal = 1;
        SendPacketReliable(remoteID, PacketType::SaveFileAck, &ackVal, sizeof(ackVal));
      } else {
        Overlay::Log("[ERR] Client: Failed to write %s!", CUSTOM_SAVE_NAME.c_str());
      }
    }
  }
}

void NetworkManager::HandleSaveFileAck(const CSteamID remoteID) {
  const uint64_t senderID = remoteID.ConvertToUint64();
  Overlay::Log("[SAVE] Received SaveFileAck from client %llu", senderID);

  m_pendingAcksFrom.erase(senderID);

  if (m_pendingAcksFrom.empty() && m_saveSyncState == SaveSyncState::WaitingForAcks) {
    Overlay::Log("[SAVE] All client acks received! Signaling load save file...");
    m_saveSyncState = SaveSyncState::Ready;

    SaveLoadSignalPacket packet = {};
    memset(&packet, 0, sizeof(packet));
    packet.teamSize = GameUtils::g_customTeamSize;
    packet.difficulty = GameUtils::g_customDifficulty;
    packet.collarIndex = 4;

    std::string collarStr;
    for (size_t i = 0; i < GameUtils::g_customCollarClasses.size(); ++i) {
        collarStr += GameUtils::g_customCollarClasses[i].c_str();
        if (i < GameUtils::g_customCollarClasses.size() - 1) {
            collarStr += ",";
        }
    }
    strncpy(packet.customCollars, collarStr.c_str(), sizeof(packet.customCollars) - 1);
    packet.customCollars[sizeof(packet.customCollars) - 1] = '\0';

    const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
    const CSteamID myID = SteamUser()->GetSteamID();
    for (int i = 0; i < numMembers; i++) {
      CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
      if (member != myID) {
        SendPacketReliable(member, PacketType::SaveLoadSignal, &packet, sizeof(packet));
      }
    }

    HandleSaveLoadSignal(&packet, sizeof(packet));
  }
}

void NetworkManager::HandleSaveLoadSignal(const void *data, const uint32_t length) {
  Overlay::Log("[SAVE] Received SaveLoadSignal. Initiating run loading flow...");

  uint32_t teamSize = GameUtils::g_customTeamSize;
  uint32_t difficulty = GameUtils::g_customDifficulty;

  if (length == sizeof(SaveLoadSignalPacket)) {
    const auto packet = (const SaveLoadSignalPacket*)data;
    teamSize = packet->teamSize;
    difficulty = packet->difficulty;
  }

  // Set custom run globals so that StartCustomRun uses them
  GameUtils::g_customTeamSize = teamSize; // NOLINT(*-narrowing-conversions)
  GameUtils::g_customDifficulty = difficulty; // NOLINT(*-narrowing-conversions)
  // GameUtils::g_customCollarIndex = collarIndex; // Deprecated

  if (length == sizeof(SaveLoadSignalPacket)) {
    const auto* packet = (const SaveLoadSignalPacket*)data;
    if (packet->customCollars[0] != '\0') {
      GameUtils::SetCustomCollarClasses(packet->customCollars);
      GameUtils::g_useCustomCollarClasses = true;
    }
  }

  // Initiate save file loading sequence
  GameUtils::g_oldDirector = GameUtils::GetMewDirectorSingleton();
  GameUtils::g_startCustomRunPending = true;

  GameUtils::LoadSaveFile(CUSTOM_SAVE_NAME.c_str());

  // Reset sync state to Idle
  m_saveSyncState = SaveSyncState::Idle;
}

int NetworkManager::GetTotalLobbyCatCount() {
  if (!m_CurrentLobby.IsValid()) return 0;

  m_lobbyMemberCatCounts[SteamUser()->GetSteamID().ConvertToUint64()] = GetLocalButchBoxCatCount();

  int total = 0;
  int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  for (int i = 0; i < numMembers; i++) {
    CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    auto it = m_lobbyMemberCatCounts.find(member.ConvertToUint64());
    if (it != m_lobbyMemberCatCounts.end()) {
      total += it->second;
    }
  }
  return total;
}

int NetworkManager::GetLobbyMemberCatCount(const uint64_t steamID) {
  if (steamID == SteamUser()->GetSteamID().ConvertToUint64()) {
    return GetLocalButchBoxCatCount();
  }
  const auto it = m_lobbyMemberCatCounts.find(steamID);
  if (it != m_lobbyMemberCatCounts.end()) {
    return it->second;
  }
  return 0;
}

void NetworkManager::SendLocalCatCount() {
  if (!m_CurrentLobby.IsValid()) return;
  ButchBoxCatCountPacket packet = {};
  packet.catCount = GetLocalButchBoxCatCount();

  m_lobbyMemberCatCounts[SteamUser()->GetSteamID().ConvertToUint64()] = packet.catCount; // NOLINT(*-narrowing-conversions)

  int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  CSteamID myID = SteamUser()->GetSteamID();
  for (int i = 0; i < numMembers; i++) {
    CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    if (member != myID) {
      SendPacketReliable(member, PacketType::ButchBoxCatCountSync, &packet, sizeof(packet));
    }
  }
}

void NetworkManager::HandleButchBoxCatCountSync(const CSteamID remoteID, const void *data, const uint32_t length) {
  if (length == sizeof(ButchBoxCatCountPacket)) {
    const auto packet = (const ButchBoxCatCountPacket*)data;
    m_lobbyMemberCatCounts[remoteID.ConvertToUint64()] = packet->catCount; // NOLINT(*-narrowing-conversions)
  }
}

void NetworkManager::HandleCollarSync(const void *data, const uint32_t length) {
  extern void HandleCollarSyncInternal(const void *data, const uint32_t length);
  HandleCollarSyncInternal(data, length);
}

void NetworkManager::HandleLobbyReady(const void *data, const uint32_t length) {
  if (length != sizeof(LobbyReadyPacket)) {
    return;
  }

  extern std::map<uint64_t, bool> g_lobbyReadyStates;
  const auto *packet = (const LobbyReadyPacket *)data;
  g_lobbyReadyStates[packet->steamID] = packet->isReady;
  Overlay::Log("[LOBBY] Player %llu ready state: %s", packet->steamID,
               packet->isReady ? "locked in" : "not ready");

  extern bool AreAllLobbyMembersReady();
  if (IsHost() && AreAllLobbyMembersReady()) {
    BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
    extern void CatSelectorHooks_TriggerLockInProceed();
    CatSelectorHooks_TriggerLockInProceed();
  }
}

void NetworkManager::HandleLobbyProceed() const {
  if (!IsHost()) {
    extern void CatSelectorHooks_TriggerLockInProceed();
    CatSelectorHooks_TriggerLockInProceed();
  }
}

void NetworkManager::HandleStorageItemSync(const void *data, const uint32_t length) {
  extern void HandleStorageItemSyncInternal(const void *data, const uint32_t length);
  HandleStorageItemSyncInternal(data, length);
}

void NetworkManager::HandleMapNodeSync(const void *data, const uint32_t length) {
  if (length != sizeof(MapNodeSyncPacket)) {
    return;
  }
  const auto *packet = (const MapNodeSyncPacket *)data;
  Overlay::Log("[MAP] Received MapNodeSync: index %u", packet->nodeIndex);

  extern void TriggerMapNodeSync(uint32_t nodeIndex);
  TriggerMapNodeSync(packet->nodeIndex);
}

void NetworkManager::HandleActSelectSync(const void *data, const uint32_t length) {
  if (length != sizeof(ActSelectPacket)) {
    return;
  }
  const auto *pkt = (const ActSelectPacket *)data;
  Overlay::Log("[ACT] Received ActSelectSync: actIndex %u", pkt->actIndex);
  if (GameUtils::IsComponentValid(ParaboxAPI::GetActSelectionScreen())) {
    extern void TriggerActSelect(uint32_t actIndex);
    TriggerActSelect(pkt->actIndex);
  }
}

void NetworkManager::HandleMapInventoryOpen(const void *data, const uint32_t length) {
  if (length != sizeof(MapInventoryOpenPacket)) return;
  Overlay::Log("[NETWORK] Received MapInventoryOpen");
  extern void ForceMapInventoryOpen();
  ForceMapInventoryOpen();
}

void NetworkManager::HandleMapInventoryClose(const void *data, const uint32_t length) {
  if (length != sizeof(MapInventoryClosePacket)) return;
  Overlay::Log("[NETWORK] Received MapInventoryClose");
  extern void *g_activeInventoryScreenThis;
  
  if (g_activeInventoryScreenThis) {
    ParaboxAPI::ForceInventoryScreen2Close(g_activeInventoryScreenThis);
    g_activeInventoryScreenThis = nullptr;
  } else {
    for (const auto* scene : GameUtils::GetCurrentScenes()) {
      if (!scene) continue;
      if (void* comp = GameUtils::FindComponentByTypeName(scene, "InventoryScreen2")) {
        ParaboxAPI::ForceInventoryScreen2Close(comp);
        break;
      }
    }
  }
}

void NetworkManager::HandleLevelUpSelectOption(const void *data, const uint32_t length) {
  if (length != sizeof(LevelUpSelectOptionPacket)) {
    return;
  }
  const auto *packet = (const LevelUpSelectOptionPacket *)data;
  Overlay::Log("[LEVELUP] Received LevelUpSelectOption: cat UID %lld, optionIndex %u", packet->catUID, packet->optionIndex);

  extern void TriggerLevelUpSelectOption(int64_t catUID, uint32_t optionIndex);
  TriggerLevelUpSelectOption(packet->catUID, packet->optionIndex);
}

void NetworkManager::HandleLevelUpReroll(const void *data, const uint32_t length) {
  if (length != sizeof(LevelUpRerollPacket)) {
    return;
  }
  const auto *packet = (const LevelUpRerollPacket *)data;
  Overlay::Log("[LEVELUP] Received LevelUpReroll: cat UID %lld", packet->catUID);

  extern void TriggerLevelUpReroll(int64_t catUID);
  TriggerLevelUpReroll(packet->catUID);
}

void NetworkManager::HandleAbilityReplace(const void *data, const uint32_t length) {
  if (length != sizeof(AbilityReplacePacket)) {
    return;
  }
  const auto *packet = (const AbilityReplacePacket *)data;
  Overlay::Log("[LEVELUP] Received AbilityReplace: cat UID %lld, slotIndex %u", packet->catUID, packet->slotIndex);

  extern void TriggerAbilityReplace(int64_t catUID, uint32_t slotIndex);
  TriggerAbilityReplace(packet->catUID, packet->slotIndex);
}

void NetworkManager::HandleWorldEventSelectOption(const void *data, const uint32_t length) {
  if (length != sizeof(WorldEventSelectOptionPacket)) {
    return;
  }
  const auto *packet = (const WorldEventSelectOptionPacket *)data;
  Overlay::Log("[WORLDEVENT] Received WorldEventSelectOption: cat UID %lld, optionIndex %u", packet->catUID, packet->optionIndex);

  extern void TriggerWorldEventSelectOption(int64_t catUID, uint32_t optionIndex);
  TriggerWorldEventSelectOption(packet->catUID, packet->optionIndex);
}

void NetworkManager::HandleWorldEventSelectCat(const void *data, const uint32_t length) {
  if (length != sizeof(WorldEventSelectCatPacket)) {
    return;
  }
  const auto *packet = (const WorldEventSelectCatPacket *)data;
  Overlay::Log("[WORLDEVENT] Received WorldEventSelectCat: selectedCatUID %lld", packet->selectedCatUID);

  extern void TriggerWorldEventSelectCat(int64_t selectedCatUID);
  TriggerWorldEventSelectCat(packet->selectedCatUID);
}

void NetworkManager::HandleWorldEventClickEnd(const void *data, const uint32_t length) {
  if (length != sizeof(WorldEventClickEndPacket)) {
    return;
  }
  const auto *packet = (const WorldEventClickEndPacket *)data;
  Overlay::Log("[WORLDEVENT] Received WorldEventClickEnd: buttonType %u", packet->buttonType);

  extern void TriggerWorldEventClickEnd(uint8_t buttonType);
  TriggerWorldEventClickEnd(packet->buttonType);
}

#include "hooks/ShopHooks.h"

void NetworkManager::HandleShopBuyItem(const void* data, uint32_t length) {
  if (length != sizeof(ShopBuyItemPacket)) return;
  const auto *packet = (const ShopBuyItemPacket *)data;
  Overlay::Log("[SHOP] Received ShopBuyItem: index %u", packet->itemIndex);
  ParaboxAPI::ForceShopBuyItem(packet->itemIndex);
}

void NetworkManager::HandleShopExitButton(const void* data, uint32_t length) {
  if (length != sizeof(ShopExitButtonPacket)) return;
  Overlay::Log("[SHOP] Received ShopExitButton");
  ParaboxAPI::ForceShopExitButton();
}



void NetworkManager::HandleShopChestClick(const void* data, uint32_t length) {
  if (length != sizeof(ShopChestClickPacket)) return;
  Overlay::Log("[SHOP] Received ShopChestClick");
  ParaboxAPI::ForceShopChestClick();
}
