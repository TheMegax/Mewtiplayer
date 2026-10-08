#include "hooks/StorageHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "MewgenicsTypes.h"
#include "Scanner.h"
#include "ParaboxAPI.h"

typedef void (__fastcall *RefreshInventoryScreen_t)(void *inventoryScreen);
typedef Equipment *(__fastcall *InventoryScreen2Lookup_t)(InventoryScreen2 *screen, int64_t itemID);
typedef int64_t (__fastcall *InventoryScreen2ItemEquippedStatus_t)(InventoryScreen2 *screen, int64_t itemID);
typedef void (__fastcall *InventoryScreen2RefreshEquippedStatus_t)(InventoryScreen2 *screen);
typedef bool (__fastcall *CatDataEquip_t)(CatData *cat, Equipment *item, int32_t loc, bool param_4);
typedef bool (__fastcall *CatDataUnequip_t)(CatData *cat, Equipment *item, int32_t loc, bool param_4, bool param_5);
typedef int32_t (__fastcall *EquipmentKind_t)(Equipment *equip);

static RefreshInventoryScreen_t g_RefreshInventoryScreen = nullptr;
static InventoryScreen2Lookup_t g_InventoryScreen2Lookup = nullptr;
static InventoryScreen2ItemEquippedStatus_t g_InventoryScreen2ItemEquippedStatus = nullptr;
static InventoryScreen2RefreshEquippedStatus_t g_InventoryScreen2RefreshEquippedStatus = nullptr;
static CatDataEquip_t g_CatDataEquip = nullptr;
static CatDataUnequip_t g_CatDataUnequip = nullptr;
static EquipmentKind_t g_EquipmentKind = nullptr;

HOOK_DEFINE(InventoryItemBox_Click, void, void *)
HOOK_DEFINE(InventoryItemBox_EquipInternal, void, void **)
HOOK_DEFINE(SceneManager_CreateScene, void *, void *, void *)
HOOK_DEFINE(Scene_AddComponent, void, void *, void *)
HOOK_DEFINE(InventoryScreen2_Close, void, void *)

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

  if (g_origScene_AddComponent) {
    g_origScene_AddComponent(scene, comp);
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

PARABOX_API InventoryScreen2* GetActiveInventoryScreen2() {
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      if (!comp || comp->deleted) continue;
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "InventoryScreen2";
        GameUtils::FreeXString(name);
        if (match) return const_cast<InventoryScreen2 *>(reinterpret_cast<const InventoryScreen2 *>(comp));
      }
    }
  }
  return nullptr;
}

PARABOX_API int32_t FindStorageSlotIndex(const void *clickedBox) {
  if (!clickedBox) return -1;
  const auto *itemBox = static_cast<const InventoryItemBox *>(clickedBox);
  auto *screen = GetActiveInventoryScreen2();
  if (!screen) screen = itemBox->parent;
  if (!screen || !screen->boxes.data_) return -1;
  for (uint32_t i = 0; i < screen->boxes.size(); i++) {
    if (screen->boxes.data_[i] == clickedBox) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

PARABOX_API int32_t GetStorageSlotCount() {
  if (auto *screen = GetActiveInventoryScreen2()) {
    return static_cast<int32_t>(screen->boxes.size());
  }
  return 0;
}

PARABOX_API void RefreshInventoryEquippedStatus() {
  if (auto *screen = GetActiveInventoryScreen2()) {
    if (g_InventoryScreen2RefreshEquippedStatus) {
      g_InventoryScreen2RefreshEquippedStatus(screen);
    }
  }
}

PARABOX_API int64_t GetItemEquippedOwner(const int64_t itemID) {
  auto *screen = GetActiveInventoryScreen2();
  if (!screen || !g_InventoryScreen2ItemEquippedStatus) {
    return -1;
  }
  if (g_InventoryScreen2RefreshEquippedStatus) {
    g_InventoryScreen2RefreshEquippedStatus(screen);
  }
  const int64_t status = g_InventoryScreen2ItemEquippedStatus(screen, itemID);
  if (status != -1 && status != -2) {
    return status;
  }
  return -1;
}

PARABOX_API int64_t GetStorageSlotEquippedOwner(const int32_t slotIndex) {
  auto *screen = GetActiveInventoryScreen2();
  if (!screen || !screen->boxes.data_) return -1;
  if (slotIndex >= 0 && static_cast<uint32_t>(slotIndex) < screen->boxes.size()) {
    if (const auto *box = screen->boxes.data_[slotIndex]) {
      return GetItemEquippedOwner(box->item_id);
    }
  }
  return -1;
}

static void UpdateStorageItemBox(InventoryScreen2 *screen, InventoryItemBox *targetBox, const int64_t catID) {
  if (!screen || !targetBox) return;

  if (g_InventoryScreen2RefreshEquippedStatus) {
    g_InventoryScreen2RefreshEquippedStatus(screen);
  }

  int64_t currentOwnerID = -1;
  if (g_InventoryScreen2ItemEquippedStatus) {
    const int64_t status = g_InventoryScreen2ItemEquippedStatus(screen, targetBox->item_id);
    if (status != -1 && status != -2) {
      currentOwnerID = status;
    }
  }

  if (catID == currentOwnerID) {
    return;
  }

  Equipment *equip = nullptr;
  if (g_InventoryScreen2Lookup) {
    equip = g_InventoryScreen2Lookup(screen, targetBox->item_id);
  }
  if (!equip) return;

  // kind >= 5 (None) triggers a null deref in CatEquipment::unequip
  if (equip->uid == -1 || !equip->name.is_valid() || equip->name.Mysize == 0) {
    Log("[STORAGE] [WARN] Skipping invalid item");
    return;
  }

  if (g_EquipmentKind) {
    const int32_t kind = g_EquipmentKind(equip);
    if (kind < 0 || kind > 4) {
      Log("[STORAGE] [WARN] Skipping item with invalid kind %d", kind);
      return;
    }
  }

  if (catID == -1) {
    if (currentOwnerID != -1 && g_CatDataUnequip) {
      CatData *ownerCat = GetCatDataById(currentOwnerID);
      if (ownerCat) {
        g_CatDataUnequip(ownerCat, equip, screen->iloc, false, false);
      }
    }
  } else {
    // strip from previous wearer before giving it to the new cat
    if (currentOwnerID != -1 && currentOwnerID != catID && g_CatDataUnequip) {
      CatData *ownerCat = GetCatDataById(currentOwnerID);
      if (ownerCat) {
        g_CatDataUnequip(ownerCat, equip, screen->iloc, false, false);
      }
    }
    if (g_CatDataEquip) {
      CatData *targetCat = GetCatDataById(catID);
      if (targetCat) {
        g_CatDataEquip(targetCat, equip, screen->iloc, false);
      }
    }
  }

  if (g_InventoryScreen2RefreshEquippedStatus) {
    g_InventoryScreen2RefreshEquippedStatus(screen);
  }
  if (g_RefreshInventoryScreen) {
    g_RefreshInventoryScreen(screen);
  }
  RefreshCatSelectorUI();
}

PARABOX_API int32_t GetItemSortOrder(const void *itemBox) {
  if (!itemBox) return -1;
  const auto *box = static_cast<const InventoryItemBox *>(itemBox);
  auto *screen = GetActiveInventoryScreen2();
  if (!screen) screen = box->parent;
  if (!screen || !g_InventoryScreen2Lookup) return -1;
  Equipment *equip = g_InventoryScreen2Lookup(screen, box->item_id);
  if (equip && equip->uid == box->item_id) {
    return equip->inventory_sortorder;
  }
  return -1;
}

PARABOX_API Equipment *GetActiveInventoryItemBySortOrder(const int32_t sortOrder, InventoryItemBox **outBox) {
  if (sortOrder < 0) return nullptr;
  auto *screen = GetActiveInventoryScreen2();
  if (!screen || !screen->boxes.data_ || !g_InventoryScreen2Lookup) return nullptr;
  for (uint32_t i = 0; i < screen->boxes.size(); i++) {
    auto *box = screen->boxes.data_[i];
    if (box) {
      Equipment *eq = g_InventoryScreen2Lookup(screen, box->item_id);
      if (eq && eq->inventory_sortorder == sortOrder) {
        if (outBox) *outBox = box;
        return eq;
      }
    }
  }
  return nullptr;
}

PARABOX_API int64_t GetSortOrderItemEquippedOwner(const int32_t sortOrder) {
  InventoryItemBox *box = nullptr;
  Equipment *eq = GetActiveInventoryItemBySortOrder(sortOrder, &box);
  if (box) {
    return GetItemEquippedOwner(box->item_id);
  }
  return -1;
}

PARABOX_API void UpdateStorageItemBySortOrder(const int32_t sortOrder, const int64_t catID) {
  auto *screen = GetActiveInventoryScreen2();
  if (!screen) return;
  InventoryItemBox *box = nullptr;
  Equipment *eq = GetActiveInventoryItemBySortOrder(sortOrder, &box);
  if (box) {
    UpdateStorageItemBox(screen, box, catID);
  }
}

PARABOX_API void UpdateStorageItem(const int64_t itemID, const int64_t catID) {
  auto *screen = GetActiveInventoryScreen2();
  if (!screen || !screen->boxes.data_) return;
  for (uint32_t i = 0; i < screen->boxes.size(); i++) {
    auto *box = screen->boxes.data_[i];
    if (box && box->item_id == itemID) {
      UpdateStorageItemBox(screen, box, catID);
      return;
    }
  }
}

PARABOX_API void UpdateStorageItemSlot(const int32_t slotIndex, const int64_t catID) {
  auto *screen = GetActiveInventoryScreen2();
  if (!screen || !screen->boxes.data_) return;
  if (slotIndex >= 0 && static_cast<uint32_t>(slotIndex) < screen->boxes.size()) {
    if (auto *box = screen->boxes.data_[slotIndex]) {
      UpdateStorageItemBox(screen, box, catID);
    }
  }
}

PARABOX_API int64_t ResolveSelectedCatID() {
  if (const auto *screen = GetActiveInventoryScreen2()) {
    if (screen->current_cat != 0 && screen->current_cat != -1) {
      return screen->current_cat;
    }
    if (screen->selector && screen->selector->current_cat != 0 && screen->selector->current_cat != -1) {
      return screen->selector->current_cat;
    }
  }
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

} // namespace ParaboxAPI

void StorageHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  RESOLVE_FUNC(gameBase, GameSymbols::InventoryScreen2_refresh_item_locations, g_RefreshInventoryScreen);
  RESOLVE_FUNC(gameBase, GameSymbols::InventoryScreen2_lookup, g_InventoryScreen2Lookup);
  RESOLVE_FUNC(gameBase, GameSymbols::InventoryScreen2_item_equipped_status, g_InventoryScreen2ItemEquippedStatus);
  RESOLVE_FUNC(gameBase, GameSymbols::InventoryScreen2_refresh_equipped_status, g_InventoryScreen2RefreshEquippedStatus);
  RESOLVE_FUNC(gameBase, GameSymbols::CatData_Equip, g_CatDataEquip);
  RESOLVE_FUNC(gameBase, GameSymbols::CatData_Unequip, g_CatDataUnequip);
  RESOLVE_FUNC(gameBase, GameSymbols::Equipment_kind, g_EquipmentKind);

  HOOK_INSTALL(mj, gameBase, InventoryItemBox_Click, GameSymbols::InventoryItemBox_click, 0);
  HOOK_INSTALL(mj, gameBase, InventoryItemBox_EquipInternal, GameSymbols::InventoryItemBox_click_equip, 0);
  HOOK_INSTALL(mj, gameBase, InventoryScreen2_Close, GameSymbols::InventoryScreen2_close, 0);
  HOOK_INSTALL(mj, gameBase, SceneManager_CreateScene, GameSymbols::Director_AddScene, 0);
  HOOK_INSTALL(mj, gameBase, Scene_AddComponent, GameSymbols::Scene_AddComponent, 0);
}

