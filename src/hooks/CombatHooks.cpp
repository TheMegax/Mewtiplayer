#include "hooks/CombatHooks.h"
#include "hooks/HookMacros.h"
#include "hooks/ModState.h"
#include "GameUtils.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "Scanner.h"
#include <deque>
#include <unordered_set>

static TurnControl *g_currentTurnControl = nullptr;
static bool g_isCombatUIProcessing = false;
static std::unordered_set<void *> g_castableAbilities;

// We use this to distinguish between UI-initiated and engine-initiated actions
// It is set in Hook_EnqueueAction and consumed in Hook_AbilityTrigger
static bool g_waitingForPlayerAction = false;
static bool g_isSyncActionPending = false;

static bool g_startedCombat = false;
static bool g_isQueueEmpty = false;
static bool g_inCombatDetected = false;

std::deque<ActionPacket> g_pendingInjections;
static ActionPacket g_lastSentTurnPackage = {};
static void *g_lastActionQueue = nullptr;

HOOK_DEFINE(TurnStart, void, TurnControl*)
HOOK_DEFINE(BeginTurn, void, void*, int)
HOOK_DEFINE(FightEnd, void, CombatResolutionState*)
HOOK_DEFINE(AbilityTrigger, void*, void*, void*)
HOOK_DEFINE(EnqueueAction, void*, void*, void*)
FaceDirection_t g_origFaceDirection = nullptr;
HOOK_DEFINE(SlotUpdateDynamicValue, void*, void*, void*)
HOOK_DEFINE(ProcessCombatInput, void*, CombatUIContext*, void*)
HOOK_DEFINE(RouteCombatInput, void*, CombatUIContext*, void*, void*, void*)

static void Hook_TurnStart(TurnControl *tc) {
  if (tc) {
    g_currentTurnControl = tc;
    GameUtils::SetTurnControlPtr(&g_currentTurnControl);
  }
  if (g_origTurnStart)
    g_origTurnStart(tc);
}

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
    if (g_modState.talkative && (currentTime - lastLogTime >= 1000) &&
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
                                            sizeof(pkt), !g_modState.packetTesting);
      Overlay::Log("[NET] Broadcast EndTurn for NUID %u", nuid);
      if (g_modState.packetTesting) {
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
                                            sizeof(pkt), !g_modState.packetTesting);
      ActionPacket actPkt{};
      actPkt.type = PacketType::TurnAction;
      actPkt.data.action = pkt;
      NetworkManager::Get().RecordAction(actPkt);
      Overlay::Log("[NET] Broadcast and Recorded Action '%s' for NUID: %d",
                   pkt.abilityName, nuid);

      if (g_modState.packetTesting) {
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

static void Hook_FightEnd(CombatResolutionState *combat) {
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
                                                sizeof(pkt), !g_modState.packetTesting);

          if (g_modState.talkative) {
            Overlay::Log("[FACE] Broadcast TurnFacing for NUID:%u | Target:(%d,%d)", nuid, nx, ny);
          }
        } else if (g_modState.talkative) {
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

static void Hook_SlotUpdateDynamicValue(void *rcx, void *rdx) {
  // For this, we are completely disabling RNG shuffling for "N" target dynamic values.
  // Despite the name, it does shuffle RNG in other particular cases too, possibly hardcoded in?
  // I don't believe the game ever needs this, given the values are afaik not even show in the UI.
  // Keeping this always enabled even without the network active for consistency's sake...
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
      g_castableAbilities.insert(ent);
    }
  }

  return g_origProcessCombatInput(ctx, outResult);
}

static void *__fastcall Hook_RouteCombatInput(CombatUIContext *ctx, void *outResult, void *param3, void *param4) {
  g_isCombatUIProcessing = false;
  g_castableAbilities.clear();

  return g_origRouteCombatInput(ctx, outResult, param3, param4);
}

void CombatHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
    HOOK_INSTALL(mj, gameBase, BeginTurn,
        "48 89 5C 24 08 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 B0 FC FF FF", 16);

    HOOK_INSTALL(mj, gameBase, FightEnd,
        "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81 EC 18 01 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 88 44 0F 29 88 78 FF FF FF 4C 8B F1", 15);

    HOOK_INSTALL(mj, gameBase, AbilityTrigger,
        "48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 68 FD FF FF", 15);

    HOOK_INSTALL(mj, gameBase, EnqueueAction,
        "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 48 89 54 24 10 57 48 83 EC 20 48 8B FA 83 3A 01", 15);

    HOOK_INSTALL(mj, gameBase, TurnStart,
        "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 28 EF FF FF B8 D8 11 00 00 E8 ? ? ? ? 48 2B E0 0F 29 B4 24 C0 11 00 00 48 8B F1", 14);

    HOOK_INSTALL(mj, gameBase, FaceDirection,
        "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 00 FD FF FF", 14);

    HOOK_INSTALL(mj, gameBase, SlotUpdateDynamicValue,
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 60 48 8B D9 4C 8B 41 10", 15);

    HOOK_INSTALL(mj, gameBase, ProcessCombatInput,
        "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 64 24 20 55 41 56 41 57 48 8D 6C 24 B9 48 81 EC D0 00 00 00", 15);

    HOOK_INSTALL(mj, gameBase, RouteCombatInput,
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 40 48 8B 41 38", 15);
}
