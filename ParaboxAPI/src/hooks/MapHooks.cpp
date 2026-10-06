#include "hooks/MapHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "MewgenicsTypes.h"
#include "Scanner.h"
#include "ParaboxAPI.h"

HOOK_DEFINE(MapNode_Click, void, void *)
HOOK_DEFINE(MapScreen_EnterNode, void, void *, void *)

typedef void(__fastcall *MapScreen_OpenInventory_t)(void *closure);
static MapScreen_OpenInventory_t g_MapScreen_OpenInventory = nullptr;
static void** g_RenderingScene = nullptr;

static void __fastcall Hook_MapNode_Click(void *self) {
  uint32_t nodeIndex = 0xFFFFFFFF;
  void *mapScreenPtr = ParaboxAPI::GetMapScreen();

  if (self && GameUtils::IsComponentValid(mapScreenPtr)) {
    const void* targetMapNode = nullptr;
    
    if ((uintptr_t)self > 0x10000) {
        const auto* closure = static_cast<const MapNodeClosure*>(self);
        targetMapNode = closure->node;
    }

    const auto *mapScreen = static_cast<const MapScreen*>(mapScreenPtr);
    const uint32_t vectorSize = mapScreen->nodes.size_;
    if (MapNode **nodes = mapScreen->nodes.data_) {
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

    MapNodeClosure* closure = static_cast<MapNodeClosure*>(self);
    MapNode* node = closure ? closure->node : nullptr;
    if (node && (ParaboxAPI::g_isHandlingNetworkMapNodeSync || node->type == MapNodeType::HOME)) {
      if (node->parent && node->parent->cat_marker) {
        node->parent->cat_marker->pathfind_to = node;
        node->parent->early_exited = false;
        return;
      }
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
  PARABOX_API bool g_isHandlingNetworkMapNodeSync = false;

  PARABOX_API void ForceMapNodeClick(void* matchedNode) {
    if (!matchedNode) return;

    auto* node = static_cast<MapNode*>(matchedNode);
    if (g_isHandlingNetworkMapNodeSync || node->type == MapNodeType::HOME) {
      if (node->parent && node->parent->cat_marker) {
        node->parent->cat_marker->pathfind_to = node;
        node->parent->early_exited = false;
        return;
      }
    }

    void* dummyClosure[2] = { nullptr, matchedNode };
    Hook_MapNode_Click(dummyClosure);
  }

  PARABOX_API void ForceMapInventoryOpen() {
    void* mapScreen = GetMapScreen();
    if (!mapScreen || !g_MapScreen_OpenInventory) {
      return;
    }
    
    // Temporarily set g_RenderingScene to mapScreen->scene to prevent a crash in CatSelector::init
    // which tries to find the main camera in the current rendering scene.
    void* scene = *(void**)((uint8_t*)mapScreen + 0x20);
    void* oldRenderingScene = nullptr;
    if (g_RenderingScene) {
      oldRenderingScene = *g_RenderingScene;
      *g_RenderingScene = scene;
    }

    void* closure[2] = { nullptr, mapScreen };
    g_MapScreen_OpenInventory(closure);

    if (g_RenderingScene) {
      *g_RenderingScene = oldRenderingScene;
    }
  }
}

void MapHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  RESOLVE_FUNC(gameBase, GameSymbols::MapScreen_open_inventory, g_MapScreen_OpenInventory);
  RESOLVE_DATA(gameBase, GameSymbols::Scene_current, g_RenderingScene);

  HOOK_INSTALL(mj, gameBase, MapScreen_EnterNode, GameSymbols::MapScreen_EnterNode, 0);
  HOOK_INSTALL(mj, gameBase, MapNode_Click, GameSymbols::MapNode_click_action, 0);
}
