#pragma once
#include <functional>
#include <vector>
#include <cstdarg>
#include "mewjector.h"

#ifdef PARABOX_EXPORTS
#define PARABOX_API __declspec(dllexport)
#else
#define PARABOX_API __declspec(dllimport)
#endif

struct Character;
struct TurnControl;
struct CombatResolutionState;
struct Ability;
struct TurnAction;
struct CombatUIContext;
struct ButchBox;
struct LoadAdventureArgs;
struct PersistentCharacter;

namespace glaiel {
    struct LevelUpScreen;
    struct LevelUpOption;
    struct AbilityChooser;
    struct WorldEvent;
    struct WorldEventOption;
    struct WorldEventCatButton;
}

namespace ParaboxAPI {

// ---------------------------------------------------------------------------
// Generic Event Publisher
// ---------------------------------------------------------------------------
template <typename T>
class Event {
public:
    using Callback = std::function<void(T &)>;

    void Subscribe(Callback cb) {
        if (m_count < 32) {
            m_subscribers[m_count++] = std::move(cb);
        }
    }

    void Publish(T &eventData) {
        for (int i = 0; i < m_count; i++) {
            m_subscribers[i](eventData);
        }
    }

private:
    Callback m_subscribers[32];
    int m_count = 0;
};

// ---------------------------------------------------------------------------
// Base event — provides a cancellation flag
// ---------------------------------------------------------------------------
struct EventBase {
    bool cancelled = false;
    void Cancel() { cancelled = true; }
};


// ---------------------------------------------------------------------------
// Event types — MiscHooks (first set to be converted)
// ---------------------------------------------------------------------------

/// Fired once per game frame. Cancel to skip calling the original RunFrame.
struct RunFrameEvent : EventBase {
    void *rcx; ///< Application instance pointer (non-null once the game is ready)
    void *rdx;
};

/// Fired when the game attempts to spawn Steven. Cancel to suppress the spawn.
struct StevenSpawnEvent : EventBase {
    void *rcx;
    void *rdx;
};

// ---------------------------------------------------------------------------
// Event types — CombatHooks
// ---------------------------------------------------------------------------
struct TurnStartEvent : EventBase {
    TurnControl *tc;
};

struct BeginTurnEvent : EventBase {
    Character *character;
    int kind;
};

struct FightEndEvent : EventBase {
    CombatResolutionState *combat;
};

struct AbilityTriggerEvent : EventBase {
    Ability *ability;
    TurnAction *turnAction;
};

struct EnqueueActionEvent : EventBase {
    void *queue;
    TurnAction *actionData;
    void *returnValue = nullptr;
};

struct FaceDirectionEvent : EventBase {
    void *character;
    uint64_t target_packed;
    bool play_animation;
    bool force;
};

struct SlotUpdateDynamicValueEvent : EventBase {
    void *rcx;
    void *rdx;
};

struct ProcessCombatInputEvent : EventBase {
    CombatUIContext *ctx;
    void *outResult;
    void *returnValue = nullptr;
};

struct RouteCombatInputEvent : EventBase {
    CombatUIContext *ctx;
    void *outResult;
    void *param3;
    void *param4;
    void *returnValue = nullptr;
};

struct CreateStrayCatEvent : EventBase {
    void *catsManager;
    void *returnValue = nullptr;
};

struct GetCollarVectorEvent : EventBase {
    int64_t *outVector;
    int64_t collarId;
    int64_t param_3;
    int64_t param_4;
    int64_t *returnValue = nullptr;
};

// ---------------------------------------------------------------------------
// Event types — AdventureBoxHooks
// ---------------------------------------------------------------------------
struct ButchBoxInitEvent : EventBase {
    ButchBox *box;
    bool param2;
};

struct ButchBoxTryPlaceCatEvent : EventBase {
    ButchBox *box;
    void *cat;
    bool returnValue = false;
};

struct ButchBoxConfigCatsEvent : EventBase {
    ButchBox *box;
};

struct ButchBoxTryRemoveCatEvent : EventBase {
    ButchBox *box;
    void *cat;
};

struct ButchBoxDepartEvent : EventBase {
    ButchBox *box;
    bool returnValue = false;
};

struct LoadAdventureEvent : EventBase {
    LoadAdventureArgs *args;
};

// ---------------------------------------------------------------------------
// Event types — ClassChooserHooks
// ---------------------------------------------------------------------------
struct CatSelectorInitEvent : EventBase {
    void *selector;
    int64_t param2;
};

struct ClassTagBoxClickEvent : EventBase {
    void *tagBox;
    int64_t catID;
    int32_t clickedIndex;
};

struct ClassChooserLockInEvent : EventBase {
    void *lambdaThis;
};

// ---------------------------------------------------------------------------
// Event types — StorageHooks
// ---------------------------------------------------------------------------
struct SceneManagerCreateSceneEvent : EventBase {
    void *self;
    void *nameStr;
    void *returnValue = nullptr;
};

struct SceneAddComponentEvent : EventBase {
    void *scene;
    void *comp;
};

struct InventoryItemBoxClickEvent : EventBase {
    void *self;
};

struct InventoryScreen2CloseEvent : EventBase {
    void *self;
};

// ---------------------------------------------------------------------------
// Event types — MapHooks
// ---------------------------------------------------------------------------
struct MapNodeClickEvent : EventBase {
    void *self;
    uint32_t nodeIndex;
};

struct MapScreenEnterNodeEvent : EventBase {
    void *self;
    void *node;
};

// ---------------------------------------------------------------------------
// Event types — ActSelectionHooks
// ---------------------------------------------------------------------------
struct ActSelectionScreenInitEvent : EventBase {
    void *self;
};

struct ActSelectionScreenSelectActEvent : EventBase {
    void *self;
    int actIndex;
};

// ---------------------------------------------------------------------------
// Event types — LevelUpHooks
// ---------------------------------------------------------------------------
struct LevelUpScreenInitEvent : EventBase {
    glaiel::LevelUpScreen *self;
    PersistentCharacter *cat;
};

struct LevelUpScreenSelectOptionEvent : EventBase {
    glaiel::LevelUpScreen *self;
    glaiel::LevelUpOption *option;
    int optionIndex;
};

struct LevelUpScreenRerollEvent : EventBase {
    glaiel::LevelUpScreen *self;
};

struct AbilityChooserInitEvent : EventBase {
    glaiel::AbilityChooser *self;
    PersistentCharacter *cat;
};

struct AbilityChooserSelectSlotEvent : EventBase {
    glaiel::AbilityChooser *self;
    uint32_t slotIndex;
};

// ---------------------------------------------------------------------------
// Event types — WorldEventHooks
// ---------------------------------------------------------------------------
struct WorldEventInitEvent : EventBase {
    glaiel::WorldEvent *self;
};

struct WorldEventClickOptionEvent : EventBase {
    glaiel::WorldEvent *self;
    glaiel::WorldEventOption *clickedOption;
    int optionIndex;
};

struct WorldEventClickCatEvent : EventBase {
    glaiel::WorldEvent *self;
    glaiel::WorldEventCatButton *clickedButton;
    PersistentCharacter *clickedCat;
};

struct WorldEventClickEnd1Event : EventBase {
    glaiel::WorldEvent *self;
};

struct WorldEventClickEnd2Event : EventBase {
    glaiel::WorldEvent *self;
};

// ---------------------------------------------------------------------------
// Logging — redirectable so ParaboxAPI stays decoupled from Overlay.h
// Mewtiplayer calls SetLogCallback(Overlay::LogV) during initialisation.
// If no callback is set, messages go to OutputDebugStringA.
// ---------------------------------------------------------------------------
using LogCallback = void(*)(const char *fmt, va_list args);

PARABOX_API void SetLogCallback(LogCallback cb);
PARABOX_API void Log(const char *fmt, ...);

// ---------------------------------------------------------------------------
// Global event instances — exported so other mods can subscribe
// ---------------------------------------------------------------------------
extern PARABOX_API Event<RunFrameEvent>    OnRunFrame;
extern PARABOX_API Event<StevenSpawnEvent> OnStevenSpawn;

// Combat events
extern PARABOX_API Event<TurnStartEvent>              OnTurnStart;
extern PARABOX_API Event<BeginTurnEvent>              OnBeginTurn;
extern PARABOX_API Event<FightEndEvent>               OnFightEnd;
extern PARABOX_API Event<AbilityTriggerEvent>         OnAbilityTrigger;
extern PARABOX_API Event<EnqueueActionEvent>          OnEnqueueAction;
extern PARABOX_API Event<FaceDirectionEvent>          OnFaceDirection;
extern PARABOX_API Event<SlotUpdateDynamicValueEvent> OnSlotUpdateDynamicValue;
extern PARABOX_API Event<ProcessCombatInputEvent>     OnProcessCombatInput;
extern PARABOX_API Event<RouteCombatInputEvent>       OnRouteCombatInput;

PARABOX_API void ForceEnqueueAction(void *queue, TurnAction *actionData);
PARABOX_API void ForceAbilityTrigger(Ability *ability, TurnAction *turnAction);
PARABOX_API void ForceFaceDirection(void* character, uint64_t packed, bool anim, bool force);
PARABOX_API void ForceSlotUpdateDynamicValue(void* rcx, void* rdx);
// Save events
extern PARABOX_API Event<CreateStrayCatEvent>         OnCreateStrayCat;
extern PARABOX_API Event<GetCollarVectorEvent>        OnGetCollarVector;

// AdventureBox events
extern PARABOX_API Event<ButchBoxInitEvent>           OnButchBoxInit;
extern PARABOX_API Event<ButchBoxTryPlaceCatEvent>    OnButchBoxTryPlaceCat;
extern PARABOX_API Event<ButchBoxConfigCatsEvent>     OnButchBoxConfigCats;
extern PARABOX_API Event<ButchBoxTryRemoveCatEvent>   OnButchBoxTryRemoveCat;
extern PARABOX_API Event<ButchBoxDepartEvent>         OnButchBoxDepart;
extern PARABOX_API Event<LoadAdventureEvent>          OnLoadAdventure;

// ClassChooser events
extern PARABOX_API Event<CatSelectorInitEvent>        OnCatSelectorInit;
extern PARABOX_API Event<ClassTagBoxClickEvent>       OnClassTagBoxClick;
extern PARABOX_API Event<ClassChooserLockInEvent>     OnClassChooserLockIn;

// Storage events
extern PARABOX_API Event<SceneManagerCreateSceneEvent> OnSceneManagerCreateScene;
extern PARABOX_API Event<SceneAddComponentEvent>       OnSceneAddComponent;
extern PARABOX_API Event<InventoryItemBoxClickEvent>   OnInventoryItemBoxClick;
extern PARABOX_API Event<InventoryScreen2CloseEvent>   OnInventoryScreen2Close;

// Map events
extern PARABOX_API Event<MapNodeClickEvent>           OnMapNodeClick;
extern PARABOX_API Event<MapScreenEnterNodeEvent>     OnMapScreenEnterNode;

// ActSelection events
extern PARABOX_API Event<ActSelectionScreenInitEvent>      OnActSelectionScreenInit;
extern PARABOX_API Event<ActSelectionScreenSelectActEvent> OnActSelectionScreenSelectAct;

// LevelUp events
extern PARABOX_API Event<LevelUpScreenInitEvent>           OnLevelUpScreenInit;
extern PARABOX_API Event<LevelUpScreenSelectOptionEvent>   OnLevelUpScreenSelectOption;
extern PARABOX_API Event<LevelUpScreenRerollEvent>         OnLevelUpScreenReroll;
extern PARABOX_API Event<AbilityChooserInitEvent>          OnAbilityChooserInit;
extern PARABOX_API Event<AbilityChooserSelectSlotEvent>    OnAbilityChooserSelectSlot;

// WorldEvent events
extern PARABOX_API Event<WorldEventInitEvent>        OnWorldEventInit;
extern PARABOX_API Event<WorldEventClickOptionEvent> OnWorldEventClickOption;
extern PARABOX_API Event<WorldEventClickCatEvent>    OnWorldEventClickCat;
extern PARABOX_API Event<WorldEventClickEnd1Event>   OnWorldEventClickEnd1;
extern PARABOX_API Event<WorldEventClickEnd2Event>   OnWorldEventClickEnd2;

PARABOX_API void InstallHooks(MewjectorAPI *mj, uintptr_t gameBase);
PARABOX_API void* GameAllocate(size_t size);

// ClassChooser API
PARABOX_API bool IsCatSelectorValid(const void *selector);
PARABOX_API void RefreshCatSelectorUI();
PARABOX_API PersistentCharacter *GetPersistentCharacterById(int64_t catID);
PARABOX_API void ApplyCollarToCharacter(PersistentCharacter *cat, const char *collarName);
PARABOX_API void RefreshClassChooserInventory();
PARABOX_API void UpdateClassChooserTagBoxes(int64_t catID, int32_t collarIndex);
PARABOX_API const char *ResolveCollarNameFromIndex(int32_t collarIndex);
PARABOX_API void ForceClassChooserLockIn(void *lambdaThis);

// Storage API
PARABOX_API int32_t FindStorageSlotIndex(const void *clickedBox);
PARABOX_API void UpdateStorageItemSlot(int32_t slotIndex, int64_t catID);
PARABOX_API int64_t ResolveSelectedCatID();
PARABOX_API void ForceInventoryScreen2Close(void *self);
PARABOX_API void *GetMapScreen();
PARABOX_API void SetMapScreen(void *screen);

// Map API
PARABOX_API void ForceMapNodeClick(void* matchedNode);

// ActSelection API
PARABOX_API void *GetActSelectionScreen();
PARABOX_API void ForceActSelectionScreenSelectAct(void *screen, int actIndex);

// LevelUp API
PARABOX_API glaiel::LevelUpScreen *GetActiveLevelUpScreen();
PARABOX_API glaiel::AbilityChooser *GetActiveAbilityChooser();
PARABOX_API void ForceLevelUpScreenSelectOption(glaiel::LevelUpScreen *self, glaiel::LevelUpOption *option);
PARABOX_API void ForceLevelUpScreenReroll(glaiel::LevelUpScreen *self);
PARABOX_API void ForceAbilityChooserSelectSlot(glaiel::AbilityChooser *self, uint32_t slotIndex);

// WorldEvent API
PARABOX_API glaiel::WorldEvent *GetActiveWorldEvent();
PARABOX_API void ForceWorldEventSelectOption(int64_t catUID, uint32_t optionIndex);
PARABOX_API void ForceWorldEventSelectCat(int64_t selectedCatUID);
PARABOX_API void ForceWorldEventClickEnd(uint8_t buttonType);

} // namespace ParaboxAPI
