#include "NetworkManager.h"
#include "GameUtils.h"
#include "ImGuiHook.h"
#include "InputGhost.h"
#include "Overlay.h"
#include "mewjector.h"
#include <cstdint>
#include <string.h>

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
}

bool NetworkManager::SendPacket(CSteamID target, PacketType type,
                                const void *data, uint32_t size) {
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

    PacketHeader *hdr = (PacketHeader *)buffer.data();
    if (hdr->magic1 != 'M' || hdr->magic2 != 'G')
      continue;

    const void *payload = buffer.data() + sizeof(PacketHeader);
    uint32_t payloadLen = hdr->length;

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
    case PacketType::MouseEvent:
      HandleMouseEvent(remoteID, payload, payloadLen);
      break;
    case PacketType::KeyEvent:
      HandleKeyEvent(remoteID, payload, payloadLen);
      break;
    case PacketType::MouseMove:
      HandleMouseMove(remoteID, payload, payloadLen);
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
                                   uint32_t length) {
  if (length == 32) {
    GameUtils::SetRNGState(data);
    Overlay::Log("RNG state synchronized with host.");
  } else {
    Overlay::Log("Received invalid RNGSync packet (length %u)", length);
  }
}

void NetworkManager::HandleMouseEvent(CSteamID remoteID, const void *data,
                                      uint32_t length) {
  if (length == sizeof(MouseEventData)) {
    MouseEventData *mouse = (MouseEventData *)data;
    if (IsHost()) {
      Overlay::Log("Sync: Received mouse event %u from client, simulating "
                   "and relaying",
                   mouse->type);
      InputGhost::SimulateClick(mouse->type, mouse->x, mouse->y,
                                ImGuiHook::GetHWND());
      BroadcastPacket(PacketType::MouseEvent, mouse, sizeof(MouseEventData),
                      true);
    } else {
      Overlay::Log("Sync: Received mouse event %u from host, simulating",
                   mouse->type);
      InputGhost::SimulateClick(mouse->type, mouse->x, mouse->y,
                                ImGuiHook::GetHWND());
    }
  }
}

void NetworkManager::HandleKeyEvent(CSteamID remoteID, const void *data,
                                    uint32_t length) {
  if (length == sizeof(KeyEventData)) {
    KeyEventData *key = (KeyEventData *)data;
    if (IsHost()) {
      // Process the input, then broadcast to other clients
      InputGhost::SimulateKeyEvent(key->type, key->keycode, key->scancode,
                                   key->mod, key->down, key->repeat);
      BroadcastPacket(PacketType::KeyEvent, key, sizeof(KeyEventData), true);
    } else {
      // Just process the input
      InputGhost::SimulateKeyEvent(key->type, key->keycode, key->scancode,
                                   key->mod, key->down, key->repeat);
    }
  }
}

void NetworkManager::HandleMouseMove(CSteamID remoteID, const void *data,
                                     uint32_t length) {
  if (length == sizeof(MouseMoveData)) {
    MouseMoveData *move = (MouseMoveData *)data;
    if (IsHost()) {
      BroadcastPacket(PacketType::MouseMove, move, sizeof(MouseMoveData), true);
    }

    uint64_t actualSender = move->steamID;

    // Only show the ghost cursor and name tag if it's NOT the local player
    if (actualSender != SteamUser()->GetSteamID().ConvertToUint64()) {
      // Only simulate the move into the engine if we are NOT focused.
      if (GetForegroundWindow() != ImGuiHook::GetHWND()) {
        InputGhost::SimulateMouseMove(move->x, move->y, ImGuiHook::GetHWND());
      }
      Overlay::UpdateRemoteCursor(actualSender, move->x, move->y,
                                  move->cursorType);
    }
  }
}

void NetworkManager::HostLobby(const char *lobbyName) {
  m_PendingLobbyName = lobbyName;
  Overlay::Log("Creating Steam Lobby '%s'...", lobbyName);
  SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, 4);
  m_LobbyCreatedCallResult.Set(call, this, &NetworkManager::OnLobbyCreated);
}

void NetworkManager::LeaveLobby() {
  if (m_CurrentLobby.IsValid()) {
    Overlay::Log("Leaving lobby %llu...", m_CurrentLobby.ConvertToUint64());
    SteamMatchmaking()->LeaveLobby(m_CurrentLobby);
    m_CurrentLobby.Clear();
    RefreshLobbyList();
  }
}

void NetworkManager::JoinLobby(CSteamID lobbyID) {
  Overlay::Log("Joining lobby %llu...", lobbyID.ConvertToUint64());
  SteamAPICall_t call = SteamMatchmaking()->JoinLobby(lobbyID);
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
  SteamAPICall_t call = SteamMatchmaking()->RequestLobbyList();
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
    return CSteamID();
  return SteamMatchmaking()->GetLobbyOwner(m_CurrentLobby);
}

void NetworkManager::BroadcastPacket(PacketType type, const void *data,
                                     uint32_t size, bool excludeSelf) {
  if (!m_CurrentLobby.IsValid())
    return;
  int numMembers = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  CSteamID myID = SteamUser()->GetSteamID();
  for (int i = 0; i < numMembers; i++) {
    CSteamID member =
        SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    if (excludeSelf && member == myID)
      continue;
    SendPacket(member, type, data, size);
  }
}

void NetworkManager::OnLobbyCreated(LobbyCreated_t *pCallback,
                                    bool bIOFailure) {
  if (bIOFailure || pCallback->m_eResult != k_EResultOK) {
    Overlay::Log("[ERR] Failed to create lobby (Result: %d)",
                 pCallback->m_eResult);
    return;
  }
  m_CurrentLobby = CSteamID(pCallback->m_ulSteamIDLobby);
  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "name",
                                   m_PendingLobbyName.c_str());
  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "mewtiplayer",
                                   m_ModID.c_str());
  Overlay::Log("[OK] Lobby created: %llu", m_CurrentLobby.ConvertToUint64());
}

void NetworkManager::OnLobbyEnter(LobbyEnter_t *pCallback, bool bIOFailure) {
  if (bIOFailure ||
      pCallback->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
    Overlay::Log("[ERR] Failed to join lobby (Response: %d)",
                 pCallback->m_EChatRoomEnterResponse);
    return;
  }
  m_CurrentLobby = CSteamID(pCallback->m_ulSteamIDLobby);
  Overlay::Log("[OK] Joined lobby: %llu", m_CurrentLobby.ConvertToUint64());

  // Send handshake to host
  SendPacket(GetHostID(), PacketType::Handshake, nullptr, 0);
}

void NetworkManager::OnLobbyMatchList(LobbyMatchList_t *pCallback,
                                      bool bIOFailure) {
  m_LobbyList.clear();
  for (uint32 i = 0; i < pCallback->m_nLobbiesMatching; i++) {
    CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i);
    LobbyInfo info;
    info.id = lobbyID;
    const char *name = SteamMatchmaking()->GetLobbyData(lobbyID, "name");
    info.name = name ? name : "Unknown Lobby";
    info.memberCount = SteamMatchmaking()->GetNumLobbyMembers(lobbyID);
    info.maxMembers = SteamMatchmaking()->GetLobbyMemberLimit(lobbyID);
    m_LobbyList.push_back(info);
  }
  Overlay::Log("Found %d lobbies.", (int)m_LobbyList.size());

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
