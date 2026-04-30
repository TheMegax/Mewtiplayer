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

typedef void(__fastcall *FightEnd_t)(void *combat);
static FightEnd_t g_origFightEnd = nullptr;

typedef void(__fastcall *Ability_Trigger_t)(void *ability, void *turnAction);
static Ability_Trigger_t g_origAbilityTrigger = nullptr;

// Tracking state for UI-initiated actions
static bool g_waitingForPlayerAction = false;
static uint64_t g_intentTimestamp = 0;

static const char *GetNarrowString(void *ptr, int offset) {
  if (!ptr)
    return "N/A";
  uintptr_t strPtr = (uintptr_t)ptr + offset;
  size_t capacity = *(size_t *)(strPtr + 24);
  if (capacity < 16) {
    return (const char *)strPtr;
  } else {
    return *(const char **)strPtr;
  }
}

static void Hook_BeginTurn(void *character, int kind) {
  if (character) {
    const wchar_t *namePtr = L"Unknown";
    size_t capacity = *(size_t *)((uintptr_t)character + 0x290 + 24);
    if (capacity < 8) {
      namePtr = (const wchar_t *)((uintptr_t)character + 0x290);
    } else {
      namePtr = *(const wchar_t **)((uintptr_t)character + 0x290);
    }

    int64_t uniqueId = -1;
    const char *className = "Collarless";
    void *persistentChar = *(void **)((uintptr_t)character + 0x88);
    if (persistentChar) {
      uniqueId = *(int64_t *)((uintptr_t)persistentChar + 128);
      className = GetNarrowString(persistentChar, 3088);
    }

    uint8_t isPlayerCat = *(uint8_t *)((uintptr_t)character + 0x489);

    Overlay::Log("BeginTurn: [%ls] UID:%lld Class:%s IsPlayerCat:%d", namePtr,
                 uniqueId, className, isPlayerCat);

    if (uniqueId != -1) {
      char narrowName[128];
      size_t converted;
      wcstombs_s(&converted, narrowName, namePtr, sizeof(narrowName));
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
// Consumes UI intent to identify player actions directly.
// ---------------------------------------------------------------------------
void __fastcall Hook_AbilityTrigger(void *ability, void *turnAction) {
  bool isSyncAction = false;
  if (g_waitingForPlayerAction) {
    isSyncAction = true;
    g_waitingForPlayerAction = false; // Consume intent
    Overlay::Log("[INTENT] Action identified via direct UI intent");
  }

  if (ability && turnAction) {
    void *definition = *(void **)((uintptr_t)ability + 0x28);
    std::string abilityName = "UNKNOWN";
    if (definition) {
      std::string *namePtr = (std::string *)((uintptr_t)definition + 0x88);
      if (namePtr && !namePtr->empty()) {
        abilityName = *namePtr;
      }
    }

    int targetX = *(int *)((uintptr_t)turnAction + 0x10);
    int targetY = *(int *)((uintptr_t)turnAction + 0x14);

    void *owner = *(void **)((uintptr_t)ability + 0x10);
    int64_t sourceUID = -1;
    if (owner) {
      void *persistentChar = *(void **)((uintptr_t)owner + 0x88);
      if (persistentChar) {
        sourceUID = *(int64_t *)((uintptr_t)persistentChar + 128);
      }
    }

    int target2X = *(int *)((uintptr_t)turnAction + 0x18);
    int target2Y = *(int *)((uintptr_t)turnAction + 0x1C);

    char buf[512];
    snprintf(buf, sizeof(buf),
             "[%s ACTION] UID:%lld | %s | T1:(%d,%d) | T2:(%d,%d)",
             isSyncAction ? "SYNC" : "AUTO", sourceUID, abilityName.c_str(),
             targetX, targetY, target2X, target2Y);
    Overlay::Log(buf);
  }

  if (g_origAbilityTrigger)
    g_origAbilityTrigger(ability, turnAction);
}

static uint64_t g_lastCombatPulse = 0;
static bool g_inCombatDetected = false;

static void Hook_UpdateCombatResolutionState(void *combat) {
  g_lastCombatPulse = GetTickCount64();

  if (!g_inCombatDetected) {
    g_inCombatDetected = true;
    if (NetworkManager::Get().IsHost()) {
      NetworkManager::Get().StartCombat();
    }
  }

  if (g_origFightEnd)
    g_origFightEnd(combat);
}

static GameUtils::ButtonGroupTracker g_moveButtons;
static GameUtils::ButtonGroupTracker g_attackButtons;
static GameUtils::ButtonGroupTracker g_spellButtons;
static GameUtils::ButtonGroupTracker g_itemButtons;
static GameUtils::ButtonGroupTracker g_endTurnButtons;

static void Hook_RunFrame(void *rcx, void *rdx) {
  if (rcx && !g_networkInitialized) {
    g_networkInitialized = true;
    Overlay::Log("Captured application instance: %p", rcx);
    NetworkManager::Get().Init(&mj, MOD_NAME "-" MOD_VERSION);
    Overlay::Setup(&mj); // Initialize overlay once network is ready or at start
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
          Overlay::Log("[Combat] Intent monitoring active");
        }
      }

      auto pollIntent = [](GameUtils::ButtonGroupTracker &tracker,
                           bool isEndTurn = false) {
        auto changes = tracker.Poll();
        for (const auto &change : changes) {
          if (isEndTurn &&
              (change.oldState == GameUtils::ButtonState_Pressed &&
               change.newState == GameUtils::ButtonState_Disabled)) {
            g_waitingForPlayerAction = false;
            Overlay::Log("[UI] End Turn intent detected - clearing window");
          } else {
            if (change.oldState == GameUtils::ButtonState_Pressed &&
                change.newState == GameUtils::ButtonState_Hovered) {
              g_waitingForPlayerAction = true;
              g_intentTimestamp = GetTickCount64();
              Overlay::Log("[UI] Action intent detected - opening window");
            }
          }
        }
      };

      pollIntent(g_moveButtons);
      pollIntent(g_attackButtons);
      pollIntent(g_spellButtons);
      pollIntent(g_itemButtons);
      pollIntent(g_endTurnButtons, true);
    }

    if (g_inCombatDetected && (GetTickCount64() - g_lastCombatPulse > 200)) {
      g_inCombatDetected = false;
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

  uintptr_t abilityTriggerRVA =
      ScanSignature(&mj, g_gameBase, "AbilityTrigger",
                    "48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D "
                    "AC 24 58 FD FF FF 48 81 EC A8 03 00 00");

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
