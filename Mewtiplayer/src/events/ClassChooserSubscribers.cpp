#include "events/ClassChooserSubscribers.h"
#include "events/SaveSubscribers.h"
#include "events/StorageSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "mew_ui_api.h"
#include "GameUtils.h"
#include <windows.h>

// ---------------------------------------------------------------------------
// ClassChooserHooks Subscribers and UI state
// ---------------------------------------------------------------------------

std::map<uint64_t, bool> g_lobbyReadyStates;
bool g_localReady = false;
void *g_activeClassChooserLambdaThis = nullptr;
bool g_hasTriggeredProceed = false;

static MewUISceneBinding g_classChooserScene;
static void* g_lockInButton = nullptr;
static bool g_classChooserSceneInitialized = false;
static bool g_lastLocalReadyState = false;
static bool g_buttonHooked = false;

bool AreAllLobbyMembersReady() {
    const CSteamID lobby = NetworkManager::Get().GetCurrentLobby();
    if (!lobby.IsValid()) return false;

    const int numMembers = SteamMatchmaking()->GetNumLobbyMembers(lobby);
    if (numMembers <= 0) return false;

    for (int i = 0; i < numMembers; i++) {
        const uint64_t memberID = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i).ConvertToUint64();
        const auto it = g_lobbyReadyStates.find(memberID);
        if (it == g_lobbyReadyStates.end() || !it->second) return false;
    }
    return true;
}

void ResetLobbyReadyStates() {
    g_lobbyReadyStates.clear();
    g_localReady = false;
    g_activeClassChooserLambdaThis = nullptr;
    g_hasTriggeredProceed = false;
}

void HandleCollarSyncInternal(const void *data, const uint32_t length) {
    if (length != sizeof(CollarSyncPacket)) return;

    const auto *packet = (const CollarSyncPacket *)data;
    PersistentCharacter *cat = ParaboxAPI::GetPersistentCharacterById(packet->catID);
    if (!cat) {
        Overlay::Log("[LOBBY] CollarSync: cat %lld not found", packet->catID);
        return;
    }

    const char *collarName = ParaboxAPI::ResolveCollarNameFromIndex(packet->collarIndex);
    ParaboxAPI::ApplyCollarToCharacter(cat, collarName);
    Overlay::Log("[LOBBY] CollarSync: updated cat %lld to %s (index %d)", packet->catID, collarName, packet->collarIndex);

    ParaboxAPI::UpdateClassChooserTagBoxes(packet->catID, packet->collarIndex);
    ParaboxAPI::RefreshClassChooserInventory();
    ParaboxAPI::RefreshCatSelectorUI();
}

static void ApplyLockInButtonText() {
    if (!g_lockInButton) return;

    const char* lockinoutText = g_localReady ? "Lock Out" : "Lock In!";
    uintptr_t gameBase = (uintptr_t)GetModuleHandleA(nullptr);
    auto initString = reinterpret_cast<MewFnInitNarrowString>(gameBase + MEW_RVA_INIT_NARROW_STRING);
    auto setTextString = reinterpret_cast<MewFnUIRootSetTextString>(gameBase + MEW_RVA_UI_ROOT_SET_TEXT_STRING);

    if (initString && setTextString) {
        MewNarrowString childNameStr = {};
        MewNarrowString textKeyStr = {};

        initString(&childNameStr, "INVENTORY_LOCKIN_BUTTON");
        initString(&textKeyStr, lockinoutText);

        setTextString(g_lockInButton, &childNameStr, &textKeyStr);
    }
}

static void __cdecl ClassChooserLockInButtonCallback(void* button, MewButtonEvent eventType, MewButtonState oldState, MewButtonState newState, void* userData) {
    if (oldState != newState) {
        ApplyLockInButtonText();
    }
}

static void __cdecl ClassChooserSceneRefreshCallback(MewUISceneBinding* binding, const MewUISceneRefreshResult result, void* oldSceneManager, void* newSceneManager, void* userData) {
    if (result == MEW_UI_SCENE_REFRESH_LOADED || result == MEW_UI_SCENE_REFRESH_CHANGED || result == MEW_UI_SCENE_REFRESH_UNLOADED) {
        g_lockInButton = nullptr;
        g_buttonHooked = false;
    }
}

void ClassChooserHooks_UITick() {
    if (!g_classChooserSceneInitialized) {
        MewUI_InitSceneBinding(&g_classChooserScene, "ClassChooser", ClassChooserSceneRefreshCallback, nullptr);
        g_classChooserSceneInitialized = true;
    }

    MewUI_RefreshSceneBinding(&g_classChooserScene);

    if (MewUI_IsSceneBindingActive(&g_classChooserScene)) {
        void* scene_manager = MewUI_GetSceneBindingScene(&g_classChooserScene);

        if (!g_buttonHooked) {
            if (scene_manager) {
                if (void* button = MewUI_FindButtonByRole(scene_manager, "CloseButton")) {
                    MewUI_RegisterExistingButton(button, nullptr, ClassChooserLockInButtonCallback, nullptr);
                    g_lockInButton = button;
                    g_buttonHooked = true;
                    g_lastLocalReadyState = !g_localReady;
                }
            }
        }

        if (g_buttonHooked && g_lockInButton) {
            if (g_localReady != g_lastLocalReadyState) {
                g_lastLocalReadyState = g_localReady;
                ApplyLockInButtonText();
                Overlay::Log("Button text set to '%s'", g_localReady ? "Lock Out" : "Lock In!");
            }
        }
    }
}

void ClassChooserHooks_Shutdown() {
    if (g_classChooserSceneInitialized) {
        MewUI_ClearSceneBinding(&g_classChooserScene);
        g_classChooserSceneInitialized = false;
    }
    g_lockInButton = nullptr;
    g_buttonHooked = false;
}

void CatSelectorHooks_TriggerLockInProceed() {
    if (g_hasTriggeredProceed) return;
    g_hasTriggeredProceed = true;

    if (g_activeClassChooserLambdaThis) {
        std::vector<PersistentCharacter*> colorlessCats;
        if (const auto director = GameUtils::GetMewDirectorSingleton()) {
            if (director->partyCatIDs && director->partyCount > 0) {
                for (int i = 0; i < director->partyCount; i++) {
                    const int64_t catID = director->partyCatIDs[i];
                    PersistentCharacter *cat = ParaboxAPI::GetPersistentCharacterById(catID);
                    if (cat && cat->className.is_valid() && cat->className.as_native_string_view() == "Colorless") {
                        colorlessCats.push_back(cat);
                    }
                }
            }
        }

        for (auto *cat : colorlessCats) {
            GameUtils::FreeXString(cat->className);
            GameUtils::InitXString(cat->className, "Fighter");
        }

        ParaboxAPI::ForceClassChooserLockIn(g_activeClassChooserLambdaThis);

        for (auto *cat : colorlessCats) {
            GameUtils::FreeXString(cat->className);
            GameUtils::InitXString(cat->className, "Colorless");
        }

        g_activeClassChooserLambdaThis = nullptr;
    }

    StorageHooks_TriggerEmbarkProceed();

    g_lobbyReadyStates.clear();
    g_localReady = false;
}

namespace {
struct ClassTagBoxLocal : Component  {
  void *classChooser;                     // 0x38
  [[maybe_unused]] char _padding_0[0x18]; // 0x40
  MsvcReleaseModeXString boxName;         // 0x58
  [[maybe_unused]] char _padding_1[0x18]; // 0x78
  int64_t catID;                          // 0x90
};

struct ClassChooserLocal : Component {
  [[maybe_unused]] char _padding_0[0x64]; // 0x38
  uint32_t numTagBoxes;                   // 0x9c
  ClassTagBoxLocal **tagBoxes;            // 0xa0
  [[maybe_unused]] char _padding_1[0x28]; // 0xa8
  int64_t catID;                          // 0xd0
};
}

void RegisterClassChooserSubscribers() {
    ParaboxAPI::OnClassTagBoxClick.Subscribe([](ParaboxAPI::ClassTagBoxClickEvent& ev) {
        if (NetworkManager::Get().GetCurrentLobby().IsValid()) {
            if (ev.catID != -1) {
                const auto ownerIt = g_catIdToOwnerSteamID.find(ev.catID);
                if (ownerIt != g_catIdToOwnerSteamID.end() && ownerIt->second != 0) {
                    const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
                    if (ownerIt->second != localSteamID) {
                        ev.Cancel(); // Block clicking the collar of other players
                        return;
                    }
                }
            }

            PersistentCharacter *cat = ParaboxAPI::GetPersistentCharacterById(ev.catID);
            if (!cat) return;

            const char *className = cat->className.is_valid() ? cat->className.begin() : "Colorless";
            const char *clickedClassName = ParaboxAPI::ResolveCollarNameFromIndex(ev.clickedIndex);
            const int32_t collarIndex = (strcmp(className, clickedClassName) == 0) ? -1 : ev.clickedIndex;

            CollarSyncPacket packet = {};
            packet.catID = ev.catID;
            packet.collarIndex = collarIndex;
            NetworkManager::Get().BroadcastPacket(PacketType::CollarSync, &packet, sizeof(packet), true);
            Overlay::Log("[LOBBY] Broadcast collar sync for cat %lld: collar index %d", ev.catID, collarIndex);
        }
    });

    ParaboxAPI::OnClassChooserLockIn.Subscribe([](ParaboxAPI::ClassChooserLockInEvent& ev) {
        if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;
        
        g_activeClassChooserLambdaThis = ev.lambdaThis;
        g_localReady = !g_localReady;

        const uint64_t localSteamID = SteamUser()->GetSteamID().ConvertToUint64();
        g_lobbyReadyStates[localSteamID] = g_localReady;

        LobbyReadyPacket packet = {};
        packet.steamID = localSteamID;
        packet.isReady = g_localReady;
        NetworkManager::Get().BroadcastPacket(PacketType::LobbyReady, &packet, sizeof(packet), false);
        Overlay::Log("[LOBBY] Local ready state: %s", g_localReady ? "locked in" : "not ready");

        ev.Cancel();

        if (NetworkManager::Get().IsHost() && AreAllLobbyMembersReady()) {
            NetworkManager::Get().BroadcastPacket(PacketType::LobbyProceed, nullptr, 0, true);
        }
    });
}
