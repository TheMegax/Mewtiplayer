#pragma once
#include <cstdint>
#include "ParaboxAPI.h"

namespace ParaboxAPI {

struct ShopBuyItemEvent {
  void* shopInstance;
  uint32_t itemIndex;
  bool cancelled;
};

struct ShopExitButtonEvent {
  void* shopInstance;
  bool cancelled;
};

struct ShopRerollEvent {
  void* shopInstance;
  bool cancelled;
};

extern PARABOX_API Event<ShopBuyItemEvent> OnShopBuyItem;
extern PARABOX_API Event<ShopExitButtonEvent> OnShopExitButton;
extern PARABOX_API Event<ShopRerollEvent> OnShopReroll;

struct ShopChestClickEvent {
  void* shopInstance;
  bool cancelled;
};

extern PARABOX_API Event<ShopChestClickEvent> OnShopChestClick;

PARABOX_API void ForceShopBuyItem(uint32_t itemIndex);
PARABOX_API void ForceShopExitButton();
PARABOX_API void ForceShopReroll();
PARABOX_API void ForceShopChestClick();

} // namespace ParaboxAPI

#include "mewjector.h"
void ShopHooks_Init(MewjectorAPI* mj, uintptr_t gameBase);
