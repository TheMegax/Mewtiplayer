#include "hooks/ShopHooks.h"
#include "hooks/HookMacros.h"
#include "Scanner.h"
#include <iostream>

namespace ParaboxAPI {
Event<ShopBuyItemEvent> OnShopBuyItem;
Event<ShopExitButtonEvent> OnShopExitButton;
Event<ShopChestClickEvent> OnShopChestClick;
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

static void __fastcall Hook_ShopBuyItem(void* capture) {
  void* shopItem = *(void**)capture;
  void* shopInstance = *((void**)capture + 1);
  g_activeShop = shopInstance;
  
  uintptr_t base = *(uintptr_t*)((uintptr_t)shopInstance + 0x60);
  uint32_t itemIndex = ((uintptr_t)shopItem - base) / 0xc0;
  
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

namespace ParaboxAPI {

void ForceShopBuyItem(uint32_t itemIndex) {
  if (g_origShopBuyItem && g_activeShop) {
    void* shopItem = (void*)(*(uintptr_t*)((uintptr_t)g_activeShop + 0x60) + itemIndex * 0xc0);
    struct {
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
    struct {
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
    struct {
      void* dummy1;
      void* shopInstance;
    } dummyCapture;
    dummyCapture.dummy1 = nullptr;
    dummyCapture.shopInstance = g_activeShop;
    
    g_origShopChestClick(&dummyCapture);
  }
}

} // namespace ParaboxAPI

void ShopHooks_Init(MewjectorAPI* mj, uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, ShopBuyItem, "48 89 5c 24 18 48 89 7c 24 20 55 48 8b ec 48 81 ec ?? ?? ?? ?? 48 8b f9 48 8b 51 08 80 7a 7b 00", 14);
  HOOK_INSTALL(mj, gameBase, ShopExitButton, "48 89 5c 24 10 57 48 83 ec 20 48 8b 41 08 48 8b d9 83 b8 88 00 00 00 00", 14);
  
  HOOK_INSTALL(mj, gameBase, ShopInit,
    "48 8b c4 44 88 48 20 4c 89 40 18 48 89 50 10 55 53 56 57",
    15);
               
  HOOK_INSTALL(mj, gameBase, ShopChestClick, 
    "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 41 08 48 8b f9 33 db 48 8b 70 38 48 8b 8e ?? ?? ?? ??",
    15);
}
