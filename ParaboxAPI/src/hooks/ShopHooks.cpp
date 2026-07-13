#include "hooks/ShopHooks.h"
#include "hooks/HookMacros.h"
#include "Scanner.h"
#include <iostream>

namespace ParaboxAPI {
Event<ShopBuyItemEvent> OnShopBuyItem;
Event<ShopExitButtonEvent> OnShopExitButton;
Event<ShopRerollEvent> OnShopReroll;
Event<ShopChestClickEvent> OnShopChestClick;
} // namespace ParaboxAPI

// This is the global active Shop state (useful for dummy captures).
static void* g_activeShop = nullptr;

HOOK_DEFINE(ShopInit, void, void*, void*, void*, uint8_t)
HOOK_DEFINE(ShopBuyItem, void, void*)
HOOK_DEFINE(ShopExitButton, void, void*)
HOOK_DEFINE(ShopReroll, void, void*)
HOOK_DEFINE(ShopChestClick, void, void*)

static void __fastcall Hook_ShopInit(void* shopInstance, void* p2, void* p3, uint8_t p4) {
  g_activeShop = shopInstance;
  if (g_origShopInit) {
    g_origShopInit(shopInstance, p2, p3, p4);
  }
}

static void __fastcall Hook_ShopBuyItem(void* capture) {
  uint32_t itemIndex = 0; // The actual index might be extracted from capture
  
  ParaboxAPI::ShopBuyItemEvent ev = {};
  ev.shopInstance = g_activeShop;
  ev.itemIndex = itemIndex;
  ParaboxAPI::OnShopBuyItem.Publish(ev);
  
  if (!ev.cancelled && g_origShopBuyItem) {
    g_origShopBuyItem(capture);
  }
}

static void __fastcall Hook_ShopExitButton(void* capture) {
  ParaboxAPI::ShopExitButtonEvent ev = {};
  ev.shopInstance = g_activeShop;
  ParaboxAPI::OnShopExitButton.Publish(ev);
  
  if (!ev.cancelled && g_origShopExitButton) {
    g_origShopExitButton(capture);
  }
}

static void __fastcall Hook_ShopReroll(void* capture) {
  ParaboxAPI::ShopRerollEvent ev = {};
  ev.shopInstance = g_activeShop;
  ParaboxAPI::OnShopReroll.Publish(ev);
  
  if (!ev.cancelled && g_origShopReroll) {
    g_origShopReroll(capture);
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
  // Unimplemented, requires resolving how the item index maps to lambda captures
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

void ForceShopReroll() {
  if (g_origShopReroll && g_activeShop) {
    struct {
      void* dummy1;
      void* shopInstance;
    } dummyCapture;
    dummyCapture.dummy1 = nullptr;
    dummyCapture.shopInstance = g_activeShop;
    
    g_origShopReroll(&dummyCapture);
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
  // We need the offsets. Using placeholder hooks for now except for ChestClick (FUN_1407991f0)
  // HOOK_INSTALL(mj, gameBase, ShopBuyItem, "...", 14);
  // HOOK_INSTALL(mj, gameBase, ShopExitButton, "...", 14);
  // HOOK_INSTALL(mj, gameBase, ShopReroll, "...", 14);
  
  HOOK_INSTALL(mj, gameBase, ShopInit,
    "48 8b c4 44 88 48 20 4c 89 40 18 48 89 50 10 55 53 56 57",
    15);
               
  HOOK_INSTALL(mj, gameBase, ShopChestClick, 
    "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b 41 08 48 8b f9 33 db 48 8b 70 38 48 8b 8e 80 00 00 00",
    15);
}
