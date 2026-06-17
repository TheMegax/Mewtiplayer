#include "hooks/MapHooks.h"
#include "hooks/HookMacros.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "MewgenicsTypes.h"
#include "../../include/ModState.h"
#include "Scanner.h"
#include "SteamABICompat.h"

void *g_MapScreen = nullptr;
bool g_isHandlingNetworkMapNodeSync = false;
uint32_t g_pendingMapNodeSyncIndex = 0xFFFFFFFF;

HOOK_DEFINE(MapScreen_EnterNode, void, void *, void *)

static void __fastcall Hook_MapScreen_EnterNode(void *self, void *node) {
  if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_origMapScreen_EnterNode) g_origMapScreen_EnterNode(self, node);
    return;
  }

  g_pendingMapNodeSyncIndex = 0xFFFFFFFF; // Clear any pending sync on entering a node

  if (!g_isHandlingNetworkMapNodeSync) {
    if (NetworkManager::Get().IsInputBlocked(SteamUser()->GetSteamID().ConvertToUint64())) {
      Overlay::Log("[MAP] Ignoring local EnterNode (Input Blocked)");
      return; 
    }

    uint32_t nodeIndex = 0xFFFFFFFF;
    if (self && node) {
      // Find the index of the node in the MapScreen's podvector
      const auto *mapScreen = static_cast<const glaiel::MapScreen*>(self);
      const uint32_t vectorSize = mapScreen->nodes.size_;
      void **nodes = mapScreen->nodes.data_;
      if (nodes) {
        for (uint32_t i = 0; i < vectorSize; i++) {
          if (nodes[i] == node) {
            nodeIndex = i;
            break;
          }
        }
      }
    }

    if (nodeIndex != 0xFFFFFFFF) {
      MapNodeSyncPacket pkt = {};
      pkt.nodeIndex = nodeIndex;
      NetworkManager::Get().BroadcastPacket(PacketType::MapNodeSync, &pkt, sizeof(pkt), true);
      Overlay::Log("[MAP] Broadcasted MapNodeSync for index %u", nodeIndex);
    } else {
      Overlay::Log("[MAP] [ERR] Could not find MapNode in MapScreen array!");
    }
  }

  if (g_origMapScreen_EnterNode) {
    g_origMapScreen_EnterNode(self, node);
  }
}

void TriggerMapNodeSync(uint32_t nodeIndex) {
  if (!GameUtils::IsComponentValid(g_MapScreen)) {
    g_pendingMapNodeSyncIndex = nodeIndex;
    Overlay::Log("[MAP] MapScreen is null or inactive, queued MapNodeSync for index %u", nodeIndex);
    return;
  }

  const auto *mapScreen = static_cast<const glaiel::MapScreen*>(g_MapScreen);
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

  g_isHandlingNetworkMapNodeSync = true;
  if (g_origMapScreen_EnterNode) {
    Overlay::Log("[MAP] Triggering network-synced EnterNode (index %u)", nodeIndex);
    g_origMapScreen_EnterNode(g_MapScreen, matchedNode);
  }
  g_isHandlingNetworkMapNodeSync = false;
}

void MapHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  // Should use a different one
  HOOK_INSTALL(mj, gameBase, MapScreen_EnterNode,
    "48 8b c4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8d a8 e8 fe ff ff 48 81 ec e0 01 00 00 0f 29 70 b8 0f 29 78 a8 48 8b fa 48 8b f1 33 c0",
    0);
}
