#include "hooks/LevelUpHooks.h"
#include "hooks/HookMacros.h"
#include "Scanner.h"
#include "ParaboxAPI.h"
#include "MewgenicsTypes.h"

// Global caches
static glaiel::LevelUpScreen* g_activeLevelUpScreen = nullptr;
static glaiel::AbilityChooser* g_activeAbilityChooser = nullptr;

// Hooks
HOOK_DEFINE(LevelUpScreen_init, void, glaiel::LevelUpScreen* self, PersistentCharacter* cat, void* param3)
HOOK_DEFINE(LevelUpScreen_select_option, void, glaiel::LevelUpScreen* self, glaiel::LevelUpOption* option)
HOOK_DEFINE(LevelUpScreen_Reroll, void, glaiel::LevelUpScreen* self)
HOOK_DEFINE(AbilityChooser_init, void, glaiel::AbilityChooser* self, void* scene, PersistentCharacter* cat, int param4, void* param5, int param6, void* param7, void* param8, void* param9, char param10)
HOOK_DEFINE(AbilityChooser_select_slot, void, glaiel::AbilityChooser* self, uint32_t slotIndex)

static void __fastcall Hook_LevelUpScreen_init(glaiel::LevelUpScreen* self, PersistentCharacter* cat, void* param3) {
  g_activeLevelUpScreen = self;

  if (g_origLevelUpScreen_init) {
    g_origLevelUpScreen_init(self, cat, param3);
  }

  ParaboxAPI::LevelUpScreenInitEvent ev = {};
  ev.self = self;
  ev.cat = cat;
  ParaboxAPI::OnLevelUpScreenInit.Publish(ev);
}

static void __fastcall Hook_LevelUpScreen_select_option(glaiel::LevelUpScreen* self, glaiel::LevelUpOption* option) {
  int optionIndex = -1;
  if (self->options.Myfirst && self->options.Mylast && option) {
    int count = static_cast<int>(self->options.size());
    for (int i = 0; i < count; i++) {
      const glaiel::LevelUpOption& item = self->options.Myfirst[i];
      if (item.type == option->type && item.optionKey.as_native_string_view() == option->optionKey.as_native_string_view()) {
        optionIndex = i;
        break;
      }
    }
  }

  ParaboxAPI::LevelUpScreenSelectOptionEvent ev = {};
  ev.self = self;
  ev.option = option;
  ev.optionIndex = optionIndex;
  ParaboxAPI::OnLevelUpScreenSelectOption.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origLevelUpScreen_select_option) {
    g_origLevelUpScreen_select_option(self, option);
  }
}

static void __fastcall Hook_LevelUpScreen_Reroll(glaiel::LevelUpScreen* self) {
  ParaboxAPI::LevelUpScreenRerollEvent ev = {};
  ev.self = self;
  ParaboxAPI::OnLevelUpScreenReroll.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origLevelUpScreen_Reroll) {
    g_origLevelUpScreen_Reroll(self);
  }
}

static void __fastcall Hook_AbilityChooser_init(glaiel::AbilityChooser* self, void* scene, PersistentCharacter* cat, int param4, void* param5, int param6, void* param7, void* param8, void* param9, char param10) {
  g_activeAbilityChooser = self;

  if (g_origAbilityChooser_init) {
    g_origAbilityChooser_init(self, scene, cat, param4, param5, param6, param7, param8, param9, param10);
  }

  ParaboxAPI::AbilityChooserInitEvent ev = {};
  ev.self = self;
  ev.cat = cat;
  ParaboxAPI::OnAbilityChooserInit.Publish(ev);
}

static void __fastcall Hook_AbilityChooser_select_slot(glaiel::AbilityChooser* self, uint32_t slotIndex) {
  ParaboxAPI::AbilityChooserSelectSlotEvent ev = {};
  ev.self = self;
  ev.slotIndex = slotIndex;
  ParaboxAPI::OnAbilityChooserSelectSlot.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origAbilityChooser_select_slot) {
    g_origAbilityChooser_select_slot(self, slotIndex);
  }
}

namespace ParaboxAPI {

PARABOX_API glaiel::LevelUpScreen *GetActiveLevelUpScreen() {
  return g_activeLevelUpScreen;
}

PARABOX_API glaiel::AbilityChooser *GetActiveAbilityChooser() {
  return g_activeAbilityChooser;
}

PARABOX_API void ForceLevelUpScreenSelectOption(glaiel::LevelUpScreen *self, glaiel::LevelUpOption *option) {
  if (g_origLevelUpScreen_select_option) {
    g_origLevelUpScreen_select_option(self, option);
  }
}

PARABOX_API void ForceLevelUpScreenReroll(glaiel::LevelUpScreen *self) {
  if (g_origLevelUpScreen_Reroll) {
    g_origLevelUpScreen_Reroll(self);
  }
}

PARABOX_API void ForceAbilityChooserSelectSlot(glaiel::AbilityChooser *self, uint32_t slotIndex) {
  if (g_origAbilityChooser_select_slot) {
    g_origAbilityChooser_select_slot(self, slotIndex);
  }
}

} // namespace ParaboxAPI

void LevelUpHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, LevelUpScreen_init,
    "48 8b c4 4c 89 40 18 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8d a8 68 f9 ff ff 48 81",
    0);

  HOOK_INSTALL(mj, gameBase, LevelUpScreen_select_option,
    "48 89 5c 24 20 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 70 ff ff ff 48 81 ec",
    0);

  HOOK_INSTALL(mj, gameBase, LevelUpScreen_Reroll,
    "48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55 41 54 41 55 41 56 41 57 48 8d 6c 24 c9 48 81 ec",
    0);

  HOOK_INSTALL(mj, gameBase, AbilityChooser_init,
    "48 89 5c 24 20 4c 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 50 fe ff ff 48 81 ec",
    0);

  HOOK_INSTALL(mj, gameBase, AbilityChooser_select_slot,
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 20 48 8b 41 40 48 8b f9 33 db 8b ea 48",
    0);
}
