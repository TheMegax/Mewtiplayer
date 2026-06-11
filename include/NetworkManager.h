// ReSharper disable CppDFANotInitializedField
#pragma once
#include "mewjector.h"
#include "steam_api.h"
#include <deque>
#include <map>
#include <string>
#include <vector>

#include <set>

struct Character;

enum class PacketType : uint8_t {
  // --- Connection ---
  Handshake,
  Ping,
  // --- Misc ---
  RNGSync,
  MouseMove,
  // --- Combat ---
  CatOwnershipSync,
  CombatStart,
  CombatEnd,
  TurnAction,
  TurnFacing,
  // --- Save Protocol ---
  SaveCatRequest,
  SaveCatResponse,
  SaveFileTransfer,
  SaveFileAck,
  SaveLoadSignal,
  ButchBoxCatCountSync,
  // --- Collar Chooser ---
  CollarSync,
  LobbyReady,
  LobbyProceed,
  // --- Storage ---
  StorageItemSync,
};

#pragma pack(push, 1)
struct MouseMoveData {
  uint64_t steamID;
  float x;
  float y;
  uint8_t cursorType;
};
#pragma pack(pop)

struct CatOwnershipData {
  int64_t catUID;
  uint64_t ownerSteamID;
};

struct CatInfo {
  int64_t uid;
  std::string name;
  std::string className;
};

struct TurnActionPacket {
  uint32_t actorNUID;
  int32_t actionType;
  char abilityName[64];
  int32_t targetX;
  int32_t targetY;
  int32_t target2X;
  int32_t target2Y;
};

struct TurnFacingPacket {
  uint32_t actorNUID;
  int32_t nx;
  int32_t ny;
  bool anim;
  bool force;
};

// Generic structure for recording/replaying any action-like packet
struct ActionPacket {
  PacketType type;
  union {
    TurnActionPacket action;
    TurnFacingPacket facing;
  } data;
};

struct LobbyInfo {
  CSteamID id;
  std::string name;
  int memberCount;
  int maxMembers;
};

#pragma pack(push, 1)
struct ChunkedTransferHeader {
  uint32_t transferId;
  uint32_t chunkIndex;
  uint32_t totalChunks;
  uint32_t totalSize;
  uint32_t chunkSize;
  uint32_t chunkOffset;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct CatBlobHeader {
  uint64_t senderSteamID;
  int64_t  sqlKey;
  uint32_t blobSize;
  int32_t  originalAge;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct CatResponseHeader {
  uint32_t numCats;
  uint32_t unlocksSize;
  uint32_t inventorySize;
  uint32_t mapFlagsSize;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SaveLoadSignalPacket {
  uint32_t teamSize;
  uint32_t difficulty;
  uint32_t collarIndex;
  char customCollars[512];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ButchBoxCatCountPacket {
  uint32_t catCount;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct CollarSyncPacket {
  int64_t catID;
  int32_t collarIndex; // -1 = Colorless / unequip
};
#pragma pack(pop)

struct LobbyReadyPacket {
  uint64_t steamID;
  bool isReady;
};

#pragma pack(push, 1)
struct StorageItemSyncPacket {
  uint64_t steamID;
  int64_t catID;
  int32_t slotIndex;
};
#pragma pack(pop)

enum class SaveSyncState : uint8_t {
  Idle,
  WaitingForCatResponses,
  SendingSaveFile,
  WaitingForAcks,
  ReceivingSaveFile,
  Ready,
};

#pragma pack(push, 1)
struct PacketHeader { // NOLINT(*-pro-type-member-init)
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

  std::vector<LobbyInfo> &GetLobbyList() { return m_LobbyList; }

  bool SendPacket(CSteamID target, PacketType type, const void *data,
                  uint32_t size);
  void ReceivePackets();

  [[nodiscard]] CSteamID GetCurrentLobby() const { return m_CurrentLobby; }
  [[nodiscard]] bool IsHost() const;
  [[nodiscard]] CSteamID GetHostID() const;
  void BroadcastPacket(PacketType type, const void *data, uint32_t size,
                       bool excludeSelf = true);

  bool IsInputBlocked(uint64_t steamID);
  void SyncOwnership(int64_t uid, uint64_t steamID);
  void SetActiveNUID(uint32_t nuid);
  void RegisterCat(int64_t uid, const char *name, const char *className);

  void StartCombat();
  void EndCombat();
  [[nodiscard]] bool IsCombatActive() const { return m_combatActive; }
  [[nodiscard]] uint32_t GetActiveNUID() const { return m_activeNUID; }
  uint64_t GetCatOwner(int64_t uid);
  std::map<int64_t, uint64_t> &GetOwnershipMap() { return m_catOwnership; }
  [[nodiscard]] const std::map<int64_t, CatInfo> &GetDiscoveredCats() const {
    return m_discoveredCats;
  }
  std::map<uint32_t, std::pair<int, int>> &GetLastFacingMap() {
    return m_lastFacing;
  }

  // Entity Mapping
  void InitializeEntityMapping();
  void UpdateDynamicEntities();
  void ResetEntityMapping();
  uint32_t GetNUID(Character *character);
  Character *GetCharacter(uint32_t nuid);
  // Action Recording & Replay
  void RecordAction(const ActionPacket &pkt);
  void ClearRecordedActions();
  [[nodiscard]] const std::vector<ActionPacket> &GetRecordedActions() const;
  void EnqueueReplayAction(const ActionPacket &pkt);

  bool SendPacketReliable(CSteamID target, PacketType type, const void *data, uint32_t size);
  void SendChunkedData(CSteamID target, PacketType type, const uint8_t* data, uint32_t totalSize, uint32_t transferId);
  void BeginMultiplayerSave();
  [[nodiscard]] SaveSyncState GetSaveSyncState() const;

  int GetTotalLobbyCatCount();
  int GetLobbyMemberCatCount(uint64_t steamID);
  void SendLocalCatCount();

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
  uint32_t m_activeNUID = 0xFFFFFFFF;
  std::map<int64_t, uint64_t> m_catOwnership;
  std::map<int64_t, CatInfo> m_discoveredCats;

  // NUID Mapping
  std::map<Character *, uint32_t> m_charToNuid;
  std::map<uint32_t, Character *> m_nuidToChar;
  uint32_t m_nextNuid = 0;

  // Action Recording & Replay
  std::vector<ActionPacket> m_recordedActions;
  std::deque<ActionPacket> m_pendingReplays;

  // Rotation cache to prevent spam
  std::map<uint32_t, std::pair<int, int>> m_lastFacing;

  CCallResult<NetworkManager, LobbyCreated_t> m_LobbyCreatedCallResult;
  void OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure);

  CCallResult<NetworkManager, LobbyEnter_t> m_LobbyEnterCallResult;
  void OnLobbyEnter(LobbyEnter_t *pCallback, bool bIOFailure);

  CCallResult<NetworkManager, LobbyMatchList_t> m_LobbyMatchListCallResult;
  void OnLobbyMatchList(LobbyMatchList_t *pCallback, bool bIOFailure);

  // Internal packet handlers
  void HandleHandshake(CSteamID remoteID);

  static void HandleRNGSync(const void *data, uint32_t length);
  void HandleMouseMove(const void *data, uint32_t length);
  void HandleCatOwnershipSync(const void *data,
                              uint32_t length);

  static void HandleTurnAction(CSteamID remoteID, const void *data, uint32_t length);
  void HandleTurnFacing(CSteamID remoteID, const void *data, uint32_t length);

  void HandleSaveCatRequest(CSteamID remoteID);
  void HandleSaveCatResponse(CSteamID remoteID, const void *data, uint32_t length);
  void HandleSaveFileTransfer(CSteamID remoteID, const void *data, uint32_t length);
  void HandleSaveFileAck(CSteamID remoteID);
  void HandleSaveLoadSignal(const void *data, uint32_t length);
  void BuildAndDistributeSave();
  void HandleButchBoxCatCountSync(CSteamID remoteID, const void *data, uint32_t length);
  void HandleCollarSync(const void *data, uint32_t length);
  void HandleLobbyReady(const void *data, uint32_t length);
  void HandleLobbyProceed() const;
  void HandleStorageItemSync(const void *data, uint32_t length);

  // Save synchronization state variables
  SaveSyncState m_saveSyncState = SaveSyncState::Idle;
  uint32_t m_nextTransferId = 1;

  struct PendingCatBlob {
    uint64_t senderSteamID;
    int64_t sqlKey;
    std::vector<uint8_t> data;
    int32_t originalAge;
  };
  std::vector<PendingCatBlob> m_collectedCatBlobs;
  std::vector<std::vector<uint8_t>> m_collectedUnlocksBlobs;
  std::vector<std::vector<std::string>> m_collectedMapFlags;
  std::vector<std::vector<uint8_t>> m_collectedInventoryBlobs;
  
  std::set<uint64_t> m_pendingCatResponseFrom;
  std::set<uint64_t> m_pendingAcksFrom;
  std::map<uint64_t, int> m_lobbyMemberCatCounts;

  // Client-side save file transfer assembly
  uint32_t m_clientSaveTransferId = 0;
  std::vector<uint8_t> m_receivedSaveBuffer;
  uint32_t m_expectedSaveSize = 0;
  uint32_t m_receivedSaveChunks = 0;
  uint32_t m_expectedSaveChunks = 0;
  std::vector<bool> m_receivedSaveChunkTracker;

  // Host-side client transfer trackers
  struct ClientTransferState {
    uint32_t transferId = 0;
    uint32_t expectedChunks = 0;
    uint32_t receivedChunks = 0;
    std::vector<uint8_t> buffer;
    std::vector<bool> chunkTracker;
  };
  std::map<uint64_t, ClientTransferState> m_clientTransfers;

  STEAM_CALLBACK(NetworkManager, OnGameLobbyJoinRequested, // NOLINT(*-use-auto)
                 GameLobbyJoinRequested_t);
  STEAM_CALLBACK(NetworkManager, OnP2PSessionRequest, P2PSessionRequest_t); // NOLINT(*-use-auto)
};
