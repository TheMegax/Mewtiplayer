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
  NUIDOwnershipSync,
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
  // --- Map ---
  MapNodeSync,
  ActSelectSync,
  MapInventoryOpen,
  MapInventoryClose,
  // --- Level Up / Ability Chooser ---
  LevelUpSelectOption,
  LevelUpReroll,
  AbilityReplace,
  WorldEventSelectOption,
  WorldEventSelectCat,
  WorldEventClickEnd,
  // --- Shop / Loot ---
  ShopBuyItem,
  ShopExitButton,
  ShopChestClick,
  ShopFastForward,
  // --- Chat ---
  ChatMessage,
  // --- RNG Check / Desync ---
  RNGCheckRequest,
  RNGCheckResponse,
  CombatDesyncDetected,
  TriggerDesyncReload,
};

#pragma pack(push, 1)
struct ChatMessagePacket {
  char message[256];
};
#pragma pack(pop)

#pragma pack(push, 1)
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

struct NUIDOwnershipData {
  uint32_t nuid;
  uint64_t ownerSteamID;
};
#pragma pack(pop)

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
  int32_t unk_28;
  int32_t unk_2C;
  uint8_t flag_30;
  uint8_t flag_31;
  uint8_t flag_32;
  uint8_t flag_33;
  uint8_t flag_34;
  uint8_t flag_35;
  uint8_t flag_36;
  bool isPassive;
  uint32_t rngState[8];
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
struct RNGCheckRequestPacket {
  uint32_t turnNumber;
  uint32_t hostRngCrc;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct RNGCheckResponsePacket {
  uint32_t turnNumber;
  uint32_t clientRngCrc;
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

#pragma pack(push, 1)
struct MapNodeSyncPacket {
  uint32_t nodeIndex;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ActSelectPacket {
  uint32_t actIndex;
};
#pragma pack(pop)

struct MapInventoryOpenPacket {
  uint8_t dummy;
};

struct MapInventoryClosePacket {
  uint8_t dummy;
};

#pragma pack(push, 1)
struct LevelUpSelectOptionPacket {
  int64_t catUID;
  uint32_t optionIndex;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct LevelUpRerollPacket {
  int64_t catUID;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct AbilityReplacePacket {
  int64_t catUID;
  uint32_t slotIndex;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct WorldEventSelectOptionPacket {
  int64_t catUID;
  uint32_t optionIndex;
};

struct WorldEventSelectCatPacket {
  int64_t selectedCatUID;
};

struct WorldEventClickEndPacket {
  uint8_t buttonType; // 1 for lambda_1, 2 for lambda_2
};

struct ShopBuyItemPacket {
  uint32_t itemIndex;
};

struct ShopExitButtonPacket {
  uint8_t dummy;
};

struct ShopChestClickPacket {
  uint8_t dummy;
};

struct ShopFastForwardPacket {
  uint8_t dummy;
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
  void SyncNUIDOwnership(uint32_t nuid, uint64_t steamID);
  void SetActiveNUID(uint32_t nuid);
  void RegisterCat(int64_t uid, const char *name, const char *className);
  void UpdateNUIDOwnership();

  void StartCombat();
  void EndCombat();
  [[nodiscard]] bool IsCombatActive() const { return m_combatActive; }
  [[nodiscard]] uint32_t GetActiveNUID() const { return m_activeNUID; }
  uint64_t GetCatOwner(int64_t uid);
  [[nodiscard]] uint64_t GetNUIDOwner(uint32_t nuid) const;
  [[nodiscard]] uint32_t GetNextNuid() const { return m_nextNuid; }
  [[nodiscard]] uint64_t GetLastControllingPlayer() const { return m_lastControllingPlayer; }
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
  std::string GetCharacterNameByNUID(uint32_t nuid);
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
  void RestoreOwnershipFromSave(const char* saveName);
  void SendChatMessage(const std::string &message);

  // Combat State Verification & Desync Handling
  uint32_t ComputeCombatStateCRC();
  void RecordTurnState(uint32_t turnNum);
  void SendDesyncCheck(uint32_t turnNum);
  void CheckAndShowDesyncPopup();
  void TriggerDesyncReload();
  uint32_t GetCurrentTurnNumber() const { return m_currentTurnNumber; }
  void IncrementTurnNumber() { m_currentTurnNumber++; }

private:
  NetworkManager() : m_mj(nullptr), m_AutoJoinStartTime(0), m_LastAutoJoinAttempt(0), m_AutoJoinFinished(false) { m_CurrentLobby.Clear(); }

  MewjectorAPI *m_mj;
  std::string m_ModID;
  std::string m_PendingLobbyName;
  CSteamID m_CurrentLobby;
  std::vector<LobbyInfo> m_LobbyList;
  bool m_AutoJoinSearch = false;
  ULONGLONG m_AutoJoinStartTime = 0;
  ULONGLONG m_LastAutoJoinAttempt = 0;
  bool m_AutoJoinFinished = false;

  // Combat/Ownership state
  bool m_combatActive = false;
  uint32_t m_activeNUID = 0xFFFFFFFF;
  std::map<int64_t, uint64_t> m_catOwnership;
  std::map<int64_t, CatInfo> m_discoveredCats;
  std::map<uint32_t, uint64_t> m_nuidOwnership;
  uint64_t m_lastControllingPlayer = 0;

  // RNG Verification & Desync state
  uint32_t m_currentTurnNumber = 0;
  std::map<uint32_t, uint32_t> m_turnRngHistory;   // turnNumber -> local RNG CRC32
  std::map<uint32_t, uint32_t> m_pendingRngChecks; // turnNumber -> host RNG CRC32 (queued when client receives request early)

  bool m_desyncDetected = false;
  bool m_desyncPopupOpened = false;

  void HandleRNGCheckRequest(const CSteamID remoteID, const void *data, uint32_t length);
  void HandleRNGCheckResponse(const CSteamID remoteID, const void *data, uint32_t length);
  void HandleCombatDesyncDetected();
  void HandleTriggerDesyncReload();

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
  void HandleCatOwnershipSync(const void *data, uint32_t length);
  void HandleNUIDOwnershipSync(const void *data, uint32_t length);
  static void HandleTurnAction(const void *data, uint32_t length);
  void HandleTurnFacing(const void *data, uint32_t length);
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
  void HandleMapNodeSync(const void *data, uint32_t length);
  void HandleActSelectSync(const void *data, uint32_t length);
  void HandleMapInventoryOpen(const void *data, uint32_t length);
  void HandleMapInventoryClose(const void *data, uint32_t length);
  void HandleLevelUpSelectOption(const void* data, uint32_t length);
  void HandleLevelUpReroll(const void* data, uint32_t length);
  void HandleAbilityReplace(const void* data, uint32_t length);
  void HandleWorldEventSelectOption(const void* data, uint32_t length);
  void HandleWorldEventSelectCat(const void* data, uint32_t length);
  void HandleWorldEventClickEnd(const void* data, uint32_t length);
  void HandleShopBuyItem(const void* data, uint32_t length);
  void HandleShopExitButton(const void* data, uint32_t length);
  void HandleShopChestClick(const void* data, uint32_t length);
  void HandleShopFastForward(const void* data, uint32_t length);
  void HandleChatMessage(CSteamID remoteID, const void *data, uint32_t length);

  // Save synchronization state variables
  SaveSyncState m_saveSyncState = SaveSyncState::Idle;
  uint32_t m_nextTransferId = 1;

  struct PendingCatBlob {
    uint64_t senderSteamID;
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
