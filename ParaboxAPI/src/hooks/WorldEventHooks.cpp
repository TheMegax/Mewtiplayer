// ReSharper disable CppDFALocalValueEscapesFunction
#include "hooks/WorldEventHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "Scanner.h"
#include "ParaboxAPI.h"
#include "MewgenicsTypes.h"
#include <vector>
#include <functional>

// Cache for the active event component
static WorldEvent* g_activeWorldEvent = nullptr;

// Cache for active custom end button callback
static std::function<void()> g_activeCustomEndButtonCallback = nullptr;

// Synchronization override
static CatData* g_overrideSelectedCat = nullptr;

// Hook definitions
HOOK_DEFINE(WorldEvent_init, void, WorldEvent* self, void* param2, void* param3)
HOOK_DEFINE(WorldEvent_ClickOption, void, WorldEventClickEvent* param_1)
HOOK_DEFINE(WorldEvent_ClickCat, void, WorldEventClickEvent* param_1)
HOOK_DEFINE(GetSelectedCat, CatData*, void* param_1, uint64_t param_2)
HOOK_DEFINE(WorldEvent_ClickEnd1, void, WorldEventClickEvent* param_1)
HOOK_DEFINE(WorldEvent_ClickEnd2, void, WorldEventClickEvent* param_1)
HOOK_DEFINE(WorldEvent_add_end_button, void, WorldEvent* self, MsvcReleaseModeXString* tokenString, void* callbackPtr)


static void __fastcall Hook_WorldEvent_init(WorldEvent* self, void* param2, void* param3) {
  g_activeWorldEvent = self;
  g_activeCustomEndButtonCallback = nullptr;
  
  if (g_origWorldEvent_init) {
    g_origWorldEvent_init(self, param2, param3);
  }

  ParaboxAPI::WorldEventInitEvent ev = {};
  ev.self = self;
  ParaboxAPI::OnWorldEventInit.Publish(ev);
}

static void __fastcall Hook_WorldEvent_ClickOption(WorldEventClickEvent* param_1) {
  WorldEvent* worldEvent = param_1->worldEvent;

  auto* clickedOption = static_cast<WorldEventOption*>(param_1->sender);
  WorldEventOption* vectorStart = worldEvent->action_pane.options.Myfirst;
  WorldEventOption* vectorEnd = worldEvent->action_pane.options.Mylast;
  int optionIndex = -1;
  if (vectorStart && vectorEnd && clickedOption) {
    intptr_t diff = clickedOption - vectorStart;
    int count = static_cast<int>(worldEvent->action_pane.options.size());
    if (diff >= 0 && diff < count) {
      optionIndex = static_cast<int>(diff);
    }
  }

  ParaboxAPI::WorldEventClickOptionEvent ev = {};
  ev.self = worldEvent;
  ev.clickedOption = clickedOption;
  ev.optionIndex = optionIndex;
  ParaboxAPI::OnWorldEventClickOption.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origWorldEvent_ClickOption) {
    g_origWorldEvent_ClickOption(param_1);
  }
}

static void __fastcall Hook_WorldEvent_ClickCat(WorldEventClickEvent* param_1) {
  WorldEvent* worldEvent = param_1->worldEvent;

  auto* clickedButton = static_cast<WorldEventCatButton*>(param_1->sender);
  void* catManager = clickedButton->catManager;
  CatSelector* selector = worldEvent->selector;
  uint64_t catUID = 0;
  if (selector) {
    catUID = selector->current_cat;
  }

  CatData* clickedCat = nullptr;
  if (g_origGetSelectedCat && catManager && catUID != 0) {
    clickedCat = g_origGetSelectedCat(catManager, catUID);
  }

  ParaboxAPI::WorldEventClickCatEvent ev = {};
  ev.self = worldEvent;
  ev.clickedButton = clickedButton;
  ev.clickedCat = clickedCat;
  ParaboxAPI::OnWorldEventClickCat.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origWorldEvent_ClickCat) {
    g_origWorldEvent_ClickCat(param_1);
  }
}

static CatData* __fastcall Hook_GetSelectedCat(void* param_1, uint64_t param_2) {
  if (g_overrideSelectedCat) {
    return g_overrideSelectedCat;
  }
  if (g_origGetSelectedCat) {
    return g_origGetSelectedCat(param_1, param_2);
  }
  return nullptr;
}

static void __fastcall Hook_WorldEvent_ClickEnd1(WorldEventClickEvent* param_1) {
  ParaboxAPI::WorldEventClickEnd1Event ev = {};
  ev.self = param_1->worldEvent;
  ParaboxAPI::OnWorldEventClickEnd1.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origWorldEvent_ClickEnd1) {
    g_origWorldEvent_ClickEnd1(param_1);
  }
}

static void __fastcall Hook_WorldEvent_ClickEnd2(WorldEventClickEvent* param_1) {
  ParaboxAPI::WorldEventClickEnd2Event ev = {};
  ev.self = param_1->worldEvent;
  ParaboxAPI::OnWorldEventClickEnd2.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origWorldEvent_ClickEnd2) {
    g_origWorldEvent_ClickEnd2(param_1);
  }
}

static void __fastcall Hook_WorldEvent_add_end_button(WorldEvent* self, MsvcReleaseModeXString* tokenString, void* callbackPtr) {
  const auto* origFuncPtr = static_cast<std::function<void()>*>(callbackPtr);
  std::function<void()> origFunc = (origFuncPtr && *origFuncPtr) ? *origFuncPtr : nullptr;
  std::string tokenStr = tokenString ? tokenString->copy_to_native_string() : "";

  g_activeCustomEndButtonCallback = origFunc;

  std::function wrappedFunc = [self, tokenStr, origFunc]() {
    ParaboxAPI::WorldEventClickEndCustomEvent ev = {};
    ev.self = self;
    ev.tokenString = tokenStr.c_str();
    ParaboxAPI::OnWorldEventClickEndCustom.Publish(ev);

    if (ev.cancelled) {
      return;
    }

    if (origFunc) {
      origFunc();
    }
  };

  if (g_origWorldEvent_add_end_button) {
    g_origWorldEvent_add_end_button(self, tokenString, &wrappedFunc);
  }
}

namespace ParaboxAPI {

PARABOX_API WorldEvent *GetActiveWorldEvent() {
  return g_activeWorldEvent;
}

PARABOX_API void ForceWorldEventSelectOption(int64_t catUID, uint32_t optionIndex) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    return;
  }

  CatData* activeCat = g_activeWorldEvent->cat_choice;
  if (!activeCat || activeCat->cat_uid != catUID) {
    return;
  }

  WorldEventOption* vectorStart = g_activeWorldEvent->action_pane.options.Myfirst;
  WorldEventOption* vectorEnd = g_activeWorldEvent->action_pane.options.Mylast;
  if (!vectorStart || !vectorEnd) {
    return;
  }

  int count = static_cast<int>(g_activeWorldEvent->action_pane.options.size());
  if (static_cast<int>(optionIndex) >= count) {
    return;
  }

  WorldEventOption* optionPtr = vectorStart + optionIndex;

  WorldEventClickEvent dummy = {};
  dummy.vtable = nullptr;
  dummy.worldEvent = g_activeWorldEvent;
  dummy.sender = optionPtr;

  if (g_origWorldEvent_ClickOption) {
    g_origWorldEvent_ClickOption(&dummy);
  }
}

// Helpers for finding buttons
static Component* FindButtonForCat(const CatData* targetCat) {
  if (!targetCat || !g_activeWorldEvent) return nullptr;
  Scene* scene = g_activeWorldEvent->scene;
  if (!scene) return nullptr;

  auto components = GameUtils::GetSceneComponents(scene);
  for (Component* comp : components) {
    if (!GameUtils::IsComponentValid(comp)) continue;

    auto* btn = reinterpret_cast<WorldEventCatButton*>(comp);
    void* catManager = btn->catManager;
    void* otherPtr = btn->otherPtr;
    if (catManager && otherPtr) {
      if (g_origGetSelectedCat) {
        CatData* cat = g_origGetSelectedCat(catManager, targetCat->cat_uid);
        if (cat && cat->cat_uid == targetCat->cat_uid) {
          return comp;
        }
      }
    }
  }
  return nullptr;
}

static Component* FindAnyCatButton() {
  if (!g_activeWorldEvent) return nullptr;
  Scene* scene = g_activeWorldEvent->scene;
  if (!scene) return nullptr;

  auto components = GameUtils::GetSceneComponents(scene);
  for (Component* comp : components) {
    if (!GameUtils::IsComponentValid(comp)) continue;

    auto* btn = reinterpret_cast<WorldEventCatButton*>(comp);
    void* catManager = btn->catManager;
    void* otherPtr = btn->otherPtr;
    if (catManager && otherPtr) {
      return comp;
    }
  }
  return nullptr;
}

static CatData* FindCatInParty(int64_t catUID) {
  MewDirector* director = GameUtils::GetMewDirectorSingleton();
  if (director && director->current_battle_cats.data_) {
    for (uint32_t i = 0; i < director->current_battle_cats.size(); ++i) {
      int64_t id = director->current_battle_cats.data_[i];
      if (id == catUID) {
        return ParaboxAPI::GetCatDataById(catUID);
      }
    }
  }
  return nullptr;
}


PARABOX_API void ForceWorldEventSelectCat(int64_t selectedCatUID) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    return;
  }

  CatData* targetCat = FindCatInParty(selectedCatUID);
  if (!targetCat) {
    return;
  }

  Component* buttonComp = FindButtonForCat(targetCat);
  if (!buttonComp) {
    buttonComp = FindAnyCatButton();
  }

  if (!buttonComp) {
    return;
  }

  // Set the override state so that GetSelectedCat returns the target cat
  g_overrideSelectedCat = targetCat;

  CatSelector dummySelector = {};
  memset(&dummySelector, 0, sizeof(dummySelector));
  dummySelector.current_cat = selectedCatUID;

  CatSelector* oldSelector = g_activeWorldEvent->selector;
  g_activeWorldEvent->selector = &dummySelector;

  WorldEventClickEvent dummyClick = {};
  dummyClick.vtable = nullptr;
  dummyClick.worldEvent = g_activeWorldEvent;
  dummyClick.sender = buttonComp;

  if (g_origWorldEvent_ClickCat) {
    g_origWorldEvent_ClickCat(&dummyClick);
  }

  g_activeWorldEvent->selector = oldSelector;
  g_overrideSelectedCat = nullptr;
}

PARABOX_API void ForceWorldEventClickEnd(uint8_t buttonType) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    return;
  }

  WorldEventClickEvent dummy = {};
  dummy.vtable = nullptr;
  dummy.worldEvent = g_activeWorldEvent;
  dummy.sender = nullptr;

  if (buttonType == 1) {
    if (g_origWorldEvent_ClickEnd1) {
      g_origWorldEvent_ClickEnd1(&dummy);
    }
  } else if (buttonType == 2) {
    if (g_origWorldEvent_ClickEnd2) {
      g_origWorldEvent_ClickEnd2(&dummy);
    }
  } else if (buttonType == 3) {
    if (g_activeCustomEndButtonCallback) {
      g_activeCustomEndButtonCallback();
    }
  }
}

} // namespace ParaboxAPI

void WorldEventHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, WorldEvent_init, GameSymbols::WorldEvent_init, 23);
  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickOption, GameSymbols::WorldEvent_setupActionChoice_action, 14);
  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickCat, GameSymbols::WorldEvent_setupCatChoice_action, 14);
  HOOK_INSTALL(mj, gameBase, GetSelectedCat, GameSymbols::CatDatabase_get_cat, 15);
  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickEnd1, GameSymbols::WorldEvent_setupEndButton_action1, 20);
  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickEnd2, GameSymbols::WorldEvent_setupEndButton_action2, 20);
  HOOK_INSTALL(mj, gameBase, WorldEvent_add_end_button, GameSymbols::WorldEvent_push_end_option, 25);
}
