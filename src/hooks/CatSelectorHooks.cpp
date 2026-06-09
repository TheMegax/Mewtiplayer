#include "hooks/CatSelectorHooks.h"
#include "hooks/SaveHooks.h"
#include "hooks/HookMacros.h"
#include "hooks/ModState.h"
#include "GameUtils.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "Scanner.h"
#include "SteamABICompat.h"
#include <cstring>

struct ClassTagBox : Component  {
  void *classChooser;                     // 0x38
  [[maybe_unused]] char _padding_0[0x18]; // 0x40
  MsvcReleaseModeXString boxName;         // 0x58
  [[maybe_unused]] char _padding_1[0x18]; // 0x78
  int64_t catID;                          // 0x90
};

struct ClassChooser : Component {
  [[maybe_unused]] char _padding_0[0x64]; // 0x38
  uint32_t numTagBoxes;                   // 0x9c
  ClassTagBox **tagBoxes;                 // 0xa0
  [[maybe_unused]] char _padding_1[0x28]; // 0xa8
  int64_t catID;                          // 0xd0
};

std::map<uint64_t, bool> g_lobbyReadyStates;
bool g_localReady = false;
void *g_activeClassChooserLambdaThis = nullptr;
void *g_activeCatSelector = nullptr;

typedef void *(__fastcall *LookupPersistentCharacter_t)(void *pedigreeState, int64_t catID);
typedef void (__fastcall *RefreshCatSelectorUI_t)(void *selector);
typedef void (__fastcall *ApplyCollar_t)(PersistentCharacter *cat, void *collarNameStr);
typedef void (__fastcall *RefreshInventoryGrid_t)(void *classChooser);

static LookupPersistentCharacter_t g_LookupPersistentCharacter = nullptr;
static RefreshCatSelectorUI_t g_RefreshCatSelectorUI = nullptr;
static ApplyCollar_t g_ApplyCollar = nullptr;
static RefreshInventoryGrid_t g_RefreshInventoryGrid = nullptr;

HOOK_DEFINE(CatSelector_init, void, void *, int64_t)
HOOK_DEFINE(ClassTagBox_Click, void, void *)
HOOK_DEFINE(ClassChooser_LockIn, void, void *)

static int64_t ResolveCatIDFromTagBox(void *tagBox) {
  const auto *box = static_cast<ClassTagBox *>(tagBox);
  int64_t catID = box->catID;
  if (catID == -1) {
    if (const auto *classChooser = static_cast<ClassChooser *>(box->classChooser)) {
      catID = classChooser->catID;
    }
  }
  return catID;
}

static void __fastcall Hook_CatSelector_init(void *self, const int64_t param2) {
  g_activeCatSelector = self;
  if (g_origCatSelector_init) {
    g_origCatSelector_init(self, param2);
  }
}

static void __fastcall Hook_ClassTagBox_Click(void *tagBox) {
  if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
    const int64_t catID = ResolveCatIDFromTagBox(tagBox);
    if (catID != -1) {
      const auto ownerIt = g_catIdToOwnerSteamID.find(catID);
      if (ownerIt != g_catIdToOwnerSteamID.end() && ownerIt->second != 0) {
        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        if (ownerIt->second != localSteamID) { // Block clicking the collar of other players
          return;
        }
      }
    }
  }

  if (g_origClassTagBox_Click) {
    g_origClassTagBox_Click(tagBox);
  }

  if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
    return;
  }

  const int64_t catID = ResolveCatIDFromTagBox(tagBox);
  if (catID == -1 || !g_LookupPersistentCharacter) {
    return;
  }

  const MewDirector *director = GameUtils::GetMewDirectorSingleton();
  void *pedigreeState = director ? director->pedigreeState : nullptr;
  if (!pedigreeState) {
    return;
  }

  const auto *cat = (PersistentCharacter *)g_LookupPersistentCharacter(pedigreeState, catID);
  if (!cat) {
    return;
  }

  CollarSyncPacket packet = {};
  packet.catID = catID;
  const char *className = cat->className.is_valid() ? cat->className.begin() : "Colorless";
  strncpy(packet.collarName, className, sizeof(packet.collarName) - 1);
  packet.collarName[sizeof(packet.collarName) - 1] = '\0';

  NetworkManager::Get().BroadcastPacket(PacketType::CollarSync, &packet, sizeof(packet), false);
  Overlay::Log("[LOBBY] Broadcast collar sync for cat %lld: %s", catID, packet.collarName);
}

static void __fastcall Hook_ClassChooser_LockIn(void *lambdaThis) {
  if (!NetworkManager::Get().GetCurrentLobby().IsValid()) {
    if (g_origClassChooser_LockIn) {
      g_origClassChooser_LockIn(lambdaThis);
    }
    return;
  }

  g_activeClassChooserLambdaThis = lambdaThis;
  g_localReady = !g_localReady;

  const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
  g_lobbyReadyStates[localSteamID] = g_localReady;

  LobbyReadyPacket packet = {};
  packet.steamID = localSteamID;
  packet.isReady = g_localReady;
  NetworkManager::Get().BroadcastPacket(PacketType::LobbyReady, &packet, sizeof(packet), false);
  Overlay::Log("[LOBBY] Local ready state: %s", g_localReady ? "locked in" : "not ready");

  if (NetworkManager::Get().IsHost() && AreAllLobbyMembersReady()) {
    NetworkManager::Get().BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
    if (g_origClassChooser_LockIn) {
      g_origClassChooser_LockIn(lambdaThis);
    }
  }
}

bool IsCatSelectorValid(const void *selector) {
  if (!selector) {
    return false;
  }

  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) {
      continue;
    }
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      if (comp == selector && !comp->deleted) {
        return true;
      }
    }
  }
  return false;
}

bool AreAllLobbyMembersReady() {
  const CSteamID lobby = NetworkManager::Get().GetCurrentLobby();
  if (!lobby.IsValid()) {
    return false;
  }

  const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(lobby);
  if (numMembers <= 0) {
    return false;
  }

  for (int i = 0; i < numMembers; i++) {
    const uint64_t memberID =
        SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i).ConvertToUint64();
    const auto it = g_lobbyReadyStates.find(memberID);
    if (it == g_lobbyReadyStates.end() || !it->second) {
      return false;
    }
  }
  return true;
}

void RefreshCatSelectorUI() {
  if (g_RefreshCatSelectorUI && IsCatSelectorValid(g_activeCatSelector)) {
    g_RefreshCatSelectorUI(g_activeCatSelector);
  }
}

PersistentCharacter *GetPersistentCharacterById(const int64_t catID) {
  if (!g_LookupPersistentCharacter || catID == -1) {
    return nullptr;
  }

  const MewDirector *director = GameUtils::GetMewDirectorSingleton();
  void *pedigreeState = director ? director->pedigreeState : nullptr;
  if (!pedigreeState) {
    return nullptr;
  }

  return (PersistentCharacter *)g_LookupPersistentCharacter(pedigreeState, catID);
}

void ResetLobbyReadyStates() {
  g_lobbyReadyStates.clear();
  g_localReady = false;
  g_activeClassChooserLambdaThis = nullptr;
  g_activeCatSelector = nullptr;
}

void CatSelectorHooks_TriggerLockInProceed() {
  if (g_origClassChooser_LockIn && g_activeClassChooserLambdaThis) {
    g_origClassChooser_LockIn(g_activeClassChooserLambdaThis);
  }
}

void ApplyCollarToCharacter(PersistentCharacter *cat, const char *collarName) {
  if (!g_ApplyCollar || !cat || !collarName) {
    return;
  }
  MsvcReleaseModeXString nameStr = {};
  GameUtils::InitXString(nameStr, collarName);
  g_ApplyCollar(cat, &nameStr);
  GameUtils::FreeXString(nameStr);
}

void RefreshClassChooserInventory() {
  if (!g_RefreshInventoryGrid) {
    return;
  }
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "ClassChooser";
        GameUtils::FreeXString(name);
        if (match) {
          g_RefreshInventoryGrid(const_cast<void *>(reinterpret_cast<const void *>(comp)));
          return;
        }
      }
    }
  }
}

void UpdateClassChooserTagBoxes(const int64_t catID, const char *collarName) {
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "ClassChooser";
        GameUtils::FreeXString(name);
        if (!match) continue;
      } else {
        continue;
      }

      const auto *classChooser = static_cast<ClassChooser *>(const_cast<Component *>(comp)); // NOLINT(*-pro-type-static-cast-downcast)
      if (!classChooser->tagBoxes || classChooser->numTagBoxes == 0) continue;

      // ReSharper disable once CppTooWideScope
      const bool isEquip = collarName[0] != '\0' && strcmp(collarName, "Colorless") != 0;

      if (isEquip) {
        for (uint32_t i = 0; i < classChooser->numTagBoxes; i++) {
          ClassTagBox *box = classChooser->tagBoxes[i];
          const char *boxNameStr = box->boxName.is_valid() ? box->boxName.begin() : "";
          if (strcmp(boxNameStr, collarName) != 0) {
            continue;
          }
          for (uint32_t j = 0; j < classChooser->numTagBoxes; j++) {
            ClassTagBox *otherBox = classChooser->tagBoxes[j];
            if (otherBox->catID == catID) {
              otherBox->catID = -1;
            }
          }
          box->catID = catID;
          break;
        }
      } else {
        for (uint32_t i = 0; i < classChooser->numTagBoxes; i++) {
          ClassTagBox *box = classChooser->tagBoxes[i];
          if (box->catID != catID) {
            continue;
          }
          box->catID = -1;
          break;
        }
      }
      return;
    }
  }
}

void HandleCollarSyncInternal(const void *data, const uint32_t length) {
  if (length != sizeof(CollarSyncPacket)) {
    return;
  }

  const auto *packet = (const CollarSyncPacket *)data;
  PersistentCharacter *cat = GetPersistentCharacterById(packet->catID);
  if (!cat) {
    Overlay::Log("[LOBBY] CollarSync: cat %lld not found", packet->catID);
    return;
  }

  ApplyCollarToCharacter(cat, packet->collarName);
  Overlay::Log("[LOBBY] CollarSync: updated cat %lld to %s", packet->catID, packet->collarName);

  UpdateClassChooserTagBoxes(packet->catID, packet->collarName);
  RefreshClassChooserInventory();
  RefreshCatSelectorUI();
}

void CatSelectorHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  SCAN_SET(mj, gameBase, RefreshInventoryGrid,
           "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 FF FF FF",
           g_RefreshInventoryGrid);

  SCAN_SET(mj, gameBase, ApplyCollar,
           "48 89 5C 24 18 48 89 54 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC A0 00 00 00",
           g_ApplyCollar);

  SCAN_SET(mj, gameBase, LookupPersistentCharacter,
           "48 89 5C 24 08 48 89 74 24 20 48 89 54 24 10 57 48 83 EC 40 4C 8B C2 48 8B F9 48 83 FA FF 0F 84",
           g_LookupPersistentCharacter);

  SCAN_SET(mj, gameBase, RefreshCatSelectorUI,
           "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 48 FF FF FF 48 81 EC B8 01 00 00 48 8B D9",
           g_RefreshCatSelectorUI);

  HOOK_INSTALL(mj, gameBase, CatSelector_init,
      "48 8B C4 48 89 50 10 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 98 FE FF FF 48 81 EC 28 02 00 00 0F 29 70 A8 0F 29 78 98",
      0);

  HOOK_INSTALL(mj, gameBase, ClassTagBox_Click,
      "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 38 FF FF FF",
      0);

  HOOK_INSTALL(mj, gameBase, ClassChooser_LockIn,
      "40 53 55 56 57 41 54 41 56 41 57 48 81 EC C0 00 00 00",
      0);
}