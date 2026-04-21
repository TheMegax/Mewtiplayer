#include "NetworkManager.h"
#include "Overlay.h"
#include "Scanner.h"
#include "imgui_hook.h"
#include "mewjector.h"
#include <stdint.h>
#include <windows.h>

#define MOD_NAME "Multigenics"

static MewjectorAPI mj;
static UINT_PTR g_gameBase = 0;

typedef void (*RunFrame_t)(void *rcx, void *rdx);
static RunFrame_t g_origRunFrame = nullptr;
static bool g_networkInitialized = false;

static void Hook_RunFrame(void *rcx, void *rdx) {
  if (rcx && !g_networkInitialized) {
    g_networkInitialized = true;
    Overlay::Log("Captured application instance: %p", rcx);
    NetworkManager::Get().Init(&mj);
    Overlay::Setup(&mj); // Initialize overlay once network is ready or at start
  }

  if (g_networkInitialized) {
    NetworkManager::Get().Update();
  }

  // --- Keybinds ---
  static bool f1 = false, f5 = false, f6 = false, f7 = false;

  bool f1_now = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
  if (f1_now && !f1) {
    Overlay::ToggleVisible();
  }
  f1 = f1_now;

  bool f5_now = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
  if (f5_now && !f5) {
    NetworkManager::Get().HostLobby();
  }
  f5 = f5_now;

  bool f6_now = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
  if (f6_now && !f6) {
    NetworkManager::Get().JoinAnyLobby();
  }
  f6 = f6_now;

  bool f7_now = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
  if (f7_now && !f7) {
    CSteamID lobby = NetworkManager::Get().GetCurrentLobby();
    if (lobby.IsValid()) {
      int n = SteamMatchmaking()->GetNumLobbyMembers(lobby);
      for (int i = 0; i < n; i++) {
        CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(lobby, i);
        if (member != SteamUser()->GetSteamID()) {
          NetworkManager::Get().SendPacket(member, PacketType::Ping, nullptr,
                                           0);
        }
      }
    } else {
      Overlay::Log("[WARN] Not in a lobby.");
    }
  }
  f7 = f7_now;

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
