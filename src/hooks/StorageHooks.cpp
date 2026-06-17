#include "hooks/StorageHooks.h"
#include "hooks/HookMacros.h"
#include "../../include/ModState.h"
#include "GameUtils.h"
#include "MewgenicsTypes.h"
#include "Overlay.h"
#include "NetworkManager.h"
#include "Scanner.h"
#include "SteamABICompat.h"
#include <vector>

#include "hooks/ClassChooserHooks.h"

typedef void (__fastcall *RefreshInventoryScreen_t)(void *inventoryScreen);
static RefreshInventoryScreen_t g_RefreshInventoryScreen = nullptr;

HOOK_DEFINE(InventoryItemBox_Click, void, void *)
HOOK_DEFINE(SceneManager_CreateScene, void *, void *, void *)
HOOK_DEFINE(Scene_AddComponent, void, void *, void *)

static std::vector<void *> g_storageItemBoxes;
extern void *g_MapScreen;

static void * __fastcall Hook_SceneManager_CreateScene(void *self, void *nameStr) {
  if (nameStr) {
    const auto *xstr = static_cast<MsvcReleaseModeXString *>(nameStr);
    if (xstr->is_valid()) {
      const auto view = xstr->as_native_string_view();
      if (view == "StorageItems") {
        g_storageItemBoxes.clear();
      }
      if (view == "House" || view == "StorageItems" || view == "Combat" || view == "MainMenu" || view == "ClassChooser") {
        // ReSharper disable once CppEntityAssignedButNoRead
        extern uint32_t g_pendingMapNodeSyncIndex;
        g_pendingMapNodeSyncIndex = 0xFFFFFFFF;
      }
    }
  }
  if (g_origSceneManager_CreateScene) {
    return g_origSceneManager_CreateScene(self, nameStr);
  }
  return nullptr;
}

static void __fastcall Hook_Scene_AddComponent(void *scene, void *comp) {
  if (g_origScene_AddComponent) {
    g_origScene_AddComponent(scene, comp);
  }

  if (!scene || !comp) return;

  const auto *s = static_cast<Scene *>(scene);
  if (s->name.is_valid()) {
    MsvcReleaseModeXString compName = {};
    if (GameUtils::SafeGetComponentName(static_cast<Component *>(comp), &compName)) {
      const auto compView = compName.as_native_string_view();
      if (s->name.as_native_string_view() == "StorageItems" && compView == "InventoryItemBox") {
        g_storageItemBoxes.push_back(comp);
      } else if (compView == "MapScreen") {
        g_MapScreen = comp;
        Overlay::Log("[MAP] Registered MapScreen %p", comp);
        extern uint32_t g_pendingMapNodeSyncIndex;
        extern void TriggerMapNodeSync(uint32_t nodeIndex);
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
}


static int32_t FindStorageSlotIndex(const void *clickedBox) {
  for (size_t i = 0; i < g_storageItemBoxes.size(); i++) {
    if (g_storageItemBoxes[i] == clickedBox) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

static int64_t ResolveSelectedCatID() {
  if (const auto *director = GameUtils::GetMewDirectorSingleton();
      director && director->pedigreeState) {
    if (const void *catSelector = g_activeCatSelector) {
      if (IsCatSelectorValid(catSelector)) {
        return static_cast<const glaiel::CatSelector *>(catSelector)->catID;
      }
    }
  }
  return -1;
}

static void __fastcall Hook_InventoryItemBox_Click(void *self) {
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    const int64_t catID = ResolveSelectedCatID();
    if (catID != -1) {
      extern std::map<int64_t, uint64_t> g_catIdToOwnerSteamID;
      const auto ownerIt = g_catIdToOwnerSteamID.find(catID);
      if (ownerIt != g_catIdToOwnerSteamID.end() && ownerIt->second != 0) {
        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        if (ownerIt->second != localSteamID) {
          Overlay::Log("[STORAGE] Blocked click on cat %lld (not owned by player)", catID);
          return;
        }
      }
    }
  }

  if (g_origInventoryItemBox_Click) {
    g_origInventoryItemBox_Click(self);
  }

  if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
    return;
  }

  const int32_t slotIndex = FindStorageSlotIndex(self);
  if (slotIndex == -1) {
    return;
  }

  StorageItemSyncPacket packet = {};
  packet.steamID = SteamUser()->GetSteamID().ConvertToUint64();
  packet.catID = ResolveSelectedCatID();
  packet.slotIndex = slotIndex;

  NetworkManager::Get().BroadcastPacket(PacketType::StorageItemSync, &packet, sizeof(packet), true);
  Overlay::Log("[STORAGE] Broadcast storage item sync: slot %d (cat %lld)", slotIndex, packet.catID);
}

void UpdateStorageItemSlot(const int32_t slotIndex, const int64_t catID) {
  if (slotIndex >= 0 && static_cast<size_t>(slotIndex) < g_storageItemBoxes.size()) {
    void *comp = g_storageItemBoxes[slotIndex];
    if (g_origInventoryItemBox_Click) {
      auto *itemBox = static_cast<glaiel::InventoryItemBox *>(comp);
      if (auto *inventoryScreen = static_cast<glaiel::InventoryScreen *>(itemBox->inventoryScreen)) {
        int64_t *screenCatIDPtr = &inventoryScreen->catID;
        const int64_t origCatID = *screenCatIDPtr;
        *screenCatIDPtr = catID;
        g_origInventoryItemBox_Click(comp);
        *screenCatIDPtr = origCatID;
        if (g_RefreshInventoryScreen) {
          g_RefreshInventoryScreen(inventoryScreen);
        }
      } else {
        g_origInventoryItemBox_Click(comp);
      }
    }
    Overlay::Log("[STORAGE] Updated storage slot %d for cat %lld using map", slotIndex, catID);
  }
}

void HandleStorageItemSyncInternal(const void *data, const uint32_t length) {
  if (length != sizeof(StorageItemSyncPacket)) {
    return;
  }

  const auto *packet = (const StorageItemSyncPacket *)data;
  Overlay::Log("[STORAGE] Received storage item sync from player %llu: slot %d (cat %lld)",
               packet->steamID, packet->slotIndex, packet->catID);

  UpdateStorageItemSlot(packet->slotIndex, packet->catID);
}

void *g_activeInventoryScreenThis = nullptr;

HOOK_DEFINE(InventoryScreen2_Close, void, void *)

static void __fastcall Hook_InventoryScreen2_Close(void *self) {
  if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_origInventoryScreen2_Close) {
      g_origInventoryScreen2_Close(self);
    }
    return;
  }

  g_activeInventoryScreenThis = self;
  g_localReady = !g_localReady;

  const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
  g_lobbyReadyStates[localSteamID] = g_localReady;

  LobbyReadyPacket packet = {};
  packet.steamID = localSteamID;
  packet.isReady = g_localReady;
  NetworkManager::Get().BroadcastPacket(PacketType::LobbyReady, &packet, sizeof(packet), false);
  Overlay::Log("[LOBBY] LockIn: Local ready state: %s", g_localReady ? "locked in" : "not ready");

  if (NetworkManager::Get().IsHost() && AreAllLobbyMembersReady()) {
    g_hasTriggeredProceed = true;
    NetworkManager::Get().BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
    if (g_origInventoryScreen2_Close) {
      g_origInventoryScreen2_Close(self);
    }
    g_activeInventoryScreenThis = nullptr;
  }
}

void StorageHooks_TriggerEmbarkProceed() {
  if (g_origInventoryScreen2_Close && g_activeInventoryScreenThis) {
    g_origInventoryScreen2_Close(g_activeInventoryScreenThis);
    g_activeInventoryScreenThis = nullptr;
  }
}

void StorageHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  SCAN_SET(mj, gameBase, RefreshInventoryScreen,
    "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 FD FF FF 48 81 EC 08 03 00 00 0F 29 70",
    g_RefreshInventoryScreen);

  HOOK_INSTALL(mj, gameBase, InventoryItemBox_Click,
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC E8 00 00 00 4C 8B E9 C7 45 67 00 00 00 00",
    0);

  HOOK_INSTALL(mj, gameBase, InventoryScreen2_Close,
    "48 89 5C 24 08 57 48 81 EC 80 00 00 00 48 8B F9 E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ??",
    0);

  HOOK_INSTALL(mj, gameBase, SceneManager_CreateScene,
    "48 89 5C 24 18 48 89 74 24 20 48 89 54 24 10 57 48 83 ec 20 48 8b fa 48 8b f1 48 8d 0d",
    0);

  HOOK_INSTALL(mj, gameBase, Scene_AddComponent,
    "48 89 5C 24 18 48 89 6c 24 20 48 89 54 24 10 56 57 41 56 48 83 ec 20 48 8b 02 48 8b f1 48 8b ca",
    0);
}

