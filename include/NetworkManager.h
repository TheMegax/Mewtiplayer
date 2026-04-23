#pragma once
#include "mewjector.h"
#include "steam_api.h"
#include <vector>
#include <string>

enum class PacketType : uint8_t {
  Handshake,
  Ping,
  GameStateSync,
  ChatMessage,
  RNGSync,
  MouseEvent
};

#pragma pack(push, 1)
struct MouseEventData {
    uint32_t type; // Message type (WM_LBUTTONDOWN, etc)
    float x;       // Normalized X (0.0 to 1.0)
    float y;       // Normalized Y (0.0 to 1.0)
};
#pragma pack(pop)
 
struct LobbyInfo {
  CSteamID id;
  std::string name;
  int memberCount;
  int maxMembers;
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

    void Init(MewjectorAPI* mj, const char* modID);
    void Update();

    void HostLobby(const char* lobbyName);
    void LeaveLobby();
    void JoinLobby(CSteamID lobbyID);
    void JoinAnyLobby();
    void RefreshLobbyList();

    const std::vector<LobbyInfo>& GetLobbyList() const { return m_LobbyList; }

    bool SendPacket(CSteamID target, PacketType type, const void* data, uint32_t size);
    void ReceivePackets();

    CSteamID GetCurrentLobby() const { return m_CurrentLobby; }
    bool IsHost() const;
    CSteamID GetHostID() const;
    void BroadcastPacket(PacketType type, const void* data, uint32_t size, bool excludeSelf = true);

private:
    NetworkManager() : m_mj(nullptr) {
        m_CurrentLobby.Clear();
    }

    MewjectorAPI* m_mj;
    std::string m_ModID;
    std::string m_PendingLobbyName;
    CSteamID m_CurrentLobby;
    std::vector<LobbyInfo> m_LobbyList;
    bool m_AutoJoinSearch = false;

    CCallResult<NetworkManager, LobbyCreated_t> m_LobbyCreatedCallResult;
    void OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure);

    CCallResult<NetworkManager, LobbyEnter_t> m_LobbyEnterCallResult;
    void OnLobbyEnter(LobbyEnter_t *pCallback, bool bIOFailure);

    CCallResult<NetworkManager, LobbyMatchList_t> m_LobbyMatchListCallResult;
    void OnLobbyMatchList(LobbyMatchList_t *pCallback, bool bIOFailure);

    STEAM_CALLBACK(NetworkManager, OnGameLobbyJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(NetworkManager, OnP2PSessionRequest, P2PSessionRequest_t);
};
