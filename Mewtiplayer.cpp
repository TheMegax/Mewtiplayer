#include "hooks/ModState.h"
#include "hooks/CombatHooks.h"
#include "hooks/SaveHooks.h"
#include "hooks/AdventureBoxHooks.h"
#include "hooks/MiscHooks.h"
#include "CrashHandler.h"
#include "ImGuiHook.h"
#include "Overlay.h"

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
  if (strstr(cmdLine, "-talkative")) {
    g_modState.talkative = true;
    Overlay::Log("[INIT] Talkative (verbose) logs Active!");
  }

  g_modState.gameBase = mj.GetGameBase();
  Overlay::Log("Game base: %p", (void *)g_modState.gameBase);

  CombatHooks_Init(&mj, g_modState.gameBase);
  SaveHooks_Init(&mj, g_modState.gameBase);
  AdventureBoxHooks_Init(&mj, g_modState.gameBase);
  MiscHooks_Init(&mj, g_modState.gameBase);
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
      ImGuiHook::Unload();
    }
  }
  return TRUE;
}