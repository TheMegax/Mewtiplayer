#include "events/ClassChooserSubscribers.h"
#include "events/SaveSubscribers.h"
#include "events/StorageSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "mew_ui_api.h"
#include "GameUtils.h"
#include <windows.h>

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

#include <map>

static std::map<int64_t, ULONGLONG> g_lastCollarClickTimePerCat;
static std::map<int64_t, int32_t> g_hostLastBroadcastCollarState;
static std::map<int64_t, int32_t> g_clientCurrentCollarState;

void ResetLobbyReadyStates() {
    g_lobbyReadyStates.clear();
    g_localReady = false;
    g_activeClassChooserLambdaThis = nullptr;
    g_hasTriggeredProceed = false;
    g_lastCollarClickTimePerCat.clear();
    g_hostLastBroadcastCollarState.clear();
    g_clientCurrentCollarState.clear();
    ResetStorageSyncState();
}

void HostAuditCollarState() {
    if (!NetworkManager::Get().IsHost()) return;
    const auto director = GameUtils::GetMewDirectorSingleton();
    if (!director || !director->current_battle_cats.data_ || director->current_battle_cats.size() == 0) return;

    std::map<int32_t, int64_t> collarToCatMap;
    for (size_t i = 0; i < director->current_battle_cats.size(); i++) {
        const int64_t catID = director->current_battle_cats.data_[i];
        CatData *cat = ParaboxAPI::GetCatDataById(catID);
        if (!cat || !cat->cat_class.is_valid()) continue;

        const char *cName = cat->cat_class.begin();
        if (strcmp(cName, "Colorless") == 0 || strcmp(cName, "Fighter") == 0) continue;

        int32_t collarIdx = -1;
        for (int idx = 0; idx < 16; idx++) {
            const char* name = ParaboxAPI::ResolveCollarNameFromIndex(idx);
            if (strcmp(name, cName) == 0) {
                collarIdx = idx;
                break;
            }
        }

        if (collarIdx != -1) {
            auto it = collarToCatMap.find(collarIdx);
            if (it != collarToCatMap.end()) {
                const int64_t existingCatID = it->second;
                Overlay::Log("[LOBBY] Host Audit: Duplicate collar %s (index %d) on cats %lld and %lld. Unequipping from cat %lld!",
                             cName, collarIdx, existingCatID, catID, catID);

                auto *chooser = ParaboxAPI::GetActiveClassChooser();
                ParaboxAPI::ResetCatOnClassChooser(chooser, catID, true);
                ParaboxAPI::UpdateClassChooserTagBoxes(catID, -1);

                CollarSyncPacket unequipPkt = {};
                unequipPkt.catID = catID;
                unequipPkt.collarIndex = -1;
                NetworkManager::Get().BroadcastPacket(PacketType::CollarSync, &unequipPkt, sizeof(unequipPkt), true);
            } else {
                collarToCatMap[collarIdx] = catID;
            }
        }
    }
}

void HostBroadcastFullCollarState(bool force) {
    if (!NetworkManager::Get().IsHost()) return;
    const auto director = GameUtils::GetMewDirectorSingleton();
    if (!director || !director->current_battle_cats.data_ || director->current_battle_cats.size() == 0) return;

    for (size_t i = 0; i < director->current_battle_cats.size(); i++) {
        const int64_t catID = director->current_battle_cats.data_[i];
        CatData *cat = ParaboxAPI::GetCatDataById(catID);
        int32_t collarIdx = -1;
        if (cat && cat->cat_class.is_valid()) {
            const char *cName = cat->cat_class.begin();
            for (int idx = 0; idx < 16; idx++) {
                const char* name = ParaboxAPI::ResolveCollarNameFromIndex(idx);
                if (strcmp(name, cName) == 0) {
                    collarIdx = idx;
                    break;
                }
            }
        }

        if (!force) {
            auto it = g_hostLastBroadcastCollarState.find(catID);
            if (it != g_hostLastBroadcastCollarState.end() && it->second == collarIdx) {
                continue;
            }
        }
        g_hostLastBroadcastCollarState[catID] = collarIdx;

        CollarSyncPacket pkt = {};
        pkt.catID = catID;
        pkt.collarIndex = collarIdx;
        NetworkManager::Get().BroadcastPacket(PacketType::CollarSync, &pkt, sizeof(pkt), true);
    }
}

void HandleCollarSyncInternal(const void *data, const uint32_t length) {
    if (length != sizeof(CollarSyncPacket)) return;

    const auto *packet = (const CollarSyncPacket *)data;
    CatData *cat = ParaboxAPI::GetCatDataById(packet->catID);
    if (!cat) {
        Overlay::Log("[LOBBY] CollarSync: cat %lld not found", packet->catID);
        return;
    }

    auto *chooser = ParaboxAPI::GetActiveClassChooser();

    if (NetworkManager::Get().IsHost()) {
        auto hostStateIt = g_hostLastBroadcastCollarState.find(packet->catID);
        if (hostStateIt != g_hostLastBroadcastCollarState.end() && hostStateIt->second == packet->collarIndex) {
            return;
        }

        if (packet->collarIndex != -1) {
            const auto director = GameUtils::GetMewDirectorSingleton();
            if (director && director->current_battle_cats.data_ && director->current_battle_cats.size() > 0) {
                for (size_t i = 0; i < director->current_battle_cats.size(); i++) {
                    const int64_t otherCatID = director->current_battle_cats.data_[i];
                    if (otherCatID == packet->catID) continue;

                    CatData *otherCat = ParaboxAPI::GetCatDataById(otherCatID);
                    if (!otherCat || !otherCat->cat_class.is_valid()) continue;

                    const char *otherClassName = otherCat->cat_class.begin();
                    const char *requestedCollarName = ParaboxAPI::ResolveCollarNameFromIndex(packet->collarIndex);

                    if (strcmp(otherClassName, requestedCollarName) == 0) {
                        Overlay::Log("[LOBBY] Host: Collar conflict detected! Cat %lld already had %s (index %d). Unequipping from cat %lld!",
                                     otherCatID, requestedCollarName, packet->collarIndex, otherCatID);

                        ParaboxAPI::ResetCatOnClassChooser(chooser, otherCatID, true);
                        ParaboxAPI::UpdateClassChooserTagBoxes(otherCatID, -1);

                        CollarSyncPacket unequipPkt = {};
                        unequipPkt.catID = otherCatID;
                        unequipPkt.collarIndex = -1;
                        NetworkManager::Get().BroadcastPacket(PacketType::CollarSync, &unequipPkt, sizeof(unequipPkt), true);
                    }
                }
            }
        }

        if (packet->collarIndex == -1) {
            ParaboxAPI::ResetCatOnClassChooser(chooser, packet->catID, true);
            ParaboxAPI::UpdateClassChooserTagBoxes(packet->catID, -1);
        } else {
            ParaboxAPI::ResetCatOnClassChooser(chooser, packet->catID, false);
            const char *collarName = ParaboxAPI::ResolveCollarNameFromIndex(packet->collarIndex);
            ParaboxAPI::ApplyCollarToCharacter(cat, collarName);
            ParaboxAPI::UpdateClassChooserTagBoxes(packet->catID, packet->collarIndex);
        }
        ParaboxAPI::RefreshClassChooserInventory();
        ParaboxAPI::RefreshCatSelectorUI();

        g_hostLastBroadcastCollarState[packet->catID] = packet->collarIndex;

        CollarSyncPacket validPkt = *packet;
        NetworkManager::Get().BroadcastPacket(PacketType::CollarSync, &validPkt, sizeof(validPkt), true);
        Overlay::Log("[LOBBY] Host broadcast collar: cat %lld -> index %d", packet->catID, packet->collarIndex);

        HostAuditCollarState();
    } else {
        const auto it = g_clientCurrentCollarState.find(packet->catID);
        if (it != g_clientCurrentCollarState.end() && it->second == packet->collarIndex) {
            return;
        }

        const auto clickTimeIt = g_lastCollarClickTimePerCat.find(packet->catID);
        if (clickTimeIt != g_lastCollarClickTimePerCat.end()) {
            if (GetTickCount64() - clickTimeIt->second < 400) {
                return;
            }
        }

        g_clientCurrentCollarState[packet->catID] = packet->collarIndex;

        if (packet->collarIndex == -1) {
            ParaboxAPI::ResetCatOnClassChooser(chooser, packet->catID, true);
            ParaboxAPI::UpdateClassChooserTagBoxes(packet->catID, -1);
        } else {
            ParaboxAPI::ResetCatOnClassChooser(chooser, packet->catID, false);
            const char *collarName = ParaboxAPI::ResolveCollarNameFromIndex(packet->collarIndex);
            ParaboxAPI::ApplyCollarToCharacter(cat, collarName);
            ParaboxAPI::UpdateClassChooserTagBoxes(packet->catID, packet->collarIndex);
        }
        Overlay::Log("[LOBBY] Client collar sync: cat %lld -> index %d", packet->catID, packet->collarIndex);

        ParaboxAPI::RefreshClassChooserInventory();
        ParaboxAPI::RefreshCatSelectorUI();
    }
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

    if (NetworkManager::Get().IsHost() && NetworkManager::Get().GetCurrentLobby().IsValid()) {
        static ULONGLONG s_lastCollarHeartbeatTime = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastCollarHeartbeatTime >= 1000) {
            s_lastCollarHeartbeatTime = now;
            if (MewUI_IsSceneBindingActive(&g_classChooserScene) || ParaboxAPI::GetActiveClassChooser() != nullptr) {
                HostAuditCollarState();
                HostBroadcastFullCollarState(true);
            }
        }
    }

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

void CatSelectorHooks_TriggerLockInProceed() {
    if (g_hasTriggeredProceed) return;
    g_hasTriggeredProceed = true;

    ParaboxAPI::ForceClassChooserClose();
    g_activeClassChooserLambdaThis = nullptr;

    StorageHooks_TriggerEmbarkProceed();

    g_lobbyReadyStates.clear();
    g_localReady = false;
}

void RegisterClassChooserSubscribers() {
    ParaboxAPI::OnClassTagBoxClick.Subscribe([](ParaboxAPI::ClassTagBoxClickEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (ev.catID != -1 && !NetworkManager::Get().IsCatControlledLocally(ev.catID)) {
                ev.Cancel(); // Block clicking the collar of other players
                return;
            }

            CatData *cat = ParaboxAPI::GetCatDataById(ev.catID);
            if (!cat || ev.clickedIndex == -1) {
                ev.Cancel();
                return;
            }

            const ULONGLONG now = GetTickCount64();
            auto clickIt = g_lastCollarClickTimePerCat.find(ev.catID);
            if (clickIt != g_lastCollarClickTimePerCat.end() && (now - clickIt->second < 150)) {
                ev.Cancel();
                return;
            }
            g_lastCollarClickTimePerCat[ev.catID] = now;

            int32_t currentCollarIndex = -1;
            const auto &stateMap = NetworkManager::Get().IsHost() ? g_hostLastBroadcastCollarState : g_clientCurrentCollarState;
            auto stateIt = stateMap.find(ev.catID);
            if (stateIt != stateMap.end()) {
                currentCollarIndex = stateIt->second;
            } else if (cat->cat_class.is_valid()) {
                const char *cName = cat->cat_class.begin();
                for (int idx = 0; idx < 16; idx++) {
                    if (strcmp(ParaboxAPI::ResolveCollarNameFromIndex(idx), cName) == 0) {
                        currentCollarIndex = idx;
                        break;
                    }
                }
            }

            const int32_t collarIndex = (currentCollarIndex == ev.clickedIndex) ? -1 : ev.clickedIndex;
            const char *clickedClassName = ParaboxAPI::ResolveCollarNameFromIndex(ev.clickedIndex);

            CollarSyncPacket packet = {};
            packet.catID = ev.catID;
            packet.collarIndex = collarIndex;

            if (NetworkManager::Get().IsHost()) {
                HandleCollarSyncInternal(&packet, sizeof(packet));
                ev.Cancel();
            } else {
                auto *chooser = ParaboxAPI::GetActiveClassChooser();

                if (collarIndex != -1) {
                    const auto director = GameUtils::GetMewDirectorSingleton();
                    if (director && director->current_battle_cats.data_ && director->current_battle_cats.size() > 0) {
                        for (size_t i = 0; i < director->current_battle_cats.size(); i++) {
                            const int64_t otherCatID = director->current_battle_cats.data_[i];
                            if (otherCatID == ev.catID) continue;

                            CatData *otherCat = ParaboxAPI::GetCatDataById(otherCatID);
                            if (!otherCat || !otherCat->cat_class.is_valid()) continue;

                            const char *otherClassName = otherCat->cat_class.begin();
                            if (strcmp(otherClassName, clickedClassName) == 0) {
                                g_clientCurrentCollarState[otherCatID] = -1;
                                ParaboxAPI::ResetCatOnClassChooser(chooser, otherCatID, true);
                                ParaboxAPI::UpdateClassChooserTagBoxes(otherCatID, -1);
                            }
                        }
                    }
                }

                g_clientCurrentCollarState[ev.catID] = collarIndex;

                if (collarIndex == -1) {
                    ParaboxAPI::ResetCatOnClassChooser(chooser, ev.catID, true);
                    ParaboxAPI::UpdateClassChooserTagBoxes(ev.catID, -1);
                } else {
                    ParaboxAPI::ResetCatOnClassChooser(chooser, ev.catID, false);
                    ParaboxAPI::ApplyCollarToCharacter(cat, clickedClassName);
                    ParaboxAPI::UpdateClassChooserTagBoxes(ev.catID, collarIndex);
                }
                ParaboxAPI::RefreshClassChooserInventory();
                ParaboxAPI::RefreshCatSelectorUI();

                NetworkManager::Get().SendPacketReliable(NetworkManager::Get().GetHostID(), PacketType::CollarSync, &packet, sizeof(packet));
                Overlay::Log("[LOBBY] Client collar request: cat %lld -> index %d", ev.catID, collarIndex);
                ev.Cancel();
            }
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
        NetworkManager::Get().BroadcastPacket(PacketType::LobbyReady, &packet, sizeof(packet), true);
        Overlay::Log("[LOBBY] Local ready state: %s", g_localReady ? "locked in" : "not ready");

        ev.Cancel();

        if (NetworkManager::Get().IsHost() && AreAllLobbyMembersReady()) {
            HostAuditCollarState();
            HostBroadcastFullCollarState(true);
            HostAuditAndBroadcastStorageState();
            NetworkManager::Get().BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
            CatSelectorHooks_TriggerLockInProceed();
        }
    });
}
