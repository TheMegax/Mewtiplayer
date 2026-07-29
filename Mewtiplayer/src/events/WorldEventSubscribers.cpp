#include "events/WorldEventSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "SteamABICompat.h"

// ---------------------------------------------------------------------------
// WorldEventHooks Subscribers
// ---------------------------------------------------------------------------

// Check if local player is allowed to interact with the event screen
static bool CanInteract(const glaiel::WorldEvent* worldEvent) {
    if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
        return true; // Single player or not in lobby, always allow
    }

    if (const PersistentCharacter* activeCat = worldEvent->activeCat) {
        const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(activeCat->sql_key);
        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        if (ownerSteamID != 0) {
            return ownerSteamID == localSteamID;
        }
    }

    // Failsafe: only the host can interact
    return NetworkManager::Get().IsHost();
}

void RegisterWorldEventSubscribers() {
    ParaboxAPI::OnWorldEventInit.Subscribe([](ParaboxAPI::WorldEventInitEvent& ev) {
        Overlay::Log("[WORLDEVENT] WorldEvent init: %p", ev.self);
    });

    ParaboxAPI::OnWorldEventClickOption.Subscribe([](ParaboxAPI::WorldEventClickOptionEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickOption for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            PersistentCharacter* activeCat = ev.self->activeCat;
            if (ev.optionIndex != -1 && activeCat) {
                WorldEventSelectOptionPacket pkt = {};
                pkt.catUID = activeCat->sql_key;
                pkt.optionIndex = static_cast<uint32_t>(ev.optionIndex);
                NetworkManager::Get().BroadcastPacket(PacketType::WorldEventSelectOption, &pkt, sizeof(pkt), true);
                Overlay::Log("[WORLDEVENT] Local owner selected option %d. Broadcasting sync.", ev.optionIndex);
            } else {
                Overlay::Log("[WORLDEVENT] [ERR] ClickOption index could not be resolved! optionIndex: %d, activeCat: %p",
                             ev.optionIndex, activeCat);
            }
        }
    });

    ParaboxAPI::OnWorldEventClickCat.Subscribe([](ParaboxAPI::WorldEventClickCatEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickCat for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            if (ev.clickedCat) {
                WorldEventSelectCatPacket pkt = {};
                pkt.selectedCatUID = ev.clickedCat->sql_key;
                NetworkManager::Get().BroadcastPacket(PacketType::WorldEventSelectCat, &pkt, sizeof(pkt), true);
                Overlay::Log("[WORLDEVENT] Local owner clicked cat. Syncing selectedCatUID %lld.", ev.clickedCat->sql_key);
            }
        }
    });

    ParaboxAPI::OnWorldEventClickEnd1.Subscribe([](ParaboxAPI::WorldEventClickEnd1Event& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickEnd1 for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            WorldEventClickEndPacket pkt = {};
            pkt.buttonType = 1;
            NetworkManager::Get().BroadcastPacket(PacketType::WorldEventClickEnd, &pkt, sizeof(pkt), true);
            Overlay::Log("[WORLDEVENT] Local owner clicked End1. Broadcasting sync.");
        }
    });

    ParaboxAPI::OnWorldEventClickEnd2.Subscribe([](ParaboxAPI::WorldEventClickEnd2Event& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (!CanInteract(ev.self)) {
                Overlay::Log("[WORLDEVENT] Blocked ClickEnd2 for non-owner/non-host.");
                ev.Cancel();
                return;
            }

            WorldEventClickEndPacket pkt = {};
            pkt.buttonType = 2;
            NetworkManager::Get().BroadcastPacket(PacketType::WorldEventClickEnd, &pkt, sizeof(pkt), true);
            Overlay::Log("[WORLDEVENT] Local owner clicked End2. Broadcasting sync.");
        }
    });
}

// Trigger functions called from network thread
void TriggerWorldEventSelectOption(int64_t catUID, uint32_t optionIndex) {
    glaiel::WorldEvent* event = ParaboxAPI::GetActiveWorldEvent();
    if (!event || !GameUtils::IsComponentValid(event)) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: g_activeWorldEvent is null or invalid.");
        return;
    }

    PersistentCharacter* activeCat = event->activeCat;
    if (!activeCat || activeCat->sql_key != catUID) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: active screen cat UID (%lld) does not match packet UID (%lld).",
                     activeCat ? activeCat->sql_key : -1, catUID);
        return;
    }

    glaiel::WorldEventOption* vectorStart = event->options.Myfirst;
    glaiel::WorldEventOption* vectorEnd = event->options.Mylast;
    if (!vectorStart || !vectorEnd) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: options vector is null.");
        return;
    }

    int count = static_cast<int>(event->options.size());
    if (static_cast<int>(optionIndex) >= count) {
        Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: optionIndex %u out of bounds (%d).", optionIndex, count);
        return;
    }

    Overlay::Log("[WORLDEVENT] Triggering synced ClickOption for index %u", optionIndex);
    ParaboxAPI::ForceWorldEventSelectOption(catUID, optionIndex);
}

void TriggerWorldEventSelectCat(int64_t selectedCatUID) {
    glaiel::WorldEvent* event = ParaboxAPI::GetActiveWorldEvent();
    if (!event || !GameUtils::IsComponentValid(event)) {
        Overlay::Log("[WORLDEVENT] TriggerWorldEventSelectCat error: g_activeWorldEvent is null or invalid.");
        return;
    }

    Overlay::Log("[WORLDEVENT] Triggering synced ClickCat for UID %lld.", selectedCatUID);
    ParaboxAPI::ForceWorldEventSelectCat(selectedCatUID);
}

void TriggerWorldEventClickEnd(uint8_t buttonType) {
    glaiel::WorldEvent* event = ParaboxAPI::GetActiveWorldEvent();
    if (!event || !GameUtils::IsComponentValid(event)) {
        Overlay::Log("[WORLDEVENT] TriggerWorldEventClickEnd error: g_activeWorldEvent is null or invalid.");
        return;
    }

    Overlay::Log("[WORLDEVENT] Triggering synced ClickEnd%d.", buttonType);
    ParaboxAPI::ForceWorldEventClickEnd(buttonType);
}
