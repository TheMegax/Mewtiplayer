// ReSharper disable CppDFALocalValueEscapesFunction
#include "hooks/WorldEventHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "Scanner.h"
#include "ParaboxAPI.h"
#include "MewgenicsTypes.h"
#include <vector>

// Cache for the active event component
static glaiel::WorldEvent* g_activeWorldEvent = nullptr;

// Synchronization override
static PersistentCharacter* g_overrideSelectedCat = nullptr;

// Hook definitions
HOOK_DEFINE(WorldEvent_init, void, glaiel::WorldEvent* self, void* param2, void* param3)
HOOK_DEFINE(WorldEvent_ClickOption, void, glaiel::WorldEventClickEvent* param_1)
HOOK_DEFINE(WorldEvent_ClickCat, void, glaiel::WorldEventClickEvent* param_1)
HOOK_DEFINE(GetSelectedCat, PersistentCharacter*, void* param_1, uint64_t param_2)
HOOK_DEFINE(WorldEvent_ClickEnd1, void, glaiel::WorldEventClickEvent* param_1)
HOOK_DEFINE(WorldEvent_ClickEnd2, void, glaiel::WorldEventClickEvent* param_1)


static void __fastcall Hook_WorldEvent_init(glaiel::WorldEvent* self, void* param2, void* param3) {
  g_activeWorldEvent = self;
  
  if (g_origWorldEvent_init) {
    g_origWorldEvent_init(self, param2, param3);
  }

  ParaboxAPI::WorldEventInitEvent ev = {};
  ev.self = self;
  ParaboxAPI::OnWorldEventInit.Publish(ev);
}

static void __fastcall Hook_WorldEvent_ClickOption(glaiel::WorldEventClickEvent* param_1) {
  glaiel::WorldEvent* worldEvent = param_1->worldEvent;

  auto* clickedOption = static_cast<glaiel::WorldEventOption*>(param_1->sender);
  glaiel::WorldEventOption* vectorStart = worldEvent->options.Myfirst;
  glaiel::WorldEventOption* vectorEnd = worldEvent->options.Mylast;
  int optionIndex = -1;
  if (vectorStart && vectorEnd && clickedOption) {
    intptr_t diff = clickedOption - vectorStart;
    int count = static_cast<int>(worldEvent->options.size());
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

static void __fastcall Hook_WorldEvent_ClickCat(glaiel::WorldEventClickEvent* param_1) {
  glaiel::WorldEvent* worldEvent = param_1->worldEvent;

  auto* clickedButton = static_cast<glaiel::WorldEventCatButton*>(param_1->sender);
  void* catManager = clickedButton->catManager;
  glaiel::WorldEventOptionState* statePtr = worldEvent->optionState;
  uint64_t catUID = 0;
  if (statePtr) {
    catUID = statePtr->catUID;
  }

  PersistentCharacter* clickedCat = nullptr;
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

static PersistentCharacter* __fastcall Hook_GetSelectedCat(void* param_1, uint64_t param_2) {
  if (g_overrideSelectedCat) {
    return g_overrideSelectedCat;
  }
  if (g_origGetSelectedCat) {
    return g_origGetSelectedCat(param_1, param_2);
  }
  return nullptr;
}

static void __fastcall Hook_WorldEvent_ClickEnd1(glaiel::WorldEventClickEvent* param_1) {
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

static void __fastcall Hook_WorldEvent_ClickEnd2(glaiel::WorldEventClickEvent* param_1) {
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

namespace ParaboxAPI {

PARABOX_API glaiel::WorldEvent *GetActiveWorldEvent() {
  return g_activeWorldEvent;
}

PARABOX_API void ForceWorldEventSelectOption(int64_t catUID, uint32_t optionIndex) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    return;
  }

  PersistentCharacter* activeCat = g_activeWorldEvent->activeCat;
  if (!activeCat || activeCat->sql_key != catUID) {
    return;
  }

  glaiel::WorldEventOption* vectorStart = g_activeWorldEvent->options.Myfirst;
  glaiel::WorldEventOption* vectorEnd = g_activeWorldEvent->options.Mylast;
  if (!vectorStart || !vectorEnd) {
    return;
  }

  int count = static_cast<int>(g_activeWorldEvent->options.size());
  if (static_cast<int>(optionIndex) >= count) {
    return;
  }

  glaiel::WorldEventOption* optionPtr = vectorStart + optionIndex;

  glaiel::WorldEventClickEvent dummy = {};
  dummy.vtable = nullptr;
  dummy.worldEvent = g_activeWorldEvent;
  dummy.sender = optionPtr;

  if (g_origWorldEvent_ClickOption) {
    g_origWorldEvent_ClickOption(&dummy);
  }
}

// Helpers for finding buttons
static Component* FindButtonForCat(const PersistentCharacter* targetCat) {
  if (!targetCat || !g_activeWorldEvent) return nullptr;
  Scene* scene = g_activeWorldEvent->scene;
  if (!scene) return nullptr;

  auto components = GameUtils::GetSceneComponents(scene);
  for (Component* comp : components) {
    if (!GameUtils::IsComponentValid(comp)) continue;

    auto* btn = reinterpret_cast<glaiel::WorldEventCatButton*>(comp);
    void* catManager = btn->catManager;
    void* otherPtr = btn->otherPtr;
    if (catManager && otherPtr) {
      if (g_origGetSelectedCat) {
        PersistentCharacter* cat = g_origGetSelectedCat(catManager, targetCat->sql_key);
        if (cat && cat->sql_key == targetCat->sql_key) {
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

    auto* btn = reinterpret_cast<glaiel::WorldEventCatButton*>(comp);
    void* catManager = btn->catManager;
    void* otherPtr = btn->otherPtr;
    if (catManager && otherPtr) {
      return comp;
    }
  }
  return nullptr;
}

static PersistentCharacter* FindCatInParty(int64_t catUID) {
  MewDirector* director = GameUtils::GetMewDirectorSingleton();
  if (director && director->partyCatIDs) {
    for (int i = 0; i < director->partyCount; ++i) {
      int64_t id = director->partyCatIDs[i];
      if (id == catUID) {
        return ParaboxAPI::GetPersistentCharacterById(catUID);
      }
    }
  }
  return nullptr;
}


PARABOX_API void ForceWorldEventSelectCat(int64_t selectedCatUID) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    return;
  }

  PersistentCharacter* targetCat = FindCatInParty(selectedCatUID);
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

  glaiel::WorldEventOptionState dummyState = {};
  memset(&dummyState, 0, sizeof(dummyState));
  dummyState.catUID = selectedCatUID;

  glaiel::WorldEventOptionState* oldState = g_activeWorldEvent->optionState;
  g_activeWorldEvent->optionState = &dummyState;

  glaiel::WorldEventClickEvent dummyClick = {};
  dummyClick.vtable = nullptr;
  dummyClick.worldEvent = g_activeWorldEvent;
  dummyClick.sender = buttonComp;

  if (g_origWorldEvent_ClickCat) {
    g_origWorldEvent_ClickCat(&dummyClick);
  }

  g_activeWorldEvent->optionState = oldState;
  g_overrideSelectedCat = nullptr;
}

PARABOX_API void ForceWorldEventClickEnd(uint8_t buttonType) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    return;
  }

  glaiel::WorldEventClickEvent dummy = {};
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
  }
}

} // namespace ParaboxAPI

void WorldEventHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, WorldEvent_init,
    "48 8b c4 48 89 50 10 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8d a8 f8 fd ff ff 48 81 ec c8 02 00 00",
    23);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickOption,
    "48 8B 05 ?? ?? ?? ?? C6 80 14 07 00 00 01 48 8B 51 08 48 8B 41 10 48 89",
    14);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickCat,
    "48 89 5C 24 10 57 48 83 EC 40 48 8B 41 10 48 8B D9 48 8B 88 98 05 00 00",
    14);

  HOOK_INSTALL(mj, gameBase, GetSelectedCat,
    "48 89 5C 24 08 48 89 74 24 20 48 89 54 24 10 57 48 83 EC 40 4C 8B C2 48",
    15);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickEnd1,
    "40 53 48 83 EC 40 48 8B 51 08 48 8B D9 80 BA F0 19 00 00 00 75 49 48 8B 05 ?? ?? ?? ?? 80 78 38 00 0F 85 C7 00 00 00 48 8B 40 18 48 89 54 24 50 48 8B 58 08 80 BB B0 04 00 00 00 0F 85 AD 00 00 00 48 8B CB E8 B7 A5 02 00",
    20);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickEnd2,
    "40 53 48 83 EC 40 48 8B 51 08 48 8B D9 80 BA F0 19 00 00 00 75 49 48 8B 05 ?? ?? ?? ?? 80 78 38 00 0F 85 C7 00 00 00 48 8B 40 18 48 89 54 24 50 48 8B 58 08 80 BB B0 04 00 00 00 0F 85 AD 00 00 00 48 8B CB E8 E7 A6 02 00",
    20);
}
