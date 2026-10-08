#include "events/StorageSubscribers.h"
#include "events/ClassChooserSubscribers.h"
#include "events/SaveSubscribers.h"
#include "events/MapSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "mew_ui_api.h"
#include "GameUtils.h"
#include "SteamABICompat.h"
#include <windows.h>

// ---------------------------------------------------------------------------
// StorageHooks Subscribers and UI state
// ---------------------------------------------------------------------------

#include <set>
#include <map>

void *g_activeInventoryScreenThis = nullptr;
static MewUISceneBinding g_storageItemsScene;
static void* g_storageLockInButton = nullptr;
static bool g_storageItemsSceneInitialized = false;
static bool g_storageLastLocalReadyState = false;
static bool g_storageButtonHooked = false;

static void ApplyStorageLockInButtonText() {
    if (!g_storageLockInButton) return;

    const char* lockinoutText = g_localReady ? "Lock Out" : "Lock In!";
    const auto gameBase = (uintptr_t)GetModuleHandleA(nullptr);
    const auto initString = reinterpret_cast<MewFnInitNarrowString>(gameBase + MEW_RVA_INIT_NARROW_STRING);
    const auto setTextString = reinterpret_cast<MewFnUIRootSetTextString>(gameBase + MEW_RVA_UI_ROOT_SET_TEXT_STRING);

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

    if (NetworkManager::Get().IsHost() && NetworkManager::Get().GetCurrentLobby().IsValid()) {
        static ULONGLONG s_lastStorageHeartbeatTime = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_lastStorageHeartbeatTime >= 1000) {
            s_lastStorageHeartbeatTime = now;
            if (MewUI_IsSceneBindingActive(&g_storageItemsScene) || ParaboxAPI::GetActiveInventoryScreen2() != nullptr) {
                HostAuditAndBroadcastStorageState();
            }
        }
    }

    if (MewUI_IsSceneBindingActive(&g_storageItemsScene)) {
        void* scene_manager = MewUI_GetSceneBindingScene(&g_storageItemsScene);

        if (!g_storageButtonHooked) {
            if (scene_manager) {
                if (void* button = MewUI_FindButtonByRole(scene_manager, "CloseButton")) {
                    bool isCatStatusScreen = false;
                    for (const auto* scene : GameUtils::GetCurrentScenes()) {
                        if (!scene) continue;
                        if (scene->name.is_valid() && scene->name.as_native_string_view() == "CatStatus") {
                            isCatStatusScreen = true;
                            break;
                        }
                    }
                    if (!isCatStatusScreen) {
                        MewUI_RegisterExistingButton(button, nullptr, StorageLockInButtonCallback, nullptr);
                        g_storageLockInButton = button;
                    } else {
                        g_storageLockInButton = nullptr;
                    }
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
    void *target = g_activeInventoryScreenThis;
    if (!target) {
        target = ParaboxAPI::GetActiveInventoryScreen2();
    }
    if (target) {
        ParaboxAPI::ForceInventoryScreen2Close(target);
        g_activeInventoryScreenThis = nullptr;
    }
}

static std::map<int32_t, int64_t> g_hostLastBroadcastStorageState;
static std::map<int32_t, int64_t> g_clientCurrentStorageState;
static std::map<int32_t, ULONGLONG> g_lastStorageClickTimePerSlot;

void ResetStorageSyncState() {
    g_hostLastBroadcastStorageState.clear();
    g_clientCurrentStorageState.clear();
    g_lastStorageClickTimePerSlot.clear();
}

void HostAuditAndBroadcastStorageState(bool force) {
    if (!NetworkManager::Get().IsHost()) return;
    auto *screen = ParaboxAPI::GetActiveInventoryScreen2();
    if (!screen || !screen->boxes.data_) return;

    ParaboxAPI::RefreshInventoryEquippedStatus();

    for (uint32_t i = 0; i < screen->boxes.size(); i++) {
        const auto *box = screen->boxes.data_[i];
        if (!box) continue;
        const int32_t sortOrder = ParaboxAPI::GetItemSortOrder(box);
        if (sortOrder < 0) continue;

        const int64_t currentOwner = ParaboxAPI::GetItemEquippedOwner(box->item_id);
        if (!force) {
            auto it = g_hostLastBroadcastStorageState.find(sortOrder);
            if (it != g_hostLastBroadcastStorageState.end()) {
                if (it->second == currentOwner) {
                    continue;
                }
            } else {
                // seed unequipped state quietly on first pass
                if (currentOwner == -1) {
                    g_hostLastBroadcastStorageState[sortOrder] = -1;
                    continue;
                }
            }
        }
        g_hostLastBroadcastStorageState[sortOrder] = currentOwner;

        StorageItemSyncPacket pkt = {};
        pkt.steamID = SteamUser()->GetSteamID().ConvertToUint64();
        pkt.catID = currentOwner;
        pkt.sortOrder = sortOrder;
        NetworkManager::Get().BroadcastPacket(PacketType::StorageItemSync, &pkt, sizeof(pkt), true);
        Overlay::Log("[STORAGE] Host broadcast: item %d -> cat %lld", sortOrder, currentOwner);
    }
}

void HostBroadcastFullStorageState(bool force) {
    HostAuditAndBroadcastStorageState(force);
}

void HandleStorageItemSyncInternal(const void *data, const uint32_t length) {
    if (length != sizeof(StorageItemSyncPacket)) return;

    const auto *packet = (const StorageItemSyncPacket *)data;

    if (NetworkManager::Get().IsHost()) {
        const int64_t currentOwner = ParaboxAPI::GetSortOrderItemEquippedOwner(packet->sortOrder);

        // make sure sender owns the cat receiving the item
        if (packet->catID != -1) {
            uint64_t targetOwner = NetworkManager::Get().GetCatOwner(packet->catID);
            if (targetOwner == 0) {
                const auto it = g_catIdToOwnerSteamID.find(packet->catID);
                if (it != g_catIdToOwnerSteamID.end()) targetOwner = it->second;
            }
            if (targetOwner != 0 && targetOwner != packet->steamID) {
                Overlay::Log("[STORAGE] Rejected sync from %llu: cat %lld owned by %llu",
                             packet->steamID, packet->catID, targetOwner);
                StorageItemSyncPacket healPkt = {};
                healPkt.steamID = SteamUser()->GetSteamID().ConvertToUint64();
                healPkt.catID = currentOwner;
                healPkt.sortOrder = packet->sortOrder;
                NetworkManager::Get().BroadcastPacket(PacketType::StorageItemSync, &healPkt, sizeof(healPkt), true);
                return;
            }
        }

        // make sure sender owns the cat currently holding the item
        if (currentOwner != -1) {
            uint64_t currentOwnerSteamID = NetworkManager::Get().GetCatOwner(currentOwner);
            if (currentOwnerSteamID == 0) {
                const auto it = g_catIdToOwnerSteamID.find(currentOwner);
                if (it != g_catIdToOwnerSteamID.end()) currentOwnerSteamID = it->second;
            }
            if (currentOwnerSteamID != 0 && currentOwnerSteamID != packet->steamID) {
                Overlay::Log("[STORAGE] Rejected sync from %llu: item %d on cat %lld owned by %llu",
                             packet->steamID, packet->sortOrder, currentOwner, currentOwnerSteamID);
                StorageItemSyncPacket healPkt = {};
                healPkt.steamID = SteamUser()->GetSteamID().ConvertToUint64();
                healPkt.catID = currentOwner;
                healPkt.sortOrder = packet->sortOrder;
                NetworkManager::Get().BroadcastPacket(PacketType::StorageItemSync, &healPkt, sizeof(healPkt), true);
                return;
            }
        }

        auto hostStateIt = g_hostLastBroadcastStorageState.find(packet->sortOrder);
        if (hostStateIt != g_hostLastBroadcastStorageState.end() && hostStateIt->second == packet->catID) {
            return;
        }

        g_isHandlingNetworkStorageItemSync = true;
        ParaboxAPI::UpdateStorageItemBySortOrder(packet->sortOrder, packet->catID);
        g_isHandlingNetworkStorageItemSync = false;

        Overlay::Log("[STORAGE] Host applied: player %llu, item %d -> cat %lld",
                     packet->steamID, packet->sortOrder, packet->catID);

        // broadcast diffs for this change (handles swapped out item too)
        HostAuditAndBroadcastStorageState(false);
    } else {
        const int64_t currentOwner = ParaboxAPI::GetSortOrderItemEquippedOwner(packet->sortOrder);
        if (currentOwner == packet->catID) {
            g_clientCurrentStorageState[packet->sortOrder] = packet->catID;
            return;
        }

        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        if (packet->steamID == localSteamID) {
            const auto clickTimeIt = g_lastStorageClickTimePerSlot.find(packet->sortOrder);
            if (clickTimeIt != g_lastStorageClickTimePerSlot.end()) {
                if (GetTickCount64() - clickTimeIt->second < 150) {
                    return;
                }
            }
        }

        g_clientCurrentStorageState[packet->sortOrder] = packet->catID;

        Overlay::Log("[STORAGE] Client sync: player %llu, item %d (%lld -> cat %lld)",
                     packet->steamID, packet->sortOrder, currentOwner, packet->catID);

        g_isHandlingNetworkStorageItemSync = true;
        ParaboxAPI::UpdateStorageItemBySortOrder(packet->sortOrder, packet->catID);
        g_isHandlingNetworkStorageItemSync = false;
        ParaboxAPI::RefreshCatSelectorUI();
    }
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
                } else if (compView == "InventoryScreen2") {
                    if (!g_isHandlingNetworkMapInventoryOpen && GameUtils::IsComponentValid(ParaboxAPI::GetMapScreen())) {
                        MapInventoryOpenPacket pkt = {};
                        NetworkManager::Get().BroadcastPacket(PacketType::MapInventoryOpen, &pkt, sizeof(pkt), true);
                    }
                }
                GameUtils::FreeXString(compName);
            }
        }
    });

    ParaboxAPI::OnInventoryItemBoxClick.Subscribe([](ParaboxAPI::InventoryItemBoxClickEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            const int64_t catID = ParaboxAPI::ResolveSelectedCatID();
            if (catID != -1 && !NetworkManager::Get().IsCatControlledLocally(catID)) {
                Overlay::Log("[STORAGE] Blocked click: cat %lld not owned locally", catID);
                ev.Cancel();
                return;
            }

            const auto *box = static_cast<const InventoryItemBox *>(ev.self);
            if (!box || catID == -1) {
                ev.Cancel();
                return;
            }

            const int32_t sortOrder = ParaboxAPI::GetItemSortOrder(box);
            if (sortOrder < 0) {
                ev.Cancel();
                return;
            }

            static std::map<int32_t, ULONGLONG> s_lastSlotUserClickTime;
            const ULONGLONG now = GetTickCount64();
            auto clickIt = s_lastSlotUserClickTime.find(sortOrder);
            if (clickIt != s_lastSlotUserClickTime.end() && (now - clickIt->second < 150)) {
                ev.Cancel();
                return;
            }
            s_lastSlotUserClickTime[sortOrder] = now;

            ParaboxAPI::RefreshInventoryEquippedStatus();
            const int64_t currentOwner = ParaboxAPI::GetItemEquippedOwner(box->item_id);

            // can't take items off cats we don't own
            if (currentOwner != -1 && !NetworkManager::Get().IsCatControlledLocally(currentOwner)) {
                Overlay::Log("[STORAGE] Blocked click: item %lld held by cat %lld (unowned)",
                             box->item_id, currentOwner);
                ev.Cancel();
                return;
            }

            const int64_t targetCatID = (currentOwner == catID) ? -1 : catID;

            StorageItemSyncPacket packet = {};
            packet.steamID = SteamUser()->GetSteamID().ConvertToUint64();
            packet.catID = targetCatID;
            packet.sortOrder = sortOrder;

            g_lastStorageClickTimePerSlot[sortOrder] = GetTickCount64();

            if (NetworkManager::Get().IsHost()) {
                HandleStorageItemSyncInternal(&packet, sizeof(packet));
                ev.Cancel();
            } else {
                g_clientCurrentStorageState[sortOrder] = targetCatID;
                g_isHandlingNetworkStorageItemSync = true;
                ParaboxAPI::UpdateStorageItemBySortOrder(sortOrder, targetCatID);
                g_isHandlingNetworkStorageItemSync = false;
                ParaboxAPI::RefreshCatSelectorUI();

                NetworkManager::Get().SendPacketReliable(NetworkManager::Get().GetHostID(), PacketType::StorageItemSync, &packet, sizeof(packet));
                Overlay::Log("[STORAGE] Client request: item %d -> cat %lld", sortOrder, targetCatID);
                ev.Cancel();
            }
        }
    });

    ParaboxAPI::OnInventoryItemBoxEquipped.Subscribe([](ParaboxAPI::InventoryItemBoxEquippedEvent& ev) {
        // Handled via OnInventoryItemBoxClick
    });

    ParaboxAPI::OnInventoryScreen2Close.Subscribe([](ParaboxAPI::InventoryScreen2CloseEvent& ev) {
        if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;

        bool isCatStatusScreen = false;
        for (const auto* scene : GameUtils::GetCurrentScenes()) {
            if (!scene) continue;
            if (scene->name.is_valid() && scene->name.as_native_string_view() == "CatStatus") {
                isCatStatusScreen = true;
                break;
            }
        }
        
        if (isCatStatusScreen) {
            MapInventoryClosePacket pkt = {};
            NetworkManager::Get().BroadcastPacket(PacketType::MapInventoryClose, &pkt, sizeof(pkt), true);
            return;
        }

        g_activeInventoryScreenThis = ev.self;
        g_localReady = !g_localReady;

        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        g_lobbyReadyStates[localSteamID] = g_localReady;

        LobbyReadyPacket packet = {};
        packet.steamID = localSteamID;
        packet.isReady = g_localReady;
        NetworkManager::Get().BroadcastPacket(PacketType::LobbyReady, &packet, sizeof(packet), true);
        Overlay::Log("[LOBBY] LockIn: Local ready state: %s", g_localReady ? "locked in" : "not ready");

        if (NetworkManager::Get().IsHost() && AreAllLobbyMembersReady()) {
            if (!g_hasTriggeredProceed) {
                HostAuditCollarState();
                HostBroadcastFullCollarState(true);
                HostAuditAndBroadcastStorageState(true);
                NetworkManager::Get().BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
                CatSelectorHooks_TriggerLockInProceed();
            } else {
                ev.Cancel();
            }
        } else {
            ev.Cancel();
        }
    });
}
