#include "hooks/LevelUpHooks.h"
#include "hooks/HookMacros.h"
#include "Scanner.h"
#include "ParaboxAPI.h"
#include "MewgenicsTypes.h"

// Global caches
static LevelUpScreen* g_activeLevelUpScreen = nullptr;
static AbilityChooser* g_activeAbilityChooser = nullptr;

// Hooks
HOOK_DEFINE(LevelUpScreen_init, void, LevelUpScreen* self, CatData* cat, void* param3)
HOOK_DEFINE(LevelUpScreen_select_option, void, LevelUpScreen* self, LevelUpOption* option)
HOOK_DEFINE(LevelUpScreen_Reroll, void, LevelUpScreen* self)
HOOK_DEFINE(AbilityChooser_init, void, AbilityChooser* self, void* scene, CatData* cat, int param4, void* param5, int param6, void* param7, void* param8, void* param9, char param10)
HOOK_DEFINE(AbilityChooser_select_slot, void, AbilityChooser* self, uint32_t slotIndex)

static void __fastcall Hook_LevelUpScreen_init(LevelUpScreen* self, CatData* cat, void* param3) {
  g_activeLevelUpScreen = self;

  if (g_origLevelUpScreen_init) {
    g_origLevelUpScreen_init(self, cat, param3);
  }

  ParaboxAPI::LevelUpScreenInitEvent ev = {};
  ev.self = self;
  ev.cat = cat;
  ParaboxAPI::OnLevelUpScreenInit.Publish(ev);
}

static void __fastcall Hook_LevelUpScreen_select_option(LevelUpScreen* self, LevelUpOption* option) {
  int optionIndex = -1;
  if (self->options.Myfirst && self->options.Mylast && option) {
    int count = static_cast<int>(self->options.size());
    for (int i = 0; i < count; i++) {
      const LevelUpOption& item = self->options.Myfirst[i];
      if (&item == option || (item.kind == option->kind && item.data.as_native_string_view() == option->data.as_native_string_view())) {
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

static void __fastcall Hook_LevelUpScreen_Reroll(LevelUpScreen* self) {
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

static void __fastcall Hook_AbilityChooser_init(AbilityChooser* self, void* scene, CatData* cat, int param4, void* param5, int param6, void* param7, void* param8, void* param9, char param10) {
  g_activeAbilityChooser = self;

  if (g_origAbilityChooser_init) {
    g_origAbilityChooser_init(self, scene, cat, param4, param5, param6, param7, param8, param9, param10);
  }

  ParaboxAPI::AbilityChooserInitEvent ev = {};
  ev.self = self;
  ev.cat = cat;
  ParaboxAPI::OnAbilityChooserInit.Publish(ev);
}

static void __fastcall Hook_AbilityChooser_select_slot(AbilityChooser* self, uint32_t slotIndex) {
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

PARABOX_API LevelUpScreen *GetActiveLevelUpScreen() {
  return g_activeLevelUpScreen;
}

PARABOX_API AbilityChooser *GetActiveAbilityChooser() {
  return g_activeAbilityChooser;
}

PARABOX_API void ForceLevelUpScreenSelectOption(LevelUpScreen *self, LevelUpOption *option) {
  if (g_origLevelUpScreen_select_option) {
    g_origLevelUpScreen_select_option(self, option);
  }
}

PARABOX_API void ForceLevelUpScreenReroll(LevelUpScreen *self) {
  if (g_origLevelUpScreen_Reroll) {
    g_origLevelUpScreen_Reroll(self);
  }
}

PARABOX_API void ForceAbilityChooserSelectSlot(AbilityChooser *self, uint32_t slotIndex) {
  if (g_origAbilityChooser_select_slot) {
    g_origAbilityChooser_select_slot(self, slotIndex);
  }
}

} // namespace ParaboxAPI

void LevelUpHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, LevelUpScreen_init, GameSymbols::LevelUpScreen_init, 0);
  HOOK_INSTALL(mj, gameBase, LevelUpScreen_select_option, GameSymbols::LevelUpScreen_select_option, 0);
  HOOK_INSTALL(mj, gameBase, LevelUpScreen_Reroll, GameSymbols::LevelUpScreen_Reroll, 0);
  HOOK_INSTALL(mj, gameBase, AbilityChooser_init, GameSymbols::AbilityChooser_init, 0);
  HOOK_INSTALL(mj, gameBase, AbilityChooser_select_slot, GameSymbols::AbilityChooser_close, 0);
}
