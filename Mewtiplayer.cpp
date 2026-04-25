#include "ImGuiHook.h"
#include "InputGhost.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "Scanner.h"
#include "mewjector.h"
#include <stdint.h>
#include <windows.h>

#define MOD_NAME "Mewtiplayer"
#define MOD_VERSION "1.0.0"

static MewjectorAPI mj;
static UINT_PTR g_gameBase = 0;

typedef void (*RunFrame_t)(void *rcx, void *rdx);
static RunFrame_t g_origRunFrame = nullptr;
static bool g_networkInitialized = false;

static void Hook_RunFrame(void *rcx, void *rdx) {
  if (rcx && !g_networkInitialized) {
    g_networkInitialized = true;
    Overlay::Log("Captured application instance: %p", rcx);
    NetworkManager::Get().Init(&mj, MOD_NAME "-" MOD_VERSION);
    Overlay::Setup(&mj); // Initialize overlay once network is ready or at start
  }

  if (g_networkInitialized) {
    NetworkManager::Get().Update();
    InputGhost::Update();
    InputGhost::SetIsHost(NetworkManager::Get().IsHost());
  }

  if (g_origRunFrame)
    g_origRunFrame(rcx, rdx);
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
static void Initialize(void) {
  if (!MJ_Resolve(&mj))
    return;
  Overlay::Log("Initializing (API v%d)...", mj.GetVersion());

  g_gameBase = mj.GetGameBase();
  Overlay::Log("Game base: %p", (void *)g_gameBase);

  uintptr_t runFrameRVA = ScanSignature(&mj, g_gameBase, "RunFrame",
                                        "40 53 41 56 41 57 48 83 EC 40");
  if (runFrameRVA) {
    mj.InstallHook(runFrameRVA, 17, (void *)Hook_RunFrame,
                   (void **)&g_origRunFrame, 10, MOD_NAME);
  }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    if (MJ_Resolve(&mj))
      Overlay::Log("Loading!");
    Initialize();
  } else if (reason == DLL_PROCESS_DETACH) {
    if (MJ_Resolve(&mj)) {
      Overlay::Log("Unloading!");
      ImGuiHook::Unload();
    }
  }
  return TRUE;
}
