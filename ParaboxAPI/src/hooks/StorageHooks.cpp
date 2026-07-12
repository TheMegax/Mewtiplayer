#include "hooks/StorageHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "MewgenicsTypes.h"
#include "Scanner.h"
#include "ParaboxAPI.h"
#include <vector>

typedef void (__fastcall *RefreshInventoryScreen_t)(void *inventoryScreen);
static RefreshInventoryScreen_t g_RefreshInventoryScreen = nullptr;

HOOK_DEFINE(InventoryItemBox_Click, void, void *)
HOOK_DEFINE(SceneManager_CreateScene, void *, void *, void *)
HOOK_DEFINE(Scene_AddComponent, void, void *, void *)
HOOK_DEFINE(InventoryScreen2_Close, void, void *)

static std::vector<void *>* g_storageItemBoxes = new std::vector<void *>();
static void *g_MapScreen = nullptr;

static void * __fastcall Hook_SceneManager_CreateScene(void *self, void *nameStr) {
  ParaboxAPI::SceneManagerCreateSceneEvent ev = {};
  ev.self = self;
  ev.nameStr = nameStr;
  ev.returnValue = nullptr;
  ParaboxAPI::OnSceneManagerCreateScene.Publish(ev);

  if (ev.cancelled) {
    return ev.returnValue;
  }

  if (nameStr) {
    const auto *xstr = static_cast<MsvcReleaseModeXString *>(nameStr);
    if (xstr->is_valid()) {
      const auto view = xstr->as_native_string_view();
      if (view == "StorageItems") {
        g_storageItemBoxes->clear();
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
        g_storageItemBoxes->push_back(comp);
      } else if (compView == "MapScreen") {
        g_MapScreen = comp;
      }
      GameUtils::FreeXString(compName);
    }
  }

  ParaboxAPI::SceneAddComponentEvent ev = {};
  ev.scene = scene;
  ev.comp = comp;
  ParaboxAPI::OnSceneAddComponent.Publish(ev);
}

static void __fastcall Hook_InventoryItemBox_Click(void *self) {
  ParaboxAPI::InventoryItemBoxClickEvent ev = {};
  ev.self = self;
  ParaboxAPI::OnInventoryItemBoxClick.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origInventoryItemBox_Click) {
    g_origInventoryItemBox_Click(self);
  }
}

static void __fastcall Hook_InventoryScreen2_Close(void *self) {
  ParaboxAPI::InventoryScreen2CloseEvent ev = {};
  ev.self = self;
  ParaboxAPI::OnInventoryScreen2Close.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origInventoryScreen2_Close) {
    g_origInventoryScreen2_Close(self);
  }
}

namespace ParaboxAPI {

PARABOX_API int32_t FindStorageSlotIndex(const void *clickedBox) {
  for (size_t i = 0; i < g_storageItemBoxes->size(); i++) {
    if ((*g_storageItemBoxes)[i] == clickedBox) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

PARABOX_API void UpdateStorageItemSlot(int32_t slotIndex, int64_t catID) {
  if (slotIndex >= 0 && static_cast<size_t>(slotIndex) < g_storageItemBoxes->size()) {
    void *comp = (*g_storageItemBoxes)[slotIndex];
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
  }
}

PARABOX_API int64_t ResolveSelectedCatID() {
  if (const auto *director = GameUtils::GetMewDirectorSingleton();
    director && director->pedigreeState) {
    for (const Scene *scene : GameUtils::GetCurrentScenes()) {
      if (!scene) continue;
      if (void* comp = GameUtils::FindComponentByTypeName(scene, "CatSelector")) {
        return static_cast<const glaiel::CatSelector *>(comp)->catID;
      }
    }
  }
  return -1;
}

PARABOX_API void ForceInventoryScreen2Close(void *self) {
  if (g_origInventoryScreen2_Close) {
    g_origInventoryScreen2_Close(self);
  }
}

PARABOX_API void *GetMapScreen() {
  return g_MapScreen;
}

PARABOX_API void SetMapScreen(void *screen) {
  g_MapScreen = screen;
}

} // namespace ParaboxAPI

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
