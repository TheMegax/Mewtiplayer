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

HOOK_DEFINE(MapNode_Click, void, void *)

static void __fastcall Hook_MapNode_Click(void *self) {
  if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_origMapNode_Click) g_origMapNode_Click(self);
    return;
  }

  uint32_t nodeIndex = 0xFFFFFFFF;

  if (self && GameUtils::IsComponentValid(g_MapScreen)) {
    const void* targetMapNode = nullptr;
    
    if ((uintptr_t)self > 0x10000) {
        targetMapNode = *(void**)((char*)self + 8);
    }

    const auto *mapScreen = static_cast<const glaiel::MapScreen*>(g_MapScreen);
    const uint32_t vectorSize = mapScreen->nodes.size_;
    if (void **nodes = mapScreen->nodes.data_) {
      for (uint32_t i = 0; i < vectorSize; i++) {
        if (nodes[i] == targetMapNode) {
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
    Overlay::Log("[MAP] [ERR] Could not find clicked MapNode in MapScreen array!");
  }

  if (g_origMapNode_Click) {
    g_origMapNode_Click(self);
  }
}

HOOK_DEFINE(MapScreen_EnterNode, void, void *, void *)

static void __fastcall Hook_MapScreen_EnterNode(void *self, void *node) {
  g_pendingMapNodeSyncIndex = 0xFFFFFFFF; // Clear any pending sync on entering a node
  if (g_origMapScreen_EnterNode) {
    g_origMapScreen_EnterNode(self, node);
  }
}

void TriggerMapNodeSync(const uint32_t nodeIndex) {
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

  if (g_origMapNode_Click) {
    Overlay::Log("[MAP] Triggering network-synced Click (index %u)", nodeIndex);
    
    // The hooked function is a lambda operator() that expects a closure object in RCX.
    // It only ever reads `[RCX + 8]` to get the captured MapNode pointer.
    // We can simulate this by constructing a dummy closure array!
    void* dummyClosure[2] = { nullptr, matchedNode };
    g_origMapNode_Click(dummyClosure);
  }
}

void MapHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, MapScreen_EnterNode,
    "48 8b c4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8d a8 e8 fe ff ff 48 81 ec e0 01 00 00 0f 29 70 b8 0f 29 78 a8 48 8b fa 48 8b f1 33 c0",
    0);

  HOOK_INSTALL(mj, gameBase, MapNode_Click,
    "48 89 5c 24 20 55 56 57 41 56 41 57 48 81 ec c0 00 00 00 48 8b 71 08 83 be 38 01 00 00 04 0f 85",
    0);
}
