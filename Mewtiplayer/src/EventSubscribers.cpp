#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "ModState.h"
#include "Overlay.h"
#include "SteamABICompat.h"
#include "mew_ui_api.h"
#include "GameUtils.h"
#include "MewSQL.h"
#include "MewgenicsTypes.h"
#include <deque>
#include <unordered_set>

static TurnControl *g_currentTurnControl = nullptr;
static bool g_isCombatUIProcessing = false;
static std::unordered_set<void *> g_castableAbilities;

// We use this to distinguish between UI-initiated and engine-initiated actions
// It is set in Hook_EnqueueAction and consumed in Hook_AbilityTrigger
static bool g_waitingForPlayerAction = false;
static bool g_isSyncActionPending = false;

// Deferred broadcast: we delay broadcasting the player's main action until
// AbilityTrigger fires so we can capture any passive reactions (e.g.
// DodgeWhenTargeted, SwapPositions) that trigger BEFORE the main action and
// broadcast them first, preserving correct ordering for the receiver.
static bool g_deferredBroadcastPending = false;
static TurnActionPacket g_deferredActionPkt = {};
static uint32_t g_deferredActorNUID = 0xFFFFFFFF;

static bool g_startedCombat = false;
static bool g_isQueueEmpty = false;
static bool g_inCombatDetected = false;

std::deque<ActionPacket> g_pendingInjections;
static ActionPacket g_lastSentTurnPackage = {};
static void *g_lastActionQueue = nullptr;

// SaveHooks state
std::map<int64_t, uint64_t> g_catIdToOwnerSteamID;

// ---------------------------------------------------------------------------
// SaveHooks Subscribers
// ---------------------------------------------------------------------------
void RegisterSaveSubscribers() {
    ParaboxAPI::OnCreateStrayCat.Subscribe([](ParaboxAPI::CreateStrayCatEvent& ev) {
        void* cat = ev.returnValue;
        if (GameUtils::g_isLoadingCustomCats && cat) {
            const int index = GameUtils::g_currentCustomCatIndex;
            Overlay::Log("[SAVE] Hooked CreateStrayCat, loading custom cat at index %d from %s...", index, "mewtiplayer.sav");

            if (glaiel::SQLSaveFile* dbFile = MewSQL::OpenSaveDatabase("mewtiplayer.sav")) {
                if (GameUtils::MewSaveFile_Load_t mewSaveFileLoad = GameUtils::GetMewSaveFileLoadPtr()) {
                    MewSaveFile dummySave = {};
                    dummySave.sqlFile = *dbFile;

                    const auto catIdPtr = &((PersistentCharacter*)cat)->catID;
                    const int64_t originalID = *catIdPtr;

                    mewSaveFileLoad(&dummySave, index, cat);
                    *catIdPtr = originalID;

                    const uint64_t ownerSteamID = MewSQL::ReadIntFromDatabase(
                        dbFile, ("cat_owner_steamid_" + std::to_string(index)).c_str(), 0);
                    const int64_t catID = ((PersistentCharacter *)cat)->catID;
                    g_catIdToOwnerSteamID[catID] = ownerSteamID;

                    const int64_t originalAge = MewSQL::ReadIntFromDatabase(dbFile, ("cat_original_age_" + std::to_string(index)).c_str(), -1);
                    if (originalAge != -1) {
                        const MewDirector* dir = GameUtils::GetMewDirectorSingleton();
                        const int32_t currentDayVal = dir ? dir->currentDay : 1;
                        ((PersistentCharacter*)cat)->birthDay = currentDayVal - (int32_t)originalAge;
                        Overlay::Log("[SAVE] Restored custom cat age to %lld", originalAge);
                    }
                }
                MewSQL::CloseSaveDatabase(dbFile);
            }
            GameUtils::g_currentCustomCatIndex++;
        }
    });

    ParaboxAPI::OnGetCollarVector.Subscribe([](ParaboxAPI::GetCollarVectorEvent& ev) {
        if (!GameUtils::g_useCustomCollarClasses || GameUtils::g_customCollarClasses.empty()) {
            return;
        }

        Overlay::Log("[SAVE] Populating custom collar vector with %zu classes...", GameUtils::g_customCollarClasses.size());

        const size_t count = GameUtils::g_customCollarClasses.size();
        const size_t bytesToAllocate = count * sizeof(MsvcReleaseModeXString);

        ev.outVector[0] = 0;
        ev.outVector[1] = 0;
        ev.outVector[2] = 0;

        if (auto* array = (MsvcReleaseModeXString*)ParaboxAPI::GameAllocate(bytesToAllocate)) {
            memset(array, 0, bytesToAllocate);
            for (size_t i = 0; i < count; ++i) {
                GameUtils::InitXString(array[i], GameUtils::g_customCollarClasses[i].c_str());
            }
            ev.outVector[0] = (int64_t)array;
            ev.outVector[1] = (int64_t)(array + count);
            ev.outVector[2] = (int64_t)(array + count);
            ev.returnValue = ev.outVector;
            ev.Cancel(); // Prevent calling original
        }
    });
}

// ---------------------------------------------------------------------------
// AdventureBoxHooks Subscribers
// ---------------------------------------------------------------------------
void RegisterAdventureBoxSubscribers() {
    ParaboxAPI::OnButchBoxInit.Subscribe([](ParaboxAPI::ButchBoxInitEvent& ev) {
        NetworkManager::Get().SendLocalCatCount();
    });

    ParaboxAPI::OnButchBoxTryPlaceCat.Subscribe([](ParaboxAPI::ButchBoxTryPlaceCatEvent& ev) {
        if (ev.returnValue) {
            NetworkManager::Get().SendLocalCatCount();
        }
    });

    ParaboxAPI::OnButchBoxTryRemoveCat.Subscribe([](ParaboxAPI::ButchBoxTryRemoveCatEvent& ev) {
        NetworkManager::Get().SendLocalCatCount();
    });
}

// ---------------------------------------------------------------------------
// CombatHooks Subscribers
// ---------------------------------------------------------------------------
void RegisterCombatSubscribers() {
    ParaboxAPI::OnTurnStart.Subscribe([](ParaboxAPI::TurnStartEvent& ev) {
        if (ev.tc) {
            g_currentTurnControl = ev.tc;
            GameUtils::SetTurnControlPtr(&g_currentTurnControl);
        }
    });

    ParaboxAPI::OnBeginTurn.Subscribe([](ParaboxAPI::BeginTurnEvent& ev) {
        g_startedCombat = true;
        g_isCombatUIProcessing = false;
        g_deferredBroadcastPending = false; // Safety: clear stale deferred state

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
            }

            const uint32_t nuid = NetworkManager::Get().GetNUID(ev.character);
            NetworkManager::Get().SetActiveNUID(nuid);
        }
    });

    ParaboxAPI::OnAbilityTrigger.Subscribe([](ParaboxAPI::AbilityTriggerEvent& ev) {
        bool isSyncAction = g_isSyncActionPending;
        g_isSyncActionPending = false; // Consume for logging

        if (ev.ability && ev.turnAction) {
            const std::string abilityName = GameUtils::GetAbilityName(ev.ability).to_string();
            int64_t sourceUID = -1;
            Character *abilityOwner = ev.ability->owner;
            if (abilityOwner && abilityOwner->persistentChar) {
                sourceUID = abilityOwner->persistentChar->sql_key;
            }

            // ---- Deferred broadcast: capture passives, flush main action ----
            if (g_deferredBroadcastPending) {
                uint32_t triggerNUID = abilityOwner
                    ? NetworkManager::Get().GetNUID(abilityOwner)
                    : 0xFFFFFFFF;
                bool isMainAction =
                    (strcmp(abilityName.c_str(), g_deferredActionPkt.abilityName) == 0 &&
                     triggerNUID == g_deferredActorNUID);

                if (!isMainAction && triggerNUID != 0xFFFFFFFF) {
                    // This is a PASSIVE reaction (e.g. SwapPositions, Dodge)
                    // fired before the main action — broadcast + record it first.
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
                    Overlay::Log("[TRIGGER] PASSIVE Broadcast: %s for NUID %u",
                                 abilityName.c_str(), triggerNUID);
                }

                if (isMainAction) {
                    // The main action's AbilityTrigger has fired — flush the
                    // deferred broadcast now (after all passives).
                    ActionPacket actPkt{};
                    actPkt.type = PacketType::TurnAction;
                    actPkt.data.action = g_deferredActionPkt;
                    NetworkManager::Get().RecordAction(actPkt);
                    NetworkManager::Get().BroadcastPacket(
                        PacketType::TurnAction, &g_deferredActionPkt,
                        sizeof(g_deferredActionPkt), true);

                    g_deferredBroadcastPending = false;
                    isSyncAction = true;
                    Overlay::Log("[TRIGGER] Flushed deferred broadcast: '%s' for NUID %u",
                                 g_deferredActionPkt.abilityName, g_deferredActionPkt.actorNUID);
                }
            }
            // ---- End deferred broadcast handling ----

            char buf[512];
            snprintf(buf, sizeof(buf),
                     "[%s ACTION] UID:%lld | %s | T1:(%d,%d) | T2:(%d,%d)",
                     isSyncAction ? "SYNC" : "AUTO", sourceUID, abilityName.c_str(),
                     ev.turnAction->targetX, ev.turnAction->targetY, ev.turnAction->target2X,
                     ev.turnAction->target2Y);
            Overlay::Log(buf);
        }
    });

    ParaboxAPI::OnEnqueueAction.Subscribe([](ParaboxAPI::EnqueueActionEvent& ev) {
        g_lastActionQueue = ev.queue;
        g_isQueueEmpty = ev.actionData && ev.actionData->type <= 1;

        if (ev.actionData && ev.actionData->type <= 1 && !g_pendingInjections.empty() && g_isCombatUIProcessing) {
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
                            Overlay::Log("[ENQUEUE] Injecting facing for NUID %u | Target:(%d,%d)",
                                         facing.actorNUID, facing.nx, facing.ny);
                        }
                        
                        ParaboxAPI::ForceFaceDirection(c, packed, facing.anim, facing.force);
                    }
                } else {
                    if (g_modState.talkative) {
                        Overlay::Log("[ENQUEUE] Dropping stale TurnFacing for NUID %u (Active is %u)",
                                     facing.actorNUID, activeNUID);
                    }
                }
                g_pendingInjections.pop_front();
            }

            if (!g_pendingInjections.empty() &&
                g_pendingInjections.front().type == PacketType::TurnAction) {
                const TurnActionPacket &pending = g_pendingInjections.front().data.action;
                uint32_t currentNUID = NetworkManager::Get().GetNUID(ev.actionData->actor);
                Character *pendingActor =
                    NetworkManager::Get().GetCharacter(pending.actorNUID);

                // Allow injection for any valid actor.  Passive reactions
                // (e.g. DodgeWhenTargeted) act on a different character than
                // the one whose turn it is, so we cannot require actorNUID ==
                // currentNUID.
                if (pendingActor) {
                    std::string abilityName(pending.abilityName);
                    Ability *ability = nullptr;
                    if (abilityName != "NULL") {
                        ability = GameUtils::FindCharacterAbility(pendingActor, abilityName.c_str());
                        if (!ability) {
                            Component* passive = GameUtils::FindCharacterPassive(pendingActor, abilityName.c_str());
                            if (passive) {
                                ability = (Ability*)passive;
                            } else {
                                Overlay::Log("[ENQUEUE] [ERROR] Could not resolve ability/passive '%s' for NUID %u - dropping",
                                             pending.abilityName, pending.actorNUID);
                                g_pendingInjections.pop_front();
                                return; // Let original function handle
                            }
                        }
                    }

                    TurnActionPacket pktCopy = pending;
                    g_pendingInjections.pop_front();

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

                    Overlay::Log("[ENQUEUE] Injecting from queue: Type %d for NUID %u (current turn NUID %u)",
                                 pktCopy.actionType, pktCopy.actorNUID, currentNUID);
                    // It will proceed to original enqueue action automatically.
                    return;
                }
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

        if (ev.actionData->type == 3 && g_waitingForPlayerAction) {
            g_waitingForPlayerAction = false;
            uint32_t nuid = NetworkManager::Get().GetNUID(ev.actionData->actor);
            if (nuid != 0xFFFFFFFF) {
                TurnActionPacket pkt{};
                pkt.actorNUID = nuid;
                pkt.actionType = 3;
                memset(pkt.abilityName, 0, sizeof(pkt.abilityName));
                strncpy_s(pkt.abilityName, "NULL", _TRUNCATE);
                pkt.targetX = ev.actionData->targetX;
                pkt.targetY = ev.actionData->targetY;
                pkt.target2X = ev.actionData->target2X;
                pkt.target2Y = ev.actionData->target2Y;
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
                                                      sizeof(pkt), !g_modState.packetTesting);
                Overlay::Log("[NET] Broadcast EndTurn for NUID %u", nuid);
                if (g_modState.packetTesting) {
                    ev.actionData->type = 0; // Cancel this action
                    Overlay::Log("[ENQUEUE] Testing Mode - Cancelled Action '%s' for NUID: %d",
                                 pkt.abilityName, nuid);
                }
            }
            return;
        }

        Ability *ability = ev.actionData->ability;
        Character *actor = ev.actionData->actor;
        if (!actor && ability)
            actor = ability->owner;

        uint32_t nuid = actor ? NetworkManager::Get().GetNUID(actor) : 0xFFFFFFFF;
        std::string actorName = actor ? actor->name.to_utf8() : "UNKNOWN";

        if (g_waitingForPlayerAction) {
            g_waitingForPlayerAction = false;
            g_isSyncActionPending = true;

            if (nuid != 0xFFFFFFFF) {
                TurnActionPacket pkt{};
                pkt.actorNUID = nuid;
                pkt.actionType = ev.actionData->type;

                std::string abilityName = GameUtils::GetAbilityName(ability).to_string();
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

                if (g_modState.packetTesting) {
                    // In testing mode, broadcast immediately (action is
                    // cancelled below so AbilityTrigger won't fire to flush).
                    NetworkManager::Get().BroadcastPacket(PacketType::TurnAction, &pkt,
                                                          sizeof(pkt), false);
                    ActionPacket actPkt{};
                    actPkt.type = PacketType::TurnAction;
                    actPkt.data.action = pkt;
                    NetworkManager::Get().RecordAction(actPkt);
                    Overlay::Log("[NET] Broadcast and Recorded Action '%s' for NUID: %d",
                                 pkt.abilityName, nuid);

                    ev.actionData->type = 0; // Cancel this action
                    Overlay::Log("[ENQUEUE] Testing Mode - Cancelled Action '%s' for NUID: %d",
                                 pkt.abilityName, nuid);
                } else {
                    // Defer the broadcast until OnAbilityTrigger fires.
                    // This lets us capture any passive reactions (e.g.
                    // SwapPositions, DodgeWhenTargeted) that fire BEFORE
                    // the main action and broadcast them first.
                    g_deferredBroadcastPending = true;
                    g_deferredActionPkt = pkt;
                    g_deferredActorNUID = nuid;
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
                g_startedCombat = false;
                g_inCombatDetected = false;
                g_waitingForPlayerAction = false;

                if (NetworkManager::Get().IsHost()) {
                    NetworkManager::Get().EndCombat();
                }
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
                                                              sizeof(pkt), !g_modState.packetTesting);

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
    });
}

// ---------------------------------------------------------------------------
// ClassChooserHooks Subscribers and UI state
// ---------------------------------------------------------------------------

std::map<uint64_t, bool> g_lobbyReadyStates;
bool g_localReady = false;
void *g_activeClassChooserLambdaThis = nullptr;
bool g_hasTriggeredProceed = false;

static MewUISceneBinding g_classChooserScene;
static void* g_lockInButton = nullptr;
static bool g_classChooserSceneInitialized = false;
static bool g_lastLocalReadyState = false;
static bool g_buttonHooked = false;

bool AreAllLobbyMembersReady() {
    const CSteamID lobby = NetworkManager::Get().GetCurrentLobby();
    if (!lobby.IsValid()) return false;

    const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(lobby);
    if (numMembers <= 0) return false;

    for (int i = 0; i < numMembers; i++) {
        const uint64_t memberID = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i).ConvertToUint64();
        const auto it = g_lobbyReadyStates.find(memberID);
        if (it == g_lobbyReadyStates.end() || !it->second) return false;
    }
    return true;
}

void ResetLobbyReadyStates() {
    g_lobbyReadyStates.clear();
    g_localReady = false;
    g_activeClassChooserLambdaThis = nullptr;
    g_hasTriggeredProceed = false;
}

void HandleCollarSyncInternal(const void *data, const uint32_t length) {
    if (length != sizeof(CollarSyncPacket)) return;

    const auto *packet = (const CollarSyncPacket *)data;
    PersistentCharacter *cat = ParaboxAPI::GetPersistentCharacterById(packet->catID);
    if (!cat) {
        Overlay::Log("[LOBBY] CollarSync: cat %lld not found", packet->catID);
        return;
    }

    const char *collarName = ParaboxAPI::ResolveCollarNameFromIndex(packet->collarIndex);
    ParaboxAPI::ApplyCollarToCharacter(cat, collarName);
    Overlay::Log("[LOBBY] CollarSync: updated cat %lld to %s (index %d)", packet->catID, collarName, packet->collarIndex);

    ParaboxAPI::UpdateClassChooserTagBoxes(packet->catID, packet->collarIndex);
    ParaboxAPI::RefreshClassChooserInventory();
    ParaboxAPI::RefreshCatSelectorUI();
}

static void ApplyLockInButtonText() {
    if (!g_lockInButton) return;

    const char* lockinoutText = g_localReady ? "Lock Out" : "Lock In!";
    uintptr_t gameBase = (uintptr_t)GetModuleHandleA(nullptr);
    auto initString = reinterpret_cast<MewFnInitNarrowString>(gameBase + MEW_RVA_INIT_NARROW_STRING);
    auto setTextString = reinterpret_cast<MewFnUIRootSetTextString>(gameBase + MEW_RVA_UI_ROOT_SET_TEXT_STRING);

    if (initString && setTextString) {
        MewNarrowString childNameStr = {};
        MewNarrowString textKeyStr = {};

        initString(&childNameStr, "INVENTORY_LOCKIN_BUTTON");
        initString(&textKeyStr, lockinoutText);

        setTextString(g_lockInButton, &childNameStr, &textKeyStr);
    }
}

static void __cdecl ClassChooserLockInButtonCallback(void* button, MewButtonEvent eventType, MewButtonState oldState, MewButtonState newState, void* userData) {
    if (oldState != newState) {
        ApplyLockInButtonText();
    }
}

static void __cdecl ClassChooserSceneRefreshCallback(MewUISceneBinding* binding, const MewUISceneRefreshResult result, void* oldSceneManager, void* newSceneManager, void* userData) {
    if (result == MEW_UI_SCENE_REFRESH_LOADED || result == MEW_UI_SCENE_REFRESH_CHANGED || result == MEW_UI_SCENE_REFRESH_UNLOADED) {
        g_lockInButton = nullptr;
        g_buttonHooked = false;
    }
}

void ClassChooserHooks_UITick() {
    if (!g_classChooserSceneInitialized) {
        MewUI_InitSceneBinding(&g_classChooserScene, "ClassChooser", ClassChooserSceneRefreshCallback, nullptr);
        g_classChooserSceneInitialized = true;
    }

    MewUI_RefreshSceneBinding(&g_classChooserScene);

    if (MewUI_IsSceneBindingActive(&g_classChooserScene)) {
        void* scene_manager = MewUI_GetSceneBindingScene(&g_classChooserScene);

        if (!g_buttonHooked) {
            if (scene_manager) {
                if (void* button = MewUI_FindButtonByRole(scene_manager, "CloseButton")) {
                    MewUI_RegisterExistingButton(button, nullptr, ClassChooserLockInButtonCallback, nullptr);
                    g_lockInButton = button;
                    g_buttonHooked = true;
                    g_lastLocalReadyState = !g_localReady;
                }
            }
        }

        if (g_buttonHooked && g_lockInButton) {
            if (g_localReady != g_lastLocalReadyState) {
                g_lastLocalReadyState = g_localReady;
                ApplyLockInButtonText();
                Overlay::Log("Button text set to '%s'", g_localReady ? "Lock Out" : "Lock In!");
            }
        }
    }
}

void ClassChooserHooks_Shutdown() {
    if (g_classChooserSceneInitialized) {
        MewUI_ClearSceneBinding(&g_classChooserScene);
        g_classChooserSceneInitialized = false;
    }
    g_lockInButton = nullptr;
    g_buttonHooked = false;
}

// Ensure trigger works
extern void StorageHooks_TriggerEmbarkProceed();
void CatSelectorHooks_TriggerLockInProceed() {
    if (g_hasTriggeredProceed) return;
    g_hasTriggeredProceed = true;

    if (g_activeClassChooserLambdaThis) {
        ParaboxAPI::ForceClassChooserLockIn(g_activeClassChooserLambdaThis);
        g_activeClassChooserLambdaThis = nullptr;
    }

    StorageHooks_TriggerEmbarkProceed();

    g_lobbyReadyStates.clear();
    g_localReady = false;
}

void RegisterClassChooserSubscribers() {
    ParaboxAPI::OnClassTagBoxClick.Subscribe([](ParaboxAPI::ClassTagBoxClickEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (ev.catID != -1) {
                const auto ownerIt = g_catIdToOwnerSteamID.find(ev.catID);
                if (ownerIt != g_catIdToOwnerSteamID.end() && ownerIt->second != 0) {
                    const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                    if (ownerIt->second != localSteamID) {
                        ev.Cancel(); // Block clicking the collar of other players
                        return;
                    }
                }
            }

            PersistentCharacter *cat = ParaboxAPI::GetPersistentCharacterById(ev.catID);
            if (!cat) return;

            const char *className = cat->className.is_valid() ? cat->className.begin() : "Colorless";
            const char *clickedClassName = ParaboxAPI::ResolveCollarNameFromIndex(ev.clickedIndex);
            const int32_t collarIndex = (strcmp(className, clickedClassName) == 0) ? -1 : ev.clickedIndex;

            CollarSyncPacket packet = {};
            packet.catID = ev.catID;
            packet.collarIndex = collarIndex;
            NetworkManager::Get().BroadcastPacket(PacketType::CollarSync, &packet, sizeof(packet), true);
            Overlay::Log("[LOBBY] Broadcast collar sync for cat %lld: collar index %d", ev.catID, collarIndex);
        }
    });

    ParaboxAPI::OnClassChooserLockIn.Subscribe([](ParaboxAPI::ClassChooserLockInEvent& ev) {
        if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;
        
        g_activeClassChooserLambdaThis = ev.lambdaThis;
        g_localReady = !g_localReady;

        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        g_lobbyReadyStates[localSteamID] = g_localReady;

        LobbyReadyPacket packet = {};
        packet.steamID = localSteamID;
        packet.isReady = g_localReady;
        NetworkManager::Get().BroadcastPacket(PacketType::LobbyReady, &packet, sizeof(packet), false);
        Overlay::Log("[LOBBY] Local ready state: %s", g_localReady ? "locked in" : "not ready");

        if (NetworkManager::Get().IsHost() && AreAllLobbyMembersReady()) {
            if (!g_hasTriggeredProceed) {
                g_hasTriggeredProceed = true;
                NetworkManager::Get().BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
                g_activeClassChooserLambdaThis = nullptr;
            }
        } else {
            ev.Cancel(); // Don't proceed yet!
        }
    });
}

// ---------------------------------------------------------------------------
// StorageHooks Subscribers and UI state
// ---------------------------------------------------------------------------

uint32_t g_pendingMapNodeSyncIndex = 0xFFFFFFFF;
extern void TriggerMapNodeSync(uint32_t nodeIndex); // Used by MapHooks

void *g_activeInventoryScreenThis = nullptr;
static MewUISceneBinding g_storageItemsScene;
static void* g_storageLockInButton = nullptr;
static bool g_storageItemsSceneInitialized = false;
static bool g_storageLastLocalReadyState = false;
static bool g_storageButtonHooked = false;

static void ApplyStorageLockInButtonText() {
    if (!g_storageLockInButton) return;

    const char* lockinoutText = g_localReady ? "Lock Out" : "Lock In!";
    uintptr_t gameBase = (uintptr_t)GetModuleHandleA(nullptr);
    auto initString = reinterpret_cast<MewFnInitNarrowString>(gameBase + MEW_RVA_INIT_NARROW_STRING);
    auto setTextString = reinterpret_cast<MewFnUIRootSetTextString>(gameBase + MEW_RVA_UI_ROOT_SET_TEXT_STRING);

    if (initString && setTextString) {
        MewNarrowString childNameStr = {};
        MewNarrowString textKeyStr = {};

        initString(&childNameStr, "INVENTORY_LOCKIN_BUTTON");
        initString(&textKeyStr, lockinoutText);

        setTextString(g_storageLockInButton, &childNameStr, &textKeyStr);
    }
}

static void __cdecl StorageLockInButtonCallback(void* button, MewButtonEvent eventType, MewButtonState oldState, MewButtonState newState, void* userData) {
    if (oldState != newState) {
        ApplyStorageLockInButtonText();
    }
}

static void __cdecl StorageItemsSceneRefreshCallback(MewUISceneBinding* binding, const MewUISceneRefreshResult result, void* oldSceneManager, void* newSceneManager, void* userData) {
    if (result == MEW_UI_SCENE_REFRESH_LOADED || result == MEW_UI_SCENE_REFRESH_CHANGED || result == MEW_UI_SCENE_REFRESH_UNLOADED) {
        g_storageLockInButton = nullptr;
        g_storageButtonHooked = false;
    }
}

void StorageHooks_UITick() {
    if (!g_storageItemsSceneInitialized) {
        MewUI_InitSceneBinding(&g_storageItemsScene, "StorageItems", StorageItemsSceneRefreshCallback, nullptr);
        g_storageItemsSceneInitialized = true;
    }

    MewUI_RefreshSceneBinding(&g_storageItemsScene);

    if (MewUI_IsSceneBindingActive(&g_storageItemsScene)) {
        void* scene_manager = MewUI_GetSceneBindingScene(&g_storageItemsScene);

        if (!g_storageButtonHooked) {
            if (scene_manager) {
                if (void* button = MewUI_FindButtonByRole(scene_manager, "CloseButton")) {
                    MewUI_RegisterExistingButton(button, nullptr, StorageLockInButtonCallback, nullptr);
                    g_storageLockInButton = button;
                    g_storageButtonHooked = true;
                    g_storageLastLocalReadyState = !g_localReady;
                }
            }
        }

        if (g_storageButtonHooked && g_storageLockInButton) {
            if (g_localReady != g_storageLastLocalReadyState) {
                g_storageLastLocalReadyState = g_localReady;
                ApplyStorageLockInButtonText();
                Overlay::Log("Storage button text set to '%s'", g_localReady ? "Lock Out" : "Lock In!");
            }
        }
    }
}

void StorageHooks_Shutdown() {
    if (g_storageItemsSceneInitialized) {
        MewUI_ClearSceneBinding(&g_storageItemsScene);
        g_storageItemsSceneInitialized = false;
    }
    g_storageLockInButton = nullptr;
    g_storageButtonHooked = false;
}

void StorageHooks_TriggerEmbarkProceed() {
    if (g_activeInventoryScreenThis) {
        ParaboxAPI::ForceInventoryScreen2Close(g_activeInventoryScreenThis);
        g_activeInventoryScreenThis = nullptr;
    }
}

void HandleStorageItemSyncInternal(const void *data, const uint32_t length) {
    if (length != sizeof(StorageItemSyncPacket)) return;

    const auto *packet = (const StorageItemSyncPacket *)data;
    Overlay::Log("[STORAGE] Received storage item sync from player %llu: slot %d (cat %lld)",
                 packet->steamID, packet->slotIndex, packet->catID);

    ParaboxAPI::UpdateStorageItemSlot(packet->slotIndex, packet->catID);
}

void RegisterStorageSubscribers() {
    ParaboxAPI::OnSceneManagerCreateScene.Subscribe([](ParaboxAPI::SceneManagerCreateSceneEvent& ev) {
        if (ev.nameStr) {
            const auto *xstr = static_cast<MsvcReleaseModeXString *>(ev.nameStr);
            if (xstr->is_valid()) {
                const auto view = xstr->as_native_string_view();
                if (view == "House" || view == "StorageItems" || view == "Combat" || view == "MainMenu" || view == "ClassChooser") {
                    g_pendingMapNodeSyncIndex = 0xFFFFFFFF;
                    ResetLobbyReadyStates();
                }
            }
        }
    });

    ParaboxAPI::OnSceneAddComponent.Subscribe([](ParaboxAPI::SceneAddComponentEvent& ev) {
        if (!ev.scene || !ev.comp) return;

        const auto *s = static_cast<Scene *>(ev.scene);
        if (s->name.is_valid()) {
            MsvcReleaseModeXString compName = {};
            if (GameUtils::SafeGetComponentName(static_cast<Component *>(ev.comp), &compName)) {
                const auto compView = compName.as_native_string_view();
                if (compView == "MapScreen") {
                    Overlay::Log("[MAP] Registered MapScreen %p", ev.comp);
                    if (g_pendingMapNodeSyncIndex != 0xFFFFFFFF) {
                        Overlay::Log("[MAP] Processing pending MapNodeSync for index %u", g_pendingMapNodeSyncIndex);
                        const uint32_t index = g_pendingMapNodeSyncIndex;
                        g_pendingMapNodeSyncIndex = 0xFFFFFFFF;
                        TriggerMapNodeSync(index);
                    }
                }
                GameUtils::FreeXString(compName);
            }
        }
    });

    ParaboxAPI::OnInventoryItemBoxClick.Subscribe([](ParaboxAPI::InventoryItemBoxClickEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            const int64_t catID = ParaboxAPI::ResolveSelectedCatID();
            if (catID != -1) {
                const auto ownerIt = g_catIdToOwnerSteamID.find(catID);
                if (ownerIt != g_catIdToOwnerSteamID.end() && ownerIt->second != 0) {
                    const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                    if (ownerIt->second != localSteamID) {
                        Overlay::Log("[STORAGE] Blocked click on cat %lld (not owned by player)", catID);
                        ev.Cancel();
                        return;
                    }
                }
            }
        }

        if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;
        
        const int32_t slotIndex = ParaboxAPI::FindStorageSlotIndex(ev.self);
        if (slotIndex == -1) return;

        StorageItemSyncPacket packet = {};
        packet.steamID = SteamUser()->GetSteamID().ConvertToUint64();
        packet.catID = ParaboxAPI::ResolveSelectedCatID();
        packet.slotIndex = slotIndex;

        NetworkManager::Get().BroadcastPacket(PacketType::StorageItemSync, &packet, sizeof(packet), true);
        Overlay::Log("[STORAGE] Broadcast storage item sync: slot %d (cat %lld)", slotIndex, packet.catID);
    });

    ParaboxAPI::OnInventoryScreen2Close.Subscribe([](ParaboxAPI::InventoryScreen2CloseEvent& ev) {
        if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;

        g_activeInventoryScreenThis = ev.self;
        g_localReady = !g_localReady;

        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        g_lobbyReadyStates[localSteamID] = g_localReady;

        LobbyReadyPacket packet = {};
        packet.steamID = localSteamID;
        packet.isReady = g_localReady;
        NetworkManager::Get().BroadcastPacket(PacketType::LobbyReady, &packet, sizeof(packet), false);
        Overlay::Log("[LOBBY] LockIn: Local ready state: %s", g_localReady ? "locked in" : "not ready");

        if (NetworkManager::Get().IsHost() && AreAllLobbyMembersReady()) {
            if (!g_hasTriggeredProceed) {
                g_hasTriggeredProceed = true;
                NetworkManager::Get().BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
                g_activeInventoryScreenThis = nullptr;
            } else {
                ev.Cancel();
            }
        } else {
            ev.Cancel();
        }
    });
}

// ---------------------------------------------------------------------------
// MapHooks Subscribers
// ---------------------------------------------------------------------------

bool g_isHandlingNetworkMapNodeSync = false;

void RegisterMapSubscribers() {
    ParaboxAPI::OnMapNodeClick.Subscribe([](ParaboxAPI::MapNodeClickEvent& ev) {
        if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;

        if (ev.nodeIndex != 0xFFFFFFFF) {
            MapNodeSyncPacket pkt = {};
            pkt.nodeIndex = ev.nodeIndex;
            NetworkManager::Get().BroadcastPacket(PacketType::MapNodeSync, &pkt, sizeof(pkt), true);
            Overlay::Log("[MAP] Broadcasted MapNodeSync for index %u", ev.nodeIndex);
        } else {
            Overlay::Log("[MAP] [ERR] Could not find clicked MapNode in MapScreen array!");
        }
    });

    ParaboxAPI::OnMapScreenEnterNode.Subscribe([](ParaboxAPI::MapScreenEnterNodeEvent& ev) {
        g_pendingMapNodeSyncIndex = 0xFFFFFFFF; // Clear any pending sync on entering a node
    });
}

void TriggerMapNodeSync(const uint32_t nodeIndex) {
    if (!GameUtils::IsComponentValid(ParaboxAPI::GetMapScreen())) {
        g_pendingMapNodeSyncIndex = nodeIndex;
        Overlay::Log("[MAP] MapScreen is null or inactive, queued MapNodeSync for index %u", nodeIndex);
        return;
    }

    const auto *mapScreen = static_cast<const glaiel::MapScreen*>(ParaboxAPI::GetMapScreen());
    const uint32_t vectorSize = mapScreen->nodes.size_;
    void **nodes = mapScreen->nodes.data_;

    if (!nodes || nodeIndex >= vectorSize) {
        Overlay::Log("[MAP] [ERR] Invalid node index %u (vector size %u)", nodeIndex, vectorSize);
        return;
    }

    void *matchedNode = nodes[nodeIndex];
    if (!matchedNode) {
        Overlay::Log("[MAP] [ERR] Node at index %u is null!", nodeIndex);
        return;
    }

    Overlay::Log("[MAP] Triggering network-synced Click (index %u)", nodeIndex);
    ParaboxAPI::ForceMapNodeClick(matchedNode);
}

// ---------------------------------------------------------------------------
// ActSelectionHooks Subscribers
// ---------------------------------------------------------------------------

uint32_t g_pendingActSelectIndex = 0;

void RegisterActSelectionSubscribers() {
    ParaboxAPI::OnActSelectionScreenInit.Subscribe([](ParaboxAPI::ActSelectionScreenInitEvent& ev) {
        Overlay::Log("[ACT] ActSelectionScreen initialized: %p", ev.self);

        if (g_pendingActSelectIndex != 0) {
            uint32_t act = g_pendingActSelectIndex;
            g_pendingActSelectIndex = 0;
            
            if (!GameUtils::IsComponentValid(ParaboxAPI::GetActSelectionScreen())) {
                g_pendingActSelectIndex = act;
                Overlay::Log("[ACT] ActSelectionScreen is null or invalid, requeued act select: %d", act);
                return;
            }

            Overlay::Log("[ACT] Triggering network-synced SelectAct (index %u)", act);
            ParaboxAPI::ForceActSelectionScreenSelectAct(ParaboxAPI::GetActSelectionScreen(), act);
        }
    });

    ParaboxAPI::OnActSelectionScreenSelectAct.Subscribe([](ParaboxAPI::ActSelectionScreenSelectActEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!NetworkManager::Get().IsHost()) {
                return;
            }

            ActSelectPacket pkt = {};
            pkt.actIndex = ev.actIndex;
            NetworkManager::Get().BroadcastPacket(PacketType::ActSelectSync, &pkt, sizeof(pkt), true);
            Overlay::Log("[ACT] Host selected act %d. Broadcasting sync.", ev.actIndex);
        }
    });
}

void TriggerActSelect(uint32_t actIndex) {
    if (!GameUtils::IsComponentValid(ParaboxAPI::GetActSelectionScreen())) {
        g_pendingActSelectIndex = actIndex;
        Overlay::Log("[ACT] ActSelectionScreen is null or invalid, queued act select: %d", actIndex);
        return;
    }

    Overlay::Log("[ACT] Triggering network-synced SelectAct (index %u)", actIndex);
    ParaboxAPI::ForceActSelectionScreenSelectAct(ParaboxAPI::GetActSelectionScreen(), actIndex);
}

// ---------------------------------------------------------------------------
// LevelUpHooks Subscribers
// ---------------------------------------------------------------------------

static std::map<glaiel::LevelUpScreen*, PersistentCharacter*> g_levelUpScreenToCat;
static std::map<glaiel::AbilityChooser*, PersistentCharacter*> g_abilityChooserToCat;

void RegisterLevelUpSubscribers() {
    ParaboxAPI::OnLevelUpScreenInit.Subscribe([](ParaboxAPI::LevelUpScreenInitEvent& ev) {
        if (ev.cat) {
            g_levelUpScreenToCat[ev.self] = ev.cat;
            Overlay::Log("[LEVELUP] LevelUpScreen init: %p, Cat UID: %lld", ev.self, ev.cat->sql_key);
        } else {
            Overlay::Log("[LEVELUP] LevelUpScreen init: %p, Cat is NULL!", ev.self);
        }
    });

    ParaboxAPI::OnLevelUpScreenSelectOption.Subscribe([](ParaboxAPI::LevelUpScreenSelectOptionEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            const PersistentCharacter* cat = nullptr;
            const auto it = g_levelUpScreenToCat.find(ev.self);
            if (it != g_levelUpScreenToCat.end()) {
                cat = it->second;
            }

            if (cat) {
                const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
                const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
                    Overlay::Log("[LEVELUP] Blocked select_option for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
                    ev.Cancel();
                    return;
                }

                if (ev.optionIndex != -1) {
                    LevelUpSelectOptionPacket pkt = {};
                    pkt.catUID = cat->sql_key;
                    pkt.optionIndex = ev.optionIndex;
                    NetworkManager::Get().BroadcastPacket(PacketType::LevelUpSelectOption, &pkt, sizeof(pkt), true);
                    Overlay::Log("[LEVELUP] Owner selected option %d. Broadcasting sync.", ev.optionIndex);
                } else {
                    Overlay::Log("[LEVELUP] [WARN] Failed to find optionIndex for select_option!");
                }
            }
        }
    });

    ParaboxAPI::OnLevelUpScreenReroll.Subscribe([](ParaboxAPI::LevelUpScreenRerollEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (const PersistentCharacter* cat = ev.self->catData) {
                const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
                const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
                    Overlay::Log("[LEVELUP] Blocked Reroll for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
                    ev.Cancel();
                    return;
                }

                LevelUpRerollPacket pkt = {};
                pkt.catUID = cat->sql_key;
                NetworkManager::Get().BroadcastPacket(PacketType::LevelUpReroll, &pkt, sizeof(pkt), true);
                Overlay::Log("[LEVELUP] Owner requested Reroll. Broadcasting sync.");
            }
        }
    });

    ParaboxAPI::OnAbilityChooserInit.Subscribe([](ParaboxAPI::AbilityChooserInitEvent& ev) {
        if (ev.cat) {
            g_abilityChooserToCat[ev.self] = ev.cat;
            Overlay::Log("[ABILITYCHOOSER] AbilityChooser init: %p, Cat UID: %lld", ev.self, ev.cat->sql_key);
        } else {
            Overlay::Log("[ABILITYCHOOSER] AbilityChooser init: %p, Cat is NULL!", ev.self);
        }
    });

    ParaboxAPI::OnAbilityChooserSelectSlot.Subscribe([](ParaboxAPI::AbilityChooserSelectSlotEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            PersistentCharacter* cat = nullptr;
            auto it = g_abilityChooserToCat.find(ev.self);
            if (it != g_abilityChooserToCat.end()) {
                cat = it->second;
            }

            if (cat) {
                const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
                const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
                    Overlay::Log("[ABILITYCHOOSER] Blocked select_slot/cancel for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
                    ev.Cancel();
                    return;
                }

                AbilityReplacePacket pkt = {};
                pkt.catUID = cat->sql_key;
                pkt.slotIndex = ev.slotIndex;
                NetworkManager::Get().BroadcastPacket(PacketType::AbilityReplace, &pkt, sizeof(pkt), true);
                Overlay::Log("[ABILITYCHOOSER] Owner selected slot %u (or cancel/skip 0xFFFFFFFF). Broadcasting sync.", ev.slotIndex);
            }
        }
    });
}

// Synced Trigger Implementations
void TriggerLevelUpSelectOption(const int64_t catUID, const uint32_t optionIndex) {
    glaiel::LevelUpScreen* screen = ParaboxAPI::GetActiveLevelUpScreen();
    if (!screen || !GameUtils::IsComponentValid(screen)) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: g_activeLevelUpScreen is null or invalid.");
        return;
    }

    PersistentCharacter* cat = screen->catData;
    if (!cat || cat->sql_key != catUID) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: active screen cat UID (%lld) does not match packet UID (%lld).",
                     cat ? cat->sql_key : -1, catUID);
        return;
    }

    if (!screen->options.Myfirst || !screen->options.Mylast) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: options vector is null.");
        return;
    }

    int count = static_cast<int>(screen->options.size());
    if (static_cast<int>(optionIndex) >= count) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: optionIndex %u out of bounds (%d).", optionIndex, count);
        return;
    }

    glaiel::LevelUpOption* optionPtr = &screen->options.Myfirst[optionIndex];
    Overlay::Log("[LEVELUP] Triggering synced select_option for index %u", optionIndex);

    ParaboxAPI::ForceLevelUpScreenSelectOption(screen, optionPtr);
}

void TriggerLevelUpReroll(const int64_t catUID) {
    glaiel::LevelUpScreen* screen = ParaboxAPI::GetActiveLevelUpScreen();
    if (!screen || !GameUtils::IsComponentValid(screen)) {
        Overlay::Log("[LEVELUP] TriggerLevelUpReroll error: g_activeLevelUpScreen is null or invalid.");
        return;
    }

    const PersistentCharacter* cat = screen->catData;
    if (!cat || cat->sql_key != catUID) {
        Overlay::Log("[LEVELUP] TriggerLevelUpReroll error: active screen cat UID (%lld) does not match packet UID (%lld).",
                     cat ? cat->sql_key : -1, catUID);
        return;
    }

    Overlay::Log("[LEVELUP] Triggering synced Reroll.");
    ParaboxAPI::ForceLevelUpScreenReroll(screen);
}

void TriggerAbilityReplace(const int64_t catUID, const uint32_t slotIndex) {
    glaiel::AbilityChooser* chooser = ParaboxAPI::GetActiveAbilityChooser();
    if (!chooser || !GameUtils::IsComponentValid(chooser)) {
        Overlay::Log("[ABILITYCHOOSER] TriggerAbilityReplace error: g_activeAbilityChooser is null or invalid.");
        return;
    }

    const PersistentCharacter* cat = nullptr;
    const auto it = g_abilityChooserToCat.find(chooser);
    if (it != g_abilityChooserToCat.end()) {
        cat = it->second;
    }

    if (!cat || cat->sql_key != catUID) {
        Overlay::Log("[ABILITYCHOOSER] TriggerAbilityReplace error: active chooser cat UID (%lld) does not match packet UID (%lld).",
                     cat ? cat->sql_key : -1, catUID);
        return;
    }

    Overlay::Log("[ABILITYCHOOSER] Triggering synced select_slot for slot %u (or cancel 0xFFFFFFFF).", slotIndex);
    ParaboxAPI::ForceAbilityChooserSelectSlot(chooser, slotIndex);
}

// ---------------------------------------------------------------------------
// WorldEventHooks Subscribers
// ---------------------------------------------------------------------------

// Check if local player is allowed to interact with the event screen
static bool CanInteract(const glaiel::WorldEvent* worldEvent) {
    if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
        return true; // Single player or not in lobby, always allow
    }

    if (const PersistentCharacter* activeCat = worldEvent->activeCat) {
        const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(activeCat->sql_key);
        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        if (ownerSteamID != 0) {
            return ownerSteamID == localSteamID;
        }
    }
    
    // Failsafe: only the host can interact
    return NetworkManager::Get().IsHost();
}

void RegisterWorldEventSubscribers() {
    ParaboxAPI::OnWorldEventInit.Subscribe([](ParaboxAPI::WorldEventInitEvent& ev) {
        Overlay::Log("[WORLDEVENT] WorldEvent init: %p", ev.self);
    });

    ParaboxAPI::OnWorldEventClickOption.Subscribe([](ParaboxAPI::WorldEventClickOptionEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickOption for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            PersistentCharacter* activeCat = ev.self->activeCat;
            if (ev.optionIndex != -1 && activeCat) {
                WorldEventSelectOptionPacket pkt = {};
                pkt.catUID = activeCat->sql_key;
                pkt.optionIndex = static_cast<uint32_t>(ev.optionIndex);
                NetworkManager::Get().BroadcastPacket(PacketType::WorldEventSelectOption, &pkt, sizeof(pkt), true);
                Overlay::Log("[WORLDEVENT] Local owner selected option %d. Broadcasting sync.", ev.optionIndex);
            } else {
                Overlay::Log("[WORLDEVENT] [ERR] ClickOption index could not be resolved! optionIndex: %d, activeCat: %p",
                             ev.optionIndex, activeCat);
            }
        }
    });

    ParaboxAPI::OnWorldEventClickCat.Subscribe([](ParaboxAPI::WorldEventClickCatEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickCat for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            if (ev.clickedCat) {
                WorldEventSelectCatPacket pkt = {};
                pkt.selectedCatUID = ev.clickedCat->sql_key;
                NetworkManager::Get().BroadcastPacket(PacketType::WorldEventSelectCat, &pkt, sizeof(pkt), true);
                Overlay::Log("[WORLDEVENT] Local owner clicked cat. Syncing selectedCatUID %lld.", ev.clickedCat->sql_key);
            }
        }
    });

    ParaboxAPI::OnWorldEventClickEnd1.Subscribe([](ParaboxAPI::WorldEventClickEnd1Event& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickEnd1 for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            WorldEventClickEndPacket pkt = {};
            pkt.buttonType = 1;
            NetworkManager::Get().BroadcastPacket(PacketType::WorldEventClickEnd, &pkt, sizeof(pkt), true);
            Overlay::Log("[WORLDEVENT] Local owner clicked End1. Broadcasting sync.");
        }
    });

    ParaboxAPI::OnWorldEventClickEnd2.Subscribe([](ParaboxAPI::WorldEventClickEnd2Event& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickEnd2 for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            WorldEventClickEndPacket pkt = {};
            pkt.buttonType = 2;
            NetworkManager::Get().BroadcastPacket(PacketType::WorldEventClickEnd, &pkt, sizeof(pkt), true);
            Overlay::Log("[WORLDEVENT] Local owner clicked End2. Broadcasting sync.");
        }
    });
}

// Trigger functions called from network thread
void TriggerWorldEventSelectOption(int64_t catUID, uint32_t optionIndex) {
    glaiel::WorldEvent* event = ParaboxAPI::GetActiveWorldEvent();
    if (!event || !GameUtils::IsComponentValid(event)) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: g_activeWorldEvent is null or invalid.");
        return;
    }

    PersistentCharacter* activeCat = event->activeCat;
    if (!activeCat || activeCat->sql_key != catUID) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: active screen cat UID (%lld) does not match packet UID (%lld).",
                     activeCat ? activeCat->sql_key : -1, catUID);
        return;
    }

    glaiel::WorldEventOption* vectorStart = event->options.Myfirst;
    glaiel::WorldEventOption* vectorEnd = event->options.Mylast;
    if (!vectorStart || !vectorEnd) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: options vector is null.");
        return;
    }

    int count = static_cast<int>(event->options.size());
    if (static_cast<int>(optionIndex) >= count) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: optionIndex %u out of bounds (%d).", optionIndex, count);
        return;
    }

    Overlay::Log("[WORLDEVENT] Triggering synced ClickOption for index %u", optionIndex);
    ParaboxAPI::ForceWorldEventSelectOption(catUID, optionIndex);
}

void TriggerWorldEventSelectCat(int64_t selectedCatUID) {
    glaiel::WorldEvent* event = ParaboxAPI::GetActiveWorldEvent();
    if (!event || !GameUtils::IsComponentValid(event)) {
        Overlay::Log("[WORLDEVENT] TriggerWorldEventSelectCat error: g_activeWorldEvent is null or invalid.");
        return;
    }

    Overlay::Log("[WORLDEVENT] Triggering synced ClickCat for UID %lld.", selectedCatUID);
    ParaboxAPI::ForceWorldEventSelectCat(selectedCatUID);
}

void TriggerWorldEventClickEnd(uint8_t buttonType) {
    glaiel::WorldEvent* event = ParaboxAPI::GetActiveWorldEvent();
    if (!event || !GameUtils::IsComponentValid(event)) {
        Overlay::Log("[WORLDEVENT] TriggerWorldEventClickEnd error: g_activeWorldEvent is null or invalid.");
        return;
    }

    Overlay::Log("[WORLDEVENT] Triggering synced ClickEnd%d.", buttonType);
    ParaboxAPI::ForceWorldEventClickEnd(buttonType);
}

// ---------------------------------------------------------------------------
// ShopHooks Subscribers
// ---------------------------------------------------------------------------
#include "hooks/ShopHooks.h"

void RegisterShopSubscribers() {
    ParaboxAPI::OnShopBuyItem.Subscribe([](ParaboxAPI::ShopBuyItemEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            ShopBuyItemPacket pkt = {};
            pkt.itemIndex = ev.itemIndex;
            NetworkManager::Get().BroadcastPacket(PacketType::ShopBuyItem, &pkt, sizeof(pkt), true);
            Overlay::Log("[SHOP] Broadcast ShopBuyItem (index %u)", ev.itemIndex);
        }
    });

    ParaboxAPI::OnShopExitButton.Subscribe([](ParaboxAPI::ShopExitButtonEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            ShopExitButtonPacket pkt = {};
            NetworkManager::Get().BroadcastPacket(PacketType::ShopExitButton, &pkt, sizeof(pkt), true);
            Overlay::Log("[SHOP] Broadcast ShopExitButton");
        }
    });

    ParaboxAPI::OnShopChestClick.Subscribe([](ParaboxAPI::ShopChestClickEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            ShopChestClickPacket pkt = {};
            NetworkManager::Get().BroadcastPacket(PacketType::ShopChestClick, &pkt, sizeof(pkt), true);
            Overlay::Log("[SHOP] Broadcast ShopChestClick");
        }
    });

    // For whatever reason, the game decides to use a different, misc RNG here instead of the seeded one.
    // This overrides that behavior, forcing it to select one option.
    ParaboxAPI::OnShopLevelUp.Subscribe([](ParaboxAPI::ShopLevelUpEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid() && ev.optionsVec && ev.optionsVec->size_ > 1) {
            if (void* pickedCat = ParaboxAPI::PickRandomCat(ev.optionsVec)) {
                ev.optionsVec->data_[0] = pickedCat;
                ev.optionsVec->size_ = 1;
                Overlay::Log("[SHOP] [HACK] Synchronized Rare Candy RNG");
            }
        }
    });
}

