#include "ParaboxAPI.h"
#include <windows.h>
#include <cstdio>

namespace ParaboxAPI {

static LogCallback g_logCallback = nullptr;
static EnqueueResultCallback g_enqueueResultCallback = nullptr;

PARABOX_API void SetLogCallback(LogCallback cb) {
    g_logCallback = cb;
}

PARABOX_API void SetEnqueueResultCallback(EnqueueResultCallback cb) {
    g_enqueueResultCallback = cb;
}

EnqueueResultCallback GetEnqueueResultCallback() {
    return g_enqueueResultCallback;
}

PARABOX_API void Log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (g_logCallback) {
        g_logCallback(fmt, args);
    } else {
        // Fallback: format and send to OutputDebugString
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, args);
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }
    va_end(args);
}

PARABOX_API Event<RunFrameEvent>    OnRunFrame;
PARABOX_API Event<StevenSpawnEvent> OnStevenSpawn;

// Combat events
PARABOX_API Event<TurnStartEvent>              OnTurnStart;
PARABOX_API Event<BeginTurnEvent>              OnBeginTurn;
PARABOX_API Event<FightEndEvent>               OnFightEnd;
PARABOX_API Event<AbilityTriggerEvent>         OnAbilityTrigger;
PARABOX_API Event<EnqueueActionEvent>          OnEnqueueAction;
PARABOX_API Event<FaceDirectionEvent>          OnFaceDirection;
PARABOX_API Event<SlotUpdateDynamicValueEvent> OnSlotUpdateDynamicValue;
PARABOX_API Event<ProcessCombatInputEvent>     OnProcessCombatInput;
PARABOX_API Event<RouteCombatInputEvent>       OnRouteCombatInput;

// Save events
PARABOX_API Event<CreateStrayCatEvent>         OnCreateStrayCat;
PARABOX_API Event<GetCollarVectorEvent>        OnGetCollarVector;

// AdventureBox events
PARABOX_API Event<ButchBoxInitEvent>           OnButchBoxInit;
PARABOX_API Event<ButchBoxTryPlaceCatEvent>    OnButchBoxTryPlaceCat;
PARABOX_API Event<ButchBoxConfigCatsEvent>     OnButchBoxConfigCats;
PARABOX_API Event<ButchBoxTryRemoveCatEvent>   OnButchBoxTryRemoveCat;
PARABOX_API Event<ButchBoxDepartEvent>         OnButchBoxDepart;
PARABOX_API Event<LoadAdventureEvent>          OnLoadAdventure;

// ClassChooser events
PARABOX_API Event<CatSelectorInitEvent>        OnCatSelectorInit;
PARABOX_API Event<ClassTagBoxClickEvent>       OnClassTagBoxClick;
PARABOX_API Event<ClassChooserLockInEvent>     OnClassChooserLockIn;

// Storage events
PARABOX_API Event<SceneManagerCreateSceneEvent> OnSceneManagerCreateScene;
PARABOX_API Event<SceneAddComponentEvent>       OnSceneAddComponent;
PARABOX_API Event<InventoryItemBoxClickEvent>   OnInventoryItemBoxClick;
PARABOX_API Event<InventoryItemBoxEquippedEvent> OnInventoryItemBoxEquipped;
PARABOX_API Event<InventoryScreen2CloseEvent>   OnInventoryScreen2Close;

// Map events
PARABOX_API Event<MapNodeClickEvent>           OnMapNodeClick;
PARABOX_API Event<MapScreenEnterNodeEvent>     OnMapScreenEnterNode;

// ActSelection events
PARABOX_API Event<ActSelectionScreenInitEvent>      OnActSelectionScreenInit;
PARABOX_API Event<ActSelectionScreenSelectActEvent> OnActSelectionScreenSelectAct;

// LevelUp events
PARABOX_API Event<LevelUpScreenInitEvent>           OnLevelUpScreenInit;
PARABOX_API Event<LevelUpScreenSelectOptionEvent>   OnLevelUpScreenSelectOption;
PARABOX_API Event<LevelUpScreenRerollEvent>         OnLevelUpScreenReroll;
PARABOX_API Event<AbilityChooserInitEvent>          OnAbilityChooserInit;
PARABOX_API Event<AbilityChooserSelectSlotEvent>    OnAbilityChooserSelectSlot;

// WorldEvent events
PARABOX_API Event<WorldEventInitEvent>        OnWorldEventInit;
PARABOX_API Event<WorldEventClickOptionEvent> OnWorldEventClickOption;
PARABOX_API Event<WorldEventClickCatEvent>    OnWorldEventClickCat;
PARABOX_API Event<WorldEventClickEnd1Event>   OnWorldEventClickEnd1;
PARABOX_API Event<WorldEventClickEnd2Event>   OnWorldEventClickEnd2;
PARABOX_API Event<WorldEventClickEndCustomEvent> OnWorldEventClickEndCustom;

} // namespace ParaboxAPI

// Forward declare hook initialization functions
void MiscHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void CombatHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void SaveHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void AdventureBoxHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void CatSelectorHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void StorageHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void MapHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void ActSelectionHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void LevelUpHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void WorldEventHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void ShopHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);

namespace ParaboxAPI {

PARABOX_API void InstallHooks(MewjectorAPI *mj, uintptr_t gameBase) {
    MiscHooks_Init(mj, gameBase);
    CombatHooks_Init(mj, gameBase);
    SaveHooks_Init(mj, gameBase);
    AdventureBoxHooks_Init(mj, gameBase);
    CatSelectorHooks_Init(mj, gameBase);
    StorageHooks_Init(mj, gameBase);
    MapHooks_Init(mj, gameBase);
    ActSelectionHooks_Init(mj, gameBase);
    LevelUpHooks_Init(mj, gameBase);
    WorldEventHooks_Init(mj, gameBase);
    ShopHooks_Init(mj, gameBase);
}

} // namespace ParaboxAPI
