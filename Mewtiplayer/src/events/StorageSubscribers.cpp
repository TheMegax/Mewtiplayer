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

void *g_activeInventoryScreenThis = nullptr;
static MewUISceneBinding g_storageItemsScene;
static void* g_storageLockInButton = nullptr;
static bool g_storageItemsSceneInitialized = false;
static bool g_storageLastLocalReadyState = false;
static bool g_storageButtonHooked = false;

static std::set<int64_t> g_pendingStorageCatSyncs;
static ULONGLONG g_lastHostStorageClickTime = 0;

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
    if (g_activeInventoryScreenThis) {
        ParaboxAPI::ForceInventoryScreen2Close(g_activeInventoryScreenThis);
        g_activeInventoryScreenThis = nullptr;
    }
}

void HandleStorageItemSyncInternal(const void *data, const uint32_t length) {
    if (length != sizeof(StorageItemSyncPacket)) return;

    const auto *packet = (const StorageItemSyncPacket *)data;
    Overlay::Log("[STORAGE] Storage item sync: player %llu, slot %d (cat %lld)",
                 packet->steamID, packet->slotIndex, packet->catID);

    g_pendingStorageCatSyncs.erase(packet->catID);

    if (NetworkManager::Get().IsHost()) {
        ParaboxAPI::g_isHandlingNetworkStorageItemSync = true;
        ParaboxAPI::UpdateStorageItemSlot(packet->slotIndex, packet->catID);
        ParaboxAPI::g_isHandlingNetworkStorageItemSync = false;
        ParaboxAPI::RefreshCatSelectorUI();

        StorageItemSyncPacket validPkt = *packet;
        NetworkManager::Get().BroadcastPacket(PacketType::StorageItemSync, &validPkt, sizeof(validPkt), true);
    } else {
        ParaboxAPI::g_isHandlingNetworkStorageItemSync = true;
        ParaboxAPI::UpdateStorageItemSlot(packet->slotIndex, packet->catID);
        ParaboxAPI::g_isHandlingNetworkStorageItemSync = false;
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
    });

    ParaboxAPI::OnInventoryItemBoxEquipped.Subscribe([](ParaboxAPI::InventoryItemBoxEquippedEvent& ev) {
        if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;
        if (ParaboxAPI::g_isHandlingNetworkStorageItemSync) return;

        const int32_t slotIndex = ParaboxAPI::FindStorageSlotIndex(ev.self);
        if (slotIndex == -1) return;

        StorageItemSyncPacket packet = {};
        packet.steamID = SteamUser()->GetSteamID().ConvertToUint64();
        packet.catID = ParaboxAPI::ResolveSelectedCatID();
        packet.slotIndex = slotIndex;

        if (NetworkManager::Get().IsHost()) {
            const ULONGLONG now = GetTickCount64();
            if (now - g_lastHostStorageClickTime < 150) {
                return;
            }
            g_lastHostStorageClickTime = now;

            StorageItemSyncPacket validPkt = packet;
            NetworkManager::Get().BroadcastPacket(PacketType::StorageItemSync, &validPkt, sizeof(validPkt), true);
            Overlay::Log("[STORAGE] Host equipped storage item: slot %d (cat %lld), broadcast to clients", slotIndex, packet.catID);
        } else {
            if (g_pendingStorageCatSyncs.count(packet.catID) > 0) {
                Overlay::Log("[STORAGE] Storage item sync in-flight for cat %lld, ignoring rapid click", packet.catID);
                return;
            }
            g_pendingStorageCatSyncs.insert(packet.catID);

            NetworkManager::Get().SendPacketReliable(NetworkManager::Get().GetHostID(), PacketType::StorageItemSync, &packet, sizeof(packet));
            Overlay::Log("[STORAGE] Client requested storage item sync from Host: slot %d (cat %lld)", slotIndex, packet.catID);
        }
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
