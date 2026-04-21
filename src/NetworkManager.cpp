#include "NetworkManager.h"
#include "Overlay.h"
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
