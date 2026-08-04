#pragma once
#include <cstdint>
#include "ParaboxAPI.h"
#include "MewgenicsTypes.h"

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

struct ShopLevelUpEvent {
  podvector<void*>* optionsVec;
};



extern PARABOX_API Event<ShopBuyItemEvent> OnShopBuyItem;
extern PARABOX_API Event<ShopExitButtonEvent> OnShopExitButton;
extern PARABOX_API Event<ShopLevelUpEvent> OnShopLevelUp;


struct ShopChestClickEvent {
  void* shopInstance;
  bool cancelled;
};

extern PARABOX_API Event<ShopChestClickEvent> OnShopChestClick;

struct ShopFastForwardEvent {
  void* shopInstance;
};

extern PARABOX_API Event<ShopFastForwardEvent> OnShopFastForward;

PARABOX_API void ForceShopBuyItem(uint32_t itemIndex);
PARABOX_API void ForceShopExitButton();

PARABOX_API void ForceShopChestClick();
PARABOX_API void ForceShopFastForward();
PARABOX_API void* PickRandomCat(void* vec);

} // namespace ParaboxAPI

#include "mewjector.h"
void ShopHooks_Init(MewjectorAPI* mj, uintptr_t gameBase);
