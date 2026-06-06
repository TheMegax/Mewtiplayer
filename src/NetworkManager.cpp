// ReSharper disable CppMemberFunctionMayBeStatic
#include "NetworkManager.h"
#include "SteamABICompat.h"
#include "GameUtils.h"
#include "ImGuiHook.h"
#include "InputGhost.h"
#include "Overlay.h"
#include "mewjector.h"
#include "MewSQL.h"
#include "hooks/AdventureBoxHooks.h"
#include <cstring>
#include <algorithm>

typedef void(__fastcall *FaceDirection_t)(void *character,
                                          uint64_t target_packed,
                                          bool play_animation, bool force);
extern FaceDirection_t g_origFaceDirection;

void NetworkManager::Init(MewjectorAPI *mj, const char *modID) {
  m_mj = mj;
  m_ModID = modID;
  if (SteamAPI_Init()) {
    Overlay::Log("SteamAPI initialized successfully!");
  } else {
    Overlay::Log("SteamAPI failed to initialize. Make sure Steam is running.");
  }
}

void NetworkManager::Update() {
  SteamAPI_RunCallbacks();
  ReceivePackets();

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
      Overlay::Log("Received Ping from %llu", remoteID.ConvertToUint64());
      break;
    case PacketType::Handshake:
      HandleHandshake(remoteID, payload, payloadLen);
      break;
    case PacketType::RNGSync:
      HandleRNGSync(remoteID, payload, payloadLen);
      break;
    case PacketType::MouseMove:
      HandleMouseMove(remoteID, payload, payloadLen);
      break;
    case PacketType::CatOwnershipSync:
      HandleCatOwnershipSync(remoteID, payload, payloadLen);
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
    default:
      Overlay::Log("Received unknown packet type %u from %llu", hdr->type,
                   remoteID.ConvertToUint64());
      break;
    }
  }
}

void NetworkManager::HandleHandshake(CSteamID remoteID, const void *data,
                                     uint32_t length) {
  Overlay::Log("Received Handshake from %llu", remoteID.ConvertToUint64());
  if (SteamMatchmaking()->GetLobbyOwner(m_CurrentLobby) ==
      SteamUser()->GetSteamID()) {
    uint8_t rngState[32];
    GameUtils::GetRNGState(rngState);
    SendPacket(remoteID, PacketType::RNGSync, rngState, 32);
    Overlay::Log("Sent RNG state to %llu", remoteID.ConvertToUint64());
  }
}

void NetworkManager::HandleRNGSync(CSteamID remoteID, const void *data,
                                   const uint32_t length) {
  if (length == 32) {
    GameUtils::SetRNGState(data);
    Overlay::Log("RNG state synchronized with host.");
  } else {
    Overlay::Log("Received invalid RNGSync packet (length %u)", length);
  }
}

void NetworkManager::HandleMouseMove(CSteamID remoteID, const void *data,
                                     const uint32_t length) {
  if (length == sizeof(MouseMoveData)) {
    const MouseMoveData *move = (MouseMoveData *)data;
    if (IsHost()) {
      BroadcastPacket(PacketType::MouseMove, move, sizeof(MouseMoveData), true);
    }

    const uint64_t actualSender = move->steamID;

    // Only show the ghost cursor and name tag if it's NOT the local player
    if (actualSender != SteamUser()->GetSteamID().ConvertToUint64()) {
      // Only simulate the move into the engine if we are NOT focused
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

void NetworkManager::HandleCatOwnershipSync(CSteamID remoteID, const void *data,
                                            const uint32_t length) {
  if (length == sizeof(CatOwnershipData)) {
    const auto sync = (const CatOwnershipData *)data;
    m_catOwnership[sync->catUID] = sync->ownerSteamID;
    const char *name = SteamFriends()->GetFriendPersonaName(sync->ownerSteamID);
    Overlay::Log("Ownership Sync: Cat %lld is now owned by %s", sync->catUID,
                 name ? name : "Unknown");
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

  const Character *activeChar = GetCharacter(m_activeNUID);
  if (!activeChar || !activeChar->persistentChar) {
    return steamID != GetHostID().ConvertToUint64();
  }

  const auto it = m_catOwnership.find(activeChar->persistentChar->sql_key);
  if (it == m_catOwnership.end()) {
    // If nobody owns this cat, only the host can move it
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
  BroadcastPacket(PacketType::CatOwnershipSync, &data, sizeof(data),
                  false); // Include self to update map
}

void NetworkManager::SetActiveNUID(const uint32_t nuid) {
  if (nuid != 0xFFFFFFFF) {
    m_combatActive = true;
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
      Overlay::Log("Combat Started (Host)");
    } else {
      Overlay::Log("Combat Started (Client)");
    }
  }
}

void NetworkManager::EndCombat() {
  if (m_combatActive) {
    Overlay::Log("Combat ended, unblocking input.");
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

void NetworkManager::HostLobby(const char *lobbyName) {
  m_PendingLobbyName = lobbyName;
  Overlay::Log("Creating Steam Lobby '%s'...", lobbyName);
  const SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, 4);
  m_LobbyCreatedCallResult.Set(call, this, &NetworkManager::OnLobbyCreated);
}

void NetworkManager::LeaveLobby() {
  if (m_CurrentLobby.IsValid()) {
    Overlay::Log("Leaving lobby %llu...", m_CurrentLobby.ConvertToUint64());
    SteamMatchmaking()->LeaveLobby(m_CurrentLobby);
    m_CurrentLobby.Clear();
    m_combatActive = false;
    m_activeNUID = 0xFFFFFFFF;
    m_catOwnership.clear();
    m_discoveredCats.clear();
    m_lobbyMemberCatCounts.clear();
    RefreshLobbyList();
  }
}

void NetworkManager::JoinLobby(const CSteamID lobbyID) {
  Overlay::Log("Joining lobby %llu...", lobbyID.ConvertToUint64());
  const SteamAPICall_t call = SteamMatchmaking()->JoinLobby(lobbyID);
  m_LobbyEnterCallResult.Set(call, this, &NetworkManager::OnLobbyEnter);
}

void NetworkManager::JoinAnyLobby() {
  Overlay::Log("Searching for lobbies...");
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

void NetworkManager::OnLobbyCreated(LobbyCreated_t *pCallback,
                                    const bool bIOFailure) {
  if (bIOFailure || pCallback->m_eResult != k_EResultOK) {
    Overlay::Log("[ERR] Failed to create lobby (Result: %d)",
                 pCallback->m_eResult);
    return;
  }
  m_CurrentLobby = CSteamID(pCallback->m_ulSteamIDLobby);
  m_combatActive = false;
  m_activeNUID = 0xFFFFFFFF;
  m_catOwnership.clear();
  m_discoveredCats.clear();

  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "name",
                                   m_PendingLobbyName.c_str());
  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "mewtiplayer",
                                   m_ModID.c_str());
  Overlay::Log("[OK] Lobby created: %llu", m_CurrentLobby.ConvertToUint64());
}

void NetworkManager::OnLobbyEnter(LobbyEnter_t *pCallback, const bool bIOFailure) {
  if (bIOFailure ||
      pCallback->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
    Overlay::Log("[ERR] Failed to join lobby (Response: %d)",
                 pCallback->m_EChatRoomEnterResponse);
    return;
  }
  m_CurrentLobby = CSteamID(pCallback->m_ulSteamIDLobby);
  m_combatActive = false;
  m_activeNUID = 0xFFFFFFFF;
  m_catOwnership.clear();
  m_discoveredCats.clear();
  m_lobbyMemberCatCounts.clear();

  Overlay::Log("[OK] Joined lobby: %llu", m_CurrentLobby.ConvertToUint64());

  // Send handshake to host
  SendPacket(GetHostID(), PacketType::Handshake, nullptr, 0);
}

void NetworkManager::OnLobbyMatchList(LobbyMatchList_t *pCallback,
                                      bool bIOFailure) {
  m_LobbyList.clear();
  for (uint32 i = 0; i < pCallback->m_nLobbiesMatching; i++) {
    const CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i);
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

void NetworkManager::OnP2PSessionRequest(P2PSessionRequest_t *pCallback) {
  SteamNetworking()->AcceptP2PSessionWithUser(pCallback->m_steamIDRemote);
}

void NetworkManager::OnGameLobbyJoinRequested(
    GameLobbyJoinRequested_t *pCallback) {
  JoinLobby(pCallback->m_steamIDLobby);
}

void NetworkManager::HandleTurnAction(CSteamID remoteID, const void *data,
                                      const uint32_t length) {
  if (length != sizeof(TurnActionPacket))
    return;

  const auto *pkt = (const TurnActionPacket *)data;
  Overlay::Log("[NET] Queuing TurnAction: Type=%d NUID=%u Ability=%s",
               pkt->actionType, pkt->actorNUID, pkt->abilityName);

  ActionPacket action{};
  action.type = PacketType::TurnAction;
  action.data.action = *pkt;

  extern std::deque<ActionPacket> g_pendingInjections;
  g_pendingInjections.push_back(action);
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
      extern FaceDirection_t g_origFaceDirection; // NOLINT(*-redundant-declaration)
      if (g_origFaceDirection) {
        g_origFaceDirection(c, packed, pkt->anim, pkt->force);
      }
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
  extern std::deque<ActionPacket> g_pendingInjections;
  g_pendingInjections.push_back(pkt);
  if (pkt.type == PacketType::TurnAction) {
    Overlay::Log("[REPLAY] Queued action: %s", pkt.data.action.abilityName);
  } else {
    Overlay::Log("[REPLAY] Queued facing: (%d, %d)", pkt.data.facing.nx, pkt.data.facing.ny);
  }
}
void NetworkManager::InitializeEntityMapping() {
  ResetEntityMapping();

  const std::vector<Character *> fighters = GameUtils::GetFighters();
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
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
uint32_t NetworkManager::GetNUID(Character *character) {
  const auto it = m_charToNuid.find(character);
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
  const std::vector<Character *> all = GameUtils::GetFighters();
  for (Character *c : all) {
    if (!c)
      continue;
    if (m_charToNuid.find(c) == m_charToNuid.end()) {
      uint32_t nuid = m_nextNuid++;
      m_charToNuid[c] = nuid;
      m_nuidToChar[nuid] = c;
      Overlay::Log("NUID: Dynamic Map [%d] -> Character %p (%s)", nuid, c,
                   c->name.to_utf8().c_str());
    }
  }
}

void NetworkManager::ResetEntityMapping() {
  m_charToNuid.clear();
  m_nuidToChar.clear();
  m_nextNuid = 0;
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
  void* activeDb = director ? director->sqlSaveFile : nullptr;

  // Get host's ButchBox cat keys
  const std::vector<int64_t> hostKeys = GetButchBoxCatKeys();
  Overlay::Log("[SAVE] Host found %zu cats in their ButchBox.", hostKeys.size());

  if (activeDb) {
    glaiel::SQLSaveFile tempDb = {};
    tempDb.db = activeDb;

    for (const int64_t key : hostKeys) {
      std::vector<uint8_t> blob = MewSQL::ReadBlobFromDatabase(&tempDb, "cats", key);
      if (!blob.empty()) {
        const size_t blobSize = blob.size();
        PendingCatBlob pending;
        pending.senderSteamID = myID.ConvertToUint64();
        pending.sqlKey = key;
        pending.data = std::move(blob);
        pending.originalAge = GetButchBoxCatAge(key);
        m_collectedCatBlobs.push_back(std::move(pending));
        Overlay::Log("[SAVE] Gathered host cat key %lld (size %zu, age %d)", key, blobSize, pending.originalAge);
      } else {
        Overlay::Log("[ERR] Failed to read blob for host cat key %lld", key);
      }
    }
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

void NetworkManager::HandleSaveCatRequest(const CSteamID remoteID) {
  Overlay::Log("[SAVE] Received SaveCatRequest from host %llu", remoteID.ConvertToUint64());

  const MewDirector* director = GameUtils::GetMewDirectorSingleton();
  void* activeDb = director ? director->sqlSaveFile : nullptr;

  const std::vector<int64_t> clientKeys = GetButchBoxCatKeys();
  Overlay::Log("[SAVE] Client found %zu cats in ButchBox.", clientKeys.size());

  std::vector<uint8_t> responseBuffer;
  const CSteamID myID = SteamUser()->GetSteamID();

  if (activeDb) {
    glaiel::SQLSaveFile tempDb = {};
    tempDb.db = activeDb;

    for (const int64_t key : clientKeys) {
      std::vector<uint8_t> blob = MewSQL::ReadBlobFromDatabase(&tempDb, "cats", key);
      if (!blob.empty()) {
        CatBlobHeader header;
        header.senderSteamID = myID.ConvertToUint64();
        header.sqlKey = key;
        header.blobSize = blob.size();
        header.originalAge = GetButchBoxCatAge(key);

        // Append header
        const size_t oldSize = responseBuffer.size();
        responseBuffer.resize(oldSize + sizeof(CatBlobHeader) + blob.size());
        memcpy(responseBuffer.data() + oldSize, &header, sizeof(CatBlobHeader));
        memcpy(responseBuffer.data() + oldSize + sizeof(CatBlobHeader), blob.data(), blob.size());

        Overlay::Log("[SAVE] Added cat key %lld (size %zu, age %d) to response buffer.", key, blob.size(), header.originalAge);
      } else {
        Overlay::Log("[ERR] Client failed to read blob for cat key %lld", key);
      }
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
      uint32_t offset = 0;
      const uint32_t bufferSize = buffer.size();
      uint32_t parsedCatsCount = 0;

      while (offset + sizeof(CatBlobHeader) <= bufferSize) {
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
      Overlay::Log("[SAVE] Successfully parsed %u cats from client %llu.", parsedCatsCount, senderID);
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

  // Open it and write all collected cat blobs
  if (glaiel::SQLSaveFile* db = MewSQL::OpenSaveDatabase(CUSTOM_SAVE_NAME)) {
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
      MewSQL::ExecSQLOnDatabase(db, query);

      // Write original age to properties table
      // We'll use it later to restore their original age once we send them back to their original saves
      std::string ageQuery = "INSERT OR REPLACE INTO properties VALUES ('cat_original_age_" + std::to_string(i + 1) + "', " + std::to_string(cat.originalAge) + ");";
      MewSQL::ExecSQLOnDatabase(db, ageQuery);
      Overlay::Log("[SAVE] Wrote original age %d for cat %zu into properties", cat.originalAge, i + 1);
    }

    MewSQL::CloseSaveDatabase(db);
    Overlay::Log("[SAVE] Finished inserting cats into %s.", CUSTOM_SAVE_NAME.c_str());
  } else {
    Overlay::Log("[ERR] Failed to open %s database for inserting cats!", CUSTOM_SAVE_NAME.c_str());
  }

  // Read raw bytes of the newly built save file to send to clients
  const std::vector<uint8_t> saveBytes = MewSQL::ReadSaveFileRaw(CUSTOM_SAVE_NAME);
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

    SaveLoadSignalPacket packet;
    packet.teamSize = GameUtils::g_customTeamSize;
    packet.difficulty = GameUtils::g_customDifficulty;
    packet.collarIndex = GameUtils::g_customCollarIndex;

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

      MewSQL::DeleteSaveFile(CUSTOM_SAVE_NAME);

      if (MewSQL::WriteSaveFileRaw(CUSTOM_SAVE_NAME, m_receivedSaveBuffer.data(), m_receivedSaveBuffer.size())) {
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

    SaveLoadSignalPacket packet;
    packet.teamSize = GameUtils::g_customTeamSize;
    packet.difficulty = GameUtils::g_customDifficulty;
    packet.collarIndex = GameUtils::g_customCollarIndex;

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
  uint32_t collarIndex = GameUtils::g_customCollarIndex;

  if (length == sizeof(SaveLoadSignalPacket)) {
    const SaveLoadSignalPacket* packet = (const SaveLoadSignalPacket*)data;
    teamSize = packet->teamSize;
    difficulty = packet->difficulty;
    collarIndex = packet->collarIndex;
  }

  // Set custom run globals so that StartCustomRun uses them
  GameUtils::g_customTeamSize = teamSize;
  GameUtils::g_customDifficulty = difficulty;
  GameUtils::g_customCollarIndex = collarIndex;

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
  ButchBoxCatCountPacket packet;
  packet.catCount = GetLocalButchBoxCatCount();

  m_lobbyMemberCatCounts[SteamUser()->GetSteamID().ConvertToUint64()] = packet.catCount;

  int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  CSteamID myID = SteamUser()->GetSteamID();
  for (int i = 0; i < numMembers; i++) {
    CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    if (member != myID) {
      SendPacketReliable(member, PacketType::ButchBoxCatCountSync, &packet, sizeof(packet));
    }
  }
}

void NetworkManager::HandleButchBoxCatCountSync(CSteamID remoteID, const void *data, uint32_t length) {
  if (length == sizeof(ButchBoxCatCountPacket)) {
    const auto packet = (const ButchBoxCatCountPacket*)data;
    m_lobbyMemberCatCounts[remoteID.ConvertToUint64()] = packet->catCount;
  }
}

