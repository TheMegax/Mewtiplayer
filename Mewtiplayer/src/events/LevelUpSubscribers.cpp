#include "events/LevelUpSubscribers.h"
#include "events/CombatSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "SteamABICompat.h"

// ---------------------------------------------------------------------------
// LevelUpHooks Subscribers
// ---------------------------------------------------------------------------

void RegisterLevelUpSubscribers() {
    ParaboxAPI::OnLevelUpScreenInit.Subscribe([](ParaboxAPI::LevelUpScreenInitEvent& ev) {
        if (ev.cat) {
            g_levelUpScreenToCat[ev.self] = ev.cat;
            Overlay::Log("[LEVELUP] LevelUpScreen init: %p, Cat UID: %lld", ev.self, ev.cat->sql_key);
        } else {
            Overlay::Log("[LEVELUP] LevelUpScreen init: %p, Cat is NULL!", ev.self);
        }
    });

    ParaboxAPI::OnLevelUpScreenSelectOption.Subscribe([](ParaboxAPI::LevelUpScreenSelectOptionEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            const PersistentCharacter* cat = nullptr;
            const auto it = g_levelUpScreenToCat.find(ev.self);
            if (it != g_levelUpScreenToCat.end()) {
                cat = it->second;
            }

            if (cat) {
                const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
                const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
                    Overlay::Log("[LEVELUP] Blocked select_option for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
                    ev.Cancel();
                    return;
                }

                if (ev.optionIndex != -1) {
                    LevelUpSelectOptionPacket pkt = {};
                    pkt.catUID = cat->sql_key;
                    pkt.optionIndex = ev.optionIndex;
                    NetworkManager::Get().BroadcastPacket(PacketType::LevelUpSelectOption, &pkt, sizeof(pkt), true);
                    Overlay::Log("[LEVELUP] Owner selected option %d. Broadcasting sync.", ev.optionIndex);
                } else {
                    Overlay::Log("[LEVELUP] [WARN] Failed to find optionIndex for select_option!");
                }
            }
        }
    });

    ParaboxAPI::OnLevelUpScreenReroll.Subscribe([](ParaboxAPI::LevelUpScreenRerollEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            const PersistentCharacter* cat = nullptr;
            const auto it = g_levelUpScreenToCat.find(ev.self);
            if (it != g_levelUpScreenToCat.end()) {
                cat = it->second;
            }
            if (!cat) {
                cat = ev.self->catData;
            }

            if (cat) {
                const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
                const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
                    Overlay::Log("[LEVELUP] Blocked Reroll for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
                    ev.Cancel();
                    return;
                }

                LevelUpRerollPacket pkt = {};
                pkt.catUID = cat->sql_key;
                NetworkManager::Get().BroadcastPacket(PacketType::LevelUpReroll, &pkt, sizeof(pkt), true);
                Overlay::Log("[LEVELUP] Owner requested Reroll. Broadcasting sync.");
            }
        }
    });

    ParaboxAPI::OnAbilityChooserInit.Subscribe([](ParaboxAPI::AbilityChooserInitEvent& ev) {
        if (ev.cat) {
            g_abilityChooserToCat[ev.self] = ev.cat;
            Overlay::Log("[ABILITYCHOOSER] AbilityChooser init: %p, Cat UID: %lld", ev.self, ev.cat->sql_key);
        } else {
            Overlay::Log("[ABILITYCHOOSER] AbilityChooser init: %p, Cat is NULL!", ev.self);
        }
    });

    ParaboxAPI::OnAbilityChooserSelectSlot.Subscribe([](ParaboxAPI::AbilityChooserSelectSlotEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            PersistentCharacter* cat = nullptr;
            auto it = g_abilityChooserToCat.find(ev.self);
            if (it != g_abilityChooserToCat.end()) {
                cat = it->second;
            }

            if (cat) {
                const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
                const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
                    Overlay::Log("[ABILITYCHOOSER] Blocked select_slot/cancel for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
                    ev.Cancel();
                    return;
                }

                AbilityReplacePacket pkt = {};
                pkt.catUID = cat->sql_key;
                pkt.slotIndex = ev.slotIndex;
                NetworkManager::Get().BroadcastPacket(PacketType::AbilityReplace, &pkt, sizeof(pkt), true);
                Overlay::Log("[ABILITYCHOOSER] Owner selected slot %u (or cancel/skip 0xFFFFFFFF). Broadcasting sync.", ev.slotIndex);
            }
        }
    });
}

// Synced Trigger Implementations
void TriggerLevelUpSelectOption(const int64_t catUID, const uint32_t optionIndex) {
    glaiel::LevelUpScreen* screen = nullptr;
    const PersistentCharacter* cat = nullptr;

    for (const auto& [scr, c] : g_levelUpScreenToCat) {
        if (c && c->sql_key == catUID) {
            screen = scr;
            cat = c;
            break;
        }
    }

    if (!screen || !GameUtils::IsComponentValid(screen)) {
        screen = ParaboxAPI::GetActiveLevelUpScreen();
        if (screen && GameUtils::IsComponentValid(screen)) {
            cat = screen->catData;
        }
    }

    if (!screen || !GameUtils::IsComponentValid(screen)) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: target LevelUpScreen is null or invalid.");
        return;
    }

    if (!cat || cat->sql_key != catUID) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: active screen cat UID (%lld) does not match packet UID (%lld).",
                     cat ? cat->sql_key : -1, catUID);
        return;
    }

    if (!screen->options.Myfirst || !screen->options.Mylast) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: options vector is null.");
        return;
    }

    int count = static_cast<int>(screen->options.size());
    if (static_cast<int>(optionIndex) >= count) {
        Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: optionIndex %u out of bounds (%d).", optionIndex, count);
        return;
    }

    glaiel::LevelUpOption* optionPtr = &screen->options.Myfirst[optionIndex];
    Overlay::Log("[LEVELUP] Triggering synced select_option for index %u", optionIndex);

    ParaboxAPI::ForceLevelUpScreenSelectOption(screen, optionPtr);
}

void TriggerLevelUpReroll(const int64_t catUID) {
    glaiel::LevelUpScreen* screen = nullptr;
    const PersistentCharacter* cat = nullptr;

    for (const auto& [scr, c] : g_levelUpScreenToCat) {
        if (c && c->sql_key == catUID) {
            screen = scr;
            cat = c;
            break;
        }
    }

    if (!screen || !GameUtils::IsComponentValid(screen)) {
        screen = ParaboxAPI::GetActiveLevelUpScreen();
        if (screen && GameUtils::IsComponentValid(screen)) {
            cat = screen->catData;
        }
    }

    if (!screen || !GameUtils::IsComponentValid(screen)) {
        Overlay::Log("[LEVELUP] TriggerLevelUpReroll error: target LevelUpScreen is null or invalid.");
        return;
    }

    if (!cat || cat->sql_key != catUID) {
        Overlay::Log("[LEVELUP] TriggerLevelUpReroll error: active screen cat UID (%lld) does not match packet UID (%lld).",
                     cat ? cat->sql_key : -1, catUID);
        return;
    }

    Overlay::Log("[LEVELUP] Triggering synced Reroll for cat UID %lld.", catUID);
    ParaboxAPI::ForceLevelUpScreenReroll(screen);
}

void TriggerAbilityReplace(const int64_t catUID, const uint32_t slotIndex) {
    glaiel::AbilityChooser* chooser = ParaboxAPI::GetActiveAbilityChooser();
    if (!chooser || !GameUtils::IsComponentValid(chooser)) {
        Overlay::Log("[ABILITYCHOOSER] TriggerAbilityReplace error: g_activeAbilityChooser is null or invalid.");
        return;
    }

    const PersistentCharacter* cat = nullptr;
    const auto it = g_abilityChooserToCat.find(chooser);
    if (it != g_abilityChooserToCat.end()) {
        cat = it->second;
    }

    if (!cat || cat->sql_key != catUID) {
        Overlay::Log("[ABILITYCHOOSER] TriggerAbilityReplace error: active chooser cat UID (%lld) does not match packet UID (%lld).",
                     cat ? cat->sql_key : -1, catUID);
        return;
    }

    Overlay::Log("[ABILITYCHOOSER] Triggering synced select_slot for slot %u (or cancel 0xFFFFFFFF).", slotIndex);
    ParaboxAPI::ForceAbilityChooserSelectSlot(chooser, slotIndex);
}
