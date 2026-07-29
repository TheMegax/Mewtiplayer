#include "events/ActSelectionSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"

// ---------------------------------------------------------------------------
// ActSelectionHooks Subscribers
// ---------------------------------------------------------------------------

uint32_t g_pendingActSelectIndex = 0;

void RegisterActSelectionSubscribers() {
    ParaboxAPI::OnActSelectionScreenInit.Subscribe([](ParaboxAPI::ActSelectionScreenInitEvent& ev) {
        Overlay::Log("[ACT] ActSelectionScreen initialized: %p", ev.self);

        if (g_pendingActSelectIndex != 0) {
            uint32_t act = g_pendingActSelectIndex;
            g_pendingActSelectIndex = 0;
            
            if (!GameUtils::IsComponentValid(ParaboxAPI::GetActSelectionScreen())) {
                g_pendingActSelectIndex = act;
                Overlay::Log("[ACT] ActSelectionScreen is null or invalid, requeued act select: %d", act);
                return;
            }

            Overlay::Log("[ACT] Triggering network-synced SelectAct (index %u)", act);
            ParaboxAPI::ForceActSelectionScreenSelectAct(ParaboxAPI::GetActSelectionScreen(), act);
        }
    });

    ParaboxAPI::OnActSelectionScreenSelectAct.Subscribe([](ParaboxAPI::ActSelectionScreenSelectActEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!NetworkManager::Get().IsHost()) {
                return;
            }

            ActSelectPacket pkt = {};
            pkt.actIndex = ev.actIndex;
            NetworkManager::Get().BroadcastPacket(PacketType::ActSelectSync, &pkt, sizeof(pkt), true);
            Overlay::Log("[ACT] Host selected act %d. Broadcasting sync.", ev.actIndex);
        }
    });
}

void TriggerActSelect(uint32_t actIndex) {
    if (!GameUtils::IsComponentValid(ParaboxAPI::GetActSelectionScreen())) {
        g_pendingActSelectIndex = actIndex;
        Overlay::Log("[ACT] ActSelectionScreen is null or invalid, queued act select: %d", actIndex);
        return;
    }

    Overlay::Log("[ACT] Triggering network-synced SelectAct (index %u)", actIndex);
    ParaboxAPI::ForceActSelectionScreenSelectAct(ParaboxAPI::GetActSelectionScreen(), actIndex);
}
