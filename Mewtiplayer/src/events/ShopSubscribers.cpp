#include "events/ShopSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "hooks/ShopHooks.h"

// ---------------------------------------------------------------------------
// ShopHooks Subscribers
// ---------------------------------------------------------------------------

void RegisterShopSubscribers() {
    ParaboxAPI::OnShopBuyItem.Subscribe([](ParaboxAPI::ShopBuyItemEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            ShopBuyItemPacket pkt = {};
            pkt.itemIndex = ev.itemIndex;
            NetworkManager::Get().BroadcastPacket(PacketType::ShopBuyItem, &pkt, sizeof(pkt), true);
            Overlay::Log("[SHOP] Broadcast ShopBuyItem (index %u)", ev.itemIndex);
        }
    });

    ParaboxAPI::OnShopExitButton.Subscribe([](ParaboxAPI::ShopExitButtonEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            ShopExitButtonPacket pkt = {};
            NetworkManager::Get().BroadcastPacket(PacketType::ShopExitButton, &pkt, sizeof(pkt), true);
            Overlay::Log("[SHOP] Broadcast ShopExitButton");
        }
    });

    ParaboxAPI::OnShopChestClick.Subscribe([](ParaboxAPI::ShopChestClickEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            ShopChestClickPacket pkt = {};
            NetworkManager::Get().BroadcastPacket(PacketType::ShopChestClick, &pkt, sizeof(pkt), true);
            Overlay::Log("[SHOP] Broadcast ShopChestClick");
        }
    });

    // For whatever reason, the game decides to use a different, misc RNG here instead of the seeded one.
    // This overrides that behavior, forcing it to select one option.
    ParaboxAPI::OnShopLevelUp.Subscribe([](ParaboxAPI::ShopLevelUpEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid() && ev.optionsVec && ev.optionsVec->size_ > 1) {
            if (void* pickedCat = ParaboxAPI::PickRandomCat(ev.optionsVec)) {
                ev.optionsVec->data_[0] = pickedCat;
                ev.optionsVec->size_ = 1;
                Overlay::Log("[SHOP] [HACK] Synchronized Rare Candy RNG");
            }
        }
    });
}
