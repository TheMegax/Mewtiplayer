#include "events/CombatSubscribers.h"
#include "NetworkManager.h"
#include "ModState.h"
#include "Overlay.h"
#include <windows.h>
#include "SteamABICompat.h"
#include "mew_ui_api.h"
#include "GameUtils.h"
#include <set>

TurnControl *g_currentTurnControl = nullptr;
bool g_isCombatUIProcessing = false;
std::unordered_set<void *> g_castableAbilities;
struct PendingPassiveRecord {
    int count = 0;
    ULONGLONG lastTimestamp = 0;
};
static std::map<std::pair<uint32_t, std::string>, PendingPassiveRecord> g_pendingNaturalPassives;
// Tracks passives that were force-executed via network packet before the engine
// had a chance to trigger them naturally. When the natural trigger fires later,
// we cancel it to prevent double execution.
static std::map<std::pair<uint32_t, std::string>, PendingPassiveRecord> g_forceExecutedPassives;

static void RecordForceExecutedPassive(uint32_t nuid, const std::string& abilityName) {
    if (nuid == 0xFFFFFFFF || abilityName.empty()) return;
    auto key = std::make_pair(nuid, abilityName);
    g_forceExecutedPassives[key].count++;
    g_forceExecutedPassives[key].lastTimestamp = GetTickCount64();
    Overlay::Log("[PASSIVE] Recorded force-executed passive '%s' for NUID %u (count: %d)",
                 abilityName.c_str(), nuid, g_forceExecutedPassives[key].count);
}

static bool ConsumeForceExecutedPassive(uint32_t nuid, const std::string& abilityName) {
    if (nuid == 0xFFFFFFFF || abilityName.empty()) return false;
    auto key = std::make_pair(nuid, abilityName);
    auto it = g_forceExecutedPassives.find(key);
    if (it != g_forceExecutedPassives.end() && it->second.count > 0) {
        ULONGLONG now = GetTickCount64();
        if (now - it->second.lastTimestamp <= 10000) {
            it->second.count--;
            if (it->second.count <= 0) {
                g_forceExecutedPassives.erase(it);
            }
            return true;
        }
        g_forceExecutedPassives.erase(it);
    }
    return false;
}

void RecordNaturalPassiveTrigger(uint32_t nuid, const std::string& abilityName) {
    if (nuid == 0xFFFFFFFF || abilityName.empty()) return;

    for (auto it = g_pendingInjections.begin(); it != g_pendingInjections.end(); ++it) {
        if (it->type == PacketType::TurnAction && it->data.action.isPassive) {
            if (it->data.action.actorNUID == nuid &&
                strcmp(it->data.action.abilityName, abilityName.c_str()) == 0) {
                Overlay::Log("[TRIGGER] Immediately removed matching pending passive packet '%s' for %s (NUID %u) from queue",
                             abilityName.c_str(), NetworkManager::Get().GetCharacterNameByNUID(nuid).c_str(), nuid);
                g_pendingInjections.erase(it);
                return;
            }
        }
    }

    auto key = std::make_pair(nuid, abilityName);
    g_pendingNaturalPassives[key].count++;
    g_pendingNaturalPassives[key].lastTimestamp = GetTickCount64();
    Overlay::Log("[TRIGGER] Registered natural passive trigger '%s' for NUID %u (pending count: %d)",
                 abilityName.c_str(), nuid, g_pendingNaturalPassives[key].count);
}

bool ConsumeNaturalPassiveTrigger(uint32_t nuid, const std::string& abilityName) {
    if (nuid == 0xFFFFFFFF || abilityName.empty()) return false;
    auto key = std::make_pair(nuid, abilityName);
    auto it = g_pendingNaturalPassives.find(key);
    if (it != g_pendingNaturalPassives.end() && it->second.count > 0) {
        ULONGLONG now = GetTickCount64();
        if (now - it->second.lastTimestamp <= 10000) {
            it->second.count--;
            if (it->second.count <= 0) {
                g_pendingNaturalPassives.erase(it);
            }
            return true;
        }
        g_pendingNaturalPassives.erase(it);
    }
    return false;
}

void ResetNaturalPassiveTriggers() {
    g_pendingNaturalPassives.clear();
    g_forceExecutedPassives.clear();
}

// We use this to distinguish between UI-initiated and engine-initiated actions
// It is set in Hook_EnqueueAction and consumed in Hook_AbilityTrigger
bool g_waitingForPlayerAction = false;
bool g_isSyncActionPending = false;

// Deferred broadcast: we delay broadcasting the player's main action until
// AbilityTrigger fires so we can capture any passive reactions (e.g.
// DodgeWhenTargeted, SwapPositions) that trigger BEFORE the main action and
// broadcast them first, preserving correct ordering for the receiver.
bool g_deferredBroadcastPending = false;
TurnActionPacket g_deferredActionPkt = {};
uint32_t g_deferredActorNUID = 0xFFFFFFFF;

bool g_isMainActionActive = false;
uint32_t g_activeMainActionActorNUID = 0xFFFFFFFF;
Ability *g_activeMainActionAbilityPtr = nullptr;
std::string g_activeMainActionAbilityName;

bool g_startedCombat = false;
bool g_isQueueEmpty = false;
bool g_inCombatDetected = false;

std::deque<ActionPacket> g_pendingInjections;
ActionPacket g_lastSentTurnPackage = {};
void *g_lastActionQueue = nullptr;

std::map<glaiel::LevelUpScreen*, PersistentCharacter*> g_levelUpScreenToCat;
std::map<glaiel::AbilityChooser*, PersistentCharacter*> g_abilityChooserToCat;

ActionPacket g_lastInjectedActionPacket = {};
bool g_injectedInCurrentCall = false;
static bool g_isInjectedActionPending = false;
static Ability *g_injectedAbilityPtr = nullptr;

void NotifyEnqueueResult(void* result) {
    if (g_injectedInCurrentCall) {
        g_injectedInCurrentCall = false;
        const uint32_t nuid = g_lastInjectedActionPacket.data.action.actorNUID;
        const char* ability = g_lastInjectedActionPacket.data.action.abilityName;
        const std::string actorName = NetworkManager::Get().GetCharacterNameByNUID(nuid);

        if (!result) {
            Overlay::Log("[ENQUEUE] [WARN] Engine rejected injected action '%s' for %s (NUID %u) - retrying",
                         ability, actorName.c_str(), nuid);
            g_pendingInjections.push_front(g_lastInjectedActionPacket);
        } else {
            Overlay::Log("[ENQUEUE] Action injected successfully: '%s' for %s (NUID %u)",
                         ability, actorName.c_str(), nuid);
        }
    }
}

void ResetCombatSubscribersState() {
    g_currentTurnControl = nullptr;
    g_isCombatUIProcessing = false;
    g_castableAbilities.clear();
    g_waitingForPlayerAction = false;
    g_isSyncActionPending = false;
    g_isInjectedActionPending = false;
    g_injectedAbilityPtr = nullptr;

    g_deferredBroadcastPending = false;
    g_deferredActionPkt = {};
    g_deferredActorNUID = 0xFFFFFFFF;

    g_isMainActionActive = false;
    g_activeMainActionActorNUID = 0xFFFFFFFF;
    g_activeMainActionAbilityPtr = nullptr;
    g_activeMainActionAbilityName.clear();

    g_startedCombat = false;
    g_isQueueEmpty = false;
    g_inCombatDetected = false;

    g_pendingInjections.clear();
    g_lastSentTurnPackage = {};
    g_lastActionQueue = nullptr;

    g_levelUpScreenToCat.clear();
    g_abilityChooserToCat.clear();

    g_injectedInCurrentCall = false;
    g_lastInjectedActionPacket = {};
    ResetNaturalPassiveTriggers();
}

// ---------------------------------------------------------------------------
// CombatHooks Subscribers
// ---------------------------------------------------------------------------
void RegisterCombatSubscribers() {
    ParaboxAPI::SetEnqueueResultCallback(NotifyEnqueueResult);

    static uint32_t g_lastFrameRNG[8] = {};
    static bool g_hasLastFrameRNG = false;

    ParaboxAPI::OnRunFrame.Subscribe([](ParaboxAPI::RunFrameEvent& ev) {
        if (!NetworkManager::Get().IsCombatActive()) return;

        uint32_t currentRNG[8] = {};
        GameUtils::GetRNGState(currentRNG);

        if (!g_hasLastFrameRNG) {
            memcpy(g_lastFrameRNG, currentRNG, sizeof(currentRNG));
            g_hasLastFrameRNG = true;
            return;
        }

        if (memcmp(currentRNG, g_lastFrameRNG, sizeof(currentRNG)) != 0) {
            memcpy(g_lastFrameRNG, currentRNG, sizeof(currentRNG));
            if (g_modState.talkative) {
                Overlay::Log("[RNG] %s RNG Advanced: %08X %08X %08X %08X",
                             NetworkManager::Get().IsHost() ? "[HOST]" : "[CLIENT]",
                             currentRNG[0], currentRNG[1], currentRNG[2], currentRNG[3]);
            }
        }
    });

    ParaboxAPI::OnProcessCombatInput.Subscribe([](ParaboxAPI::ProcessCombatInputEvent& ev) {
        if (NetworkManager::Get().IsCombatActive()) {
            NetworkManager::Get().CheckAndShowDesyncPopup();
        }
    });

    ParaboxAPI::OnRouteCombatInput.Subscribe([](ParaboxAPI::RouteCombatInputEvent& ev) {
        if (NetworkManager::Get().IsCombatActive()) {
            NetworkManager::Get().CheckAndShowDesyncPopup();
        }
    });

    ParaboxAPI::OnTurnStart.Subscribe([](ParaboxAPI::TurnStartEvent& ev) {
        if (ev.tc) {
            g_currentTurnControl = ev.tc;
            GameUtils::SetTurnControlPtr(&g_currentTurnControl);
        }

        if (NetworkManager::Get().IsCombatActive()) {
            NetworkManager::Get().CheckAndShowDesyncPopup();
        }

        // static bool g_combatPopupShown = false;
        // if (!g_combatPopupShown) {
        //     g_combatPopupShown = true;
        //     const bool opened = ParaboxAPI::ShowYesNoPopup(
        //     "[img:champion] Testing stuff! [img:champion]\n Can you see this?",
        //         [] {
        //             Overlay::Log("[POPUP] User clicked YES.");
        //             ParaboxAPI::ShowOkPopup(
        //                 "You clicked YES! [img:champion]\n Awesome.",
        //                 [] { Overlay::Log("[POPUP] User clicked OK."); }
        //             );
        //         },
        //         [] {
        //             Overlay::Log("[POPUP] User clicked NO.");
        //             ParaboxAPI::ShowOkPopup("Oh okay.",
        //                 [] {
        //                     Overlay::Log("[POPUP] User clicked OK.");
        //                 });
        //         }
        //     );
        //     if (!opened) {
        //         Overlay::Log("[POPUP] ShowYesNoPopup returned false (scene was busy)");
        //     }
        // }
    });

    ParaboxAPI::OnBeginTurn.Subscribe([](ParaboxAPI::BeginTurnEvent& ev) {
        g_startedCombat = true;
        g_isCombatUIProcessing = false;
        g_deferredBroadcastPending = false; // Safety: clear stale deferred state
        g_isMainActionActive = false;
        g_activeMainActionActorNUID = 0xFFFFFFFF;
        g_activeMainActionAbilityPtr = nullptr;
        g_activeMainActionAbilityName.clear();

        if (NetworkManager::Get().IsCombatActive()) {
            NetworkManager::Get().IncrementTurnNumber();
            const uint32_t turnNum = NetworkManager::Get().GetCurrentTurnNumber();
            NetworkManager::Get().RecordTurnState(turnNum);
            if (NetworkManager::Get().IsHost()) {
                NetworkManager::Get().SendDesyncCheck(turnNum);
            }
            NetworkManager::Get().CheckAndShowDesyncPopup();
        }

        if (ev.character) {
            std::wstring name = L"Unknown";
            const auto w_name = (MsvcReleaseModeXString *)&ev.character->name;
            if (w_name->Myres < 8) {
                name = (const wchar_t *)&w_name->Bx.Buf[0];
            } else {
                name = *(const wchar_t **)&w_name->Bx.Ptr;
            }

            int64_t uniqueId = -1;
            auto className = "Classless";
            if (ev.character->persistentChar) {
                uniqueId = ev.character->persistentChar->sql_key;
                className = ev.character->persistentChar->className.begin();
            }

            const uint8_t isPlayerCat = ev.character->isPlayerCat;
            Overlay::Log("[TURN] Begin Turn: [%ls] UID:%lld Class:%s IsPlayerCat:%d",
                         name.c_str(), uniqueId, className, isPlayerCat);

            if (uniqueId != -1) {
                char narrowName[128];
                size_t converted;
                wcstombs_s(&converted, narrowName, name.c_str(), sizeof(narrowName));
                NetworkManager::Get().RegisterCat(uniqueId, narrowName, className);
            }

            if (!g_inCombatDetected) {
                g_inCombatDetected = true;
                g_startedCombat = true;
                NetworkManager::Get().StartCombat();
                g_pendingInjections.clear();
                NetworkManager::Get().ClearRecordedActions();
                NetworkManager::Get().InitializeEntityMapping();
            }

            const uint32_t nuid = NetworkManager::Get().GetNUID(ev.character);
            NetworkManager::Get().SetActiveNUID(nuid);
        }
    });

    ParaboxAPI::OnAbilityTrigger.Subscribe([](ParaboxAPI::AbilityTriggerEvent& ev) {
        bool isSyncAction = g_isSyncActionPending;
        g_isSyncActionPending = false; // Consume for logging

        if (g_isInjectedActionPending) {
            if (g_injectedAbilityPtr == nullptr || g_injectedAbilityPtr == ev.ability) {
                isSyncAction = true;
                g_isInjectedActionPending = false;
                g_injectedAbilityPtr = nullptr;
            }
        }

        if (ev.ability && ev.turnAction) {
            const std::string abilityName = GameUtils::GetAbilityName(ev.ability).to_string();
            Character *abilityOwner = ev.ability->owner;
            uint32_t triggerNUID = abilityOwner
                ? NetworkManager::Get().GetNUID(abilityOwner)
                : 0xFFFFFFFF;

            // ---- Deferred broadcast: capture passives, flush main action ----
            if (g_deferredBroadcastPending) {
                bool isMainAction =
                    (strcmp(abilityName.c_str(), g_deferredActionPkt.abilityName) == 0 &&
                     triggerNUID == g_deferredActorNUID);

                if (!isMainAction && triggerNUID != 0xFFFFFFFF) {
                    // This is a PASSIVE reaction (e.g. SwapPositions, Dodge)
                    // fired before the main action
                    TurnActionPacket passivePkt{};
                    passivePkt.actorNUID = triggerNUID;
                    passivePkt.actionType = ev.turnAction->type;
                    passivePkt.isPassive = true;
                    GameUtils::GetRNGState(passivePkt.rngState);
                    memset(passivePkt.abilityName, 0, sizeof(passivePkt.abilityName));
                    strncpy_s(passivePkt.abilityName, abilityName.c_str(), _TRUNCATE);
                    passivePkt.targetX  = ev.turnAction->targetX;
                    passivePkt.targetY  = ev.turnAction->targetY;
                    passivePkt.target2X = ev.turnAction->target2X;
                    passivePkt.target2Y = ev.turnAction->target2Y;
                    passivePkt.unk_28   = ev.turnAction->unk_28;
                    passivePkt.unk_2C   = ev.turnAction->unk_2C;
                    passivePkt.flag_30  = ev.turnAction->flag_30;
                    passivePkt.flag_31  = ev.turnAction->flag_31;
                    passivePkt.flag_32  = ev.turnAction->flag_32;
                    passivePkt.flag_33  = ev.turnAction->flag_33;
                    passivePkt.flag_34  = ev.turnAction->flag_34;
                    passivePkt.flag_35  = ev.turnAction->flag_35;
                    passivePkt.flag_36  = ev.turnAction->flag_36;

                    ActionPacket actPkt{};
                    actPkt.type = PacketType::TurnAction;
                    actPkt.data.action = passivePkt;
                    NetworkManager::Get().RecordAction(actPkt);
                    NetworkManager::Get().BroadcastPacket(
                        PacketType::TurnAction, &passivePkt, sizeof(passivePkt), true);

                    isSyncAction = true;
                    Overlay::Log("[TRIGGER] PASSIVE Broadcast: '%s' for %s (NUID %u)",
                                 abilityName.c_str(), NetworkManager::Get().GetCharacterNameByNUID(triggerNUID).c_str(), triggerNUID);
                }

                if (isMainAction) {
                    // The main action's AbilityTrigger has fired, flush the
                    // deferred broadcast now (after all passives).
                    // Refresh RNG state to reflect any advances caused by passives
                    GameUtils::GetRNGState(g_deferredActionPkt.rngState);

                    ActionPacket actPkt{};
                    actPkt.type = PacketType::TurnAction;
                    actPkt.data.action = g_deferredActionPkt;
                    NetworkManager::Get().RecordAction(actPkt);
                    NetworkManager::Get().BroadcastPacket(
                        PacketType::TurnAction, &g_deferredActionPkt,
                        sizeof(g_deferredActionPkt), true);

                    g_deferredBroadcastPending = false;
                    isSyncAction = true;
                    g_isMainActionActive = true;
                    g_activeMainActionActorNUID = g_deferredActorNUID;
                    g_activeMainActionAbilityPtr = ev.ability;
                    g_activeMainActionAbilityName = abilityName;
                    Overlay::Log("[TRIGGER] Flushed deferred broadcast: '%s' for %s (NUID %u)",
                                 g_deferredActionPkt.abilityName, NetworkManager::Get().GetCharacterNameByNUID(g_deferredActorNUID).c_str(), g_deferredActorNUID);
                }
            }
            // ---- End deferred broadcast handling ----

            // ---- Authoritative turn: broadcast all auto actions ----
            // After the deferred main action has been flushed (or when no
            // deferred broadcast is pending), any further actions that fire
            // during our turn need to be broadcast to remote clients.
            const uint32_t activeNUID = NetworkManager::Get().GetActiveNUID();
            const bool isSelfTrigger = (triggerNUID != 0xFFFFFFFF && triggerNUID == activeNUID);

            if (!g_deferredBroadcastPending && !isSyncAction &&
                triggerNUID != 0xFFFFFFFF && !isSelfTrigger) {
                const uint64_t myID = SteamUser()->GetSteamID().ConvertToUint64();
                const uint64_t ownerID = NetworkManager::Get().GetNUIDOwner(activeNUID);
                if (ownerID != 0 && ownerID == myID) {
                    TurnActionPacket autoPkt{};
                    autoPkt.actorNUID = triggerNUID;
                    autoPkt.actionType = ev.turnAction->type;
                    autoPkt.isPassive = true; // Trigger immediately on receipt via ForceAbilityTrigger
                    GameUtils::GetRNGState(autoPkt.rngState);
                    memset(autoPkt.abilityName, 0, sizeof(autoPkt.abilityName));
                    strncpy_s(autoPkt.abilityName, abilityName.c_str(), _TRUNCATE);
                    autoPkt.targetX  = ev.turnAction->targetX;
                    autoPkt.targetY  = ev.turnAction->targetY;
                    autoPkt.target2X = ev.turnAction->target2X;
                    autoPkt.target2Y = ev.turnAction->target2Y;
                    autoPkt.unk_28   = ev.turnAction->unk_28;
                    autoPkt.unk_2C   = ev.turnAction->unk_2C;
                    autoPkt.flag_30  = ev.turnAction->flag_30;
                    autoPkt.flag_31  = ev.turnAction->flag_31;
                    autoPkt.flag_32  = ev.turnAction->flag_32;
                    autoPkt.flag_33  = ev.turnAction->flag_33;
                    autoPkt.flag_34  = ev.turnAction->flag_34;
                    autoPkt.flag_35  = ev.turnAction->flag_35;
                    autoPkt.flag_36  = ev.turnAction->flag_36;

                    NetworkManager::Get().BroadcastPacket(
                        PacketType::TurnAction, &autoPkt, sizeof(autoPkt), true);

                    isSyncAction = true;
                    Overlay::Log("[TRIGGER] AUTO Broadcast: '%s' for %s (NUID %u)",
                                 abilityName.c_str(), NetworkManager::Get().GetCharacterNameByNUID(triggerNUID).c_str(), triggerNUID);
                } else if (ownerID != 0 && ownerID != myID) {
                    // Natural local trigger on remote client. Check if the passive was
                    // already force-executed via a network packet. If so, cancel the
                    // duplicate natural trigger to prevent double execution.
                    if (triggerNUID != 0xFFFFFFFF && ConsumeForceExecutedPassive(triggerNUID, abilityName)) {
                        Overlay::Log("[TRIGGER] Suppressed duplicate natural trigger '%s' for %s (NUID %u). Already force-executed via network",
                                     abilityName.c_str(), NetworkManager::Get().GetCharacterNameByNUID(triggerNUID).c_str(), triggerNUID);
                        ev.Cancel();
                        return;
                    }
                    Overlay::Log("[TRIGGER] Natural local AbilityTrigger '%s' for %s on remote client",
                                 abilityName.c_str(), NetworkManager::Get().GetCharacterNameByNUID(triggerNUID).c_str());
                    if (triggerNUID != 0xFFFFFFFF) {
                        RecordNaturalPassiveTrigger(triggerNUID, abilityName);
                    }
                }
            }

            std::string actorName = (abilityOwner) ? abilityOwner->name.to_utf8() : "Unknown";
            if (actorName.empty() || actorName == "UNKNOWN" || actorName == "NULL") {
                actorName = NetworkManager::Get().GetCharacterNameByNUID(triggerNUID);
            }
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "[ACTION] Actor:%s | %s | T1:(%d,%d) | T2:(%d,%d)",
                     actorName.c_str(), abilityName.c_str(),
                     ev.turnAction->targetX, ev.turnAction->targetY, ev.turnAction->target2X,
                     ev.turnAction->target2Y);
            Overlay::Log(buf);
        }
    });

    ParaboxAPI::OnEnqueueAction.Subscribe([](ParaboxAPI::EnqueueActionEvent& ev) {
        // ---- Authoritative turn: cancel all local actions on remote clients ----
        // Must run FIRST, before the injection check, so that suppressed actions
        // (type set to 0) fall through into the injection path below.
        // The controller broadcasts all actions. Remote clients only inject.
        // Internal engine actions (like Type 7) are allowed to resolve locally.
        if (ev.actionData && ev.actionData->type > 1 && ev.actionData->type != 7) {
            Character *actor = ev.actionData->actor;
            uint32_t actionActorNUID = actor ? NetworkManager::Get().GetNUID(actor) : 0xFFFFFFFF;

            const uint32_t activeNUID_sup = NetworkManager::Get().GetActiveNUID();
            if (activeNUID_sup != 0xFFFFFFFF) {
                const uint64_t myID_sup = SteamUser()->GetSteamID().ConvertToUint64();
                const uint64_t ownerID_sup = NetworkManager::Get().GetNUIDOwner(activeNUID_sup);
                if (ownerID_sup != 0 && ownerID_sup != myID_sup) {
                    if (g_modState.talkative) {
                        Overlay::Log("[ENQUEUE] Suppressed local action Type=%d for %s (remote turn for %s)",
                                     ev.actionData->type,
                                     NetworkManager::Get().GetCharacterNameByNUID(actionActorNUID).c_str(),
                                     NetworkManager::Get().GetCharacterNameByNUID(activeNUID_sup).c_str());
                    }
                    ev.actionData->type = 0; // Demote to idle
                }
            }
        }

        g_lastActionQueue = ev.queue;
        g_isQueueEmpty = ev.actionData && ev.actionData->type <= 1;

        if (g_isQueueEmpty && g_pendingInjections.empty() && !g_deferredBroadcastPending) {
            g_isMainActionActive = false;
            g_activeMainActionActorNUID = 0xFFFFFFFF;
            g_activeMainActionAbilityPtr = nullptr;
            g_activeMainActionAbilityName.clear();
        }

        if (ev.actionData && ev.actionData->type <= 1 && !g_pendingInjections.empty()) {
            // Peek for TurnAction specifically, or handle TurnFacing immediately
            while (!g_pendingInjections.empty() &&
                   g_pendingInjections.front().type == PacketType::TurnFacing) {
                const TurnFacingPacket &facing = g_pendingInjections.front().data.facing;
                uint32_t activeNUID = NetworkManager::Get().GetActiveNUID();

                if (facing.actorNUID == activeNUID) {
                    if (Character *c = NetworkManager::Get().GetCharacter(facing.actorNUID)) {
                        const auto ux = static_cast<uint32_t>(facing.nx);
                        const auto uy = static_cast<uint32_t>(facing.ny);
                        uint64_t packed = static_cast<uint64_t>(ux) | static_cast<uint64_t>(uy) << 32;
                        if (g_modState.talkative) {
                            Overlay::Log("[INJECT] Injecting facing for %s (NUID %u) | Target:(%d,%d)",
                                         NetworkManager::Get().GetCharacterNameByNUID(facing.actorNUID).c_str(), facing.actorNUID, facing.nx, facing.ny);
                        }
                        
                        ParaboxAPI::ForceFaceDirection(c, packed, facing.anim, facing.force);
                    }
                } else {
                    if (g_modState.talkative) {
                        Overlay::Log("[ENQUEUE] Dropping stale TurnFacing for %s (NUID %u)",
                                     NetworkManager::Get().GetCharacterNameByNUID(facing.actorNUID).c_str(), facing.actorNUID);
                    }
                }
                g_pendingInjections.pop_front();
            }

            if (!g_pendingInjections.empty() &&
                g_pendingInjections.front().type == PacketType::TurnAction) {
                const TurnActionPacket &pending = g_pendingInjections.front().data.action;

                if (pending.isPassive) {
                    if (ConsumeNaturalPassiveTrigger(pending.actorNUID, pending.abilityName)) {
                        Overlay::Log("[ENQUEUE] Dropping duplicate passive '%s' for %s (NUID %u) as already executed naturally",
                                     pending.abilityName, NetworkManager::Get().GetCharacterNameByNUID(pending.actorNUID).c_str(), pending.actorNUID);
                        g_pendingInjections.pop_front();
                        return;
                    }
                }

                uint32_t activeNUID = NetworkManager::Get().GetActiveNUID();
                uint32_t currentNUID = NetworkManager::Get().GetNUID(ev.actionData->actor);
                Character *pendingActor =
                    NetworkManager::Get().GetCharacter(pending.actorNUID);

                if (!pendingActor) {
                    Overlay::Log("[ENQUEUE] [ERROR] Could not resolve character for NUID %u - dropping action '%s'",
                                 pending.actorNUID, pending.abilityName);
                    g_pendingInjections.pop_front();
                    return;
                }
                
                if (const uint64_t ownerID = NetworkManager::Get().GetNUIDOwner(pending.actorNUID);
                    ownerID == 0 && !pendingActor->isPlayerCat && pending.actionType == 3) {
                    // For unowned AI / NPC characters (ownerID == 0 and !isPlayerCat), local AI executes on both sides.
                    // Ignore AI EndTurn packets on receipt.
                    Overlay::Log("[ENQUEUE] Ignored AI EndTurn for %s (NUID %u)",
                                 NetworkManager::Get().GetCharacterNameByNUID(pending.actorNUID).c_str(), pending.actorNUID);
                    g_pendingInjections.pop_front();
                    return;
                }
                
                if (pending.actorNUID != activeNUID && !pending.isPassive) {
                    // Non-passive actions are in-turn actions for pending.actorNUID's turn.
                    // Leave in g_pendingInjections until activeNUID matches pending.actorNUID.
                    return;
                }

                std::string abilityName(pending.abilityName);
                Ability *ability = nullptr;
                if (abilityName != "NULL" && abilityName != "EndTurn" && abilityName != "Escape" && pending.actionType != 3 && pending.actionType != 5) {
                    ability = GameUtils::FindCharacterAbility(pendingActor, abilityName.c_str());
                    if (!ability) {
                        if (Component* passive = GameUtils::FindCharacterPassive(pendingActor, abilityName.c_str())) {
                            ability = (Ability*)passive;
                        } else {
                            Overlay::Log("[ENQUEUE] [ERROR] Could not resolve ability/passive '%s' for %s (NUID %u) - dropping",
                                         pending.abilityName, NetworkManager::Get().GetCharacterNameByNUID(pending.actorNUID).c_str(), pending.actorNUID);
                            g_pendingInjections.pop_front();
                            return; // Let original function handle
                        }
                    }
                }

                ActionPacket actPktCopy = g_pendingInjections.front();
                TurnActionPacket pktCopy = pending;
                g_pendingInjections.pop_front();

                g_lastInjectedActionPacket = actPktCopy;
                g_injectedInCurrentCall = true;
                g_isInjectedActionPending = true;
                g_injectedAbilityPtr = ability;

                g_isMainActionActive = true;
                g_activeMainActionActorNUID = pktCopy.actorNUID;
                g_activeMainActionAbilityPtr = ability;
                g_activeMainActionAbilityName = pktCopy.abilityName;

                if (pktCopy.actorNUID != currentNUID) {
                    if (pktCopy.isPassive) {
                        // Record that we're force-executing this passive via network.
                        // If the engine later fires a natural trigger for the same passive,
                        // OnAbilityTrigger will consume this record and cancel the duplicate.
                        RecordForceExecutedPassive(pktCopy.actorNUID, pktCopy.abilityName);

                        // Out-of-turn passive action injection: execute directly on pendingActor via ForceAbilityTrigger
                        // to avoid passing a mismatched actor into currentNUID's queue.
                        GameUtils::SetRNGState(pktCopy.rngState);

                        TurnAction actionToRun{};
                        actionToRun.type = pktCopy.actionType;
                        actionToRun.ability = ability;
                        actionToRun.actor = pendingActor;
                        actionToRun.targetX = pktCopy.targetX;
                        actionToRun.targetY = pktCopy.targetY;
                        actionToRun.target2X = pktCopy.target2X;
                        actionToRun.target2Y = pktCopy.target2Y;
                        actionToRun.unk_28 = pktCopy.unk_28;
                        actionToRun.unk_2C = pktCopy.unk_2C;
                        actionToRun.flag_30 = pktCopy.flag_30;
                        actionToRun.flag_31 = pktCopy.flag_31;
                        actionToRun.flag_32 = pktCopy.flag_32;
                        actionToRun.flag_33 = pktCopy.flag_33;
                        actionToRun.flag_34 = pktCopy.flag_34;
                        actionToRun.flag_35 = pktCopy.flag_35;
                        actionToRun.flag_36 = pktCopy.flag_36;
                        actionToRun.magic84 = 0x544c5541; // "AULT"

                        Overlay::Log("[ENQUEUE] Executed out-of-turn passive '%s' for %s (NUID %u, current turn NUID %u) via ForceAbilityTrigger",
                                     pktCopy.abilityName, NetworkManager::Get().GetCharacterNameByNUID(pktCopy.actorNUID).c_str(), pktCopy.actorNUID, currentNUID);

                        if (ability) {
                            ParaboxAPI::ForceAbilityTrigger(ability, &actionToRun);
                        }
                        return;
                    }
                    // Mismatched currentNUID for non-passive action. Put back into queue and wait for matching currentNUID.
                    g_pendingInjections.push_front(actPktCopy);
                    g_injectedInCurrentCall = false;
                    g_isInjectedActionPending = false;
                    g_injectedAbilityPtr = nullptr;
                    return;
                }

                ev.actionData->type = pktCopy.actionType;
                ev.actionData->ability = ability;
                ev.actionData->actor = pendingActor;
                ev.actionData->targetX = pktCopy.targetX;
                ev.actionData->targetY = pktCopy.targetY;
                ev.actionData->target2X = pktCopy.target2X;
                ev.actionData->target2Y = pktCopy.target2Y;
                ev.actionData->unk_28 = pktCopy.unk_28;
                ev.actionData->unk_2C = pktCopy.unk_2C;
                ev.actionData->flag_30 = pktCopy.flag_30;
                ev.actionData->flag_31 = pktCopy.flag_31;
                ev.actionData->flag_32 = pktCopy.flag_32;
                ev.actionData->flag_33 = pktCopy.flag_33;
                ev.actionData->flag_34 = pktCopy.flag_34;
                ev.actionData->flag_35 = pktCopy.flag_35;
                ev.actionData->flag_36 = pktCopy.flag_36;
                ev.actionData->magic84 = 0x544c5541; // "AULT"
                
                GameUtils::SetRNGState(pktCopy.rngState);

                const auto fighters = GameUtils::GetFighters();
                bool isValidFighter = false;
                for (auto fighter : fighters) {
                    if (fighter == pendingActor) {
                        isValidFighter = true;
                        break;
                    }
                }

                Overlay::Log("[ENQUEUE] Injecting from queue: Type %d for NUID %u (current turn NUID %u, actor ptr %p, valid fighter: %s)",
                             pktCopy.actionType, pktCopy.actorNUID, currentNUID, (void*)pendingActor,
                             isValidFighter ? "YES" : "NO");

                if (!isValidFighter) {
                    Overlay::Log("[ENQUEUE] [WARN] Injected actor ptr %p for NUID %u is NOT in active combat scene fighters list!",
                                 (void*)pendingActor, pktCopy.actorNUID);
                }
                // It will proceed to original enqueue action automatically.
                return;
            }

            static ULONGLONG lastLogTime = 0;
            ULONGLONG currentTime = GetTickCount64();
            if (g_modState.talkative && (currentTime - lastLogTime >= 1000) &&
                !g_pendingInjections.empty()) {
                lastLogTime = currentTime;
                uint32_t currentNUID = NetworkManager::Get().GetNUID(ev.actionData->actor);
                Overlay::Log("[ENQUEUE] Skipping injection: pending NUID %u != current NUID %u",
                             g_pendingInjections.front().data.action.actorNUID,
                             currentNUID);
            }
        }

        if (!ev.actionData || ev.actionData->type <= 1)
            return;

        if (g_isCombatUIProcessing) {
            g_waitingForPlayerAction = ev.actionData->type == 3 ||
                (ev.actionData->ability != nullptr &&
                 g_castableAbilities.count(ev.actionData->ability) > 0);
        }

        if (ev.actionData->type == 3) {
            Character *actor = ev.actionData->actor;
            uint32_t nuid = actor ? NetworkManager::Get().GetNUID(actor) : 0xFFFFFFFF;
            if (nuid == 0xFFFFFFFF) {
                nuid = NetworkManager::Get().GetActiveNUID();
            }

            const uint64_t ownerID = NetworkManager::Get().GetNUIDOwner(nuid);
            const bool isPlayerCat = (actor && actor->isPlayerCat) ||
                                     (nuid != 0xFFFFFFFF && NetworkManager::Get().GetCharacter(nuid) && NetworkManager::Get().GetCharacter(nuid)->isPlayerCat);
            const bool isAI = (ownerID == 0 && !isPlayerCat);

            if (isAI) {
                // AI turns execute locally on both Host and Client. Do not broadcast AI EndTurn packets.
                g_waitingForPlayerAction = false;
                return;
            }

            const bool isInputBlocked = NetworkManager::Get().IsInputBlocked(
                SteamUser()->GetSteamID().ConvertToUint64());

            if (nuid != 0xFFFFFFFF && !isInputBlocked) {
                TurnActionPacket pkt{};
                pkt.actorNUID = nuid;
                pkt.actionType = 3;
                memset(pkt.abilityName, 0, sizeof(pkt.abilityName));
                strncpy_s(pkt.abilityName, "EndTurn", _TRUNCATE);
                pkt.targetX = ev.actionData->targetX;
                pkt.targetY = ev.actionData->targetY;
                pkt.target2X = ev.actionData->target2X;
                pkt.target2Y = ev.actionData->target2Y;
                pkt.unk_28 = ev.actionData->unk_28;
                pkt.unk_2C = ev.actionData->unk_2C;
                pkt.flag_30 = ev.actionData->flag_30;
                pkt.flag_31 = ev.actionData->flag_31;
                pkt.flag_32 = ev.actionData->flag_32;
                pkt.flag_33 = ev.actionData->flag_33;
                pkt.flag_34 = ev.actionData->flag_34;
                pkt.flag_35 = ev.actionData->flag_35;
                pkt.flag_36 = ev.actionData->flag_36;
                GameUtils::GetRNGState(pkt.rngState);

                ActionPacket actPkt{};
                actPkt.type = PacketType::TurnAction;
                actPkt.data.action = pkt;

                if (g_lastSentTurnPackage.type == PacketType::TurnFacing) {
                    NetworkManager::Get().RecordAction(g_lastSentTurnPackage);
                    g_lastSentTurnPackage.type = PacketType::Ping;
                }

                NetworkManager::Get().RecordAction(actPkt);
                NetworkManager::Get().BroadcastPacket(PacketType::TurnAction, &pkt,
                                                      sizeof(pkt), true);
                Overlay::Log("[NET] Broadcast EndTurn for NUID %u", nuid);

                g_isMainActionActive = true;
                g_activeMainActionActorNUID = nuid;
                g_activeMainActionAbilityPtr = nullptr;
                g_activeMainActionAbilityName = "EndTurn";
            }
            g_waitingForPlayerAction = false;
            return;
        }

        Ability *ability = ev.actionData->ability;
        Character *actor = ev.actionData->actor;
        if (!actor && ability)
            actor = ability->owner;

        uint32_t nuid = actor ? NetworkManager::Get().GetNUID(actor) : 0xFFFFFFFF;
        std::string actorName = actor ? actor->name.to_utf8() : "UNKNOWN";

        const uint64_t myID_enqueue = SteamUser()->GetSteamID().ConvertToUint64();
        const uint32_t activeNUID_enqueue = NetworkManager::Get().GetActiveNUID();
        const uint64_t ownerID_enqueue = NetworkManager::Get().GetNUIDOwner(activeNUID_enqueue);
        const bool isMyAuthoritativeTurn = ownerID_enqueue != 0 && ownerID_enqueue == myID_enqueue;
        const bool isTurnAction = (ev.actionData->type > 1 && ev.actionData->type != 7);

        if (g_waitingForPlayerAction || (isMyAuthoritativeTurn && isTurnAction)) {
            g_waitingForPlayerAction = false;
            g_isSyncActionPending = true;

            if (nuid != 0xFFFFFFFF) {
                TurnActionPacket pkt{};
                pkt.actorNUID = nuid;
                pkt.actionType = ev.actionData->type;

                std::string abilityName = GameUtils::GetAbilityName(ability).to_string();
                if (abilityName == "NULL" || abilityName == "UNKNOWN" || abilityName.empty()) {
                    if (ev.actionData->type == 3) {
                        abilityName = "EndTurn";
                    } else if (ev.actionData->type == 5) {
                        abilityName = "Escape";
                    }
                }
                memset(pkt.abilityName, 0, sizeof(pkt.abilityName));
                strncpy_s(pkt.abilityName, abilityName.c_str(), _TRUNCATE);

                pkt.targetX = ev.actionData->targetX;
                pkt.targetY = ev.actionData->targetY;
                pkt.target2X = ev.actionData->target2X;
                pkt.target2Y = ev.actionData->target2Y;
                pkt.unk_28 = ev.actionData->unk_28;
                pkt.unk_2C = ev.actionData->unk_2C;
                pkt.flag_30 = ev.actionData->flag_30;
                pkt.flag_31 = ev.actionData->flag_31;
                pkt.flag_32 = ev.actionData->flag_32;
                pkt.flag_33 = ev.actionData->flag_33;
                pkt.flag_34 = ev.actionData->flag_34;
                pkt.flag_35 = ev.actionData->flag_35;
                pkt.flag_36 = ev.actionData->flag_36;
                GameUtils::GetRNGState(pkt.rngState);

                Overlay::Log("[ENQUEUE] SYNC Action: Actor=%s | Ability=%s | "
                             "T1=(%d,%d) | T2=(%d,%d) | Type=%d",
                             actorName.c_str(), pkt.abilityName,
                             ev.actionData->targetX, ev.actionData->targetY,
                             ev.actionData->target2X, ev.actionData->target2Y,
                             ev.actionData->type);

                if (!ability || strcmp(pkt.abilityName, "NULL") == 0 || strcmp(pkt.abilityName, "EndTurn") == 0 || strcmp(pkt.abilityName, "Escape") == 0 || pkt.actionType == 5 || pkt.actionType == 3) {
                    // Non-ability actions (such as Escape, Type 5) do not trigger OnAbilityTrigger.
                    // Broadcast them immediately so remote peers receive the action.
                    ActionPacket actPkt{};
                    actPkt.type = PacketType::TurnAction;
                    actPkt.data.action = pkt;
                    NetworkManager::Get().RecordAction(actPkt);
                    NetworkManager::Get().BroadcastPacket(PacketType::TurnAction, &pkt,
                                                          sizeof(pkt), true);

                    g_isMainActionActive = true;
                    g_activeMainActionActorNUID = nuid;
                    g_activeMainActionAbilityPtr = ability;
                    g_activeMainActionAbilityName = pkt.abilityName;

                    Overlay::Log("[NET] Broadcast non-ability action '%s' (Type %d) for NUID: %d",
                                 pkt.abilityName, pkt.actionType, nuid);
                } else {
                    // Defer the broadcast until OnAbilityTrigger fires.
                    // This lets us capture any passive reactions (e.g.
                    // SwapPositions, DodgeWhenTargeted) that fire BEFORE
                    // the main action and broadcast them first.
                    g_deferredBroadcastPending = true;
                    g_deferredActionPkt = pkt;
                    g_deferredActorNUID = nuid;

                    g_isMainActionActive = true;
                    g_activeMainActionActorNUID = nuid;
                    g_activeMainActionAbilityPtr = ability;
                    g_activeMainActionAbilityName = pkt.abilityName;
                    Overlay::Log("[NET] Deferred broadcast of '%s' for NUID: %d",
                                 pkt.abilityName, nuid);
                }
            }
        } else {
            g_isSyncActionPending = false;
            Overlay::Log("[ENQUEUE] AUTO Action: Actor=%s | Type=%d", actorName.c_str(),
                         ev.actionData->type);
        }
    });

    ParaboxAPI::OnFightEnd.Subscribe([](ParaboxAPI::FightEndEvent& ev) {
        if (!g_startedCombat) return;

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

            if (ev.combat && (ev.combat->victory || ev.combat->defeat)) {
                Overlay::Log("[COMBAT] Fight End Detected: %s",
                             ev.combat->victory ? "Victory" : "Defeat");

                // Both Host and Client perform full cleanup immediately.
                // Host's EndCombat also broadcasts CombatEnd packet to peers.
                NetworkManager::Get().EndCombat();
            }
        }
    });

    ParaboxAPI::OnFaceDirection.Subscribe([](ParaboxAPI::FaceDirectionEvent& ev) {
        if (ev.character) {
            const auto x = static_cast<uint32_t>(ev.target_packed & 0xFFFFFFFF);
            const auto y = static_cast<uint32_t>(ev.target_packed >> 32);
            const int sx = static_cast<int>(x);
            const int sy = static_cast<int>(y);
            int nx = (sx > 0) ? 1 : (sx < 0 ? -1 : 0);
            int ny = (sy > 0) ? 1 : (sy < 0 ? -1 : 0);

            auto *c = static_cast<Character *>(ev.character);
            const uint32_t nuid = NetworkManager::Get().GetNUID(c);
            const bool isLocalActiveNUID =
                nuid != 0xFFFFFFFF && nuid == NetworkManager::Get().GetActiveNUID();

            if (nx != 0 || ny != 0) {
                if (auto &lastFacing = NetworkManager::Get().GetLastFacingMap();
                    lastFacing.count(nuid) && lastFacing[nuid].first == nx && lastFacing[nuid].second == ny && !ev.force) {
                } else {
                    lastFacing[nuid] = {nx, ny};

                    if (isLocalActiveNUID && g_isCombatUIProcessing && g_isQueueEmpty && g_pendingInjections.empty()) {
                        TurnFacingPacket pkt{};
                        pkt.actorNUID = nuid;
                        pkt.nx = nx;
                        pkt.ny = ny;
                        pkt.anim = ev.play_animation;
                        pkt.force = ev.force;

                        ActionPacket actPkt{};
                        actPkt.type = PacketType::TurnFacing;
                        actPkt.data.facing = pkt;
                        // ReSharper disable once CppSomeObjectMembersMightNotBeInitialized
                        g_lastSentTurnPackage = actPkt;
                        NetworkManager::Get().BroadcastPacket(PacketType::TurnFacing, &pkt,
                                                              sizeof(pkt), true);

                        if (g_modState.talkative) {
                            Overlay::Log("[FACE] Broadcast TurnFacing for NUID:%u | Target:(%d,%d)", nuid, nx, ny);
                        }
                    } else if (g_modState.talkative) {
                        if (isLocalActiveNUID) {
                            Overlay::Log("[FACE] Active NUID (%d), Queue not empty | Target:(%d,%d)", nuid, nx, ny);
                        } else if (nuid != 0xFFFFFFFF) {
                            Overlay::Log("[FACE] Queue empty, NUID not active (%d) | Target:(%d,%d)", nuid, nx, ny);
                        }
                    }
                }
            }
        }
    });

    ParaboxAPI::OnSlotUpdateDynamicValue.Subscribe([](ParaboxAPI::SlotUpdateDynamicValueEvent& ev) {
        // Prevent multiplayer desync caused by UI updates polling RNG
        uint32_t state[8] = {};
        GameUtils::GetRNGState(state);
        
        ev.Cancel();
        ParaboxAPI::ForceSlotUpdateDynamicValue(ev.rcx, ev.rdx);
        
        GameUtils::SetRNGState(state);
    });

    ParaboxAPI::OnProcessCombatInput.Subscribe([](ParaboxAPI::ProcessCombatInputEvent& ev) {
        if (NetworkManager::Get().IsInputBlocked(
                SteamUser()->GetSteamID().ConvertToUint64())) {
            if (ev.outResult) {
                auto *out = static_cast<uint32_t *>(ev.outResult);
                memset(out, 0, 132);
                out[0] = 1;
                ev.returnValue = ev.outResult;
            }
            ev.Cancel();
            return;
        }
        g_isCombatUIProcessing = true;
        if (ev.ctx && ev.ctx->entityManager) {
            auto entities = GameUtils::GetUIAbilitySlots(ev.ctx->entityManager);
            for (auto *ent : entities) {
                g_castableAbilities.insert(ent);
            }
        }
    });

    ParaboxAPI::OnRouteCombatInput.Subscribe([](ParaboxAPI::RouteCombatInputEvent& ev) {
        g_isCombatUIProcessing = false;
        g_castableAbilities.clear();

        if (NetworkManager::Get().IsInputBlocked(
                SteamUser()->GetSteamID().ConvertToUint64())) {
            if (ev.outResult) {
                auto *out = static_cast<uint32_t *>(ev.outResult);
                memset(out, 0, 132);
                out[0] = 3;
                ev.returnValue = ev.outResult;
            }
            ev.Cancel();
        }
    });

}
