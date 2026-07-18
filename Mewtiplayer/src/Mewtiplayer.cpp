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

ModState g_modState;

extern const ParaboxAPI::String CUSTOM_SAVE_NAME = ParaboxAPI::MakeString("mewtiplayer.sav");

static MewjectorAPI mj;

// Forward declaration — defined below DllMain
void InitializeMewUI();
void RegisterCombatSubscribers();
void RegisterSaveSubscribers();
void RegisterAdventureBoxSubscribers();
void RegisterClassChooserSubscribers();
void RegisterStorageSubscribers();
void RegisterMapSubscribers();
void RegisterActSelectionSubscribers();
void RegisterLevelUpSubscribers();
void RegisterWorldEventSubscribers();
void RegisterShopSubscribers();

// Temporary forward declarations until extracted from hook files
void ClassChooserHooks_UITick();
void StorageHooks_UITick();
void MapHooks_UITick();
void ClassChooserHooks_Shutdown();
void StorageHooks_Shutdown();
void MapHooks_Shutdown();

// ---------------------------------------------------------------------------
// ParaboxAPI subscribers — centralised Mewtiplayer logic
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
        if (g_modState.noSteven) {
            Overlay::Log("[STEVEN] Go away!");
            ev.Cancel();
        } else if (!NetworkManager::Get().IsHost()) {
            Overlay::Log("[COMBAT] Suppressing Steven spawn as client");
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
    Overlay::Log("Initializing (API v%d)...", mj.GetVersion());

    const char *cmdLine = GetCommandLineA();

    if (strstr(cmdLine, "-packet_testing")) {
        g_modState.packetTesting = true;
        Overlay::Log("[INIT] Packet testing Active!");
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