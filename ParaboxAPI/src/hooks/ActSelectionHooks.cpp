#include "hooks/ActSelectionHooks.h"
#include "hooks/HookMacros.h"
#include "Scanner.h"
#include "ParaboxAPI.h"

static void *g_ActSelectionScreen = nullptr;

HOOK_DEFINE(ActSelectionScreen_init, void, void *)
HOOK_DEFINE(ActSelectionScreen_SelectAct, void, void *, int)

static void __fastcall Hook_ActSelectionScreen_init(void *self) {
  g_ActSelectionScreen = self;

  if (g_origActSelectionScreen_init) {
    g_origActSelectionScreen_init(self);
  }

  ParaboxAPI::ActSelectionScreenInitEvent ev = {};
  ev.self = self;
  ParaboxAPI::OnActSelectionScreenInit.Publish(ev);
}

static void __fastcall Hook_ActSelectionScreen_SelectAct(void *self, int actIndex) {
  ParaboxAPI::ActSelectionScreenSelectActEvent ev = {};
  ev.self = self;
  ev.actIndex = actIndex;
  ParaboxAPI::OnActSelectionScreenSelectAct.Publish(ev);

  if (ev.cancelled) {
    return;
  }

  if (g_origActSelectionScreen_SelectAct) {
    g_origActSelectionScreen_SelectAct(self, actIndex);
  }
}

namespace ParaboxAPI {

PARABOX_API void *GetActSelectionScreen() {
  return g_ActSelectionScreen;
}

PARABOX_API void ForceActSelectionScreenSelectAct(void *screen, int actIndex) {
  if (g_origActSelectionScreen_SelectAct) {
    g_origActSelectionScreen_SelectAct(screen, actIndex);
  }
}

} // namespace ParaboxAPI

void ActSelectionHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, ActSelectionScreen_init, GameSymbols::ActSelectionScreen_init, 0);
  HOOK_INSTALL(mj, gameBase, ActSelectionScreen_SelectAct, GameSymbols::ActSelectionScreen_SelectAct, 0);
}
