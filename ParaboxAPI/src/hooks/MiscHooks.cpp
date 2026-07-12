#include "hooks/MiscHooks.h"
#include "hooks/HookMacros.h"
#include "ParaboxAPI.h"
#include "GameUtils.h"
#include "Scanner.h"

HOOK_DEFINE(StevenSpawn, void*, void*, void*)
HOOK_DEFINE(PauseGame,   void,  void*)
HOOK_DEFINE(RunFrame,    void,  void*, void*)

static void *__fastcall Hook_StevenSpawn(void *rcx, void *rdx) {
    ParaboxAPI::StevenSpawnEvent ev = {};
    ev.rcx = rcx;
    ev.rdx = rdx;
    ParaboxAPI::OnStevenSpawn.Publish(ev);

    if (ev.cancelled)
        return nullptr;

    if (g_origStevenSpawn)
        return g_origStevenSpawn(rcx, rdx);
    return nullptr;
}

static void __fastcall Hook_PauseGame(void *pauseMenuScene) {
    if (g_origPauseGame)
        g_origPauseGame(pauseMenuScene);
    // (Commented-out pause-menu logic preserved here intentionally)
}

static void Hook_RunFrame(void *rcx, void *rdx) {
    ParaboxAPI::RunFrameEvent ev = {};
    ev.rcx = rcx;
    ev.rdx = rdx;
    ParaboxAPI::OnRunFrame.Publish(ev);

    if (ev.cancelled)
        return;

    if (g_origRunFrame)
        g_origRunFrame(rcx, rdx);
}

void MiscHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
    MewDirector **pMewDirector = nullptr;
    SCAN_RESOLVE(mj, gameBase, MewDirectorSingleton,
        "48 89 5C 24 10 48 89 4C 24 08 57 48 83 EC 40 48 8B CA 48 8B 05 ?? ?? ?? ?? 48 8B B8 A8 05 00 00",
        pMewDirector, 18, 3, 7);
    if (pMewDirector) GameUtils::SetMewDirectorSingletonPtr(pMewDirector);

    GameUtils::ContinueFile_t continueFile = nullptr;
    SCAN_SET(mj, gameBase, ContinueFile,
        "48 89 5c 24 08 48 89 74 24 10 55 57 41 56 48 8d 6c 24 b9 48 81 ec b0 00 00 00 8b fa 48 8b f1",
        continueFile);
    if (continueFile) GameUtils::SetContinueFilePtr(continueFile);

    GameUtils::StartRun_t startRun = nullptr;
    SCAN_SET(mj, gameBase, StartRun,
        "48 8B C4 48 89 58 20 44 89 40 18 48 89 50 10 55",
        startRun);
    if (startRun) GameUtils::SetStartRunPtr(startRun);

    void **activeScenePtr = nullptr;
    SCAN_RESOLVE(mj, gameBase, ActiveSceneFunc,
        "48 89 5C 24 10 57 48 83 EC 20 33 FF 48 8B D9 48 85 C9 75 10 48 8B 1D",
        activeScenePtr, 20, 3, 7);
    if (activeScenePtr) GameUtils::SetActiveScenePtr(activeScenePtr);

    HOOK_INSTALL(mj, gameBase, RunFrame,
        "40 53 41 56 41 57 48 83 EC 40", 17);

    HOOK_INSTALL(mj, gameBase, StevenSpawn,
        "48 8B C4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FE FF FF 48 81 EC E0 01 00 00 0F 29 70 B8 0F 29 78 A8 8B F2", 16);

    HOOK_INSTALL(mj, gameBase, PauseGame,
        "48 8B C4 53 48 83 EC 60 80 B9 C0 00 00 00 00 48 8B D9 0F 85", 15);
}
