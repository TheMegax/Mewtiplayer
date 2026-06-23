// ReSharper disable CppDFALocalValueEscapesFunction
#include "hooks/WorldEventHooks.h"
#include "hooks/HookMacros.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "SteamABICompat.h"
#include "Scanner.h"
#include "ModState.h"
#include <vector>

// Cache for the active event component
glaiel::WorldEvent* g_activeWorldEvent = nullptr;

// Synchronization flags/overrides
static bool g_isSyncingSelectOption = false;
static bool g_isSyncingClickCat = false;
static bool g_isSyncingClickEnd = false;
PersistentCharacter* g_overrideSelectedCat = nullptr;

// Hook definitions
HOOK_DEFINE(WorldEvent_init, void, glaiel::WorldEvent* self, void* param2, void* param3)
HOOK_DEFINE(WorldEvent_ClickOption, void, glaiel::WorldEventClickEvent* param_1)
HOOK_DEFINE(WorldEvent_ClickCat, void, glaiel::WorldEventClickEvent* param_1)
HOOK_DEFINE(GetSelectedCat, PersistentCharacter*, void* param_1, uint64_t param_2)
HOOK_DEFINE(WorldEvent_ClickEnd1, void, glaiel::WorldEventClickEvent* param_1)
HOOK_DEFINE(WorldEvent_ClickEnd2, void, glaiel::WorldEventClickEvent* param_1)

// Helper to find a cat in the party by UID
static PersistentCharacter* FindCatInParty(int64_t catUID) {
  MewDirector* director = GameUtils::GetMewDirectorSingleton();
  if (director && director->partyData) {
    for (int i = 0; i < director->partyCount; ++i) {
      PersistentCharacter* cat = director->partyData[i];
      if (cat && cat->sql_key == catUID) {
        return cat;
      }
    }
  }
  return nullptr;
}

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

static void __fastcall Hook_WorldEvent_init(glaiel::WorldEvent* self, void* param2, void* param3) {
  g_activeWorldEvent = self;
  Overlay::Log("[WORLDEVENT] WorldEvent init: %p", self);
  if (g_origWorldEvent_init) {
    g_origWorldEvent_init(self, param2, param3);
  }
}

static void __fastcall Hook_WorldEvent_ClickOption(glaiel::WorldEventClickEvent* param_1) {
  glaiel::WorldEvent* worldEvent = param_1->worldEvent;
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_isSyncingSelectOption) {
      if (g_origWorldEvent_ClickOption) {
        g_origWorldEvent_ClickOption(param_1);
      }
      return;
    }

    if (!CanInteract(worldEvent)) {
      Overlay::Log("[WORLDEVENT] Blocked ClickOption for non-owner/non-host.");
      return;
    }

    // Local player is the owner/host. Broadcast selection!
    auto* clickedOption = static_cast<glaiel::WorldEventOption*>(param_1->sender);
    glaiel::WorldEventOption* vectorStart = worldEvent->options.Myfirst;
    glaiel::WorldEventOption* vectorEnd = worldEvent->options.Mylast;
    int optionIndex = -1;
    if (vectorStart && vectorEnd && clickedOption) {
      intptr_t diff = clickedOption - vectorStart;
      int count = static_cast<int>(worldEvent->options.size());
      if (diff >= 0 && diff < count) {
        optionIndex = static_cast<int>(diff);
      }
    }

    PersistentCharacter* activeCat = worldEvent->activeCat;
    if (optionIndex != -1 && activeCat) {
      WorldEventSelectOptionPacket pkt = {};
      pkt.catUID = activeCat->sql_key;
      pkt.optionIndex = static_cast<uint32_t>(optionIndex);
      NetworkManager::Get().BroadcastPacket(PacketType::WorldEventSelectOption, &pkt, sizeof(pkt), true);
      Overlay::Log("[WORLDEVENT] Local owner selected option %d. Broadcasting sync.", optionIndex);
    } else {
      Overlay::Log("[WORLDEVENT] [ERR] ClickOption index could not be resolved! optionIndex: %d, activeCat: %p, clickedOption: %p, vectorStart: %p, vectorEnd: %p",
                   optionIndex, activeCat, clickedOption, vectorStart, vectorEnd);
    }
  }

  if (g_origWorldEvent_ClickOption) {
    g_origWorldEvent_ClickOption(param_1);
  }
}

static void __fastcall Hook_WorldEvent_ClickCat(glaiel::WorldEventClickEvent* param_1) {
  glaiel::WorldEvent* worldEvent = param_1->worldEvent;
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_isSyncingClickCat) {
      if (g_origWorldEvent_ClickCat) {
        g_origWorldEvent_ClickCat(param_1);
      }
      return;
    }

    if (!CanInteract(worldEvent)) {
      Overlay::Log("[WORLDEVENT] Blocked ClickCat for non-owner/non-host.");
      return;
    }

    // Resolve the clicked cat
    auto* clickedButton = static_cast<glaiel::WorldEventCatButton*>(param_1->sender);
    void* catManager = clickedButton->catManager;
    glaiel::WorldEventOptionState* statePtr = worldEvent->optionState;
    uint64_t catUID = 0;
    if (statePtr) {
      catUID = statePtr->catUID;
    }

    PersistentCharacter* clickedCat = nullptr;
    if (g_origGetSelectedCat && catManager && catUID != 0) {
      clickedCat = g_origGetSelectedCat(catManager, catUID);
    }

    if (clickedCat) {
      WorldEventSelectCatPacket pkt = {};
      pkt.selectedCatUID = clickedCat->sql_key;
      NetworkManager::Get().BroadcastPacket(PacketType::WorldEventSelectCat, &pkt, sizeof(pkt), true);
      Overlay::Log("[WORLDEVENT] Local owner clicked cat. Syncing selectedCatUID %lld.", clickedCat->sql_key);
    }
  }

  if (g_origWorldEvent_ClickCat) {
    g_origWorldEvent_ClickCat(param_1);
  }
}

static PersistentCharacter* __fastcall Hook_GetSelectedCat(void* param_1, uint64_t param_2) {
  if (g_overrideSelectedCat) {
    return g_overrideSelectedCat;
  }
  if (g_origGetSelectedCat) {
    return g_origGetSelectedCat(param_1, param_2);
  }
  return nullptr;
}

static void __fastcall Hook_WorldEvent_ClickEnd1(glaiel::WorldEventClickEvent* param_1) {
  glaiel::WorldEvent* worldEvent = param_1->worldEvent;
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_isSyncingClickEnd) {
      if (g_origWorldEvent_ClickEnd1) {
        g_origWorldEvent_ClickEnd1(param_1);
      }
      return;
    }

    if (!CanInteract(worldEvent)) {
      Overlay::Log("[WORLDEVENT] Blocked ClickEnd1 for non-owner/non-host.");
      return;
    }

    WorldEventClickEndPacket pkt = {};
    pkt.buttonType = 1;
    NetworkManager::Get().BroadcastPacket(PacketType::WorldEventClickEnd, &pkt, sizeof(pkt), true);
    Overlay::Log("[WORLDEVENT] Local owner clicked End1. Broadcasting sync.");
  }

  if (g_origWorldEvent_ClickEnd1) {
    g_origWorldEvent_ClickEnd1(param_1);
  }
}

static void __fastcall Hook_WorldEvent_ClickEnd2(glaiel::WorldEventClickEvent* param_1) {
  glaiel::WorldEvent* worldEvent = param_1->worldEvent;
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_isSyncingClickEnd) {
      if (g_origWorldEvent_ClickEnd2) {
        g_origWorldEvent_ClickEnd2(param_1);
      }
      return;
    }

    if (!CanInteract(worldEvent)) {
      Overlay::Log("[WORLDEVENT] Blocked ClickEnd2 for non-owner/non-host.");
      return;
    }

    WorldEventClickEndPacket pkt = {};
    pkt.buttonType = 2;
    NetworkManager::Get().BroadcastPacket(PacketType::WorldEventClickEnd, &pkt, sizeof(pkt), true);
    Overlay::Log("[WORLDEVENT] Local owner clicked End2. Broadcasting sync.");
  }

  if (g_origWorldEvent_ClickEnd2) {
    g_origWorldEvent_ClickEnd2(param_1);
  }
}

// Helpers for finding buttons
static Component* FindButtonForCat(const PersistentCharacter* targetCat) {
  if (!targetCat || !g_activeWorldEvent) return nullptr;
  Scene* scene = g_activeWorldEvent->scene;
  if (!scene) return nullptr;

  std::vector<Component*> components = GameUtils::GetSceneComponents(scene);
  for (Component* comp : components) {
    if (!GameUtils::IsComponentValid(comp)) continue;

    auto* btn = reinterpret_cast<glaiel::WorldEventCatButton*>(comp);
    void* catManager = btn->catManager;
    void* otherPtr = btn->otherPtr;
    if (catManager && otherPtr) {
      if (g_origGetSelectedCat) {
        PersistentCharacter* cat = g_origGetSelectedCat(catManager, targetCat->sql_key);
        if (cat && cat->sql_key == targetCat->sql_key) {
          return comp;
        }
      }
    }
  }
  return nullptr;
}

static Component* FindAnyCatButton() {
  if (!g_activeWorldEvent) return nullptr;
  Scene* scene = g_activeWorldEvent->scene;
  if (!scene) return nullptr;

  std::vector<Component*> components = GameUtils::GetSceneComponents(scene);
  for (Component* comp : components) {
    if (!GameUtils::IsComponentValid(comp)) continue;

    auto* btn = reinterpret_cast<glaiel::WorldEventCatButton*>(comp);
    void* catManager = btn->catManager;
    void* otherPtr = btn->otherPtr;
    if (catManager && otherPtr) {
      return comp;
    }
  }
  return nullptr;
}

// Trigger functions called from network thread
void TriggerWorldEventSelectOption(int64_t catUID, uint32_t optionIndex) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: g_activeWorldEvent is null or invalid.");
    return;
  }

  PersistentCharacter* activeCat = g_activeWorldEvent->activeCat;
  if (!activeCat || activeCat->sql_key != catUID) {
    Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: active screen cat UID (%lld) does not match packet UID (%lld).",
                 activeCat ? activeCat->sql_key : -1, catUID);
    return;
  }

  glaiel::WorldEventOption* vectorStart = g_activeWorldEvent->options.Myfirst;
  glaiel::WorldEventOption* vectorEnd = g_activeWorldEvent->options.Mylast;
  if (!vectorStart || !vectorEnd) {
    Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: options vector is null.");
    return;
  }

  int count = static_cast<int>(g_activeWorldEvent->options.size());
  if (static_cast<int>(optionIndex) >= count) {
    Overlay::Log("[WORLDEVENT] [ERR] TriggerWorldEventSelectOption: optionIndex %u out of bounds (%d).", optionIndex, count);
    return;
  }

  glaiel::WorldEventOption* optionPtr = vectorStart + optionIndex;
  Overlay::Log("[WORLDEVENT] Triggering synced ClickOption for index %u", optionIndex);

  glaiel::WorldEventClickEvent dummy = {};
  dummy.vtable = nullptr;
  dummy.worldEvent = g_activeWorldEvent;
  dummy.sender = optionPtr;

  g_isSyncingSelectOption = true;
  if (g_origWorldEvent_ClickOption) {
    g_origWorldEvent_ClickOption(&dummy);
  }
  g_isSyncingSelectOption = false;
}

void TriggerWorldEventSelectCat(int64_t selectedCatUID) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    Overlay::Log("[WORLDEVENT] TriggerWorldEventSelectCat error: g_activeWorldEvent is null or invalid.");
    return;
  }

  PersistentCharacter* targetCat = FindCatInParty(selectedCatUID);
  if (!targetCat) {
    Overlay::Log("[WORLDEVENT] TriggerWorldEventSelectCat error: could not find cat in party with UID %lld.", selectedCatUID);
    return;
  }

  Component* buttonComp = FindButtonForCat(targetCat);
  if (!buttonComp) {
    buttonComp = FindAnyCatButton();
    Overlay::Log("[WORLDEVENT] TriggerWorldEventSelectCat: exact button not found, using fallback button %p.", buttonComp);
  }

  if (!buttonComp) {
    Overlay::Log("[WORLDEVENT] TriggerWorldEventSelectCat error: no button component found.");
    return;
  }

  // Set the override state so that GetSelectedCat returns the target cat
  g_overrideSelectedCat = targetCat;

  glaiel::WorldEventOptionState dummyState = {};
  memset(&dummyState, 0, sizeof(dummyState));
  dummyState.catUID = selectedCatUID;

  glaiel::WorldEventOptionState* oldState = g_activeWorldEvent->optionState;
  g_activeWorldEvent->optionState = &dummyState;

  glaiel::WorldEventClickEvent dummyClick = {};
  dummyClick.vtable = nullptr;
  dummyClick.worldEvent = g_activeWorldEvent;
  dummyClick.sender = buttonComp;

  g_isSyncingClickCat = true;
  Overlay::Log("[WORLDEVENT] Triggering synced ClickCat for UID %lld.", selectedCatUID);
  if (g_origWorldEvent_ClickCat) {
    g_origWorldEvent_ClickCat(&dummyClick);
  }
  g_isSyncingClickCat = false;

  g_activeWorldEvent->optionState = oldState;
  g_overrideSelectedCat = nullptr;
}

void TriggerWorldEventClickEnd(uint8_t buttonType) {
  if (!g_activeWorldEvent || !GameUtils::IsComponentValid(g_activeWorldEvent)) {
    Overlay::Log("[WORLDEVENT] TriggerWorldEventClickEnd error: g_activeWorldEvent is null or invalid.");
    return;
  }

  glaiel::WorldEventClickEvent dummy = {};
  dummy.vtable = nullptr;
  dummy.worldEvent = g_activeWorldEvent;
  dummy.sender = nullptr;

  g_isSyncingClickEnd = true;
  if (buttonType == 1) {
    Overlay::Log("[WORLDEVENT] Triggering synced ClickEnd1.");
    if (g_origWorldEvent_ClickEnd1) {
      g_origWorldEvent_ClickEnd1(&dummy);
    }
  } else if (buttonType == 2) {
    Overlay::Log("[WORLDEVENT] Triggering synced ClickEnd2.");
    if (g_origWorldEvent_ClickEnd2) {
      g_origWorldEvent_ClickEnd2(&dummy);
    }
  }
  g_isSyncingClickEnd = false;
}

void WorldEventHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
  HOOK_INSTALL(mj, gameBase, WorldEvent_init,
    "48 8b c4 48 89 50 10 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8d a8 f8 fd ff ff 48 81 ec c8 02 00 00",
    23);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickOption,
    "48 8B 05 ?? ?? ?? ?? C6 80 14 07 00 00 01 48 8B 51 08 48 8B 41 10 48 89",
    14);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickCat,
    "48 89 5C 24 10 57 48 83 EC 40 48 8B 41 10 48 8B D9 48 8B 88 98 05 00 00",
    14);

  HOOK_INSTALL(mj, gameBase, GetSelectedCat,
    "48 89 5C 24 08 48 89 74 24 20 48 89 54 24 10 57 48 83 EC 40 4C 8B C2 48",
    15);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickEnd1,
    "40 53 48 83 EC 40 48 8B 51 08 48 8B D9 80 BA F0 19 00 00 00 75 49 48 8B 05 ?? ?? ?? ?? 80 78 38 00 0F 85 C7 00 00 00 48 8B 40 18 48 89 54 24 50 48 8B 58 08 80 BB B0 04 00 00 00 0F 85 AD 00 00 00 48 8B CB E8 B7 A5 02 00",
    20);

  HOOK_INSTALL(mj, gameBase, WorldEvent_ClickEnd2,
    "40 53 48 83 EC 40 48 8B 51 08 48 8B D9 80 BA F0 19 00 00 00 75 49 48 8B 05 ?? ?? ?? ?? 80 78 38 00 0F 85 C7 00 00 00 48 8B 40 18 48 89 54 24 50 48 8B 58 08 80 BB B0 04 00 00 00 0F 85 AD 00 00 00 48 8B CB E8 E7 A6 02 00",
    20);
}
