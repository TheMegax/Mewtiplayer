#include "hooks/MapHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "MewgenicsTypes.h"
#include "Scanner.h"
#include "ParaboxAPI.h"

HOOK_DEFINE(MapNode_Click, void, void *)
HOOK_DEFINE(MapScreen_EnterNode, void, void *, void *)

static void __fastcall Hook_MapNode_Click(void *self) {
  uint32_t nodeIndex = 0xFFFFFFFF;
  void *mapScreenPtr = ParaboxAPI::GetMapScreen();

  if (self && GameUtils::IsComponentValid(mapScreenPtr)) {
    const void* targetMapNode = nullptr;
    
    if ((uintptr_t)self > 0x10000) {
        targetMapNode = *(void**)((char*)self + 8);
    }

    const auto *mapScreen = static_cast<const glaiel::MapScreen*>(mapScreenPtr);
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

  ParaboxAPI::MapNodeClickEvent ev = {};
  ev.self = self;
  ev.nodeIndex = nodeIndex;
  ParaboxAPI::OnMapNodeClick.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origMapNode_Click) {
    g_origMapNode_Click(self);
  }
}

static void __fastcall Hook_MapScreen_EnterNode(void *self, void *node) {
  ParaboxAPI::MapScreenEnterNodeEvent ev = {};
  ev.self = self;
  ev.node = node;
  ParaboxAPI::OnMapScreenEnterNode.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origMapScreen_EnterNode) {
    g_origMapScreen_EnterNode(self, node);
  }
}

namespace ParaboxAPI {
  PARABOX_API void ForceMapNodeClick(void* matchedNode) {
    if (g_origMapNode_Click) {
      void* dummyClosure[2] = { nullptr, matchedNode };
      g_origMapNode_Click(dummyClosure);
    }
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
