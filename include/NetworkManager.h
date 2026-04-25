#pragma once
#include "mewjector.h"
#include "steam_api.h"
#include <map>
#include <string>
#include <vector>

enum class PacketType : uint8_t {
  Handshake,
  Ping,
  GameStateSync,
  ChatMessage,
  RNGSync,
  MouseEvent,
  KeyEvent,
  MouseMove,
  CatOwnershipSync
};

#pragma pack(push, 1)
struct KeyEventData {
  uint64_t steamID;
  uint32_t type;     // SDL_EVENT_KEY_DOWN / SDL_EVENT_KEY_UP
  uint32_t keycode;  // SDL_Keycode
  uint32_t scancode; // SDL_Scancode
  uint16_t mod;      // SDL_Keymod
  uint8_t repeat;
  uint8_t down;
};

struct MouseEventData {
  uint64_t steamID;
  uint32_t type; // Message type (WM_LBUTTONDOWN, etc)
  float x;       // Normalized X (0.0 to 1.0)
  float y;       // Normalized Y (0.0 to 1.0)
};

struct MouseMoveData {
  uint64_t steamID;
  float x;
  float y;
  uint8_t cursorType;
};

struct CatOwnershipData {
  int64_t catUID;
  uint64_t ownerSteamID;
};

struct CatInfo {
  int64_t uid;
  std::string name;
  std::string className;
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
  static NetworkManager &Get() {
    static NetworkManager instance;
    return instance;
  }

  void Init(MewjectorAPI *mj, const char *modID);
  void Update();

  void HostLobby(const char *lobbyName);
  void LeaveLobby();
  void JoinLobby(CSteamID lobbyID);
  void JoinAnyLobby();
  void RefreshLobbyList();

  const std::vector<LobbyInfo> &GetLobbyList() const { return m_LobbyList; }

  bool SendPacket(CSteamID target, PacketType type, const void *data,
                  uint32_t size);
  void ReceivePackets();

  CSteamID GetCurrentLobby() const { return m_CurrentLobby; }
  bool IsHost() const;
  CSteamID GetHostID() const;
  void BroadcastPacket(PacketType type, const void *data, uint32_t size,
                       bool excludeSelf = true);

  bool IsInputBlocked(uint64_t steamID);
  void SyncOwnership(int64_t uid, uint64_t steamID);
  void SetActiveCat(int64_t uid);
  void RegisterCat(int64_t uid, const char *name, const char *className);

  bool IsCombatActive() const { return m_combatActive; }
  int64_t GetActiveCatUID() const { return m_activeCatUID; }
  uint64_t GetCatOwner(int64_t uid);
  std::map<int64_t, uint64_t> &GetOwnershipMap() { return m_catOwnership; }
  const std::map<int64_t, CatInfo> &GetDiscoveredCats() const {
    return m_discoveredCats;
  }

private:
  NetworkManager() : m_mj(nullptr) { m_CurrentLobby.Clear(); }

  MewjectorAPI *m_mj;
  std::string m_ModID;
  std::string m_PendingLobbyName;
  CSteamID m_CurrentLobby;
  std::vector<LobbyInfo> m_LobbyList;
  bool m_AutoJoinSearch = false;

  // Combat/Ownership state
  bool m_combatActive = false;
  int64_t m_activeCatUID = -1;
  std::map<int64_t, uint64_t> m_catOwnership;
  std::map<int64_t, CatInfo> m_discoveredCats;

  CCallResult<NetworkManager, LobbyCreated_t> m_LobbyCreatedCallResult;
  void OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure);

  CCallResult<NetworkManager, LobbyEnter_t> m_LobbyEnterCallResult;
  void OnLobbyEnter(LobbyEnter_t *pCallback, bool bIOFailure);

  CCallResult<NetworkManager, LobbyMatchList_t> m_LobbyMatchListCallResult;
  void OnLobbyMatchList(LobbyMatchList_t *pCallback, bool bIOFailure);

  // Internal packet handlers
  void HandleHandshake(CSteamID remoteID, const void *data, uint32_t length);
  void HandleRNGSync(CSteamID remoteID, const void *data, uint32_t length);
  void HandleMouseEvent(CSteamID remoteID, const void *data, uint32_t length);
  void HandleKeyEvent(CSteamID remoteID, const void *data, uint32_t length);
  void HandleMouseMove(CSteamID remoteID, const void *data, uint32_t length);
  void HandleCatOwnershipSync(CSteamID remoteID, const void *data,
                              uint32_t length);

  STEAM_CALLBACK(NetworkManager, OnGameLobbyJoinRequested,
                 GameLobbyJoinRequested_t);
  STEAM_CALLBACK(NetworkManager, OnP2PSessionRequest, P2PSessionRequest_t);
};
