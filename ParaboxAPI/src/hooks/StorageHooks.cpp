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
HOOK_DEFINE(InventoryItemBox_EquipInternal, void, void **)
HOOK_DEFINE(SceneManager_CreateScene, void *, void *, void *)
HOOK_DEFINE(Scene_AddComponent, void, void *, void *)
HOOK_DEFINE(InventoryScreen2_Close, void, void *)

static std::vector<void *>* g_storageItemBoxes = new std::vector<void *>();

static void __fastcall Hook_InventoryItemBox_EquipInternal(void **itemBoxPtr) {
  if (g_origInventoryItemBox_EquipInternal) {
    g_origInventoryItemBox_EquipInternal(itemBoxPtr);
  }

  if (!ParaboxAPI::g_isHandlingNetworkStorageItemSync && itemBoxPtr && *itemBoxPtr) {
    ParaboxAPI::InventoryItemBoxEquippedEvent ev = {};
    ev.self = *itemBoxPtr;
    ParaboxAPI::OnInventoryItemBoxEquipped.Publish(ev);
  }
}

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
  if (!scene || !comp) {
    if (g_origScene_AddComponent) g_origScene_AddComponent(scene, comp);
    return;
  }

  const auto *s = static_cast<Scene *>(scene);
  bool isInventoryScreen2 = false;
  bool isInventoryItemBox = false;
  bool isClassChooser = false;
  bool isClassTagBox = false;
  if (s->name.is_valid()) {
    MsvcReleaseModeXString compName = {};
    if (GameUtils::SafeGetComponentName(static_cast<Component *>(comp), &compName)) {
      const auto compView = compName.as_native_string_view();
      if (compView == "InventoryScreen2") isInventoryScreen2 = true;
      else if (compView == "InventoryItemBox") isInventoryItemBox = true;
      else if (compView == "ClassChooser") isClassChooser = true;
      else if (compView == "ClassTagBox") isClassTagBox = true;
      GameUtils::FreeXString(compName);
    }
  }

  if (isInventoryScreen2) {
    g_storageItemBoxes->clear();
  }
  if (isClassChooser) {
    ParaboxAPI::ClearClassTagBoxes();
  }

  if (g_origScene_AddComponent) {
    g_origScene_AddComponent(scene, comp);
  }

  if (s->name.is_valid()) {
    if (isInventoryItemBox) {
      g_storageItemBoxes->push_back(comp);
    }
    if (isClassTagBox) {
      ParaboxAPI::RegisterClassTagBox(comp);
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

PARABOX_API bool g_isHandlingNetworkStorageItemSync = false;

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
    auto *itemBox = static_cast<InventoryItemBox *>(comp);
    if (auto *inventoryScreen = static_cast<InventoryScreen2 *>(itemBox->parent)) {
      int64_t *screenCatIDPtr = &inventoryScreen->current_cat;
      const int64_t origCatID = *screenCatIDPtr;
      *screenCatIDPtr = catID;

      if (g_origInventoryItemBox_EquipInternal) {
        void* compPtr = comp;
        g_origInventoryItemBox_EquipInternal(&compPtr);
      } else if (g_origInventoryItemBox_Click) {
        g_origInventoryItemBox_Click(comp);
      }

      *screenCatIDPtr = origCatID;
      if (g_RefreshInventoryScreen) {
        g_RefreshInventoryScreen(inventoryScreen);
      }
    } else {
      if (g_origInventoryItemBox_EquipInternal) {
        void* compPtr = comp;
        g_origInventoryItemBox_EquipInternal(&compPtr);
      } else if (g_origInventoryItemBox_Click) {
        g_origInventoryItemBox_Click(comp);
      }
    }
  }
}

PARABOX_API int64_t ResolveSelectedCatID() {
  if (const auto *selector = static_cast<const CatSelector *>(GetActiveCatSelector());
      selector && IsCatSelectorValid(selector)) {
    return selector->current_cat;
  }
  return -1;
}

PARABOX_API void ForceInventoryScreen2Close(void *self) {
  if (g_origInventoryScreen2_Close) {
    g_origInventoryScreen2_Close(self);
  }
}

PARABOX_API void *GetMapScreen() {
  for (const auto* scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    if (scene->name.is_valid() && scene->name.as_native_string_view() == "Map") {
      return GameUtils::FindComponentByTypeName(scene, "MapScreen");
    }
  }
  return nullptr;
}

PARABOX_API int32_t GetStorageSlotCount() {
  return g_storageItemBoxes ? static_cast<int32_t>(g_storageItemBoxes->size()) : 0;
}

} // namespace ParaboxAPI

void StorageHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  RESOLVE_FUNC(gameBase, GameSymbols::InventoryScreen2_refresh_item_locations, g_RefreshInventoryScreen);

  HOOK_INSTALL(mj, gameBase, InventoryItemBox_Click, GameSymbols::InventoryItemBox_click, 0);
  HOOK_INSTALL(mj, gameBase, InventoryItemBox_EquipInternal, GameSymbols::InventoryItemBox_click_equip, 0);
  HOOK_INSTALL(mj, gameBase, InventoryScreen2_Close, GameSymbols::InventoryScreen2_close, 0);
  HOOK_INSTALL(mj, gameBase, SceneManager_CreateScene, GameSymbols::Director_AddScene, 0);
  HOOK_INSTALL(mj, gameBase, Scene_AddComponent, GameSymbols::Scene_AddComponent, 0);
}
