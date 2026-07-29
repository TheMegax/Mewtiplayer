#include "events/MapSubscribers.h"
#include "events/SaveSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "mew_ui_api.h"
#include "GameUtils.h"
#include "ModState.h"

// ---------------------------------------------------------------------------
// MapHooks Subscribers
// ---------------------------------------------------------------------------

uint32_t g_pendingMapNodeSyncIndex = 0xFFFFFFFF;
bool g_isHandlingNetworkMapNodeSync = false;

static MewUISceneBinding g_mapScene = {};
static bool g_mapSceneInitialized = false;
static void* g_mapInventoryButton = nullptr;
static bool g_mapInventoryButtonHooked = false;

static bool g_ownershipRestoredForThisLoad = false;

static void __cdecl MapSceneRefreshCallback(MewUISceneBinding* binding, MewUISceneRefreshResult result, void* old_scene_manager, void* new_scene_manager, void* user_data) {
    if (result == MEW_UI_SCENE_REFRESH_LOADED || result == MEW_UI_SCENE_REFRESH_CHANGED) {
        g_mapInventoryButton = nullptr;
        g_mapInventoryButtonHooked = false;

        // Restore ownership from the cat_ownership table whenever the Map scene loads
        // while in a lobby. This covers the Continue Run case where CreateStrayCat
        // does not fire. The one-shot flag prevents double-restoring per load.
        if (!g_ownershipRestoredForThisLoad &&
            NetworkManager::Get().GetCurrentLobby().IsValid()) {
            g_ownershipRestoredForThisLoad = true;
            NetworkManager::Get().RestoreOwnershipFromSave(CUSTOM_SAVE_NAME.c_str());
        }
    } else if (result == MEW_UI_SCENE_REFRESH_UNLOADED) {
        g_mapInventoryButton = nullptr;
        g_mapInventoryButtonHooked = false;
        g_ownershipRestoredForThisLoad = false; // Reset for the next load
    }
}

void MapHooks_UITick() {
    if (!g_mapSceneInitialized) {
        MewUI_InitSceneBinding(&g_mapScene, "Map", MapSceneRefreshCallback, nullptr);
        g_mapSceneInitialized = true;
    }

    MewUI_RefreshSceneBinding(&g_mapScene);

    if (MewUI_IsSceneBindingActive(&g_mapScene)) {
        void* scene_manager = MewUI_GetSceneBindingScene(&g_mapScene);

        if (!g_mapInventoryButtonHooked) {
            if (scene_manager) {
                // Keep the button hooked so we can activate it from the remote side
                if (void* button = MewUI_FindButtonByRole(scene_manager, "Map_Backpack")) {
                    g_mapInventoryButton = button;
                    g_mapInventoryButtonHooked = true;
                    Overlay::Log("[MAP] Hooked Map Inventory Button!");
                }
            }
        }
    }
}

void MapHooks_Shutdown() {
    if (g_mapSceneInitialized) {
        MewUI_ClearSceneBinding(&g_mapScene);
        g_mapSceneInitialized = false;
    }
    g_mapInventoryButton = nullptr;
    g_mapInventoryButtonHooked = false;
}

void ForceMapInventoryOpen() {
    g_isHandlingNetworkMapInventoryOpen = true;
    ParaboxAPI::ForceMapInventoryOpen();
    g_isHandlingNetworkMapInventoryOpen = false;
}

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
