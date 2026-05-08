#include "GameUtils.h"
#include "ImGuiHook.h"
#include "InputGhost.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "Scanner.h"
#include "mewjector.h"
#include <stdint.h>
#include <windows.h>

#define MOD_NAME "Mewtiplayer"
#define MOD_VERSION "1.0.0"

static MewjectorAPI mj;
static UINT_PTR g_gameBase = 0;

typedef void (*RunFrame_t)(void *rcx, void *rdx);
static RunFrame_t g_origRunFrame = nullptr;
static bool g_networkInitialized = false;

typedef void(__fastcall *BeginTurn_t)(void *character, int kind);
static BeginTurn_t g_origBeginTurn = nullptr;
static TurnControl *g_currentTurnControl = nullptr;

typedef void(__fastcall *TurnStart_t)(TurnControl *tc);
static TurnStart_t g_origTurnStart = nullptr;

typedef void(__fastcall *FightEnd_t)(void *combat);
static FightEnd_t g_origFightEnd = nullptr;

typedef void *(__fastcall *AbilityTrigger_t)(void *ability, void *turnAction);
AbilityTrigger_t g_origAbilityTrigger = nullptr;

// We use this to distinguish between UI-initiated and engine-initiated actions
// It is set in Hook_EnqueueAction and consumed in Hook_AbilityTrigger
static bool g_waitingForPlayerAction = false;
bool g_isSyncActionPending = false;
static bool g_testingMode = false;

bool g_startedCombat = false;

static void Hook_TurnStart(TurnControl *tc) {
  if (tc) {
    g_currentTurnControl = tc;
    GameUtils::SetTurnControlPtr(&g_currentTurnControl);
  }
  if (g_origTurnStart)
    g_origTurnStart(tc);
}

// Why is there two different turn start functions? Idk don't ask me
static void Hook_BeginTurn(Character *character, int kind) {
  g_startedCombat = true;

  if (character) {
    std::wstring name = L"Unknown";
    auto wname = (MsvcReleaseModeXString *)&character->name;
    // Note: Layout is same, but we treat it as wchar_t*
    if (wname->_Myres < 8) {
      name = (const wchar_t *)&wname->_Bx._Buf[0];
    } else {
      name = *(const wchar_t **)&wname->_Bx._Ptr;
    }

    int64_t uniqueId = -1;
    const char *className = "Classless";
    if (character->persistentChar) {
      uniqueId = character->persistentChar->sql_key;
      className = character->persistentChar->className.begin();
    }

    uint8_t isPlayerCat = character->isPlayerCat;

    Overlay::Log("BeginTurn: [%ls] UID:%lld Class:%s IsPlayerCat:%d",
                 name.c_str(), uniqueId, className, isPlayerCat);

    if (uniqueId != -1) {
      char narrowName[128];
      size_t converted;
      wcstombs_s(&converted, narrowName, name.c_str(), sizeof(narrowName));
      NetworkManager::Get().RegisterCat(uniqueId, narrowName, className);
      if (isPlayerCat == 1) {
        NetworkManager::Get().SetActiveCat(uniqueId);
      }
    }
  }

  if (g_origBeginTurn)
    g_origBeginTurn(character, kind);
}

// ---------------------------------------------------------------------------
// Hook: AbilityTrigger
// ---------------------------------------------------------------------------
void __fastcall Hook_AbilityTrigger(Ability *ability, TurnAction *turnAction) {
  bool isSyncAction = g_isSyncActionPending;
  g_isSyncActionPending = false; // Consume for logging

  if (ability && turnAction) {
    std::string abilityName = GameUtils::GetAbilityName(ability);

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

std::deque<TurnActionPacket> g_pendingInjections;

static void LogHexDump(const void *data, size_t size, const char *label) {
  const unsigned char *p = (const unsigned char *)data;
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

static void *__fastcall Hook_EnqueueAction(void *queue,
                                           TurnAction *actionData) {
  g_lastActionQueue = queue;

  if (actionData && actionData->type <= 1 && !g_pendingInjections.empty()) {
    const TurnActionPacket &pending = g_pendingInjections.front();
    uint32_t currentNUID = NetworkManager::Get().GetNUID(actionData->actor);
    Character *pendingActor =
        NetworkManager::Get().GetCharacter(pending.actorNUID);

    if (pendingActor && pending.actorNUID == currentNUID) {
      // Resolve ability lazily — actor is now guaranteed to be in the NUID map
      std::string abilityName(pending.abilityName);
      Ability *ability = nullptr;
      if (abilityName != "NULL") {
        ability = GameUtils::FindCharacterAbility(pendingActor, abilityName);
        if (!ability) {
          Overlay::Log(
              "[ENQUEUE] Could not resolve ability '%s' for NUID %u - dropping",
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
      else
        return nullptr;
    }

    static ULONGLONG lastLogTime = 0;
    ULONGLONG currentTime = GetTickCount64();
    if (g_testingMode && (currentTime - lastLogTime >= 1000)) {
      lastLogTime = currentTime;
      Overlay::Log(
          "[ENQUEUE] Skipping injection: pending NUID %u != current NUID %u",
          pending.actorNUID, currentNUID);
    }
  }

  if (!actionData ||
      actionData->type <= 1) // Actions under or equal to 1 are null actions
    return g_origEnqueueAction ? g_origEnqueueAction(queue, actionData)
                               : nullptr;

  // Actions of type 3 are End Turn actions
  if (actionData->type == 3 && g_waitingForPlayerAction) {
    g_waitingForPlayerAction = false;
    uint32_t nuid = NetworkManager::Get().GetNUID(actionData->actor);
    if (nuid != 0xFFFFFFFF) {
      TurnActionPacket pkt;
      pkt.actorNUID = nuid;
      pkt.actionType = 3;
      memset(pkt.abilityName, 0, sizeof(pkt.abilityName));
      strncpy_s(pkt.abilityName, "NULL", _TRUNCATE);
      pkt.targetX = actionData->targetX;
      pkt.targetY = actionData->targetY;
      pkt.target2X = actionData->target2X;
      pkt.target2Y = actionData->target2Y;

      NetworkManager::Get().RecordAction(pkt);
      NetworkManager::Get().BroadcastPacket(PacketType::TurnAction, &pkt,
                                            sizeof(pkt), !g_testingMode);
      Overlay::Log("[NET] Broadcast EndTurn for NUID %u", nuid);
      if (g_testingMode) {
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
      TurnActionPacket pkt;
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
                                            sizeof(pkt), !g_testingMode);
      NetworkManager::Get().RecordAction(pkt);
      Overlay::Log("[NET] Broadcast and Recorded Action '%s' for NUID: %d",
                   pkt.abilityName, nuid);

      if (g_testingMode) {
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

static bool g_inCombatDetected = false;

static GameUtils::ButtonGroupTracker g_moveButtons;
static GameUtils::ButtonGroupTracker g_attackButtons;
static GameUtils::ButtonGroupTracker g_spellButtons;
static GameUtils::ButtonGroupTracker g_itemButtons;
static GameUtils::ButtonGroupTracker g_endTurnButtons;

static void Hook_UpdateCombatResolutionState(void *combat) {
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
    // Combat already active, check for new entities
    NetworkManager::Get().UpdateDynamicEntities();

    // Explicit end-of-fight detection
    bool victory = *(char *)((uintptr_t)combat + 0x1AA) != 0;
    bool defeat = *(char *)((uintptr_t)combat + 0x35) != 0;

    if (victory || defeat) {
      Overlay::Log("[COMBAT] Fight End Detected: %s",
                   victory ? "Victory" : "Defeat");
      g_startedCombat = false;
      g_inCombatDetected = false;
      g_waitingForPlayerAction = false;
      g_moveButtons.Reset();
      g_attackButtons.Reset();
      g_spellButtons.Reset();
      g_itemButtons.Reset();
      g_endTurnButtons.Reset();
      if (NetworkManager::Get().IsHost()) {
        NetworkManager::Get().EndCombat();
      }
    }
  }

  if (g_origFightEnd)
    g_origFightEnd(combat);
}

static void Hook_RunFrame(void *rcx, void *rdx) {
  if (rcx && !g_networkInitialized) {
    g_networkInitialized = true;
    Overlay::Log("Captured application instance: %p", rcx);
    NetworkManager::Get().Init(&mj, MOD_NAME "-" MOD_VERSION);
    Overlay::Setup(&mj); // Initialize overlay once network is ready or at start
    if (g_testingMode) {
      NetworkManager::Get().HostLobby("Debug Lobby");
    }
  }

  if (g_networkInitialized) {
    NetworkManager::Get().Update();
    InputGhost::Update();
    InputGhost::SetIsHost(NetworkManager::Get().IsHost());

    if (g_inCombatDetected) {
      if (g_moveButtons.buttons.empty()) {
        Scene *battle = GameUtils::GetSceneByName("Battle");
        if (battle) {
          g_moveButtons.Init(battle, "Combat_MoveButton");
          g_attackButtons.Init(battle, "Combat_AttackButton");
          g_spellButtons.Init(battle, "Combat_SpellButton");
          g_itemButtons.Init(battle, "Combat_ItemButton");
          g_endTurnButtons.Init(battle, "Combat_EndTurnButton");
        }
      }

      auto pollIntent = [](GameUtils::ButtonGroupTracker &tracker) {
        auto changes = tracker.Poll();
        for (const auto &change : changes) {
          if (change.oldState == GameUtils::ButtonState_Pressed &&
              (change.newState == GameUtils::ButtonState_Hovered ||
               change.newState == GameUtils::ButtonState_Disabled)) {
            Overlay::Log("[INPUT] Button '%s' clicked.", tracker.roleName);
            g_waitingForPlayerAction = true;
          }
        }
      };

      pollIntent(g_moveButtons);
      pollIntent(g_attackButtons);
      pollIntent(g_spellButtons);
      pollIntent(g_itemButtons);
      pollIntent(g_endTurnButtons);
    }
  }

  if (g_origRunFrame)
    g_origRunFrame(rcx, rdx);
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
static void Initialize(void) {
  if (!MJ_Resolve(&mj))
    return;
  Overlay::Log("Initializing (API v%d)...", mj.GetVersion());

  const char *cmdLine = GetCommandLineA();
  if (strstr(cmdLine, "-testing_mode")) {
    g_testingMode = true;
    Overlay::Log("[INIT] Testing Mode Active!");
  }

  g_gameBase = mj.GetGameBase();
  Overlay::Log("Game base: %p", (void *)g_gameBase);

  // "RunFrame" - RVA 0x9A5020
  uintptr_t runFrameRVA = ScanSignature(&mj, g_gameBase, "RunFrame",
                                        "40 53 41 56 41 57 48 83 EC 40");

  // glaiel::Character::BeginTurn(TurnKind) - RVA 0x108C30
  uintptr_t beginTurnRVA =
      ScanSignature(&mj, g_gameBase, "BeginTurn",
                    "48 89 5C 24 08 89 54 24 10 55 56 57 41 54 41 55 41 56 41 "
                    "57 48 8D AC 24 B0 FC FF FF");

  // "UpdateCombatResolutionState" - RVA 0x35F4B0
  uintptr_t updateCombatResolutionStateRVA =
      ScanSignature(&mj, g_gameBase, "UpdateCombatResolutionState",
                    "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 "
                    "48 81 EC 18 01 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 "
                    "88 44 0F 29 88 78 FF FF FF 4C 8B F1");

  // glaiel::Ability::trigger(struct glaiel::TurnAction) - RVA 0x031ed0
  uintptr_t abilityTriggerRVA =
      ScanSignature(&mj, g_gameBase, "AbilityTrigger",
                    "48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D "
                    "AC 24 58 FD FF FF 48 81 EC A8 03 00 00");

  // "ActionManager::enqueueAction" - RVA 0x8D6FE0
  uintptr_t enqueueActionRVA =
      ScanSignature(&mj, g_gameBase, "EnqueueAction",
                    "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 48 89 54 24 "
                    "10 57 48 83 EC 20 48 8B FA 83 3A 01");

  // "TurnStart" - RVA 0x8D73D0
  uintptr_t turnStartRVA = ScanSignature(
      &mj, g_gameBase, "TurnStart",
      "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 28 EF FF "
      "FF B8 D8 11 00 00 E8 ? ? ? ? 48 2B E0 0F 29 B4 24 C0 11 00 00 48 8B F1");

  // Mewdirector - RVA 0x9288B0
  uintptr_t pMewDirectorSig = ScanSignature(
      &mj, g_gameBase, "MewDirectorSingleton",
      "48 89 5C 24 10 48 89 4C 24 08 57 48 83 EC 40 48 8B CA 48 8B 05 ?? ?? ?? "
      "?? 48 8B B8 A8 05 00 00");

  if (pMewDirectorSig) {
    uintptr_t pMewDirectorPtr =
        ResolveRIP(g_gameBase + pMewDirectorSig + 18, 3, 7);
    GameUtils::SetMewDirectorSingletonPtr((MewDirector **)pMewDirectorPtr);
  }

  if (runFrameRVA) {
    mj.InstallHook(runFrameRVA, 17, (void *)Hook_RunFrame,
                   (void **)&g_origRunFrame, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find RunFrame!");
  }

  if (beginTurnRVA) {
    mj.InstallHook(beginTurnRVA, 16, (void *)Hook_BeginTurn,
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
    mj.InstallHook(abilityTriggerRVA, 15, (void *)Hook_AbilityTrigger,
                   (void **)&g_origAbilityTrigger, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find AbilityTrigger!");
  }

  if (enqueueActionRVA) {
    Overlay::Log("Found EnqueueAction at RVA: 0x%llX", enqueueActionRVA);
    mj.InstallHook(enqueueActionRVA, 15, (void *)Hook_EnqueueAction,
                   (void **)&g_origEnqueueAction, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find EnqueueAction!");
  }

  if (turnStartRVA) {
    mj.InstallHook(turnStartRVA, 14, (void *)Hook_TurnStart,
                   (void **)&g_origTurnStart, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find TurnStart!");
  }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    if (MJ_Resolve(&mj))
      Overlay::Log("Loading!");
    Initialize();
  } else if (reason == DLL_PROCESS_DETACH) {
    if (MJ_Resolve(&mj)) {
      Overlay::Log("Unloading!");
      ImGuiHook::Unload();
    }
  }
  return TRUE;
}
