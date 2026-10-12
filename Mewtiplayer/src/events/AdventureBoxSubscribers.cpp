#include "events/AdventureBoxSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"


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
