#include "events/ActSelectionSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"

// ---------------------------------------------------------------------------
// ActSelectionHooks Subscribers
// ---------------------------------------------------------------------------

uint32_t g_pendingActSelectIndex = 0;
static bool g_isHandlingNetworkActSelect = false;

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
            g_isHandlingNetworkActSelect = true;
            ParaboxAPI::ForceActSelectionScreenSelectAct(ParaboxAPI::GetActSelectionScreen(), act);
            g_isHandlingNetworkActSelect = false;
        }
    });

    ParaboxAPI::OnActSelectionScreenSelectAct.Subscribe([](ParaboxAPI::ActSelectionScreenSelectActEvent& ev) {
        if (g_isHandlingNetworkActSelect) return;

        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            ActSelectPacket pkt = {};
            pkt.actIndex = ev.actIndex;

            if (NetworkManager::Get().IsHost()) {
                Overlay::Log("[ACT] Host selected act %d. Broadcasting sync.", ev.actIndex);
                NetworkManager::Get().BroadcastPacket(PacketType::ActSelectSync, &pkt, sizeof(pkt), true);
            } else {
                Overlay::Log("[ACT] Client requested act %d selection from Host.", ev.actIndex);
                NetworkManager::Get().SendPacketReliable(NetworkManager::Get().GetHostID(), PacketType::ActSelectSync, &pkt, sizeof(pkt));
                ev.Cancel();
            }
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
    g_isHandlingNetworkActSelect = true;
    ParaboxAPI::ForceActSelectionScreenSelectAct(ParaboxAPI::GetActSelectionScreen(), actIndex);
    g_isHandlingNetworkActSelect = false;
}
