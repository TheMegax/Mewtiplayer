#include "hooks/LevelUpHooks.h"
#include "hooks/HookMacros.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "SteamABICompat.h"
#include "Scanner.h"
#include "ModState.h"
#include <map>

// Global caches
glaiel::LevelUpScreen* g_activeLevelUpScreen = nullptr;
glaiel::AbilityChooser* g_activeAbilityChooser = nullptr;

// Maps to track which cat belongs to which screen/chooser
static std::map<glaiel::LevelUpScreen*, PersistentCharacter*> g_levelUpScreenToCat;
static std::map<glaiel::AbilityChooser*, PersistentCharacter*> g_abilityChooserToCat;

// Synchronizing flags
static bool g_isSyncingSelectOption = false;
static bool g_isSyncingReroll = false;
static bool g_isSyncingAbilityReplace = false;

// Hooks
HOOK_DEFINE(LevelUpScreen_init, void, glaiel::LevelUpScreen* self, PersistentCharacter* cat, void* param3)
HOOK_DEFINE(LevelUpScreen_select_option, void, glaiel::LevelUpScreen* self, glaiel::LevelUpOption* option)
HOOK_DEFINE(LevelUpScreen_Reroll, void, glaiel::LevelUpScreen* self)
HOOK_DEFINE(AbilityChooser_init, void, glaiel::AbilityChooser* self, void* scene, PersistentCharacter* cat, int param4, void* param5, int param6, void* param7, void* param8, void* param9, char param10)
HOOK_DEFINE(AbilityChooser_select_slot, void, glaiel::AbilityChooser* self, uint32_t slotIndex)

static void __fastcall Hook_LevelUpScreen_init(glaiel::LevelUpScreen* self, PersistentCharacter* cat, void* param3) {
  g_activeLevelUpScreen = self;
  if (cat) {
    g_levelUpScreenToCat[self] = cat;
    Overlay::Log("[LEVELUP] LevelUpScreen init: %p, Cat UID: %lld", self, cat->sql_key);
  } else {
    Overlay::Log("[LEVELUP] LevelUpScreen init: %p, Cat is NULL!", self);
  }

  if (g_origLevelUpScreen_init) {
    g_origLevelUpScreen_init(self, cat, param3);
  }
}

static void __fastcall Hook_LevelUpScreen_select_option(glaiel::LevelUpScreen* self, glaiel::LevelUpOption* option) {
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_isSyncingSelectOption) {
      if (g_origLevelUpScreen_select_option) {
        g_origLevelUpScreen_select_option(self, option);
      }
      return;
    }

    const PersistentCharacter* cat = nullptr;
    const auto it = g_levelUpScreenToCat.find(self);
    if (it != g_levelUpScreenToCat.end()) {
      cat = it->second;
    }

    if (cat) {
      const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
      const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
      if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
        Overlay::Log("[LEVELUP] Blocked select_option for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
        return; // Block input!
      }

      // We are the owner! We need to find the optionIndex.
      int optionIndex = -1;
      if (self->options.Myfirst && self->options.Mylast && option) {
        int count = static_cast<int>(self->options.size());
        for (int i = 0; i < count; i++) {
          const glaiel::LevelUpOption& item = self->options.Myfirst[i];
          if (item.type == option->type && item.optionKey.as_native_string_view() == option->optionKey.as_native_string_view()) {
            optionIndex = i;
            break;
          }
        }
      }

      if (optionIndex != -1) {
        LevelUpSelectOptionPacket pkt = {};
        pkt.catUID = cat->sql_key;
        pkt.optionIndex = optionIndex;
        NetworkManager::Get().BroadcastPacket(PacketType::LevelUpSelectOption, &pkt, sizeof(pkt), true);
        Overlay::Log("[LEVELUP] Owner selected option %d. Broadcasting sync.", optionIndex);
      } else {
        Overlay::Log("[LEVELUP] [WARN] Failed to find optionIndex for select_option!");
      }
    }
  }

  if (g_origLevelUpScreen_select_option) {
    g_origLevelUpScreen_select_option(self, option);
  }
}

static void __fastcall Hook_LevelUpScreen_Reroll(glaiel::LevelUpScreen* self) {
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_isSyncingReroll) {
      if (g_origLevelUpScreen_Reroll) {
        g_origLevelUpScreen_Reroll(self);
      }
      return;
    }

    if (const PersistentCharacter* cat = self->catData) {
      const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
      const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
      if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
        Overlay::Log("[LEVELUP] Blocked Reroll for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
        return; // Block input!
      }

      LevelUpRerollPacket pkt = {};
      pkt.catUID = cat->sql_key;
      NetworkManager::Get().BroadcastPacket(PacketType::LevelUpReroll, &pkt, sizeof(pkt), true);
      Overlay::Log("[LEVELUP] Owner requested Reroll. Broadcasting sync.");
    }
  }

  if (g_origLevelUpScreen_Reroll) {
    g_origLevelUpScreen_Reroll(self);
  }
}

static void __fastcall Hook_AbilityChooser_init(glaiel::AbilityChooser* self, void* scene, PersistentCharacter* cat, int param4, void* param5, int param6, void* param7, void* param8, void* param9, char param10) {
  g_activeAbilityChooser = self;
  if (cat) {
    g_abilityChooserToCat[self] = cat;
    Overlay::Log("[ABILITYCHOOSER] AbilityChooser init: %p, Cat UID: %lld", self, cat->sql_key);
  } else {
    Overlay::Log("[ABILITYCHOOSER] AbilityChooser init: %p, Cat is NULL!", self);
  }

  if (g_origAbilityChooser_init) {
    g_origAbilityChooser_init(self, scene, cat, param4, param5, param6, param7, param8, param9, param10);
  }
}

static void __fastcall Hook_AbilityChooser_select_slot(glaiel::AbilityChooser* self, uint32_t slotIndex) {
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_isSyncingAbilityReplace) {
      if (g_origAbilityChooser_select_slot) {
        g_origAbilityChooser_select_slot(self, slotIndex);
      }
      return;
    }

    PersistentCharacter* cat = nullptr;
    auto it = g_abilityChooserToCat.find(self);
    if (it != g_abilityChooserToCat.end()) {
      cat = it->second;
    }

    if (cat) {
      const uint64_t ownerSteamID = NetworkManager::Get().GetCatOwner(cat->sql_key);
      const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
      if (ownerSteamID != 0 && ownerSteamID != localSteamID) {
        Overlay::Log("[ABILITYCHOOSER] Blocked select_slot/cancel for non-owner. Owner: %llu, Local: %llu", ownerSteamID, localSteamID);
        return; // Block input!
      }

      AbilityReplacePacket pkt = {};
      pkt.catUID = cat->sql_key;
      pkt.slotIndex = slotIndex;
      NetworkManager::Get().BroadcastPacket(PacketType::AbilityReplace, &pkt, sizeof(pkt), true);
      Overlay::Log("[ABILITYCHOOSER] Owner selected slot %u (or cancel/skip 0xFFFFFFFF). Broadcasting sync.", slotIndex);
    }
  }

  if (g_origAbilityChooser_select_slot) {
    g_origAbilityChooser_select_slot(self, slotIndex);
  }
}

// Synced Trigger Implementations
void TriggerLevelUpSelectOption(const int64_t catUID, const uint32_t optionIndex) {
  if (!g_activeLevelUpScreen || !GameUtils::IsComponentValid(g_activeLevelUpScreen)) {
    Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: g_activeLevelUpScreen is null or invalid.");
    return;
  }

  PersistentCharacter* cat = g_activeLevelUpScreen->catData;
  if (!cat || cat->sql_key != catUID) {
    Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: active screen cat UID (%lld) does not match packet UID (%lld).",
                 cat ? cat->sql_key : -1, catUID);
    return;
  }

  if (!g_activeLevelUpScreen->options.Myfirst || !g_activeLevelUpScreen->options.Mylast) {
    Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: options vector is null.");
    return;
  }

  int count = static_cast<int>(g_activeLevelUpScreen->options.size());
  if (static_cast<int>(optionIndex) >= count) {
    Overlay::Log("[LEVELUP] TriggerLevelUpSelectOption error: optionIndex %u out of bounds (%d).", optionIndex, count);
    return;
  }

  glaiel::LevelUpOption* optionPtr = &g_activeLevelUpScreen->options.Myfirst[optionIndex];
  Overlay::Log("[LEVELUP] Triggering synced select_option for index %u", optionIndex);

  g_isSyncingSelectOption = true;
  if (g_origLevelUpScreen_select_option) {
    g_origLevelUpScreen_select_option(g_activeLevelUpScreen, optionPtr);
  }
  g_isSyncingSelectOption = false;
}

void TriggerLevelUpReroll(const int64_t catUID) {
  if (!g_activeLevelUpScreen || !GameUtils::IsComponentValid(g_activeLevelUpScreen)) {
    Overlay::Log("[LEVELUP] TriggerLevelUpReroll error: g_activeLevelUpScreen is null or invalid.");
    return;
  }

  const PersistentCharacter* cat = g_activeLevelUpScreen->catData;
  if (!cat || cat->sql_key != catUID) {
    Overlay::Log("[LEVELUP] TriggerLevelUpReroll error: active screen cat UID (%lld) does not match packet UID (%lld).",
                 cat ? cat->sql_key : -1, catUID);
    return;
  }

  Overlay::Log("[LEVELUP] Triggering synced Reroll.");

  g_isSyncingReroll = true;
  if (g_origLevelUpScreen_Reroll) {
    g_origLevelUpScreen_Reroll(g_activeLevelUpScreen);
  }
  g_isSyncingReroll = false;
}

void TriggerAbilityReplace(const int64_t catUID, const uint32_t slotIndex) {
  if (!g_activeAbilityChooser || !GameUtils::IsComponentValid(g_activeAbilityChooser)) {
    Overlay::Log("[ABILITYCHOOSER] TriggerAbilityReplace error: g_activeAbilityChooser is null or invalid.");
    return;
  }

  const PersistentCharacter* cat = nullptr;
  const auto it = g_abilityChooserToCat.find(g_activeAbilityChooser);
  if (it != g_abilityChooserToCat.end()) {
    cat = it->second;
  }

  if (!cat || cat->sql_key != catUID) {
    Overlay::Log("[ABILITYCHOOSER] TriggerAbilityReplace error: active chooser cat UID (%lld) does not match packet UID (%lld).",
                 cat ? cat->sql_key : -1, catUID);
    return;
  }

  Overlay::Log("[ABILITYCHOOSER] Triggering synced select_slot for slot %u (or cancel 0xFFFFFFFF).", slotIndex);

  g_isSyncingAbilityReplace = true;
  if (g_origAbilityChooser_select_slot) {
    g_origAbilityChooser_select_slot(g_activeAbilityChooser, slotIndex);
  }
  g_isSyncingAbilityReplace = false;
}

void LevelUpHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, LevelUpScreen_init,
    "48 8b c4 4c 89 40 18 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8d a8 68 f9 ff ff 48 81",
    0);

  HOOK_INSTALL(mj, gameBase, LevelUpScreen_select_option,
    "48 89 5c 24 20 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 70 ff ff ff 48 81 ec",
    0);

  HOOK_INSTALL(mj, gameBase, LevelUpScreen_Reroll,
    "48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55 41 54 41 55 41 56 41 57 48 8d 6c 24 c9 48 81 ec",
    0);

  HOOK_INSTALL(mj, gameBase, AbilityChooser_init,
    "48 89 5c 24 20 4c 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 50 fe ff ff 48 81 ec",
    0);

  HOOK_INSTALL(mj, gameBase, AbilityChooser_select_slot,
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 20 48 8b 41 40 48 8b f9 33 db 8b ea 48",
    0);
}
