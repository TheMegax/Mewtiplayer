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
    if (!director || !director->partyCatIDs || director->partyCount <= 0) return;

    if (isClassChooser) {
        const int64_t targetCatID = director->partyCatIDs[g_evilCollarCounter % director->partyCount];
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
        const int64_t targetCatID = ParaboxAPI::ResolveSelectedCatID();
        if (targetCatID != -1) {
            const int32_t storageBoxCount = ParaboxAPI::GetStorageSlotCount();
            if (storageBoxCount > 0) {
                const int32_t slotIndex = g_evilStorageCounter % storageBoxCount;
                g_evilStorageCounter++;

                StorageItemSyncPacket packet = {};
                packet.steamID = SteamUser()->GetSteamID().ConvertToUint64();
                packet.catID = targetCatID;
                packet.slotIndex = slotIndex;

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
            } else if (dir != GameUtils::g_oldDirector && dir->director && dir->house != nullptr) {
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

    if (strstr(cmdLine, "-packet_testing")) {
        g_modState.packetTesting = true;
        Overlay::Log("[INIT] Network Simulation (Packet Testing) Active! (Ping: %u ms, Loss: %.1f%%)",
                     g_modState.simPingMs, g_modState.simLossRate);
    }
    if (const char *pingArg = strstr(cmdLine, "-sim_ping=")) {
        g_modState.simPingMs = (uint32_t)atoi(pingArg + 10);
        g_modState.packetTesting = true;
        Overlay::Log("[INIT] Custom Sim Ping: %u ms", g_modState.simPingMs);
    }
    if (const char *lossArg = strstr(cmdLine, "-sim_loss=")) {
        g_modState.simLossRate = (float)atof(lossArg + 10);
        g_modState.packetTesting = true;
        Overlay::Log("[INIT] Custom Sim Loss: %.1f%%", g_modState.simLossRate);
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