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
  SCAN_SET(mj, gameBase, MapScreen_OpenInventory,
    "40 57 48 83 EC 40 48 8B F9 33 D2 48 8B 49 08 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? 48 8B 4F 08",
    g_MapScreen_OpenInventory);

  SCAN_RESOLVE(mj, gameBase, GetMainCamera,
    "48 89 5C 24 10 57 48 83 EC 20 33 FF 48 8B D9 48 85 C9 75 10 48 8B 1D",
    g_RenderingScene, 0x14, 3, 7);

  HOOK_INSTALL(mj, gameBase, MapScreen_EnterNode,
    "48 8b c4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8d a8 e8 fe ff ff 48 81 ec e0 01 00 00 0f 29 70 b8 0f 29 78 a8 48 8b fa 48 8b f1 33 c0",
    0);

  HOOK_INSTALL(mj, gameBase, MapNode_Click,
    "48 89 5c 24 20 55 56 57 41 56 41 57 48 81 ec c0 00 00 00 48 8b 71 08 83 be 38 01 00 00 04 0f 85",
    0);
}
