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
#include "GameSymbols.h"
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

CombatMenu *g_activeCombatMenu = nullptr;
TurnAbilitySelectPacket g_lastSentSelectPacket = {};

bool IsRemoteTurn(Character *character = nullptr) {
    if (!NetworkManager::Get().IsCombatActive()) return false;

    const uint32_t activeNUID = NetworkManager::Get().GetActiveNUID();
    if (activeNUID == 0xFFFFFFFF) return false;

    if (character) {
        uint32_t charNUID = NetworkManager::Get().GetNUID(character);
        if (charNUID != 0xFFFFFFFF && charNUID != activeNUID) {
            return false;
        }
    }

    const uint64_t mySteamID = SteamUser()->GetSteamID().ConvertToUint64();
    if (NetworkManager::Get().IsInputBlocked(mySteamID)) {
        return true;
    }

    const uint64_t ownerID = NetworkManager::Get().GetNUIDOwner(activeNUID);
    if (ownerID != 0) {
        return (ownerID != mySteamID);
    }
    return !NetworkManager::Get().IsHost();
}

CombatMenu *GetActiveCombatMenu(PlayerBrain *brain = nullptr) {
    if (g_activeCombatMenu) return g_activeCombatMenu;
    if (!brain && NetworkManager::Get().IsCombatActive()) {
        const uint32_t activeNUID = NetworkManager::Get().GetActiveNUID();
        Character *activeChar = (activeNUID != 0xFFFFFFFF) ? NetworkManager::Get().GetCharacter(activeNUID) : nullptr;
        if (activeChar && activeChar->brain) {
            brain = reinterpret_cast<PlayerBrain*>(activeChar->brain);
        }
    }
    if (brain) {
        static auto findCombatMenu = reinterpret_cast<CombatMenu*(*)(void*)>(
            GameSymbols::Component_FindCombatMenu + reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr)));
        if (findCombatMenu) {
            CombatMenu *menu = findCombatMenu(brain);
            if (menu && !g_activeCombatMenu) {
                g_activeCombatMenu = menu;
            }
            return menu;
        }
    }
    return nullptr;
}

bool IsCombatMenuButton(void *button) {
    if (!button) return false;
    CombatMenu *menu = GetActiveCombatMenu();
    if (!menu) return false;
    if (!menu->buttons.Myfirst || !menu->buttons.Mylast || menu->buttons.Myfirst > menu->buttons.Mylast) {
        return false;
    }
    for (Button *b : menu->buttons) {
        if (reinterpret_cast<void*>(b) == button) {
            return true;
        }
    }
    return false;
}

static void RenderRemoteAbilityOverlay(PlayerBrain *brain) {
    if (!NetworkManager::Get().IsCombatActive()) return;
    if (!brain || !brain->character) return;
    if (!IsRemoteTurn(brain->character)) return;

    CombatMenu *menu = GetActiveCombatMenu(brain);
    const TurnAbilitySelectPacket &remoteSelect = NetworkManager::Get().GetRemoteAbilitySelect();
    const uint32_t charNUID = NetworkManager::Get().GetNUID(brain->character);

    if (remoteSelect.isSelected && (remoteSelect.actorNUID == charNUID || charNUID == 0xFFFFFFFF)) {
        Ability *selAbility = nullptr;
        if (remoteSelect.abilityName[0] != '\0') {
            selAbility = GameUtils::FindCharacterAbility(brain->character, remoteSelect.abilityName);
            if (!selAbility) {
                if (Component *passive = GameUtils::FindCharacterPassive(brain->character, remoteSelect.abilityName)) {
                    selAbility = reinterpret_cast<Ability*>(passive);
                }
            }
        }

        if (menu) {
            menu->currently_casting = selAbility;
        }

        if (selAbility) {
            // Check & bypass ImmediateModeGameUI->dont_draw (+0x3C)
            static auto findGameUI = reinterpret_cast<void*(*)(void*)>(
                GameSymbols::Component_FindImmediateModeGameUI + reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr)));
            void* gameUI = findGameUI ? findGameUI(brain) : nullptr;
            bool *dontDrawPtr = gameUI ? reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(gameUI) + 0x3C) : nullptr;
            const bool origDontDraw = dontDrawPtr ? *dontDrawPtr : false;
            if (dontDrawPtr && origDontDraw) {
                *dontDrawPtr = false;
            }

            GameUtils::DrawAbilityRange(brain, selAbility, -1);
            if (remoteSelect.hasTargetTile) {
                iVec2D targetTile{remoteSelect.targetTileX, remoteSelect.targetTileY};
                iVec2D orientation{remoteSelect.orientX, remoteSelect.orientY};
                if (orientation.x == 0 && orientation.y == 0 && brain->character) {
                    orientation = brain->character->orientation;
                }
                GameUtils::DrawAbilityAOE(brain, selAbility, targetTile, orientation, 0x13);
            }

            if (remoteSelect.orientX != 0 || remoteSelect.orientY != 0) {
                if (brain->character->orientation.x != remoteSelect.orientX || brain->character->orientation.y != remoteSelect.orientY) {
                    uint64_t packed = static_cast<uint64_t>(static_cast<uint32_t>(remoteSelect.orientX)) |
                                     (static_cast<uint64_t>(static_cast<uint32_t>(remoteSelect.orientY)) << 32);
                    ParaboxAPI::ForceFaceDirection(brain->character, packed, false, false);
                }
            }

            if (dontDrawPtr && origDontDraw) {
                *dontDrawPtr = origDontDraw;
            }
        }
    } else {
        if (menu && menu->currently_casting) {
            menu->currently_casting = nullptr;
        }
    }
}

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

    if (g_activeCombatMenu) {
        g_activeCombatMenu->currently_casting = nullptr;
    }
    g_activeCombatMenu = nullptr;
    g_lastSentSelectPacket = {};
    NetworkManager::Get().ClearRemoteAbilitySelect();
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

        const uint32_t activeNUID = NetworkManager::Get().GetActiveNUID();
        Character *activeChar = (activeNUID != 0xFFFFFFFF) ? NetworkManager::Get().GetCharacter(activeNUID) : nullptr;
        if (activeChar && activeChar->brain && activeChar->is_player_cat) {
            if (!IsRemoteTurn(activeChar)) {
                auto *pBrain = reinterpret_cast<PlayerBrain*>(activeChar->brain);
                TurnAbilitySelectPacket currentPkt{};
                currentPkt.actorNUID = activeNUID;

                if (pBrain->pending_ability) {
                    currentPkt.isSelected = true;
                    std::string abilityName = GameUtils::GetAbilityName(pBrain->pending_ability).to_string();
                    strncpy_s(currentPkt.abilityName, abilityName.c_str(), _TRUNCATE);

                    int32_t tileX = pBrain->prev_hovered_square.x;
                    int32_t tileY = pBrain->prev_hovered_square.y;
                    if (tileX <= -1000 || tileY <= -1000) {
                        tileX = pBrain->pending_choice.tile.x;
                        tileY = pBrain->pending_choice.tile.y;
                    }

                    if (tileX > -1000 && tileY > -1000) {
                        currentPkt.hasTargetTile = true;
                        currentPkt.targetTileX = tileX;
                        currentPkt.targetTileY = tileY;
                    } else {
                        currentPkt.hasTargetTile = false;
                        currentPkt.targetTileX = -5000;
                        currentPkt.targetTileY = -5000;
                    }

                    currentPkt.orientX = pBrain->pending_choice.orientation.x;
                    currentPkt.orientY = pBrain->pending_choice.orientation.y;
                    if (currentPkt.orientX == 0 && currentPkt.orientY == 0) {
                        currentPkt.orientX = activeChar->orientation.x;
                        currentPkt.orientY = activeChar->orientation.y;
                    }

                    bool isDiff = false;
                    if (!g_lastSentSelectPacket.isSelected) isDiff = true;
                    else if (g_lastSentSelectPacket.actorNUID != currentPkt.actorNUID) isDiff = true;
                    else if (strcmp(g_lastSentSelectPacket.abilityName, currentPkt.abilityName) != 0) isDiff = true;
                    else if (g_lastSentSelectPacket.hasTargetTile != currentPkt.hasTargetTile) isDiff = true;
                    else if (currentPkt.hasTargetTile && (g_lastSentSelectPacket.targetTileX != currentPkt.targetTileX || g_lastSentSelectPacket.targetTileY != currentPkt.targetTileY)) isDiff = true;
                    else if (g_lastSentSelectPacket.orientX != currentPkt.orientX || g_lastSentSelectPacket.orientY != currentPkt.orientY) isDiff = true;

                    if (isDiff) {
                        g_lastSentSelectPacket = currentPkt;
                        NetworkManager::Get().BroadcastPacket(PacketType::TurnAbilitySelect, &currentPkt, sizeof(currentPkt), false);
                    }
                } else if (g_lastSentSelectPacket.isSelected) {
                    currentPkt.isSelected = false;
                    currentPkt.hasTargetTile = false;
                    currentPkt.targetTileX = -5000;
                    currentPkt.targetTileY = -5000;
                    g_lastSentSelectPacket = currentPkt;
                    NetworkManager::Get().BroadcastPacket(PacketType::TurnAbilitySelect, &currentPkt, sizeof(currentPkt), true);
                }
            } else if (g_lastSentSelectPacket.isSelected) {
                TurnAbilitySelectPacket clearPkt{};
                clearPkt.actorNUID = g_lastSentSelectPacket.actorNUID;
                clearPkt.isSelected = false;
                clearPkt.hasTargetTile = false;
                clearPkt.targetTileX = -5000;
                clearPkt.targetTileY = -5000;
                g_lastSentSelectPacket = clearPkt;
                NetworkManager::Get().BroadcastPacket(PacketType::TurnAbilitySelect, &clearPkt, sizeof(clearPkt), true);
            }
        } else if (g_lastSentSelectPacket.isSelected) {
            TurnAbilitySelectPacket clearPkt{};
            clearPkt.actorNUID = g_lastSentSelectPacket.actorNUID;
            clearPkt.isSelected = false;
            clearPkt.hasTargetTile = false;
            clearPkt.targetTileX = -5000;
            clearPkt.targetTileY = -5000;
            g_lastSentSelectPacket = clearPkt;
            NetworkManager::Get().BroadcastPacket(PacketType::TurnAbilitySelect, &clearPkt, sizeof(clearPkt), true);
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

        g_lastSentSelectPacket = {};
        NetworkManager::Get().ClearRemoteAbilitySelect();
        if (g_activeCombatMenu) {
            g_activeCombatMenu->currently_casting = nullptr;
        }

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
            NetworkManager::Get().ClearRemoteAbilitySelect();
            if (g_activeCombatMenu) {
                g_activeCombatMenu->currently_casting = nullptr;
            }
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

                g_lastSentSelectPacket = {};
                NetworkManager::Get().ClearRemoteAbilitySelect();
                if (g_activeCombatMenu) {
                    g_activeCombatMenu->currently_casting = nullptr;
                }

                // Host tells peers the fight ended, then both clean up state.
                NetworkManager::Get().EndCombat();
            }
        }
    });

    ParaboxAPI::OnFaceDirection.Subscribe([](ParaboxAPI::FaceDirectionEvent& ev) {
        if (!NetworkManager::Get().IsCombatActive()) return;
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
                (nuid != 0xFFFFFFFF && nuid == NetworkManager::Get().GetActiveNUID());

            if (nx != 0 || ny != 0) {
                auto &lastFacing = NetworkManager::Get().GetLastFacingMap();
                if (lastFacing.count(nuid) && lastFacing[nuid].first == nx && lastFacing[nuid].second == ny && !ev.force) {
                    return;
                }
                lastFacing[nuid] = {nx, ny};

                // Interim facing updates sent unreliably during local player's turn
                if (isLocalActiveNUID && !IsRemoteTurn(c)) {
                    TurnFacingPacket pkt{};
                    pkt.actorNUID = nuid;
                    pkt.nx = nx;
                    pkt.ny = ny;
                    pkt.anim = ev.play_animation;
                    pkt.force = ev.force;
                    NetworkManager::Get().BroadcastPacket(PacketType::TurnFacing, &pkt, sizeof(pkt), false);
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

        const bool isRemoteTurn = IsRemoteTurn(character);

        if (isRemoteTurn) {
            g_isCombatUIProcessing = false;
            g_castableAbilities.clear();

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

                    CombatMenu *menu = GetActiveCombatMenu(ev.ctx);
                    if (menu) {
                        menu->currently_casting = nullptr;
                    }
                    NetworkManager::Get().ClearRemoteAbilitySelect();

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

        if (g_lastSentSelectPacket.isSelected) {
            TurnAbilitySelectPacket clearPkt{};
            clearPkt.actorNUID = charNUID;
            clearPkt.isSelected = false;
            clearPkt.hasTargetTile = false;
            clearPkt.targetTileX = -5000;
            clearPkt.targetTileY = -5000;
            g_lastSentSelectPacket = clearPkt;
            NetworkManager::Get().BroadcastPacket(PacketType::TurnAbilitySelect, &clearPkt, sizeof(clearPkt), true);
        }

        // Ensure the last orientation before ending the turn (or submitting action) is sent reliably
        if (character) {
            TurnFacingPacket finalFacing{};
            finalFacing.actorNUID = charNUID;
            finalFacing.nx = character->orientation.x;
            finalFacing.ny = character->orientation.y;
            if (finalFacing.nx == 0 && finalFacing.ny == 0 && ev.actionData) {
                finalFacing.nx = ev.actionData->orientation.x;
                finalFacing.ny = ev.actionData->orientation.y;
            }
            finalFacing.anim = false;
            finalFacing.force = true;
            NetworkManager::Get().BroadcastPacket(PacketType::TurnFacing, &finalFacing, sizeof(finalFacing), true);
        }

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

    ParaboxAPI::OnCombatMenuShow.Subscribe([](ParaboxAPI::CombatMenuShowEvent& ev) {
        if (ev.menu) {
            g_activeCombatMenu = static_cast<CombatMenu*>(ev.menu);
        }
    });

    ParaboxAPI::OnCombatMenuHide.Subscribe([](ParaboxAPI::CombatMenuHideEvent& ev) {
        if (g_activeCombatMenu == ev.menu) {
            g_activeCombatMenu = nullptr;
        }
    });

    ParaboxAPI::OnButtonCanActivate.Subscribe([](ParaboxAPI::ButtonCanActivateEvent& ev) {
        if (IsRemoteTurn() && IsCombatMenuButton(ev.button)) {
            ev.returnValue = 0;
            ev.Cancel();
        }
    });

    ParaboxAPI::OnButtonActivate.Subscribe([](ParaboxAPI::ButtonActivateEvent& ev) {
        if (IsRemoteTurn() && IsCombatMenuButton(ev.button)) {
            ev.Cancel();
        }
    });

    ParaboxAPI::OnPlayerBrainUpdate.Subscribe([](ParaboxAPI::PlayerBrainUpdateEvent &ev) {
        if (!NetworkManager::Get().IsCombatActive()) return;
        if (!ev.brain || !ev.brain->character) return;

        if (!IsRemoteTurn(ev.brain->character)) return;

        // Remote cat's turn!
        // Cancel native PlayerBrain::update to:
        // 1. Prevent wiping CombatMenu->currently_casting
        // 2. Prevent handling local mouse/hotkey input on remote brain
        ev.Cancel();

        RenderRemoteAbilityOverlay(ev.brain);
    });

}
