#include "GameUtils.h"
#include "CrashHandler.h"
#include "ImGuiHook.h"
#include "InputGhost.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "Scanner.h"
#include "mewjector.h"
#include "MewSQL.h"
#include <unordered_set>

#define MOD_NAME "Mewtiplayer"
#define MOD_VERSION "1.0.0"

static MewjectorAPI mj;
static UINT_PTR g_gameBase = 0;

static bool g_networkInitialized = false;
static bool g_packetTesting = false;
static bool g_noSteven = false;
static bool g_autoLobby = false;
static bool g_talkative = false;
static bool g_loadMewtiplayerSave = false;
static ActionPacket g_lastSentTurnPackage = {};

typedef void (*RunFrame_t)(void *rcx, void *rdx);
static RunFrame_t g_origRunFrame = nullptr;

typedef void(__fastcall *InitializeSave_t)(void *gameStateMap, void *saveNameStr);
static InitializeSave_t g_origInitializeSave = nullptr;

typedef void(__fastcall *BeginTurn_t)(void *character, int kind);
static BeginTurn_t g_origBeginTurn = nullptr;

static TurnControl *g_currentTurnControl = nullptr;
typedef void(__fastcall *TurnStart_t)(TurnControl *tc);
static TurnStart_t g_origTurnStart = nullptr;

typedef void(__fastcall *FightEnd_t)(CombatResolutionState *combat);
static FightEnd_t g_origFightEnd = nullptr;

typedef void *(__fastcall *AbilityTrigger_t)(void *ability, void *turnAction);
AbilityTrigger_t g_origAbilityTrigger = nullptr;

typedef void(__fastcall *FaceDirection_t)(void *character, uint64_t target_packed, bool play_animation, bool force);
FaceDirection_t g_origFaceDirection = nullptr;

typedef void *(__fastcall *StevenSpawn_t)(void *rcx, void *rdx);
static StevenSpawn_t g_origStevenSpawn = nullptr;

typedef void *(__fastcall *SlotUpdateDynamicValue_t)(void *rcx, void *rdx);
static SlotUpdateDynamicValue_t g_origSlotUpdateDynamicValue = nullptr;

typedef void *(__fastcall *ProcessCombatInput_t)(CombatUIContext *battleContext, void *outResult);
static ProcessCombatInput_t g_origProcessCombatInput = nullptr;

typedef void *(__fastcall *RouteCombatInput_t)(CombatUIContext *ctx, void *outResult, void *param3, void *param4);
static RouteCombatInput_t g_origRouteCombatInput = nullptr;

typedef void(__fastcall *PauseGame_t)(void *pauseMenuScene);
static PauseGame_t g_origPauseGame = nullptr;

static bool g_isCombatUIProcessing = false;
static std::unordered_set<void *> g_castableAbilities;

static void *__fastcall Hook_StevenSpawn(void *rcx, void *rdx) {
  if (g_noSteven) {
    Overlay::Log("[STEVEN] Go away!");
    return nullptr;
  }
  if (g_origStevenSpawn)
    return g_origStevenSpawn(rcx, rdx);
  return nullptr;
}

// We use this to distinguish between UI-initiated and engine-initiated actions
// It is set in Hook_EnqueueAction and consumed in Hook_AbilityTrigger
static bool g_waitingForPlayerAction = false;
bool g_isSyncActionPending = false;

bool g_startedCombat = false;
static bool g_isQueueEmpty = false;
static bool g_inCombatDetected = false;
#include <deque>
std::deque<ActionPacket> g_pendingInjections;

// ---------------------------------------------------------------------------
// Hook: TurnStart
// ---------------------------------------------------------------------------
static void Hook_TurnStart(TurnControl *tc) {
  if (tc) {
    g_currentTurnControl = tc;
    GameUtils::SetTurnControlPtr(&g_currentTurnControl);
  }
  if (g_origTurnStart)
    g_origTurnStart(tc);
}

// ---------------------------------------------------------------------------
// Hook: BeginTurn
// ---------------------------------------------------------------------------
static void Hook_BeginTurn(Character *character, int kind) {
  // Why is there two different turn start functions? idk don't ask me
  g_startedCombat = true;
  g_isCombatUIProcessing = false;

  if (character) {
    std::wstring name = L"Unknown";
    const auto w_name = (MsvcReleaseModeXString *)&character->name;
    // Note: Layout is same, but we treat it as wchar_t*
    if (w_name->Myres < 8) {
      name = (const wchar_t *)&w_name->Bx.Buf[0];
    } else {
      name = *(const wchar_t **)&w_name->Bx.Ptr;
    }

    int64_t uniqueId = -1;
    auto className = "Classless";
    if (character->persistentChar) {
      uniqueId = character->persistentChar->sql_key;
      className = character->persistentChar->className.begin();
    }

    const uint8_t isPlayerCat = character->isPlayerCat;

    Overlay::Log("BeginTurn: [%ls] UID:%lld Class:%s IsPlayerCat:%d",
                 name.c_str(), uniqueId, className, isPlayerCat);

    if (uniqueId != -1) {
      char narrowName[128];
      size_t converted;
      wcstombs_s(&converted, narrowName, name.c_str(), sizeof(narrowName));
      NetworkManager::Get().RegisterCat(uniqueId, narrowName, className);

      if (!g_inCombatDetected) {
        g_inCombatDetected = true;
        if (NetworkManager::Get().IsHost()) {
          NetworkManager::Get().StartCombat();
        }
        g_pendingInjections.clear();
        NetworkManager::Get().ClearRecordedActions();
        NetworkManager::Get().InitializeEntityMapping();
      }

      const uint32_t nuid = NetworkManager::Get().GetNUID(character);
      if (isPlayerCat == 1 && nuid != 0xFFFFFFFF) {
        NetworkManager::Get().SetActiveNUID(nuid);
      }
    }
  }

  if (g_origBeginTurn)
    g_origBeginTurn(character, kind);
}

// ---------------------------------------------------------------------------
// Hook: InitializeSave
// ---------------------------------------------------------------------------
void __fastcall Hook_InitializeSave(void* gameStateMap, void* saveNameStr) {
  if (GameUtils::g_injectCustomSaveData) {
    MewSQL::CloseActiveSaveConnection(GameUtils::GetMewDirectorSingleton());
    Overlay::Log("[SAVE] Wiping save file for Start New Run...");
    MewSQL::DeleteSaveFile("mewtiplayer.sav");
  }

  // First, call the original function to create/open the DB.
  if (g_origInitializeSave) {
    g_origInitializeSave(gameStateMap, saveNameStr);
  }

  if (!GameUtils::g_injectCustomSaveData) {
      return; // Do not inject custom data into other saves
  }
  GameUtils::g_injectCustomSaveData = false;
  GameUtils::g_startCustomRunPending = true;

  Overlay::Log("[SAVE] Committing custom data...");

  std::vector<std::string> keys = {
    "mapflag_BoneyardUnlocked",
    "mapflag_BothObelisksUnlocked",
    "mapflag_BunkerUnlocked",
    "mapflag_CavesUnlocked",
    "mapflag_CoreObeliskUnlocked",
    "mapflag_CoreUnlocked",
    "mapflag_CraterUnlocked",
    "mapflag_DesertUnlocked",
    "mapflag_DimensionXUnlocked",
    "mapflag_HardPathUnlocked",
    "mapflag_JunkyardUnlocked",
    "mapflag_LabUnlocked",
    "mapflag_MeatWorldUnlocked",
    "mapflag_MeatWorldUnlockedFull",
    "mapflag_MoonObeliskUnlocked",
    "mapflag_MoonUnlocked",
    "mapflag_SewersUnlocked",
    "mapflag_ThrobbingArteryDone",
    "mapflag_WallOfFleshDone",
    "mapflag_TutorialUnlocked",
    "mapflag_TutorialDone",
    "game_began",
  };

  for (const auto& key : keys) {
    GameUtils::SetSaveProperty(key, 1);
  }

  // Go away Tink >:(
  GameUtils::ExecuteSQL("INSERT OR REPLACE INTO files VALUES "
                        "('tutorial_tokens', "
                        "X'02000000000000001f00000000000000636f6d626"
                        "1745f7475746f7269616c2e676f6e2e686f7573655f"
                        "696e74726f2200000000000000636f6d6261745f747"
                        "5746f7269616c2e676f6e2e686f7573655f70617373"
                        "5f646179');");

  Overlay::Log("[SAVE] Custom SQL executed successfully!");
}

// ---------------------------------------------------------------------------
// Hook: CreateStrayCat
// ---------------------------------------------------------------------------
typedef void* (__fastcall *CreateStrayCat_t)(void* catsManager);
static CreateStrayCat_t g_origCreateStrayCat = nullptr;

void* __fastcall Hook_CreateStrayCat(void* catsManager) {
  void* cat = nullptr;
  if (g_origCreateStrayCat) {
    cat = g_origCreateStrayCat(catsManager);
  }

  if (GameUtils::g_isLoadingCustomCats && cat) {
    const int index = GameUtils::g_currentCustomCatIndex;
    Overlay::Log("[CAT LOAD] Hooked CreateStrayCat, loading custom cat at index %d from test00.sav...", index);

    if (glaiel::SQLSaveFile* dbFile = MewSQL::OpenSaveDatabase("test00.sav")) {
      // ReSharper disable once CppLocalVariableMayBeConst
      if (GameUtils::MewSaveFile_Load_t mewSaveFileLoad = GameUtils::GetMewSaveFileLoadPtr()) {
        char dummySave[0x600] = {};
        memcpy(dummySave + 0x470, dbFile, sizeof(glaiel::SQLSaveFile));

        const auto catIdPtr = (int64_t*)((char*)cat + 3144);
        const int64_t originalID = *catIdPtr;

        mewSaveFileLoad((void*)dummySave, index, cat);
        *catIdPtr = originalID;

        Overlay::Log("[CAT LOAD] Custom cat loaded successfully! OriginalID: %lld restored.", originalID);
      } else {
        Overlay::Log("[CAT LOAD] Error: MewSaveFile::Load pointer not set!");
      }
      MewSQL::CloseSaveDatabase(dbFile);
    } else {
      Overlay::Log("[CAT LOAD] Error: Could not open test00.sav database!");
    }
    GameUtils::g_currentCustomCatIndex++;
  }

  return cat;
}

// ---------------------------------------------------------------------------
// Hook: GetCollarVector & GameAllocate
// ---------------------------------------------------------------------------
typedef void* (__fastcall *GameAllocate_t)(size_t size);
static GameAllocate_t g_GameAllocate = nullptr;

typedef int64_t* (__fastcall *GetCollarVector_t)(int64_t* outVector, int64_t collarId, int64_t param_3, int64_t param_4);
static GetCollarVector_t g_origGetCollarVector = nullptr;

int64_t* __fastcall Hook_GetCollarVector(int64_t* outVector, int64_t collarId, int64_t param_3, int64_t param_4) {
  if (GameUtils::g_useCustomCollarClasses && !GameUtils::g_customCollarClasses.empty()) {
    Overlay::Log("[SAVE] Populating custom collar vector with %zu classes...", GameUtils::g_customCollarClasses.size());

    outVector[0] = 0;
    outVector[1] = 0;
    outVector[2] = 0;

    const size_t count = GameUtils::g_customCollarClasses.size();
    const size_t bytesToAllocate = count * sizeof(MsvcReleaseModeXString);

    if (g_GameAllocate) {
      if (auto* array = (MsvcReleaseModeXString*)g_GameAllocate(bytesToAllocate)) {
        memset(array, 0, bytesToAllocate);
        for (size_t i = 0; i < count; ++i) {
          GameUtils::InitXString(array[i], GameUtils::g_customCollarClasses[i]);
        }
        outVector[0] = (int64_t)array;
        outVector[1] = (int64_t)(array + count);
        outVector[2] = (int64_t)(array + count);
      } else {
        Overlay::Log("[SAVE] Error: GameAllocate failed to allocate %zu bytes!", bytesToAllocate);
      }
    } else {
      Overlay::Log("[SAVE] Error: GameAllocate pointer not resolved!");
    }
    return outVector;
  }

  if (g_origGetCollarVector) {
    return g_origGetCollarVector(outVector, collarId, param_3, param_4);
  }
  return outVector;
}

// ---------------------------------------------------------------------------
// Hook: AbilityTrigger
// ---------------------------------------------------------------------------
void __fastcall Hook_AbilityTrigger(Ability *ability, TurnAction *turnAction) {
  const bool isSyncAction = g_isSyncActionPending;
  g_isSyncActionPending = false; // Consume for logging

  if (ability && turnAction) {
    const std::string abilityName = GameUtils::GetAbilityName(ability);

    int64_t sourceUID = -1;
    if (ability->owner && ability->owner->persistentChar) {
      sourceUID = ability->owner->persistentChar->sql_key;
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
             "[%s ACTION] UID:%lld | %s | T1:(%d,%d) | T2:(%d,%d)",
             isSyncAction ? "SYNC" : "AUTO", sourceUID, abilityName.c_str(),
             turnAction->targetX, turnAction->targetY, turnAction->target2X,
             turnAction->target2Y);
    Overlay::Log(buf);
  }

  if (g_origAbilityTrigger)
    g_origAbilityTrigger(ability, turnAction);
}

// ---------------------------------------------------------------------------
// Action Queue Interception
// ---------------------------------------------------------------------------
typedef void *(__fastcall *EnqueueAction_t)(void *queue, void *actionData);
EnqueueAction_t g_origEnqueueAction = nullptr;
void *g_lastActionQueue = nullptr;


[[maybe_unused]] static void LogHexDump(const void *data, size_t size, const char *label) {
  const auto *p = static_cast<const unsigned char *>(data);
  Overlay::Log("--- %s Hex Dump (%zu bytes) ---", label, size);
  char lineBuffer[128];
  for (size_t i = 0; i < size; i += 16) {
    int offset = snprintf(lineBuffer, sizeof(lineBuffer), "%04zX  ", i);
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < size) {
        offset += snprintf(lineBuffer + offset, sizeof(lineBuffer) - offset,
                           "%02X ", p[i + j]);
      } else {
        offset +=
            snprintf(lineBuffer + offset, sizeof(lineBuffer) - offset, "   ");
      }
    }
    Overlay::Log("%s", lineBuffer);
  }
  Overlay::Log("--------------------------------");
}

// ---------------------------------------------------------------------------
// Hook: EnqueueAction
// ---------------------------------------------------------------------------
static void *__fastcall Hook_EnqueueAction(void *queue,
                                           TurnAction *actionData) {
  g_lastActionQueue = queue;

  g_isQueueEmpty = actionData && actionData->type <= 1;

  if (actionData && actionData->type <= 1 && !g_pendingInjections.empty() && g_isCombatUIProcessing) {
    // Peek for TurnAction specifically, or handle TurnFacing immediately
    while (!g_pendingInjections.empty() &&
           g_pendingInjections.front().type == PacketType::TurnFacing) {
      const TurnFacingPacket &facing = g_pendingInjections.front().data.facing;

      if (Character *c = NetworkManager::Get().GetCharacter(facing.actorNUID)) {
        const auto ux = static_cast<uint32_t>(facing.nx);
        const auto uy = static_cast<uint32_t>(facing.ny);
        uint64_t packed = static_cast<uint64_t>(ux) | static_cast<uint64_t>(uy) << 32;
        Overlay::Log("[ENQUEUE] Injecting facing for NUID %u | Target:(%d,%d)",
                     facing.actorNUID, facing.nx, facing.ny);
        if (g_origFaceDirection)
          g_origFaceDirection(c, packed, facing.anim, facing.force);
      }
      g_pendingInjections.pop_front();
    }

    if (!g_pendingInjections.empty() &&
        g_pendingInjections.front().type == PacketType::TurnAction) {
      const TurnActionPacket &pending = g_pendingInjections.front().data.action;
      uint32_t currentNUID = NetworkManager::Get().GetNUID(actionData->actor);
      Character *pendingActor =
          NetworkManager::Get().GetCharacter(pending.actorNUID);

      if (pendingActor && pending.actorNUID == currentNUID) {
        std::string abilityName(pending.abilityName);
        Ability *ability = nullptr;
        if (abilityName != "NULL") {
          ability = GameUtils::FindCharacterAbility(pendingActor, abilityName);
          if (!ability) {
            Overlay::Log("[ENQUEUE] [ERROR] Could not resolve ability '%s' for NUID %u "
                         "- dropping",
                         pending.abilityName, pending.actorNUID);
            g_pendingInjections.pop_front();
            return g_origEnqueueAction ? g_origEnqueueAction(queue, actionData)
                                       : nullptr;
          }
        }

        TurnActionPacket pktCopy = pending;
        g_pendingInjections.pop_front();

        actionData->type = pktCopy.actionType;
        actionData->ability = ability;
        actionData->actor = pendingActor;
        actionData->targetX = pktCopy.targetX;
        actionData->targetY = pktCopy.targetY;
        actionData->target2X = pktCopy.target2X;
        actionData->target2Y = pktCopy.target2Y;
        actionData->magic84 = 0x544c5541; // "AULT"

        Overlay::Log("[ENQUEUE] Injecting from queue: Type %d for NUID %u",
                     pktCopy.actionType, pktCopy.actorNUID);
        if (g_origEnqueueAction)
          return g_origEnqueueAction(queue, actionData);
        return nullptr;
      }
    }

    static ULONGLONG lastLogTime = 0;
    ULONGLONG currentTime = GetTickCount64();
    if (g_talkative && (currentTime - lastLogTime >= 1000) &&
        !g_pendingInjections.empty()) {
      lastLogTime = currentTime;
      uint32_t currentNUID = NetworkManager::Get().GetNUID(actionData->actor);
      Overlay::Log("[ENQUEUE] Skipping injection: pending NUID %u != current "
                   "NUID %u",
                   g_pendingInjections.front().data.action.actorNUID,
                   currentNUID);
    }
  }

  // Actions equal to 1 are null actions (idle).
  // I haven't seen actions with type 0 or lower, but the game ignores them anyway.
  if (!actionData || actionData->type <= 1)
    return g_origEnqueueAction ? g_origEnqueueAction(queue, actionData)
                               : nullptr;

  if (g_isCombatUIProcessing) {
    g_waitingForPlayerAction = actionData->type == 3 ||
        (actionData->ability != nullptr &&
         g_castableAbilities.count(reinterpret_cast<void *>(actionData->ability)) > 0);
  }

  // Actions of type 3 are End Turn actions
  if (actionData->type == 3 && g_waitingForPlayerAction) {
    g_waitingForPlayerAction = false;
    uint32_t nuid = NetworkManager::Get().GetNUID(actionData->actor);
    if (nuid != 0xFFFFFFFF) {
      TurnActionPacket pkt{};
      pkt.actorNUID = nuid;
      pkt.actionType = 3;
      memset(pkt.abilityName, 0, sizeof(pkt.abilityName));
      strncpy_s(pkt.abilityName, "NULL", _TRUNCATE);
      pkt.targetX = actionData->targetX;
      pkt.targetY = actionData->targetY;
      pkt.target2X = actionData->target2X;
      pkt.target2Y = actionData->target2Y;

      ActionPacket actPkt{};
      actPkt.type = PacketType::TurnAction;
      actPkt.data.action = pkt;

      // Right before sending the package, record the last direction the player faced to the queue.
      if (g_lastSentTurnPackage.type == PacketType::TurnFacing) {
        NetworkManager::Get().RecordAction(g_lastSentTurnPackage);
        g_lastSentTurnPackage.type = PacketType::Ping;
      }

      NetworkManager::Get().RecordAction(actPkt);
      NetworkManager::Get().BroadcastPacket(PacketType::TurnAction, &pkt,
                                            sizeof(pkt), !g_packetTesting);
      Overlay::Log("[NET] Broadcast EndTurn for NUID %u", nuid);
      if (g_packetTesting) {
        actionData->type = 0; // Cancel this action
        Overlay::Log(
            "[ENQUEUE] Testing Mode - Cancelled Action '%s' for NUID: %d",
            pkt.abilityName, nuid);
        return g_origEnqueueAction(queue, actionData);
      }
    }

    return g_origEnqueueAction(queue, actionData);
  }
  // LogHexDump(actionData, sizeof(TurnAction), "NATIVE ActionData");
  Ability *ability = actionData->ability;
  Character *actor = actionData->actor;
  if (!actor && ability)
    actor = ability->owner;

  uint32_t nuid = actor ? NetworkManager::Get().GetNUID(actor) : 0xFFFFFFFF;
  std::string actorName = actor ? actor->name.to_utf8() : "UNKNOWN";

  if (g_waitingForPlayerAction) {
    g_waitingForPlayerAction = false;
    g_isSyncActionPending = true;

    // BROADCAST ACTION
    if (nuid != 0xFFFFFFFF) {
      TurnActionPacket pkt{};
      pkt.actorNUID = nuid;
      pkt.actionType = actionData->type;

      std::string abilityName = GameUtils::GetAbilityName(ability);
      memset(pkt.abilityName, 0, sizeof(pkt.abilityName));
      strncpy_s(pkt.abilityName, abilityName.c_str(), _TRUNCATE);

      pkt.targetX = actionData->targetX;
      pkt.targetY = actionData->targetY;
      pkt.target2X = actionData->target2X;
      pkt.target2Y = actionData->target2Y;

      Overlay::Log("[ENQUEUE] SYNC Action: Actor=%s | Ability=%p | "
                   "T1=(%d,%d) | T2=(%d,%d) | Type=%d",
                   actorName.c_str(), ability, actionData->targetX,
                   actionData->targetY, actionData->target2X,
                   actionData->target2Y, actionData->type);

      NetworkManager::Get().BroadcastPacket(PacketType::TurnAction, &pkt,
                                            sizeof(pkt), !g_packetTesting);
      ActionPacket actPkt{};
      actPkt.type = PacketType::TurnAction;
      actPkt.data.action = pkt;
      NetworkManager::Get().RecordAction(actPkt);
      Overlay::Log("[NET] Broadcast and Recorded Action '%s' for NUID: %d",
                   pkt.abilityName, nuid);

      if (g_packetTesting) {
        actionData->type = 0; // Cancel this action
        Overlay::Log(
            "[ENQUEUE] Testing Mode - Cancelled Action '%s' for NUID: %d",
            pkt.abilityName, nuid);
        return g_origEnqueueAction(queue, actionData);
      }
    }
  } else {
    g_isSyncActionPending = false;
    Overlay::Log("[ENQUEUE] AUTO Action: Actor=%s | Type=%d", actorName.c_str(),
                 actionData->type);
  }

  if (g_origEnqueueAction)
    return g_origEnqueueAction(queue, actionData);
  return nullptr;
}

// ---------------------------------------------------------------------------
// Hook: UpdateCombatResolutionState
// ---------------------------------------------------------------------------
static void Hook_UpdateCombatResolutionState(CombatResolutionState *combat) {
  if (!g_startedCombat)
    return;

  if (!g_inCombatDetected) {
    g_inCombatDetected = true;
    if (NetworkManager::Get().IsHost()) {
      NetworkManager::Get().StartCombat();
    }
    g_pendingInjections.clear();
    NetworkManager::Get().ClearRecordedActions();
    NetworkManager::Get().InitializeEntityMapping();
  } else {
    NetworkManager::Get().UpdateDynamicEntities();

    if (combat && (combat->victory || combat->defeat)) {
      Overlay::Log("[COMBAT] Fight End Detected: %s",
                   combat->victory ? "Victory" : "Defeat");
      g_startedCombat = false;
      g_inCombatDetected = false;
      g_waitingForPlayerAction = false;

      if (NetworkManager::Get().IsHost()) {
        NetworkManager::Get().EndCombat();
      }
    }
  }

  if (g_origFightEnd)
    g_origFightEnd(combat);
}

// ---------------------------------------------------------------------------
// Hook: FaceDirection
// ---------------------------------------------------------------------------
static void Hook_FaceDirection(void *character, const uint64_t target_packed,
                               const bool play_animation, const bool force) {
  if (character) {
    const auto x = static_cast<uint32_t>(target_packed & 0xFFFFFFFF);
    const auto y = static_cast<uint32_t>(target_packed >> 32);

    const int sx = static_cast<int>(x);
    const int sy = static_cast<int>(y);

    // Normalize to -1, 0, 1
    int nx = (sx > 0) ? 1 : (sx < 0 ? -1 : 0);
    int ny = (sy > 0) ? 1 : (sy < 0 ? -1 : 0);

    auto *c = static_cast<Character *>(character);
    const uint32_t nuid = NetworkManager::Get().GetNUID(c);
    const bool isLocalActiveNUID =
        nuid != 0xFFFFFFFF && nuid == NetworkManager::Get().GetActiveNUID();

    if (nx != 0 || ny != 0) {
      if (auto &lastFacing = NetworkManager::Get().GetLastFacingMap();
        lastFacing.count(nuid) && lastFacing[nuid].first == nx && lastFacing[nuid].second == ny && !force) {
      } else {
        lastFacing[nuid] = {nx, ny};

        if (isLocalActiveNUID && g_isQueueEmpty && g_pendingInjections.empty()) {
          TurnFacingPacket pkt{};
          pkt.actorNUID = nuid;
          pkt.nx = nx;
          pkt.ny = ny;
          pkt.anim = play_animation;
          pkt.force = force;

          ActionPacket actPkt{};
          actPkt.type = PacketType::TurnFacing;
          actPkt.data.facing = pkt;

          // ReSharper disable once CppSomeObjectMembersMightNotBeInitialized
          g_lastSentTurnPackage = actPkt;
          NetworkManager::Get().BroadcastPacket(PacketType::TurnFacing, &pkt,
                                                sizeof(pkt), !g_packetTesting);

          if (g_talkative) {
            Overlay::Log("[FACE] Broadcast TurnFacing for NUID:%u | Target:(%d,%d)", nuid, nx, ny);
          }
        } else if (g_talkative) {
          if (isLocalActiveNUID) {
            Overlay::Log("[FACE] Active NUID (%d), Queue not empty | Target:(%d,%d)", nuid, nx, ny);
          } else if (nuid != 0xFFFFFFFF) {
            Overlay::Log(
                "[FACE] Queue empty, NUID not active (%d) | Target:(%d,%d)", nuid, nx, ny);
          }
        }
      }
    }
  }

  if (g_origFaceDirection)
    g_origFaceDirection(character, target_packed, play_animation, force);
}

// ---------------------------------------------------------------------------
// Hook: SlotUpdateDynamicValue
// ---------------------------------------------------------------------------
static void Hook_SlotUpdateDynamicValue(void *rcx, void *rdx) {
  // For this, we are completely disabling RNG shuffling for "N" target dynamic values.
  // Despite the name, it does shuffle RNG in other particular cases too, possibly hardcoded in?
  // I don't believe the game ever needs this, given the values are afaik not even show in the UI.
  // Keeping this always enabled even without the network active for consistency’s sake...
  // ...but I'll keep an eye out for side effects.
  uint32_t state[8] = {};
  GameUtils::GetRNGState(state);

  if (g_origSlotUpdateDynamicValue) {
    g_origSlotUpdateDynamicValue(rcx, rdx);
  }
  GameUtils::SetRNGState(state);
}

static void *__fastcall Hook_ProcessCombatInput(CombatUIContext *ctx, void *outResult) {
  g_isCombatUIProcessing = true;

  if (ctx && ctx->entityManager) {
    auto entities = GameUtils::GetUIAbilitySlots(ctx->entityManager);
    for (auto *ent : entities) {
      g_castableAbilities.insert(reinterpret_cast<void *>(ent));
    }
  }

  return g_origProcessCombatInput(ctx, outResult);
}

// ---------------------------------------------------------------------------
// Hook: RouteCombatInput
// ---------------------------------------------------------------------------
static void *__fastcall Hook_RouteCombatInput(CombatUIContext *ctx, void *outResult, void *param3, void *param4) {
  g_isCombatUIProcessing = false;
  g_castableAbilities.clear();

  return g_origRouteCombatInput(ctx, outResult, param3, param4);
}

// ---------------------------------------------------------------------------
// Hook: PauseGame
// ---------------------------------------------------------------------------
static void __fastcall Hook_PauseGame(void *pauseMenuScene) {
  if (g_origPauseGame) {
    g_origPauseGame(pauseMenuScene);
  }

  if (pauseMenuScene && NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (void *sceneManager = *(void **)((char *)pauseMenuScene + 0x28)) {
      void **start = *(void ***)((char *)sceneManager + 0x0);
      void **end = *(void ***)((char *)sceneManager + 0x8);
      if (start && end) {
        for (void **p = start; p != end; ++p) {
          if (void *scene = *p) {
            *((char *)scene + 0x4d8) = 0;
          }
        }
      }
    }
    *(int32_t *)((char *)pauseMenuScene + 0xa4) = 0;
  }
}

static void Hook_RunFrame(void *rcx, void *rdx) {
  if (rcx && !g_networkInitialized) {
    g_networkInitialized = true;
    Overlay::Log("Captured application instance: %p", rcx);
    NetworkManager::Get().Init(&mj, MOD_NAME "-" MOD_VERSION);
    Overlay::Setup(&mj); // Initialize overlay once network is ready or at start
    if (g_autoLobby) {
      NetworkManager::Get().HostLobby("Debug Lobby");
    }
  }

  if (g_networkInitialized) {
    NetworkManager::Get().Update();
    InputGhost::Update();
    InputGhost::SetIsHost(NetworkManager::Get().IsHost());
  }

  if (Overlay::ConsumeSaveLoad()) {
    g_loadMewtiplayerSave = true;
    GameUtils::LoadSaveFile("mewtiplayer.sav");
  }

  if (GameUtils::g_startCustomRunPending) {
    MewDirector* dir = GameUtils::GetMewDirectorSingleton();
    if (dir && dir->director && *(void**)((char*)dir + 1448) != nullptr) {
      GameUtils::g_startCustomRunPending = false;
      GameUtils::StartCustomRun(GameUtils::g_customTeamSize, GameUtils::g_customDifficulty, GameUtils::g_customCollarIndex);
    }
  }

  if (g_origRunFrame)
    g_origRunFrame(rcx, rdx);
}


// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
static void Initialize() {
  if (!MJ_Resolve(&mj))
    return;
  Overlay::Log("Initializing (API v%d)...", mj.GetVersion());

  const char *cmdLine = GetCommandLineA();

  if (strstr(cmdLine, "-packet_testing")) {
    g_packetTesting = true;
    Overlay::Log("[INIT] Packet testing Active!");
  }
  if (strstr(cmdLine, "-no_steven")) {
    g_noSteven = true;
    Overlay::Log("[INIT] No Steven Active!");
  }
  if (strstr(cmdLine, "-auto_lobby")) {
    g_autoLobby = true;
    Overlay::Log("[INIT] Auto lobby Active!");
  }
  if (strstr(cmdLine, "-talkative")) {
    g_talkative = true;
    Overlay::Log("[INIT] Talkative (verbose) logs Active!");
  }

  g_gameBase = mj.GetGameBase();
  Overlay::Log("Game base: %p", (void *)g_gameBase);

  // Signature scans //

  // "RunFrame"
  const uintptr_t runFrameRVA = ScanSignature(&mj, g_gameBase, "RunFrame",
                                        "40 53 41 56 41 57 48 83 EC 40");

  // glaiel::Character::BeginTurn(TurnKind)
  const uintptr_t beginTurnRVA =
      ScanSignature(&mj, g_gameBase, "BeginTurn",
                    "48 89 5C 24 08 89 54 24 10 55 56 57 41 54 41 55 41 56 41 "
                    "57 48 8D AC 24 B0 FC FF FF");

  // "UpdateCombatResolutionState"
  const uintptr_t updateCombatResolutionStateRVA =
      ScanSignature(&mj, g_gameBase, "UpdateCombatResolutionState",
                    "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 "
                    "48 81 EC 18 01 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 "
                    "88 44 0F 29 88 78 FF FF FF 4C 8B F1");

  // glaiel::Ability::trigger(struct glaiel::TurnAction) - RVA 0x031ED0
  const uintptr_t abilityTriggerRVA =
      ScanSignature(&mj, g_gameBase, "AbilityTrigger",
                    "48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D "
                    "AC 24 68 FD FF FF");

  // "ActionManager::enqueueAction"
  const uintptr_t enqueueActionRVA =
      ScanSignature(&mj, g_gameBase, "EnqueueAction",
                    "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 48 89 54 24 "
                    "10 57 48 83 EC 20 48 8B FA 83 3A 01");

  // "TurnStart"
  const uintptr_t turnStartRVA = ScanSignature(
      &mj, g_gameBase, "TurnStart",
      "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 28 EF FF "
      "FF B8 D8 11 00 00 E8 ? ? ? ? 48 2B E0 0F 29 B4 24 C0 11 00 00 48 8B F1");

  // "FaceDirection"
  const uintptr_t faceDirectionRVA =
      ScanSignature(&mj, g_gameBase, "FaceDirection",
                    "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC "
                    "24 00 FD FF FF");

  // "StevenSpawn"
  const uintptr_t stevenSpawnRVA = ScanSignature(
      &mj, g_gameBase, "StevenSpawn",
      "48 8B C4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FE FF FF "
      "48 81 EC E0 01 00 00 0F 29 70 B8 0F 29 78 A8 8B F2");

  // "SlotUpdateDynamicValue"
  const uintptr_t slotUpdateDynamicValueRVA = ScanSignature(
      &mj, g_gameBase, "SlotUpdateDynamicValue",
      "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 60 48 8B D9 4C 8B 41 10");

  // Mewdirector
  const uintptr_t pMewDirectorSig = ScanSignature(
      &mj, g_gameBase, "MewDirectorSingleton",
      "48 89 5C 24 10 48 89 4C 24 08 57 48 83 EC 40 48 8B CA 48 8B 05 ?? ?? ?? "
      "?? 48 8B B8 A8 05 00 00");

  // "ProcessCombatInput"
  const uintptr_t processCombatInputRVA = ScanSignature(
    &mj, g_gameBase, "ProcessCombatInput",
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 64 24 20 55 41 56 41 57 48 8D 6C 24"
    "B9 48 81 EC D0 00 00 00");

  // "RouteCombatInput"
  const uintptr_t routeCombatInputRVA = ScanSignature(
    &mj, g_gameBase, "RouteCombatInput",
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 40 48 8B 41 38");

  // "PauseGame"
  const uintptr_t pauseGameRVA = ScanSignature(
      &mj, g_gameBase, "PauseGame",
      "48 8B C4 53 48 83 EC 60 80 B9 C0 00 00 00 00 48 8B D9 0F 85");

  // "InitializeSave"
  const uintptr_t initializeSaveRVA = ScanSignature(
    &mj, g_gameBase, "InitializeSave",
    "48 8B C4 48 89 58 08 48 89 50 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81 EC 20 01 00 00");

  // glaiel::SQLSaveFile::ExecSQL
  const uintptr_t execSqlRVA = ScanSignature(
    &mj, g_gameBase, "ExecSQL",
    "48 89 5C 24 08 4C 89 44 24 18 48 89 54 24 10 55 56 57 48 8D 6C 24 F0 48 81 EC 10 01 00 00 49 8B D8");

  // "ContinueFile"
  const uintptr_t continueFileRVA = ScanSignature(
    &mj, g_gameBase, "ContinueFile",
    "48 89 5c 24 08 48 89 74 24 10 55 57 41 56 48 8d 6c 24 b9 48 81 ec b0 00 00 00 8b fa 48 8b f1");

  // "StartRun"
  const uintptr_t startRunRVA = ScanSignature(
    &mj, g_gameBase, "StartRun",
    "48 8B C4 48 89 58 20 44 89 40 18 48 89 50 10 55");

  // "ActiveSceneFunc"
  const uintptr_t activeSceneFuncRVA = ScanSignature(
    &mj, g_gameBase, "ActiveSceneFunc",
    "48 89 5C 24 10 57 48 83 EC 20 33 FF 48 8B D9 48 85 C9 75 10 48 8B 1D");

  // Custom cat loader signature scans
  const uintptr_t createStrayCatRVA = ScanSignature(
    &mj, g_gameBase, "CreateStrayCat",
    "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 30 41 8B F8 48 8B E9");

  const uintptr_t mewSaveFileLoadRVA = ScanSignature(
    &mj, g_gameBase, "MewSaveFile::Load",
    "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 56 41 57 48 8D AC 24 30 FF FF FF");

  // MewSQL signature scans
  const uintptr_t sqlOpenRVA = ScanSignature(
    &mj, g_gameBase, "SQLSaveFile::open",
    "48 89 5C 24 08 48 89 74 24 18 48 89 54 24 10 57 48 83 EC 20 48 8B DA 48 8B F1 48 8D 79 08 48 3B FA 74 16 48 83 7A 18 0F");

  const uintptr_t sqlRetrieveRVA = ScanSignature(
    &mj, g_gameBase, "SQLSaveFile::Retrieve",
    "4C 89 44 24 18 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E9 48 81 EC C8 00 00 00 49 8B F8 4C 8B FA 33 C9 89 0A 48 8D 45 CF 48 89 45 67 48 8D 05 ?? ?? ?? ?? 48 89 45 CF");

  const uintptr_t sqlCloseRVA = ScanSignature(
    &mj, g_gameBase, "CloseConnection",
    "48 89 7C 24 20 41 56 48 83 EC 30 44 8B F2 48 8B F9 48 85 C9");

  const uintptr_t destructStrRVA = ScanSignature(
    &mj, g_gameBase, "DestructString",
    "40 53 48 83 EC 20 48 8B 51 18 48 8B D9 48 83 FA 0F 76 2C 48 8B 09 48 FF C2 48 81 FA 00 10 00 00");

  const uintptr_t baseSavePathSig = ScanSignature(
    &mj, g_gameBase, "BaseSavePathLookup",
    "48 83 3D ?? ?? ?? ?? 0F 4C 0F 47 25 ?? ?? ?? ??");

  const uintptr_t gameAllocateRVA = ScanSignature(
    &mj, g_gameBase, "GameAllocate",
    "48 83 EC 28 48 85 C9 75 07 33 C0 48 83 C4 28 C3 48 81 F9 00 10 00 00");

  const uintptr_t getCollarVectorRVA = ScanSignature(
    &mj, g_gameBase, "GetCollarVector",
    "48 8B C4 48 89 58 10 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 F8 FD FF FF");

  // Resolve addresses //
  if (baseSavePathSig) {
    const uintptr_t baseSavePathAddr = ResolveRIP(g_gameBase + baseSavePathSig + 8, 4, 8);
    MewSQL::SetBaseSavePathPtr((MsvcReleaseModeXString*)baseSavePathAddr);
  } else {
    Overlay::Log("FAILED to find BaseSavePathLookup signature!");
  }

  if (sqlOpenRVA) {
    MewSQL::SetOpenPtr((MewSQL::SQLSaveFile_open_t)(g_gameBase + sqlOpenRVA));
  } else {
    Overlay::Log("FAILED to find SQLSaveFile::open signature!");
  }

  if (sqlRetrieveRVA) {
    MewSQL::SetRetrievePtr((MewSQL::Retrieve_t)(g_gameBase + sqlRetrieveRVA));
  } else {
    Overlay::Log("FAILED to find SQLSaveFile::Retrieve signature!");
  }

  if (sqlCloseRVA) {
    MewSQL::SetCloseConnectionPtr((MewSQL::CloseConnection_t)(g_gameBase + sqlCloseRVA));
  } else {
    Overlay::Log("FAILED to find CloseConnection signature!");
  }

  if (destructStrRVA) {
    const auto destructFunc = (GameUtils::DestructString_t)(g_gameBase + destructStrRVA);
    MewSQL::SetDestructStringPtr(destructFunc);
    GameUtils::SetDestructStringPtr(destructFunc);
  } else {
    Overlay::Log("FAILED to find DestructString signature!");
  }

  if (execSqlRVA) {
    MewSQL::SetExecSQLPtr((MewSQL::ExecSQL_t)(g_gameBase + execSqlRVA));
  }

  if (pMewDirectorSig) {
    const uintptr_t pMewDirectorPtr =
        ResolveRIP(g_gameBase + pMewDirectorSig + 18, 3, 7);
    GameUtils::SetMewDirectorSingletonPtr((MewDirector **)pMewDirectorPtr);
  }

  if (continueFileRVA) {
    GameUtils::SetContinueFilePtr((GameUtils::ContinueFile_t)(g_gameBase + continueFileRVA));
  } else {
    Overlay::Log("FAILED to find ContinueFile signature!");
  }

  if (startRunRVA) {
    GameUtils::SetStartRunPtr((GameUtils::StartRun_t)(g_gameBase + startRunRVA));
  } else {
    Overlay::Log("FAILED to find StartRun signature!");
  }

  if (activeSceneFuncRVA) {
    const uintptr_t activeScenePtr = ResolveRIP(g_gameBase + activeSceneFuncRVA + 20, 3, 7);
    GameUtils::SetActiveScenePtr((void**)activeScenePtr);
  } else {
    Overlay::Log("FAILED to find ActiveSceneFunc signature!");
  }

  if (mewSaveFileLoadRVA) {
    GameUtils::SetMewSaveFileLoadPtr((GameUtils::MewSaveFile_Load_t)(g_gameBase + mewSaveFileLoadRVA));
  } else {
    Overlay::Log("FAILED to find MewSaveFile::Load signature!");
  }

  if (createStrayCatRVA) {
    mj.InstallHook(createStrayCatRVA, 15,
      (void *)Hook_CreateStrayCat,
      (void **)&g_origCreateStrayCat, 10, MOD_NAME);
  } else {
    Overlay::Log("FAILED to find CreateStrayCat signature!");
  }

  if (gameAllocateRVA) {
    g_GameAllocate = (GameAllocate_t)(g_gameBase + gameAllocateRVA);
  } else {
    Overlay::Log("FAILED to find GameAllocate signature!");
  }

  if (getCollarVectorRVA) {
    mj.InstallHook(getCollarVectorRVA, 16,
      (void *)Hook_GetCollarVector,
      (void **)&g_origGetCollarVector, 10, MOD_NAME);
  } else {
    Overlay::Log("FAILED to find GetCollarVector signature!");
  }

  if (execSqlRVA) {
    GameUtils::SetExecSQLPtr((GameUtils::ExecSQL_t)(g_gameBase + execSqlRVA));
  }

  if (initializeSaveRVA) {
    mj.InstallHook(initializeSaveRVA, 16,
      (void *)Hook_InitializeSave,
      (void **)&g_origInitializeSave, 10, MOD_NAME);
  }

  if (runFrameRVA) {
    mj.InstallHook(runFrameRVA, 17,
      (void *)Hook_RunFrame,
      (void **)&g_origRunFrame, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find RunFrame!");
  }

  if (beginTurnRVA) {
    mj.InstallHook(beginTurnRVA, 16,
      (void *)Hook_BeginTurn,
      (void **)&g_origBeginTurn, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find BeginTurn!");
  }

  if (updateCombatResolutionStateRVA) {
    mj.InstallHook(updateCombatResolutionStateRVA, 15,
      (void *)Hook_UpdateCombatResolutionState,
      (void **)&g_origFightEnd, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find UpdateCombatResolutionState!");
  }

  if (abilityTriggerRVA) {
    mj.InstallHook(abilityTriggerRVA, 15,
      (void *)Hook_AbilityTrigger,
      (void **)&g_origAbilityTrigger, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find AbilityTrigger!");
  }

  if (enqueueActionRVA) {
    Overlay::Log("Found EnqueueAction at RVA: 0x%llX", enqueueActionRVA);
    mj.InstallHook(enqueueActionRVA, 15,
      (void *)Hook_EnqueueAction,
      (void **)&g_origEnqueueAction, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find EnqueueAction!");
  }

  if (turnStartRVA) {
    mj.InstallHook(turnStartRVA, 14,
      (void *)Hook_TurnStart,
      (void **)&g_origTurnStart, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find TurnStart!");
  }

  if (faceDirectionRVA) {
    mj.InstallHook(faceDirectionRVA, 14,
      (void *)Hook_FaceDirection,
      (void **)&g_origFaceDirection, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find FaceDirection!");
  }

  if (stevenSpawnRVA) {
    mj.InstallHook(stevenSpawnRVA, 16,
      (void *)Hook_StevenSpawn,
      (void **)&g_origStevenSpawn, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find StevenSpawn!");
  }

  if (slotUpdateDynamicValueRVA) {
    mj.InstallHook(slotUpdateDynamicValueRVA, 15,
      (void *)Hook_SlotUpdateDynamicValue,
      (void **)&g_origSlotUpdateDynamicValue, 10, MOD_NAME);
  }

  if (processCombatInputRVA) {
    mj.InstallHook(processCombatInputRVA, 15,
      (void *)Hook_ProcessCombatInput,
      (void **)&g_origProcessCombatInput, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find ProcessCombatInput!");
  }

  if (routeCombatInputRVA) {
    mj.InstallHook(routeCombatInputRVA, 15,
      (void *)Hook_RouteCombatInput,
      (void **)&g_origRouteCombatInput, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find RouteCombatInput!");
  }

  if (pauseGameRVA) {
    mj.InstallHook(pauseGameRVA, 15,
      (void *)Hook_PauseGame,
      (void **)&g_origPauseGame, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find PauseGame!");
  }
}

BOOL APIENTRY DllMain(HMODULE hModule, const DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    CrashHandler::Register();
    if (MJ_Resolve(&mj))
      Overlay::Log("Loading!");
    Initialize();
  } else if (reason == DLL_PROCESS_DETACH) {
    CrashHandler::Unregister();
    if (MJ_Resolve(&mj)) {
      Overlay::Log("Unloading!");
      ImGuiHook::Unload();
    }
  }
  return TRUE;
}