#include "hooks/ShopHooks.h"
#include "hooks/HookMacros.h"
#include "Scanner.h"
#include "GameUtils.h"
#include <iostream>

namespace ParaboxAPI {
Event<ShopBuyItemEvent> OnShopBuyItem;
Event<ShopExitButtonEvent> OnShopExitButton;
Event<ShopChestClickEvent> OnShopChestClick;
Event<ShopFastForwardEvent> OnShopFastForward;
Event<ShopLevelUpEvent> OnShopLevelUp;

typedef void* (*PickRandom_t)(void* vec, void* rngState);
static PickRandom_t g_PickRandom = nullptr;

PARABOX_API void* PickRandomCat(void* vec) {
  if (g_PickRandom) {
    if (auto* tls = GameUtils::GetThreadLocalStoragePointer()) {
      const auto global_rng = (void*)((uintptr_t)tls + 0x178);
      return g_PickRandom(vec, global_rng);
    }
  }
  return nullptr;
}

} // namespace ParaboxAPI

// This is the global active Shop state (useful for dummy captures).
static void* g_activeShop = nullptr;

HOOK_DEFINE(ShopInit, void, void*, void*, void*, uint8_t)
HOOK_DEFINE(ShopBuyItem, void, void*)
HOOK_DEFINE(ShopExitButton, void, void*)
HOOK_DEFINE(ShopChestClick, void, void*)

static void __fastcall Hook_ShopInit(void* shopInstance, void* p2, void* p3, uint8_t p4) {
  g_activeShop = shopInstance;
  if (g_origShopInit) {
    g_origShopInit(shopInstance, p2, p3, p4);
  }
}


HOOK_DEFINE(LevelUpLambda, void, void* capture)
static void __fastcall Hook_LevelUpLambda(void* capture) {
  if (capture) {
    auto* vec = (podvector<void*>*)((uintptr_t)capture + 0x10);
    
    ParaboxAPI::ShopLevelUpEvent e{vec};
    ParaboxAPI::OnShopLevelUp.Publish(e);
  }
  
  if (g_origLevelUpLambda) {
    g_origLevelUpLambda(capture);
  }
}


static void __fastcall Hook_ShopBuyItem(void* capture) {
  void* shopItem = *(void**)capture;
  void* shopInstance = *((void**)capture + 1);
  g_activeShop = shopInstance;
  
  auto* shop = (Shop*)shopInstance;
  const uint32_t itemIndex = ((uintptr_t)shopItem - (uintptr_t)shop->items.begin()) / sizeof(ShopItem);
  
  ParaboxAPI::ShopBuyItemEvent ev = {};
  ev.shopInstance = shopInstance;
  ev.itemIndex = itemIndex;
  ParaboxAPI::OnShopBuyItem.Publish(ev);
  
  if (!ev.cancelled && g_origShopBuyItem) {
    g_origShopBuyItem(capture);
  }
}

static void __fastcall Hook_ShopExitButton(void* capture) {
  void* shopInstance = *(void**)((uintptr_t)capture + 8);
  g_activeShop = shopInstance;

  ParaboxAPI::ShopExitButtonEvent ev = {};
  ev.shopInstance = shopInstance;
  ParaboxAPI::OnShopExitButton.Publish(ev);
  
  if (!ev.cancelled && g_origShopExitButton) {
    g_origShopExitButton(capture);
  }
}

static void __fastcall Hook_ShopChestClick(void* capture) {
  // Capture structure for lambda has the shop instance at offset 8.
  void* shopInstance = *(void**)((uintptr_t)capture + 8);
  g_activeShop = shopInstance;

  ParaboxAPI::ShopChestClickEvent ev = {};
  ev.shopInstance = shopInstance;
  ParaboxAPI::OnShopChestClick.Publish(ev);
  
  if (!ev.cancelled && g_origShopChestClick) {
    g_origShopChestClick(capture);
  }
}

HOOK_DEFINE(ShopSpawnItems, void*, void*, void*, void*, void*)
static void* __fastcall Hook_ShopSpawnItems(void* p1, void* p2, void* p3, void* p4) {
  if (g_activeShop) {
    ParaboxAPI::ShopFastForwardEvent ff_ev = {};
    ff_ev.shopInstance = g_activeShop;
    ParaboxAPI::OnShopFastForward.Publish(ff_ev);
  }
  return g_origShopSpawnItems ? g_origShopSpawnItems(p1, p2, p3, p4) : nullptr;
}

namespace ParaboxAPI {

void ForceShopBuyItem(uint32_t itemIndex) {
  if (g_origShopBuyItem && g_activeShop) {
    auto* shop = (Shop*)g_activeShop;
    void* shopItem = shop->items.begin() + itemIndex;
    struct { // NOLINT(*-pro-type-member-init)
      void* shopItem;
      void* shopInstance;
    } dummyCapture;
    dummyCapture.shopItem = shopItem;
    dummyCapture.shopInstance = g_activeShop;
    
    g_origShopBuyItem(&dummyCapture);
  }
}

void ForceShopExitButton() {
  if (g_origShopExitButton && g_activeShop) {
    // Construct dummy capture
    struct { // NOLINT(*-pro-type-member-init)
      void* dummy1;
      void* shopInstance;
    } dummyCapture;
    dummyCapture.dummy1 = nullptr;
    dummyCapture.shopInstance = g_activeShop;
    
    g_origShopExitButton(&dummyCapture);
  }
}

void ForceShopChestClick() {
  if (g_origShopChestClick && g_activeShop) {
    // Construct dummy capture
    // The capture needs the shop pointer at +8
    struct { // NOLINT(*-pro-type-member-init)
      void* dummy1;
      void* shopInstance;
    } dummyCapture;
    dummyCapture.dummy1 = nullptr;
    dummyCapture.shopInstance = g_activeShop;
    
    g_origShopChestClick(&dummyCapture);
  }
}

void ForceShopFastForward() {
  if (g_activeShop) {
    auto* shop = (Shop*)g_activeShop;
    shop->treasure_timer = 9999.0;
  }
}

} // namespace ParaboxAPI

void ShopHooks_Init(MewjectorAPI* mj, uintptr_t gameBase) {
  RESOLVE_FUNC(gameBase, GameSymbols::Shop_random_cat, ParaboxAPI::g_PickRandom);

  HOOK_INSTALL(mj, gameBase, ShopBuyItem, GameSymbols::Shop_buy_item_action, 14);
  HOOK_INSTALL(mj, gameBase, ShopExitButton, GameSymbols::Shop_exit_action, 14);
  HOOK_INSTALL(mj, gameBase, ShopInit, GameSymbols::Shop_init, 15);
  HOOK_INSTALL(mj, gameBase, ShopChestClick, GameSymbols::Shop_chest_click_action, 15);

  mj->InstallHook(GameSymbols::Shop_ItemOutTransition, 15, (void*)Hook_ShopSpawnItems, (void**)&g_origShopSpawnItems, 10, PARABOX_MOD_TAG);

  HOOK_INSTALL(mj, gameBase, LevelUpLambda, GameSymbols::Shop_levelup_action, 15);
}
