#include "NetworkManager.h"
#include "GameUtils.h"
#include "InputGhost.h"
#include "Overlay.h"
#include "imgui_hook.h"
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

    switch (hdr->type) {
    case PacketType::Ping:
      Overlay::Log("Received Ping from %llu", remoteID.ConvertToUint64());
      break;
    case PacketType::Handshake:
      Overlay::Log("Received Handshake from %llu", remoteID.ConvertToUint64());
      if (SteamMatchmaking()->GetLobbyOwner(m_CurrentLobby) ==
          SteamUser()->GetSteamID()) {
        uint8_t rngState[32];
        GameUtils::GetRNGState(rngState);
        SendPacket(remoteID, PacketType::RNGSync, rngState, 32);
        Overlay::Log("Sent RNG state to %llu", remoteID.ConvertToUint64());
      }
      break;
    case PacketType::RNGSync:
      if (hdr->length == 32) {
        GameUtils::SetRNGState(buffer.data() + sizeof(PacketHeader));
        Overlay::Log("RNG state synchronized with host.");
      } else {
        Overlay::Log("Received invalid RNGSync packet (length %u)",
                     hdr->length);
      }
      break;
    case PacketType::MouseEvent:
      if (hdr->length == sizeof(MouseEventData)) {
        MouseEventData *mouse =
            (MouseEventData *)(buffer.data() + sizeof(PacketHeader));
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
      break;
    default:
      break;
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
  Overlay::Log("Joining Steam Lobby %llu...", lobbyID.ConvertToUint64());
  SteamAPICall_t call = SteamMatchmaking()->JoinLobby(lobbyID);
  m_LobbyEnterCallResult.Set(call, this, &NetworkManager::OnLobbyEnter);
  RefreshLobbyList();
}

void NetworkManager::JoinAnyLobby() {
  m_AutoJoinSearch = true;
  RefreshLobbyList();
}

void NetworkManager::RefreshLobbyList() {
  Overlay::Log("Searching for '%s' lobbies...", m_ModID.c_str());
  SteamMatchmaking()->AddRequestLobbyListStringFilter("mod_id", m_ModID.c_str(),
                                                      k_ELobbyComparisonEqual);
  SteamMatchmaking()->AddRequestLobbyListDistanceFilter(
      k_ELobbyDistanceFilterWorldwide);
  SteamMatchmaking()->AddRequestLobbyListFilterSlotsAvailable(1);
  SteamAPICall_t call = SteamMatchmaking()->RequestLobbyList();
  m_LobbyMatchListCallResult.Set(call, this, &NetworkManager::OnLobbyMatchList);
}

void NetworkManager::OnLobbyCreated(LobbyCreated_t *pCB, bool bIO) {
  if (bIO || pCB->m_eResult != k_EResultOK) {
    Overlay::Log("Failed to create lobby! Error: %d", pCB->m_eResult);
    return;
  }
  m_CurrentLobby = pCB->m_ulSteamIDLobby;
  Overlay::Log("Lobby created: %llu", m_CurrentLobby.ConvertToUint64());
  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "name",
                                   m_PendingLobbyName.c_str());
  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "mod_id", m_ModID.c_str());
}

void NetworkManager::OnLobbyEnter(LobbyEnter_t *pCB, bool bIO) {
  if (bIO || pCB->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
    Overlay::Log("Failed to join lobby! Response: %d",
                 pCB->m_EChatRoomEnterResponse);
    return;
  }
  m_CurrentLobby = pCB->m_ulSteamIDLobby;
  Overlay::Log("Joined lobby: %llu", m_CurrentLobby.ConvertToUint64());

  // If we are not the host, send a handshake to the host to request state
  CSteamID hostID = SteamMatchmaking()->GetLobbyOwner(m_CurrentLobby);
  if (hostID != SteamUser()->GetSteamID()) {
    SendPacket(hostID, PacketType::Handshake, nullptr, 0);
    Overlay::Log("Sent handshake to host %llu", hostID.ConvertToUint64());
  }
}

void NetworkManager::OnLobbyMatchList(LobbyMatchList_t *pCB, bool bIO) {
  if (bIO)
    return;
  m_LobbyList.clear();
  CSteamID myID = SteamUser()->GetSteamID();
  for (int i = 0; i < (int)pCB->m_nLobbiesMatching; i++) {
    CSteamID lobbyID = SteamMatchmaking()->GetLobbyByIndex(i);

    LobbyInfo info;
    info.id = lobbyID;
    const char *name = SteamMatchmaking()->GetLobbyData(lobbyID, "name");
    info.name = (name && strlen(name) > 0) ? name : "Steam Lobby";
    info.memberCount = SteamMatchmaking()->GetNumLobbyMembers(lobbyID);
    info.maxMembers = SteamMatchmaking()->GetLobbyMemberLimit(lobbyID);
    m_LobbyList.push_back(info);

    if (m_AutoJoinSearch) {
      if (lobbyID == m_CurrentLobby)
        continue;
      CSteamID ownerID = SteamMatchmaking()->GetLobbyOwner(lobbyID);
      if (ownerID == myID)
        continue;

      JoinLobby(lobbyID);
      m_AutoJoinSearch = false; // Reset flag
      return;
    }
  }

  if (m_AutoJoinSearch) {
    Overlay::Log("No suitable lobbies found (all were full).");
    m_AutoJoinSearch = false;
  } else {
    Overlay::Log("Found %d lobbies.", (int)m_LobbyList.size());
  }
}

void NetworkManager::OnGameLobbyJoinRequested(GameLobbyJoinRequested_t *pCB) {
  Overlay::Log("Lobby join requested via Steam overlay.");
  JoinLobby(pCB->m_steamIDLobby);
}

void NetworkManager::OnP2PSessionRequest(P2PSessionRequest_t *pCB) {
  Overlay::Log("P2P Session request from %llu — accepting.",
               pCB->m_steamIDRemote.ConvertToUint64());
  SteamNetworking()->AcceptP2PSessionWithUser(pCB->m_steamIDRemote);
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

  int memberCount = SteamMatchmaking()->GetNumLobbyMembers(m_CurrentLobby);
  CSteamID myID = SteamUser()->GetSteamID();

  for (int i = 0; i < memberCount; i++) {
    CSteamID memberID =
        SteamMatchmaking()->GetLobbyMemberByIndex(m_CurrentLobby, i);
    if (excludeSelf && memberID == myID)
      continue;
    SendPacket(memberID, type, data, size);
  }
}
