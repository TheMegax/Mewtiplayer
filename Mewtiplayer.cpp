#include "include/ModState.h"
#include "hooks/CombatHooks.h"
#include "hooks/SaveHooks.h"
#include "hooks/AdventureBoxHooks.h"
#include "hooks/ClassChooserHooks.h"
#include "hooks/StorageHooks.h"
#include "hooks/MiscHooks.h"
#include "hooks/MapHooks.h"
#include "hooks/ActSelectionHooks.h"
#include "hooks/LevelUpHooks.h"
#include "hooks/WorldEventHooks.h"
#include "CrashHandler.h"
#include "ImGuiHook.h"
#include "Overlay.h"
#include "mew_ui_api.h"

#include <string>

ModState g_modState;

extern const std::string CUSTOM_SAVE_NAME = "mewtiplayer.sav";

static MewjectorAPI mj;

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

  CombatHooks_Init(&mj, g_modState.gameBase);
  SaveHooks_Init(&mj, g_modState.gameBase);
  AdventureBoxHooks_Init(&mj, g_modState.gameBase);
  CatSelectorHooks_Init(&mj, g_modState.gameBase);
  StorageHooks_Init(&mj, g_modState.gameBase);
  MiscHooks_Init(&mj, g_modState.gameBase);
  MapHooks_Init(&mj, g_modState.gameBase);
  ActSelectionHooks_Init(&mj, g_modState.gameBase);
  LevelUpHooks_Init(&mj, g_modState.gameBase);
  WorldEventHooks_Init(&mj, g_modState.gameBase);
}

static void __cdecl Mewtiplayer_UITick(void* userData) {
  (void)userData;
  ClassChooserHooks_UITick();
  StorageHooks_UITick();
}

void InitializeMewUI() {
  MewUI_Start("Mewtiplayer", 0, 100, 0, Mewtiplayer_UITick, nullptr);
}

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
      MewUI_Stop();
      ImGuiHook::Unload();
    }
  }
  return TRUE;
}