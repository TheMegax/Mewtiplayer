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
  HOOK_INSTALL(mj, gameBase, ActSelectionScreen_init,
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 48 fd ff ff 48 81 ec b8 03 00 00",
    0);

  HOOK_INSTALL(mj, gameBase, ActSelectionScreen_SelectAct,
    "4c 8b dc 48 81 ec 88 00 00 00 49 8d 43 b8 49 89 43 08 89 54 24 20 49 89 4b a0",
    0);
}
