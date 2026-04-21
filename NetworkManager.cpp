#include "NetworkManager.h"
#include "Overlay.h"
#include <string.h>

void NetworkManager::Init(MewjectorAPI *mj) {
  m_mj = mj;
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
  header.type   = type;
  header.length = size;

  std::vector<uint8_t> buffer(sizeof(PacketHeader) + size);
  memcpy(buffer.data(), &header, sizeof(PacketHeader));
  if (size > 0 && data)
    memcpy(buffer.data() + sizeof(PacketHeader), data, size);

  return SteamNetworking()->SendP2PPacket(target, buffer.data(),
                                          (uint32)buffer.size(),
                                          k_EP2PSendUnreliable);
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
      Overlay::Log("Received Ping from %llu",
                remoteID.ConvertToUint64());
      break;
    case PacketType::Handshake:
      Overlay::Log("Received Handshake from %llu",
                remoteID.ConvertToUint64());
      break;
    default:
      break;
    }
  }
}

void NetworkManager::HostLobby() {
  Overlay::Log("Creating Steam Lobby...");
  SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, 4);
  m_LobbyCreatedCallResult.Set(call, this, &NetworkManager::OnLobbyCreated);
}

void NetworkManager::JoinLobby(CSteamID lobbyID) {
  Overlay::Log("Joining Steam Lobby %llu...",
            lobbyID.ConvertToUint64());
  SteamAPICall_t call = SteamMatchmaking()->JoinLobby(lobbyID);
  m_LobbyEnterCallResult.Set(call, this, &NetworkManager::OnLobbyEnter);
}

void NetworkManager::JoinAnyLobby() {
  Overlay::Log("Searching for lobbies...");
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
  SteamMatchmaking()->SetLobbyData(m_CurrentLobby, "name", "Multigenics Match");
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
  if (bIO) return;
  if (pCB->m_nLobbiesMatching == 0) {
    Overlay::Log("No lobbies found.");
    return;
  }
  JoinLobby(SteamMatchmaking()->GetLobbyByIndex(0));
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
