#include "hooks/ActSelectionHooks.h"
#include "hooks/HookMacros.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "SteamABICompat.h"
#include "Scanner.h"
#include "ModState.h"

void *g_ActSelectionScreen = nullptr;
uint32_t g_pendingActSelectIndex = 0;

HOOK_DEFINE(ActSelectionScreen_init, void, void *)
HOOK_DEFINE(ActSelectionScreen_SelectAct, void, void *, int)

static void __fastcall Hook_ActSelectionScreen_init(void *self) {
  g_ActSelectionScreen = self;
  Overlay::Log("[ACT] ActSelectionScreen initialized: %p", self);

  if (g_origActSelectionScreen_init) {
    g_origActSelectionScreen_init(self);
  }

  if (g_pendingActSelectIndex != 0) {
    uint32_t act = g_pendingActSelectIndex;
    g_pendingActSelectIndex = 0;
    TriggerActSelect(act);
  }
}

static void __fastcall Hook_ActSelectionScreen_SelectAct(void *self, int actIndex) {
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (!NetworkManager::Get().IsHost()) {
      return;
    }

    ActSelectPacket pkt = {};
    pkt.actIndex = actIndex;
    NetworkManager::Get().BroadcastPacket(PacketType::ActSelectSync, &pkt, sizeof(pkt), true);
    Overlay::Log("[ACT] Host selected act %d. Broadcasting sync.", actIndex);
  }

  if (g_origActSelectionScreen_SelectAct) {
    g_origActSelectionScreen_SelectAct(self, actIndex);
  }
}

void TriggerActSelect(uint32_t actIndex) {
  if (!GameUtils::IsComponentValid(g_ActSelectionScreen)) {
    g_pendingActSelectIndex = actIndex;
    Overlay::Log("[ACT] ActSelectionScreen is null or invalid, queued act select: %d", actIndex);
    return;
  }

  Overlay::Log("[ACT] Triggering network-synced SelectAct (index %u)", actIndex);
  if (g_origActSelectionScreen_SelectAct) {
    g_origActSelectionScreen_SelectAct(g_ActSelectionScreen, actIndex);
  }
}

void ActSelectionHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, ActSelectionScreen_init,
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 48 fd ff ff 48 81 ec b8 03 00 00",
    0);

  HOOK_INSTALL(mj, gameBase, ActSelectionScreen_SelectAct,
    "4c 8b dc 48 81 ec 88 00 00 00 49 8d 43 b8 49 89 43 08 89 54 24 20 49 89 4b a0",
    0);
}
