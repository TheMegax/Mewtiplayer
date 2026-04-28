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

typedef void(__fastcall *BeginTurn_t)(void *character, int kind);
static BeginTurn_t g_origBeginTurn = nullptr;

typedef void(__fastcall *FightEnd_t)(void *combat);
static FightEnd_t g_origFightEnd = nullptr;

static const char *GetNarrowString(void *ptr, int offset) {
  if (!ptr)
    return "N/A";
  uintptr_t strPtr = (uintptr_t)ptr + offset;
  size_t capacity = *(size_t *)(strPtr + 24);
  if (capacity < 16) {
    return (const char *)strPtr;
  } else {
    return *(const char **)strPtr;
  }
}

static void Hook_BeginTurn(void *character, int kind) {
  if (character) {
    // 0x290 is Name (glaiel::wstring)
    // 0x88 is PersistentChar* (GameChar)
    // persistent_char + 128 is UID (int64)
    // persistent_char + 3088 is ClassName (narrow string)

    const wchar_t *namePtr = L"Unknown";
    size_t capacity = *(size_t *)((uintptr_t)character + 0x290 +
                                  24); // std::wstring capacity offset
    if (capacity < 8) {
      namePtr = (const wchar_t *)((uintptr_t)character + 0x290);
    } else {
      namePtr = *(const wchar_t **)((uintptr_t)character + 0x290);
    }

    int64_t uniqueId = -1;
    const char *className = "Collarless";
    void *persistentChar = *(void **)((uintptr_t)character + 0x88);
    if (persistentChar) {
      uniqueId = *(int64_t *)((uintptr_t)persistentChar + 128);
      className = GetNarrowString(persistentChar, 3088);
    }

    Overlay::Log("BeginTurn: [%ls] UID:%lld Class:%s", namePtr, uniqueId,
                 className);

    if (uniqueId != -1) {
      char narrowName[128];
      size_t converted;
      wcstombs_s(&converted, narrowName, namePtr, sizeof(narrowName));
      NetworkManager::Get().RegisterCat(uniqueId, narrowName, className);
      NetworkManager::Get().SetActiveCat(uniqueId);
    }
  }

  if (g_origBeginTurn)
    g_origBeginTurn(character, kind);
}

static uint64_t g_lastCombatPulse = 0;
static bool g_inCombatDetected = false;

static void Hook_UpdateCombatResolutionState(void *combat) {
  g_lastCombatPulse = GetTickCount64();

  if (!g_inCombatDetected) {
    g_inCombatDetected = true;
    if (NetworkManager::Get().IsHost()) {
      NetworkManager::Get().StartCombat();
    }
  }

  if (g_origFightEnd)
    g_origFightEnd(combat);
}

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

    // Watchdog: If no combat pulse for 200ms, assume combat ended
    // TODO: This should only run on the host side
    if (g_inCombatDetected && (GetTickCount64() - g_lastCombatPulse > 200)) {
      g_inCombatDetected = false;
      if (NetworkManager::Get().IsHost()) {
        NetworkManager::Get().EndCombat();
      }
    }
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

  // "RunFrame" - RVA 0x9A5020
  uintptr_t runFrameRVA = ScanSignature(&mj, g_gameBase, "RunFrame",
                                        "40 53 41 56 41 57 48 83 EC 40");

  // glaiel::Character::BeginTurn(TurnKind) - RVA 0x108C30
  uintptr_t beginTurnRVA =
      ScanSignature(&mj, g_gameBase, "BeginTurn",
                    "48 89 5C 24 08 89 54 24 10 55 56 57 41 54 41 55 41 56 41 "
                    "57 48 8D AC 24 B0 FC FF FF");

  // "UpdateCombatResolutionState" - RVA 0x35F4B0
  uintptr_t updateCombatResolutionStateRVA =
      ScanSignature(&mj, g_gameBase, "UpdateCombatResolutionState",
                    "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 "
                    "48 81 EC 18 01 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 "
                    "88 44 0F 29 88 78 FF FF FF 4C 8B F1");

  if (runFrameRVA) {
    mj.InstallHook(runFrameRVA, 17, (void *)Hook_RunFrame,
                   (void **)&g_origRunFrame, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find RunFrame!");
  }

  if (beginTurnRVA) {
    mj.InstallHook(beginTurnRVA, 16, (void *)Hook_BeginTurn,
                   (void **)&g_origBeginTurn, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find BeginTurn!");
  }

  if (updateCombatResolutionStateRVA) {
    mj.InstallHook(updateCombatResolutionStateRVA, 15,
                   (void *)Hook_UpdateCombatResolutionState,
                   (void **)&g_origFightEnd, 10, MOD_NAME);
  } else {
    Overlay::Log("Failed to find UpdateCombatResolutionState!");
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
