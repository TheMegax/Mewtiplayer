#include "hooks/StorageHooks.h"
#include "hooks/HookMacros.h"
#include "hooks/ModState.h"
#include "GameUtils.h"
#include "Overlay.h"
#include "NetworkManager.h"
#include "Scanner.h"
#include "SteamABICompat.h"
#include <vector>

#include "hooks/ClassChooserHooks.h"

typedef void (__fastcall *RefreshInventoryScreen_t)(void *inventoryScreen);
static RefreshInventoryScreen_t g_RefreshInventoryScreen = nullptr;

HOOK_DEFINE(InventoryItemBox_Click, void, void *)


static int32_t FindStorageSlotIndex(const void *clickedBox) {
  int32_t index = 0;
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "InventoryItemBox";
        GameUtils::FreeXString(name);
        if (match) {
          if (comp == clickedBox) {
            return index;
          }
          index++;
        }
      }
    }
  }
  return -1;
}

static int64_t ResolveSelectedCatID() {
  if (const auto *director = GameUtils::GetMewDirectorSingleton();
      director && director->pedigreeState) {
    if (const void *catSelector = g_activeCatSelector) {
      if (IsCatSelectorValid(catSelector)) {
        return static_cast<const int64_t *>(catSelector)[0x11];
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

bool IsStorageItemsSceneValid() {
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "StorageItems";
        GameUtils::FreeXString(name);
        if (match) return true;
      }
    }
  }
  return false;
}

void UpdateStorageItemSlot(const int32_t slotIndex, const int64_t catID) {
  int32_t index = 0;
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "InventoryItemBox";
        GameUtils::FreeXString(name);
        if (match) {
          if (index == slotIndex) {
            if (g_origInventoryItemBox_Click) {
              if (void *inventoryScreen = *reinterpret_cast<void **>(reinterpret_cast<char *>(const_cast<Component *>(comp)) + 0x38)) {
                const auto screenCatIDPtr = reinterpret_cast<int64_t *>(static_cast<char *>(inventoryScreen) + 0x108);
                const int64_t origCatID = *screenCatIDPtr;
                *screenCatIDPtr = catID;
                g_origInventoryItemBox_Click(const_cast<Component *>(comp));
                *screenCatIDPtr = origCatID;
                if (g_RefreshInventoryScreen) {
                  g_RefreshInventoryScreen(inventoryScreen);
                }
              } else {
                g_origInventoryItemBox_Click(const_cast<Component *>(comp));
              }
            }
            Overlay::Log("[STORAGE] Updated storage slot %d for cat %lld", slotIndex, catID);
            return;
          }
          index++;
        }
      }
    }
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

  const auto *actionStr = reinterpret_cast<const MsvcReleaseModeXString *>(reinterpret_cast<const char *>(self) + 0x110);
  const bool isEmbark = actionStr && actionStr->is_valid() && actionStr->as_native_string_view() == "embark";

  if (!isEmbark) {
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
  Overlay::Log("[STORAGE] LockIn: Local ready state: %s", g_localReady ? "locked in" : "not ready");

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
}

