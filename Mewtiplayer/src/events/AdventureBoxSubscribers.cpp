#include "events/AdventureBoxSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"

// ---------------------------------------------------------------------------
// AdventureBoxHooks Subscribers
// ---------------------------------------------------------------------------
void RegisterAdventureBoxSubscribers() {
    ParaboxAPI::OnButchBoxInit.Subscribe([](ParaboxAPI::ButchBoxInitEvent& ev) {
        NetworkManager::Get().SendLocalCatCount();
    });

    ParaboxAPI::OnButchBoxTryPlaceCat.Subscribe([](ParaboxAPI::ButchBoxTryPlaceCatEvent& ev) {
        if (ev.returnValue) {
            NetworkManager::Get().SendLocalCatCount();
        }
    });

    ParaboxAPI::OnButchBoxTryRemoveCat.Subscribe([](ParaboxAPI::ButchBoxTryRemoveCatEvent& ev) {
        NetworkManager::Get().SendLocalCatCount();
    });
}
