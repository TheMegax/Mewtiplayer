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
  
  auto* shop = (glaiel::Shop*)shopInstance;
  const uint32_t itemIndex = ((uintptr_t)shopItem - (uintptr_t)shop->items.begin()) / sizeof(glaiel::ShopItem);
  
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
    auto* shop = (glaiel::Shop*)g_activeShop;
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
    auto* shop = (glaiel::Shop*)g_activeShop;
    shop->timer = 9999.0;
  }
}

} // namespace ParaboxAPI

void ShopHooks_Init(MewjectorAPI* mj, uintptr_t gameBase) {
  // There is a duplicate pickRandom, so this is a monstrous sig :plead:
  SCAN_SET(mj, gameBase, PickRandom,
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 8B 79 04 48 8B F2 48 8B D9 85 FF 75 12 33 C0 "
    "48 8B 5C 24 08 48 8B 74 24 10 48 8B 7C 24 18 C3 48 8B 4A 08 0F 57 C9 4C 8B 52 18 4C 8B "
    "C1 4C 8B 0A 49 8B D1 49 C1 E0 11 48 33 56 10 48 8B C2 49 33 D0 4F 8D 1C 11 48 89 56 10 "
    "4C 33 D1 49 C1 EB 0B 4D 33 CA 48 33 C1 49 C1 CA 13 48 89 46 08 4C 89 0E 4C 89 56 18 4D "
    "85 DB 78 07 F2 49 0F 2A CB EB 16 49 8B C3 41 83 E3 01 48 D1 E8 49 0B C3 F2 48 0F 2A C8 "
    "F2 0F 58 C9 F2 0F 59 0D ?? ?? ?? ?? 48 8B 74 24 10 66 0F 6E C7 48 8B 7C 24 18 F3 0F E6 "
    "C0 F2 0F 59 C8 F2 0F 2C C1 48 63 C8 48 8B 43 08 48 8B 5C 24 08 48 8B 04 C8 C3", 
    ParaboxAPI::g_PickRandom);

  HOOK_INSTALL(mj, gameBase, ShopBuyItem, "48 89 5c 24 18 48 89 7c 24 20 55 48 8b ec 48 81 ec ?? ?? ?? ?? 48 8b f9 48 8b 51 08 80 7a 7b 00", 14);
  HOOK_INSTALL(mj, gameBase, ShopExitButton, "48 89 5c 24 10 57 48 83 ec 20 48 8b 41 08 48 8b d9 83 b8 88 00 00 00 00", 14);
  
  HOOK_INSTALL(mj, gameBase, ShopInit,
    "48 8b c4 44 88 48 20 4c 89 40 18 48 89 50 10 55 53 56 57",
    15);
               
  HOOK_INSTALL(mj, gameBase, ShopChestClick, 
    "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 41 08 48 8b f9 33 db 48 8b 70 38 48 8b 8e ?? ?? ?? ??",
    15);

  SCAN_RESOLVE(mj, gameBase, ShopSpawnItemsCall, "48 8b cb e8 ?? ?? ?? ?? 4c 8d 4d 20 48 8b d0 48 8b cb e8 ?? ?? ?? ?? 4c 8d 9c 24 80 00 00 00 49 8b 5b 28", g_origShopSpawnItems, 18, 1, 5);
  if (g_origShopSpawnItems) {
      mj->InstallHook((uintptr_t)g_origShopSpawnItems - gameBase, 15, (void*)Hook_ShopSpawnItems, (void**)&g_origShopSpawnItems, 10, "ParaboxAPI");
  }

  HOOK_INSTALL(mj, gameBase, LevelUpLambda,
    "48 89 5C 24 18 57 48 83 EC 40 48 8B 41 08 48 8D 54 24 20 48 8B F9 48 8B 48 28 48 8B 05 ?? ?? ?? ??",
    15);
}
