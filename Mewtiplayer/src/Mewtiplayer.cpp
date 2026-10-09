#include "ModState.h"

#include "CrashHandler.h"
#include "ImGuiHook.h"
#include "Overlay.h"
#include "NetworkManager.h"
#include "InputGhost.h"
#include "GameUtils.h"
#include "ParaboxAPI.h"
#include "mew_ui_api.h"

#include <string>

#include "EventSubscribers.h"
#include "events/ClassChooserSubscribers.h"
#include "events/StorageSubscribers.h"
#include "SceneSyncManager.h"

ModState g_modState;

extern const ParaboxAPI::String CUSTOM_SAVE_NAME = ParaboxAPI::MakeString("mewtiplayer.sav");

static MewjectorAPI mj;

void InitializeMewUI();

static ULONGLONG g_lastEvilSpamTime = 0;
static size_t g_evilCollarCounter = 0;
static size_t g_evilStorageCounter = 0;

static void EvilModeUpdate() {
    if (!g_modState.evilMode || !g_modState.evilShuffler) return;
    if (!NetworkManager::Get().GetCurrentLobby().IsValid()) return;

    const ULONGLONG now = GetTickCount64();
    if (now - g_lastEvilSpamTime < 150) {
        return;
    }
    g_lastEvilSpamTime = now;

    bool isClassChooser = false;
    bool isStorageItems = false;
    for (const auto* scene : GameUtils::GetCurrentScenes()) {
        if (!scene || !scene->name.is_valid()) continue;
        const auto name = scene->name.as_native_string_view();
        if (name == "ClassChooser") isClassChooser = true;
        if (name == "StorageItems" || name == "CatStatus") isStorageItems = true;
    }

    const auto director = GameUtils::GetMewDirectorSingleton();
    if (!director || !director->current_battle_cats.data_ || director->current_battle_cats.size() == 0) return;

    std::vector<int64_t> myControlledCats;
    for (size_t i = 0; i < director->current_battle_cats.size(); ++i) {
        const int64_t catUID = director->current_battle_cats.data_[i];
        if (NetworkManager::Get().IsCatControlledLocally(catUID)) {
            myControlledCats.push_back(catUID);
        }
    }
    if (myControlledCats.empty()) return;

    if (isClassChooser) {
        const int64_t targetCatID = myControlledCats[g_evilCollarCounter % myControlledCats.size()];
        const int32_t tagBoxCount = ParaboxAPI::GetClassTagBoxCount();
        if (tagBoxCount > 0) {
            const int32_t collarIndex = g_evilCollarCounter % (tagBoxCount + 1) - 1;
            g_evilCollarCounter++;

            CollarSyncPacket packet = {};
            packet.catID = targetCatID;
            packet.collarIndex = collarIndex;

            if (NetworkManager::Get().IsHost()) {
                HandleCollarSyncInternal(&packet, sizeof(packet));
            } else {
                NetworkManager::Get().SendPacketReliable(NetworkManager::Get().GetHostID(), PacketType::CollarSync, &packet, sizeof(packet));
            }
        }
    } else if (isStorageItems) {
        const int64_t selectedCatID = ParaboxAPI::ResolveSelectedCatID();
        const int64_t targetCatID = (selectedCatID != -1 && NetworkManager::Get().IsCatControlledLocally(selectedCatID))
                                        ? selectedCatID
                                        : myControlledCats[g_evilStorageCounter % myControlledCats.size()];

        auto *screen = ParaboxAPI::GetActiveInventoryScreen2();
        if (screen && screen->boxes.data_ && screen->boxes.size() > 0) {
            const uint32_t boxIdx = g_evilStorageCounter % screen->boxes.size();
            g_evilStorageCounter++;
            const auto *box = screen->boxes.data_[boxIdx];
            if (box) {
                ParaboxAPI::RefreshInventoryEquippedStatus();
                const int64_t currentOwner = ParaboxAPI::GetItemEquippedOwner(box->item_id);

                // don't touch teammates' gear
                if (currentOwner != -1 && !NetworkManager::Get().IsCatControlledLocally(currentOwner)) {
                    return;
                }

                // toggle our cat's item or grab loose storage items
                int64_t targetCatIDForBox = -1;
                if (currentOwner == targetCatID) {
                    targetCatIDForBox = -1;
                } else if (currentOwner == -1) {
                    targetCatIDForBox = targetCatID;
                } else {
                    return;
                }

                const int32_t sortOrder = ParaboxAPI::GetItemSortOrder(box);
                if (sortOrder < 0) {
                    return;
                }

                StorageItemSyncPacket packet = {};
                packet.steamID = SteamUser()->GetSteamID().ConvertToUint64();
                packet.catID = targetCatIDForBox;
                packet.sortOrder = sortOrder;

                if (NetworkManager::Get().IsHost()) {
                    HandleStorageItemSyncInternal(&packet, sizeof(packet));
                } else {
                    NetworkManager::Get().SendPacketReliable(NetworkManager::Get().GetHostID(), PacketType::StorageItemSync, &packet, sizeof(packet));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ParaboxAPI subscribers
// ---------------------------------------------------------------------------

void RegisterSubscribers() {
    ParaboxAPI::OnRunFrame.Subscribe([](ParaboxAPI::RunFrameEvent& ev) {
        if (!ev.rcx) return;
        
        static bool networkInitialized = false;
        if (!networkInitialized) {
            networkInitialized = true;
            Overlay::Log("Captured application instance: %p", ev.rcx);
            NetworkManager::Get().Init(g_modState.mj, "Mewtiplayer");
            Overlay::Setup(g_modState.mj);
            InitializeMewUI();
            if (g_modState.autoLobby) {
                NetworkManager::Get().HostLobby("Debug Lobby");
            }
        }

        if (networkInitialized) {
            NetworkManager::Get().Update();
            InputGhost::Update();
            InputGhost::SetIsHost(NetworkManager::Get().IsHost());
            EvilModeUpdate();

            static ULONGLONG s_lastHostPeriodicSyncTime = 0;
            const ULONGLONG now = GetTickCount64();
            if (NetworkManager::Get().IsHost() && (now - s_lastHostPeriodicSyncTime >= 1000)) {
                s_lastHostPeriodicSyncTime = now;
                bool isClassChooser = false;
                bool isStorageItems = false;
                for (const auto* scene : GameUtils::GetCurrentScenes()) {
                    if (!scene || !scene->name.is_valid()) continue;
                    const auto name = scene->name.as_native_string_view();
                    if (name == "ClassChooser") isClassChooser = true;
                    if (name == "StorageItems" || name == "CatStatus") isStorageItems = true;
                }

                if (isClassChooser) {
                    HostBroadcastFullCollarState();
                } else if (isStorageItems) {
                    HostBroadcastFullStorageState();
                }
            }
        }


        if (GameUtils::g_startCustomRunPending) {
            MewDirector *dir = GameUtils::GetMewDirectorSingleton();
            if (dir == nullptr) {
                GameUtils::g_oldDirector = nullptr;
            } else if (dir != GameUtils::g_oldDirector && dir->director && dir->progression != nullptr) {
                GameUtils::g_startCustomRunPending = false;
                GameUtils::g_oldDirector           = nullptr;
                GameUtils::StartCustomRun(GameUtils::g_customTeamSize, GameUtils::g_customDifficulty, 4);
            }
        }
    });

    ParaboxAPI::OnStevenSpawn.Subscribe([](ParaboxAPI::StevenSpawnEvent& ev) {
        if (g_modState.noSteven || NetworkManager::Get().GetCurrentLobby().IsValid()) {
            Overlay::Log("[STEVEN] Go away!");
            ev.Cancel();
        }
    });

    RegisterCombatSubscribers();
    RegisterSaveSubscribers();
    RegisterAdventureBoxSubscribers();
    RegisterClassChooserSubscribers();
    RegisterStorageSubscribers();
    RegisterMapSubscribers();
    RegisterActSelectionSubscribers();
    RegisterLevelUpSubscribers();
    RegisterWorldEventSubscribers();
    RegisterShopSubscribers();
    SceneSyncManager::Get().Init();
}

// ---------------------------------------------------------------------------
// Core initialisation
// ---------------------------------------------------------------------------

static void Initialize() {
    if (!MJ_Resolve(&mj))
        return;
    g_modState.mj = &mj;
    ParaboxAPI::SetLogCallback(Overlay::LogV);
    Overlay::Log("Initializing (API v%d)...", mj.GetVersion());

    const char *cmdLine = GetCommandLineA();

    if (strstr(cmdLine, "-network_simulation")) {
        g_modState.networkSimulation = true;
        Overlay::Log("[INIT] Network simulation active (ping: %ums, loss: %.1f%%, scene delay: %ds)",
                     g_modState.simPingMs, g_modState.simLossRate, g_modState.simSceneLoadDelaySeconds);
    }
    if (const char *pingArg = strstr(cmdLine, "-sim_ping=")) {
        g_modState.simPingMs = (uint32_t)atoi(pingArg + 10);
        g_modState.networkSimulation = true;
        Overlay::Log("[INIT] Custom sim ping: %ums", g_modState.simPingMs);
    }
    if (const char *lossArg = strstr(cmdLine, "-sim_loss=")) {
        g_modState.simLossRate = (float)atof(lossArg + 10);
        g_modState.networkSimulation = true;
        Overlay::Log("[INIT] Custom sim loss: %.1f%%", g_modState.simLossRate);
    }
    if (const char *sceneDelayArg = strstr(cmdLine, "-sim_scene_delay=")) {
        g_modState.simSceneLoadDelaySeconds = atoi(sceneDelayArg + 17);
        g_modState.networkSimulation = true;
        Overlay::Log("[INIT] Custom sim scene delay: %ds", g_modState.simSceneLoadDelaySeconds);
    } else if (const char *sceneDelayArg2 = strstr(cmdLine, "-sim_scene_load_delay=")) {
        g_modState.simSceneLoadDelaySeconds = atoi(sceneDelayArg2 + 22);
        g_modState.networkSimulation = true;
        Overlay::Log("[INIT] Custom sim scene delay: %ds", g_modState.simSceneLoadDelaySeconds);
    }

    if (strstr(cmdLine, "-no_steven")) {
        g_modState.noSteven = true;
        Overlay::Log("[INIT] No Steven Active!");
    }
    if (strstr(cmdLine, "-auto_lobby")) {
        g_modState.autoLobby = true;
        Overlay::Log("[INIT] Auto lobby Active!");
    }
    if (strstr(cmdLine, "-auto_join")) {
        g_modState.autoJoin = true;
        Overlay::Log("[INIT] Auto join Active!");
    }
    if (strstr(cmdLine, "-talkative")) {
        g_modState.talkative = true;
        Overlay::Log("[INIT] Talkative (verbose) logs Active!");
    }
    if (strstr(cmdLine, "-evil_mode")) {
        g_modState.evilMode = true;
        Overlay::Log("[INIT] Evil Mode Active!");
    }

    g_modState.gameBase = mj.GetGameBase();
    Overlay::Log("Game base: %p", (void *)g_modState.gameBase);

    // Register all ParaboxAPI subscribers before installing hooks
    RegisterSubscribers();

    ParaboxAPI::InstallHooks(&mj, g_modState.gameBase);
}

// ---------------------------------------------------------------------------
// UI tick (called each ImGui frame)
// ---------------------------------------------------------------------------

static void __cdecl Mewtiplayer_UITick(void *userData) {
    (void)userData;
    ClassChooserHooks_UITick();
    StorageHooks_UITick();
    MapHooks_UITick();
}

void InitializeMewUI() {
    MewUI_Start("Mewtiplayer", 0, 100, 0, Mewtiplayer_UITick, nullptr);
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, const DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CrashHandler::Register();
        if (MJ_Resolve(&mj))
            Overlay::Log("Loading!");
        Initialize();
    } else if (reason == DLL_PROCESS_DETACH) {
        CrashHandler::Unregister();
        if (MJ_Resolve(&mj)) {
            Overlay::Log("Unloading!");
            ClassChooserHooks_Shutdown();
            StorageHooks_Shutdown();
            MapHooks_Shutdown();
            MewUI_Stop();
            ImGuiHook::Unload();
        }
    }
    return TRUE;
}