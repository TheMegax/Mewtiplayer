#pragma once
#include "mewjector.h"
#include "steam_api.h"
#include <vector>

enum class PacketType : uint8_t {
  Handshake,
  Ping,
  GameStateSync,
  ChatMessage
};

#pragma pack(push, 1)
struct PacketHeader {
  uint8_t magic1 = 'M';
  uint8_t magic2 = 'G';
  PacketType type;
  uint32_t length;
};
#pragma pack(pop)

class NetworkManager {
public:
    static NetworkManager& Get() {
        static NetworkManager instance;
        return instance;
    }

    void Init(MewjectorAPI* mj);
    void Update();

    void HostLobby();
    void JoinLobby(CSteamID lobbyID);
    void JoinAnyLobby();

    bool SendPacket(CSteamID target, PacketType type, const void* data, uint32_t size);
    void ReceivePackets();

    CSteamID GetCurrentLobby() const { return m_CurrentLobby; }

private:
    NetworkManager() : m_mj(nullptr) {
        m_CurrentLobby.Clear();
    }

    MewjectorAPI* m_mj;
    CSteamID m_CurrentLobby;

    CCallResult<NetworkManager, LobbyCreated_t> m_LobbyCreatedCallResult;
    void OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure);

    CCallResult<NetworkManager, LobbyEnter_t> m_LobbyEnterCallResult;
    void OnLobbyEnter(LobbyEnter_t *pCallback, bool bIOFailure);

    CCallResult<NetworkManager, LobbyMatchList_t> m_LobbyMatchListCallResult;
    void OnLobbyMatchList(LobbyMatchList_t *pCallback, bool bIOFailure);

    STEAM_CALLBACK(NetworkManager, OnGameLobbyJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(NetworkManager, OnP2PSessionRequest, P2PSessionRequest_t);
};
