/*
 * Turn loop sync notes:
 *
 * When our cat is active, PlayerBrain::OnRequestAction runs normally and returns WAITING
 * each tick until someone clicks an ability, ends turn, or flees. Once they pick something,
 * OnPostProcessCombatInput grabs the choice and current RNG seed, then blasts it across Steam.
 * Local game runs it immediately.
 *
 * For remote cats, OnProcessCombatInput cuts in before the UI gets touched. If their packet
 * is still in flight, we feed WAITING back to TurnControl so the game idles for the frame.
 * When the packet shows up, we restore their RNG seed, dump the action into outResult, and
 * cancel the original function. The engine processes it like a local click.
 *
 * Passives, reactions, and chained moves stay local. Because both clients kick off the
 * exact same action with matching RNG, every follow-up from damage rolls to CompleteAbilityNow
 * chains plays out the same way on both ends without extra network packets.
 */

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

bool g_waitingForPlayerAction = false;
bool g_isSyncActionPending = false;

bool g_startedCombat = false;
bool g_isQueueEmpty = false;
bool g_inCombatDetected = false;

std::deque<ActionPacket> g_pendingInjections;
ActionPacket g_lastSentTurnPackage = {};
void *g_lastActionQueue = nullptr;

std::map<LevelUpScreen*, CatData*> g_levelUpScreenToCat;
std::map<AbilityChooser*, CatData*> g_abilityChooserToCat;

void ResetCombatSubscribersState() {
    g_currentTurnControl = nullptr;
    g_isCombatUIProcessing = false;
    g_castableAbilities.clear();
    g_waitingForPlayerAction = false;
    g_isSyncActionPending = false;

    g_startedCombat = false;
    g_isQueueEmpty = false;
    g_inCombatDetected = false;

    g_pendingInjections.clear();
    g_lastSentTurnPackage = {};
    g_lastActionQueue = nullptr;

    g_levelUpScreenToCat.clear();
    g_abilityChooserToCat.clear();
}

// Hook subscriptions
void RegisterCombatSubscribers() {
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

    ParaboxAPI::OnTurnStart.Subscribe([](ParaboxAPI::TurnStartEvent& ev) {
        if (ev.tc) {
            g_currentTurnControl = ev.tc;
            GameUtils::SetTurnControlPtr(&g_currentTurnControl);
        }

        if (NetworkManager::Get().IsCombatActive()) {
            NetworkManager::Get().CheckAndShowDesyncPopup();
        }
    });

    ParaboxAPI::OnBeginTurn.Subscribe([](ParaboxAPI::BeginTurnEvent& ev) {
        g_startedCombat = true;
        g_isCombatUIProcessing = false;

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
            std::string name = "Unknown";
            if (ev.character->display_name.is_valid()) {
                name = ev.character->display_name.to_utf8();
            }
            if (name.empty() && ev.character->pcat_data && ev.character->pcat_data->name_.is_valid()) {
                name = ev.character->pcat_data->name_.to_utf8();
            }
            if (name.empty() && ev.character->name.is_valid()) {
                name = ev.character->name.copy_to_native_string();
            }
            if (name.empty()) {
                name = "Unknown";
            }

            int64_t uniqueId = -1;
            const char *className = "Classless";
            if (ev.character->pcat_data) {
                uniqueId = ev.character->pcat_data->cat_uid;
                if (ev.character->pcat_data->cat_class.is_valid()) {
                    className = ev.character->pcat_data->cat_class.begin();
                }
            }

            const uint8_t isPlayerCat = ev.character->is_player_cat;
            Overlay::Log("[TURN] Begin Turn: [%s] UID:%lld Class:%s IsPlayerCat:%d",
                         name.c_str(), uniqueId, className, isPlayerCat);

            if (uniqueId != -1) {
                NetworkManager::Get().RegisterCat(uniqueId, name.c_str(), className);
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
        if (!NetworkManager::Get().IsCombatActive()) return;

        if (ev.ability && ev.turnAction) {
            const std::string abilityName = GameUtils::GetAbilityName(ev.ability).to_string();
            Character *abilityOwner = ev.ability->character;
            uint32_t triggerNUID = abilityOwner ? NetworkManager::Get().GetNUID(abilityOwner) : 0xFFFFFFFF;
            std::string actorName = (abilityOwner) ? abilityOwner->display_name.to_utf8() : "Unknown";
            if (actorName.empty() || actorName == "UNKNOWN") {
                actorName = NetworkManager::Get().GetCharacterNameByNUID(triggerNUID);
            }

            char buf[512];
            snprintf(buf, sizeof(buf),
                     "[ACTION] Actor:%s | %s | T1:(%d,%d) | T2:(%d,%d)",
                     actorName.c_str(), abilityName.c_str(),
                     ev.turnAction->tile.x, ev.turnAction->tile.y,
                     ev.turnAction->orientation.x, ev.turnAction->orientation.y);
            Overlay::Log(buf);
        }
    });

    ParaboxAPI::OnEnqueueAction.Subscribe([](ParaboxAPI::EnqueueActionEvent& ev) {
        g_lastActionQueue = ev.queue;
        g_isQueueEmpty = (ev.actionData && ev.actionData->kind <= ActionKind::WAITING);

        if (ev.actionData && !g_isQueueEmpty && g_modState.talkative) {
            Overlay::Log("[QUEUE] Enqueued action: Kind=%d | Target=(%d,%d)",
                         ev.actionData->kind, ev.actionData->tile.x, ev.actionData->tile.y);
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

            if (ev.level && (ev.level->won || ev.level->lost)) {
                Overlay::Log("[COMBAT] Fight End Detected: %s",
                             ev.level->won ? "Victory" : "Defeat");

                // Host tells peers the fight ended, then both clean up state.
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
        // Tooltips burn RNG numbers if we don't preserve state across this call.
        uint32_t state[8] = {};
        GameUtils::GetRNGState(state);
        
        ev.Cancel();
        ParaboxAPI::ForceSlotUpdateDynamicValue(ev.rcx, ev.rdx);
        
        GameUtils::SetRNGState(state);
    });

    ParaboxAPI::OnProcessCombatInput.Subscribe([](ParaboxAPI::ProcessCombatInputEvent& ev) {
        if (!NetworkManager::Get().IsCombatActive()) return;

        NetworkManager::Get().CheckAndShowDesyncPopup();

        Character *character = ev.ctx ? ev.ctx->character : nullptr;
        uint32_t charNUID = character ? NetworkManager::Get().GetNUID(character) : 0xFFFFFFFF;
        if (charNUID == 0xFFFFFFFF) {
            charNUID = NetworkManager::Get().GetActiveNUID();
        }

        const uint64_t mySteamID = SteamUser()->GetSteamID().ConvertToUint64();
        const uint64_t ownerID = NetworkManager::Get().GetNUIDOwner(charNUID);

        bool isRemoteTurn = false;
        if (ownerID != 0) {
            isRemoteTurn = (ownerID != mySteamID);
        } else {
            // Host runs unassigned cats.
            isRemoteTurn = !NetworkManager::Get().IsHost();
        }

        if (NetworkManager::Get().IsInputBlocked(mySteamID)) {
            isRemoteTurn = true;
        }

        if (isRemoteTurn) {
            g_isCombatUIProcessing = false;
            g_castableAbilities.clear();

            // Flush any facing updates sent while aiming.
            while (!g_pendingInjections.empty() && g_pendingInjections.front().type == PacketType::TurnFacing) {
                const TurnFacingPacket &facing = g_pendingInjections.front().data.facing;
                if (Character *c = NetworkManager::Get().GetCharacter(facing.actorNUID)) {
                    const auto ux = static_cast<uint32_t>(facing.nx);
                    const auto uy = static_cast<uint32_t>(facing.ny);
                    uint64_t packed = static_cast<uint64_t>(ux) | (static_cast<uint64_t>(uy) << 32);
                    ParaboxAPI::ForceFaceDirection(c, packed, facing.anim, facing.force);
                }
                g_pendingInjections.pop_front();
            }

            if (!g_pendingInjections.empty() && g_pendingInjections.front().type == PacketType::TurnAction) {
                const TurnActionPacket &pkt = g_pendingInjections.front().data.action;

                if (pkt.actorNUID == charNUID || charNUID == 0xFFFFFFFF) {
                    TurnActionPacket pktCopy = pkt;
                    g_pendingInjections.pop_front();

                    // Match sender RNG so hit and damage rolls stay identical.
                    GameUtils::SetRNGState(pktCopy.rngState);

                    Ability *ability = nullptr;
                    if (character && pktCopy.abilityName[0] != '\0' &&
                        strcmp(pktCopy.abilityName, "EndTurn") != 0 &&
                        strcmp(pktCopy.abilityName, "Escape") != 0 &&
                        strcmp(pktCopy.abilityName, "NULL") != 0 &&
                        pktCopy.actionType != ActionKind::END_TURN &&
                        pktCopy.actionType != ActionKind::END_TURN_MANUAL &&
                        pktCopy.actionType != ActionKind::RUN_AWAY) {
                        ability = GameUtils::FindCharacterAbility(character, pktCopy.abilityName);
                        if (!ability) {
                            if (Component *passive = GameUtils::FindCharacterPassive(character, pktCopy.abilityName)) {
                                ability = reinterpret_cast<Ability*>(passive);
                            }
                        }
                    }

                    if (ev.outResult) {
                        auto *out = static_cast<TurnAction*>(ev.outResult);
                        memset(out, 0, sizeof(TurnAction));
                        out->kind = pktCopy.actionType;
                        out->ability = ability;
                        out->source.obj = character;
                        out->source.generation = pktCopy.actorId;
                        out->tile.x = pktCopy.targetX;
                        out->tile.y = pktCopy.targetY;
                        out->orientation.x = pktCopy.target2X;
                        out->orientation.y = pktCopy.target2Y;
                        out->no_cost = pktCopy.noCost;
                        out->prime_trigger = pktCopy.primeTrigger;
                        out->is_chain = pktCopy.isChain;
                        out->even_if_dead = pktCopy.evenIfDead;
                        out->force_display_name = pktCopy.forceDisplayName;
                        out->auto_recompute_target = pktCopy.autoRecomputeTarget;
                        out->respect_prime_when_nocost = pktCopy.respectPrimeWhenNoCost;
                        out->intentional = pktCopy.intentional;
                        out->additional_data = pktCopy.additionalData;
                        out->animate = pktCopy.animate;

                        ev.returnValue = ev.outResult;
                    }

                    Overlay::Log("[INPUT] Injected remote action: '%s' (Type %d) for %s (NUID %u)",
                                 pktCopy.abilityName, pktCopy.actionType,
                                 NetworkManager::Get().GetCharacterNameByNUID(pktCopy.actorNUID).c_str(),
                                 pktCopy.actorNUID);

                    ev.Cancel();
                    return;
                }
            }

            // Tell TurnControl to wait for next tick.
            if (ev.outResult) {
                auto *out = static_cast<TurnAction*>(ev.outResult);
                memset(out, 0, sizeof(TurnAction));
                out->kind = ActionKind::WAITING;
                ev.returnValue = ev.outResult;
            }
            ev.Cancel();
            return;
        }

        // Local turn, open up UI targeting.
        g_isCombatUIProcessing = true;
        if (ev.ctx && ev.ctx->character) {
            auto entities = GameUtils::GetCharacterAbilities(ev.ctx->character);
            for (auto *ent : entities) {
                g_castableAbilities.insert(ent);
            }
        }
    });

    ParaboxAPI::OnPostProcessCombatInput.Subscribe([](ParaboxAPI::PostProcessCombatInputEvent& ev) {
        if (!NetworkManager::Get().IsCombatActive()) return;

        if (!ev.actionData || ev.actionData->kind <= ActionKind::WAITING) {
            return;
        }

        Character *character = ev.ctx ? ev.ctx->character : nullptr;
        uint32_t charNUID = character ? NetworkManager::Get().GetNUID(character) : 0xFFFFFFFF;
        if (charNUID == 0xFFFFFFFF) {
            charNUID = NetworkManager::Get().GetActiveNUID();
        }

        const uint64_t mySteamID = SteamUser()->GetSteamID().ConvertToUint64();
        const uint64_t ownerID = NetworkManager::Get().GetNUIDOwner(charNUID);

        // Don't broadcast turns we don't own.
        if (ownerID != 0 && ownerID != mySteamID) {
            return;
        }
        if (ownerID == 0 && !NetworkManager::Get().IsHost()) {
            return;
        }

        g_isCombatUIProcessing = false;
        g_castableAbilities.clear();

        TurnActionPacket pkt{};
        pkt.actorNUID = charNUID;
        pkt.actionType = ev.actionData->kind;

        std::string abilityName;
        if (ev.actionData->ability) {
            abilityName = GameUtils::GetAbilityName(ev.actionData->ability).to_string();
        }
        if (abilityName.empty() || abilityName == "UNKNOWN" || abilityName == "NULL") {
            if (ev.actionData->kind == ActionKind::END_TURN || ev.actionData->kind == ActionKind::END_TURN_MANUAL) {
                abilityName = "EndTurn";
            } else if (ev.actionData->kind == ActionKind::RUN_AWAY) {
                abilityName = "Escape";
            }
        }
        memset(pkt.abilityName, 0, sizeof(pkt.abilityName));
        strncpy_s(pkt.abilityName, abilityName.c_str(), _TRUNCATE);

        pkt.targetX = ev.actionData->tile.x;
        pkt.targetY = ev.actionData->tile.y;
        pkt.target2X = ev.actionData->orientation.x;
        pkt.target2Y = ev.actionData->orientation.y;
        pkt.actorId = ev.actionData->source.generation;
        pkt.noCost = ev.actionData->no_cost;
        pkt.primeTrigger = ev.actionData->prime_trigger;
        pkt.isChain = ev.actionData->is_chain;
        pkt.evenIfDead = ev.actionData->even_if_dead;
        pkt.forceDisplayName = ev.actionData->force_display_name;
        pkt.autoRecomputeTarget = ev.actionData->auto_recompute_target;
        pkt.respectPrimeWhenNoCost = ev.actionData->respect_prime_when_nocost;
        pkt.intentional = ev.actionData->intentional;
        pkt.additionalData = ev.actionData->additional_data;
        pkt.animate = ev.actionData->animate;

        // Grab RNG seed right as the action locks in.
        GameUtils::GetRNGState(pkt.rngState);

        ActionPacket actPkt{};
        actPkt.type = PacketType::TurnAction;
        actPkt.data.action = pkt;

        NetworkManager::Get().RecordAction(actPkt);
        NetworkManager::Get().BroadcastPacket(PacketType::TurnAction, &pkt, sizeof(pkt), true);

        std::string actorName = character ? character->display_name.to_utf8() : "";
        if (actorName.empty() || actorName == "UNKNOWN") {
            actorName = NetworkManager::Get().GetCharacterNameByNUID(charNUID);
        }

        Overlay::Log("[ACTION] Broadcast player choice: '%s' (Type %d) for %s (NUID %u) | Target:(%d,%d)",
                     pkt.abilityName, pkt.actionType, actorName.c_str(), charNUID,
                     pkt.targetX, pkt.targetY);
    });

    ParaboxAPI::OnRouteCombatInput.Subscribe([](ParaboxAPI::RouteCombatInputEvent& ev) {
        if (!NetworkManager::Get().IsCombatActive()) return;

        NetworkManager::Get().CheckAndShowDesyncPopup();

        const uint64_t mySteamID = SteamUser()->GetSteamID().ConvertToUint64();
        if (NetworkManager::Get().IsInputBlocked(mySteamID)) {
            if (ev.outResult) {
                auto *out = static_cast<TurnAction *>(ev.outResult);
                memset(out, 0, sizeof(TurnAction));
                out->kind = ActionKind::WAITING;
                ev.returnValue = ev.outResult;
            }
            ev.Cancel();
        }
    });

}
