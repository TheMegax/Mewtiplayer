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
    RESOLVE_DATA(gameBase, GameSymbols::MewDirector_instance, pMewDirector);
    if (pMewDirector) GameUtils::SetMewDirectorSingletonPtr(pMewDirector);

    GameUtils::ContinueFile_t continueFile = nullptr;
    RESOLVE_FUNC(gameBase, GameSymbols::SaveSelection_ContinueFile, continueFile);
    if (continueFile) GameUtils::SetContinueFilePtr(continueFile);

    GameUtils::StartRun_t startRun = nullptr;
    RESOLVE_FUNC(gameBase, GameSymbols::MewDirector_GoOnAnAdventure, startRun);
    if (startRun) GameUtils::SetStartRunPtr(startRun);

    GameUtils::Director_DestroyScene_t destroyScene = nullptr;
    RESOLVE_FUNC(gameBase, GameSymbols::Director_DestroyScene, destroyScene);
    if (destroyScene) GameUtils::SetDestroyScenePtr(destroyScene);

    void **activeScenePtr = nullptr;
    RESOLVE_DATA(gameBase, GameSymbols::Scene_current, activeScenePtr);
    if (activeScenePtr) GameUtils::SetActiveScenePtr(activeScenePtr);

    HOOK_INSTALL(mj, gameBase, RunFrame, GameSymbols::ApplicationBase_consume_time, 17);
    HOOK_INSTALL(mj, gameBase, StevenSpawn, GameSymbols::TrollEngine_TriggerSaveScum, 16);
    HOOK_INSTALL(mj, gameBase, PauseGame, GameSymbols::PauseMenuScene_pause, 15);
}
