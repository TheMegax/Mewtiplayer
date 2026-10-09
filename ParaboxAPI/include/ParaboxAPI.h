#pragma once
#include <functional>
#include <vector>
#include <cstdarg>
#include <cstdint>
#include "mewjector.h"
#include "ParaboxArray.h"

#ifdef PARABOX_EXPORTS
#define PARABOX_API __declspec(dllexport)
#else
#define PARABOX_API __declspec(dllimport)
#endif

#ifndef IVEC2D_DEFINED
#define IVEC2D_DEFINED
struct iVec2D {
    int32_t x;
    int32_t y;
};
#endif

struct Character;
struct TurnControl;
struct Level;
struct Ability;
struct TurnAction;
struct PlayerBrain;
struct ButchBox;
struct LoadAdventureArgs;
struct CatData;

struct LevelUpScreen;
struct LevelUpOption;
struct AbilityChooser;
struct WorldEvent;
struct WorldEventOption;
struct WorldEventCatButton;
struct ClassChooser;
struct ClassTagBox;
struct InventoryScreen2;
struct InventoryItemBox;
struct Equipment;

namespace ParaboxAPI {

// ---------------------------------------------------------------------------
// Generic Event Publisher
// ---------------------------------------------------------------------------
template <typename T>
class Event {
public:
    using Callback = void(*)(T &);

    void Subscribe(Callback cb) {
        if (m_count < 32 && cb) {
            m_subscribers[m_count++] = cb;
        }
    }

    void Publish(T &eventData) {
        for (int i = 0; i < m_count; i++) {
            if (m_subscribers[i]) {
                m_subscribers[i](eventData);
            }
        }
    }

    void Clear() {
        m_count = 0;
    }

private:
    Callback m_subscribers[32] = {};
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
    Level *level;
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
    PlayerBrain *ctx;
    void *outResult;
    void *returnValue = nullptr;
};

struct PostProcessCombatInputEvent : EventBase {
    PlayerBrain *ctx;
    TurnAction *actionData;
};

struct RouteCombatInputEvent : EventBase {
    PlayerBrain *ctx;
    void *outResult;
    void *param3;
    void *param4;
    void *returnValue = nullptr;
};

struct PlayerBrainUpdateEvent : EventBase {
    PlayerBrain *brain;
};

struct CombatMenuShowEvent : EventBase {
    void *menu;
    void *actions;
    void *param3;
};

struct CombatMenuHideEvent : EventBase {
    void *menu;
};

struct ButtonCanActivateEvent : EventBase {
    void *button;
    int32_t button_index;
    uint8_t strict_mouse;
    uint8_t returnValue;
};

struct ButtonActivateEvent : EventBase {
    void *button;
    uint8_t from_mouse;
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

struct SceneAddedEvent : EventBase {
    void *director;
    void *scene;
    const char *sceneName;
};

struct SceneAddComponentEvent : EventBase {
    void *scene;
    void *comp;
};

struct InventoryItemBoxClickEvent : EventBase {
    void *self;
};

struct InventoryItemBoxEquippedEvent : EventBase {
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

struct ResumeMapEvent : EventBase {
    void *mewDirector;
    void *mapScene;
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
    LevelUpScreen *self;
    CatData *cat;
};

struct LevelUpScreenSelectOptionEvent : EventBase {
    LevelUpScreen *self;
    LevelUpOption *option;
    int optionIndex;
};

struct LevelUpScreenRerollEvent : EventBase {
    LevelUpScreen *self;
};

struct AbilityChooserInitEvent : EventBase {
    AbilityChooser *self;
    CatData *cat;
};

struct AbilityChooserSelectSlotEvent : EventBase {
    AbilityChooser *self;
    uint32_t slotIndex;
};

// ---------------------------------------------------------------------------
// Event types — WorldEventHooks
// ---------------------------------------------------------------------------
struct WorldEventInitEvent : EventBase {
    WorldEvent *self;
};

struct WorldEventClickOptionEvent : EventBase {
    WorldEvent *self;
    WorldEventOption *clickedOption;
    int optionIndex;
};

struct WorldEventClickCatEvent : EventBase {
    WorldEvent *self;
    WorldEventCatButton *clickedButton;
    CatData *clickedCat;
};

struct WorldEventClickEnd1Event : EventBase {
    WorldEvent *self;
};

struct WorldEventClickEnd2Event : EventBase {
    WorldEvent *self;
};

struct WorldEventClickEndCustomEvent : EventBase {
    WorldEvent *self;
    const char *tokenString;
};

// ---------------------------------------------------------------------------
// Event types — PopupHooks
// ---------------------------------------------------------------------------

/// Fired just before any YesNoPrompt dialog is shown (native or custom).
/// `prompt` is the raw UTF-8 body text. Cancel to suppress the popup.
struct PopupShowEvent : EventBase {
    void       *self;    ///< YesNoPrompt component pointer (may be null during construction)
    const char *prompt;  ///< Dialog body text (read-only, valid only during the callback)
    bool        okOnly;  ///< True if this is a single-button OK variant
};

/// Fired when the user clicks YES (choice == true) or NO (choice == false)
/// on a popup created via ShowYesNoPopup / ShowOkPopup.
struct PopupChoiceEvent : EventBase {
    void *self;   ///< YesNoPrompt component pointer (may be null)
    bool  choice; ///< true = YES clicked, false = NO clicked
};


// ---------------------------------------------------------------------------
// Logging callback. Can be redirected with SetLogCallback().
// If no callback is set, messages go to OutputDebugStringA.
// ---------------------------------------------------------------------------
using LogCallback = void(*)(const char *fmt, va_list args);

PARABOX_API void SetLogCallback(LogCallback cb);
PARABOX_API void Log(const char *fmt, ...);

// ---------------------------------------------------------------------------
// Global event instances
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
extern PARABOX_API Event<PostProcessCombatInputEvent> OnPostProcessCombatInput;
extern PARABOX_API Event<RouteCombatInputEvent>       OnRouteCombatInput;
extern PARABOX_API Event<CombatMenuShowEvent>         OnCombatMenuShow;
extern PARABOX_API Event<CombatMenuHideEvent>         OnCombatMenuHide;
extern PARABOX_API Event<ButtonCanActivateEvent>      OnButtonCanActivate;
extern PARABOX_API Event<ButtonActivateEvent>         OnButtonActivate;
extern PARABOX_API Event<PlayerBrainUpdateEvent>      OnPlayerBrainUpdate;

PARABOX_API void ForceEnqueueAction(void *queue, TurnAction *actionData);
PARABOX_API void ForceAbilityTrigger(Ability *ability, TurnAction *turnAction);
PARABOX_API void ForceFaceDirection(void* character, uint64_t packed, bool anim, bool force);
PARABOX_API void ForceSlotUpdateDynamicValue(void* rcx, void* rdx);
PARABOX_API void ForceCombatMenuHide(void *menu);
PARABOX_API void ForceCombatMenuShow(void *menu, void *actions, void *param3 = nullptr);
PARABOX_API void DrawAbilityRange(void *brain, Ability *ability, int param2 = -1);
PARABOX_API void DrawAbilityAOE(void *brain, Ability *ability, iVec2D targetTile, iVec2D orientation, int param5 = 0);

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
extern PARABOX_API Event<SceneAddedEvent>              OnSceneAdded;
extern PARABOX_API Event<SceneAddComponentEvent>       OnSceneAddComponent;
extern PARABOX_API Event<InventoryItemBoxClickEvent>   OnInventoryItemBoxClick;
extern PARABOX_API Event<InventoryItemBoxEquippedEvent> OnInventoryItemBoxEquipped;
extern PARABOX_API Event<InventoryScreen2CloseEvent>   OnInventoryScreen2Close;

// Map events
extern PARABOX_API Event<MapNodeClickEvent>           OnMapNodeClick;
extern PARABOX_API Event<MapScreenEnterNodeEvent>     OnMapScreenEnterNode;
extern PARABOX_API Event<ResumeMapEvent>              OnResumeMap;

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
extern PARABOX_API Event<WorldEventClickEndCustomEvent> OnWorldEventClickEndCustom;

// Popup events
extern PARABOX_API Event<PopupShowEvent>   OnPopupShow;
extern PARABOX_API Event<PopupChoiceEvent> OnPopupChoice;

PARABOX_API void InstallHooks(MewjectorAPI *mj, uintptr_t gameBase);
PARABOX_API void* GameAllocate(size_t size);

// ClassChooser API
PARABOX_API void* GetActiveCatSelector();
PARABOX_API bool IsCatSelectorValid(const void *selector);
PARABOX_API void RefreshCatSelectorUI();
PARABOX_API CatData *GetCatDataById(int64_t catID);
PARABOX_API void ApplyCollarToCharacter(CatData *cat, const char *collarName);
PARABOX_API void RefreshClassChooserInventory();
PARABOX_API void ClearClassTagBoxes();
PARABOX_API void RegisterClassTagBox(void *comp);
PARABOX_API int32_t FindTagBoxIndex(const void *tagBoxPtr, const void *classChooserPtr);
PARABOX_API void UpdateClassChooserTagBoxes(int64_t catID, int32_t collarIndex);
PARABOX_API const char *ResolveCollarNameFromIndex(int32_t collarIndex);
PARABOX_API void ForceClassChooserLockIn(void *lambdaThis);
PARABOX_API ClassChooser* GetActiveClassChooser();
PARABOX_API void ResetCatOnClassChooser(ClassChooser *chooser, int64_t catID, bool restoreDefaults = true);
PARABOX_API void ForceClassChooserClose(ClassChooser *chooser = nullptr);

// Storage API
PARABOX_API extern bool g_isHandlingNetworkStorageItemSync;
PARABOX_API int32_t FindStorageSlotIndex(const void *clickedBox);
PARABOX_API void UpdateStorageItemSlot(int32_t slotIndex, int64_t catID);
PARABOX_API int64_t ResolveSelectedCatID();
PARABOX_API void ForceInventoryScreen2Close(void *self);
PARABOX_API void *GetMapScreen();
PARABOX_API InventoryScreen2* GetActiveInventoryScreen2();
PARABOX_API void UpdateStorageItem(int64_t itemID, int64_t catID);
PARABOX_API int64_t GetItemEquippedOwner(int64_t itemID);
PARABOX_API int32_t GetItemSortOrder(const void *itemBox);
PARABOX_API int64_t GetSortOrderItemEquippedOwner(int32_t sortOrder);
PARABOX_API void UpdateStorageItemBySortOrder(int32_t sortOrder, int64_t catID);
PARABOX_API Equipment *GetActiveInventoryItemBySortOrder(int32_t sortOrder, InventoryItemBox **outBox = nullptr);


// Map API
PARABOX_API extern bool g_isHandlingNetworkMapNodeSync;
PARABOX_API void ForceMapNodeClick(void* matchedNode);
PARABOX_API void ForceMapInventoryOpen();

// ActSelection API
PARABOX_API void *GetActSelectionScreen();
PARABOX_API void ForceActSelectionScreenSelectAct(void *screen, int actIndex);

// LevelUp API
PARABOX_API LevelUpScreen *GetActiveLevelUpScreen();
PARABOX_API AbilityChooser *GetActiveAbilityChooser();
PARABOX_API void ForceLevelUpScreenSelectOption(LevelUpScreen *self, LevelUpOption *option);
PARABOX_API void ForceLevelUpScreenReroll(LevelUpScreen *self);
PARABOX_API void ForceAbilityChooserSelectSlot(AbilityChooser *self, uint32_t slotIndex);

// WorldEvent API
PARABOX_API WorldEvent *GetActiveWorldEvent();
PARABOX_API void ForceWorldEventSelectOption(int64_t catUID, uint32_t optionIndex);
PARABOX_API void ForceWorldEventSelectCat(int64_t selectedCatUID);
PARABOX_API void ForceWorldEventClickEnd(uint8_t buttonType);

// Combat API
PARABOX_API void ShowCombatPopup(Character *character, const char *text, float speedScale = 0.35f);

// Popup API
/// Show a Yes/No confirmation dialog using the game's YesNoPrompt system.
/// `prompt` is a UTF-8 string (narrow char*).
/// onYes / onNo are called (and OnPopupChoice fired) when the user clicks.
/// Returns false if the popup could not be opened (scene is busy/destroying).
PARABOX_API bool ShowYesNoPopup(
    const char           *prompt,
    std::function<void()> onYes = nullptr,
    std::function<void()> onNo  = nullptr);

/// Wide-string overload: converts prompt to UTF-8 and calls ShowYesNoPopup.
PARABOX_API bool ShowYesNoPopup(
    const wchar_t        *prompt,
    std::function<void()> onYes = nullptr,
    std::function<void()> onNo  = nullptr);

/// Show a single-button OK dialog.  onOk is called on dismiss.
PARABOX_API bool ShowOkPopup(
    const char           *prompt,
    std::function<void()> onOk = nullptr);

PARABOX_API bool ShowOkPopup(
    const wchar_t        *prompt,
    std::function<void()> onOk = nullptr);

PARABOX_API int32_t GetClassTagBoxCount();
PARABOX_API int32_t GetStorageSlotCount();
PARABOX_API int64_t GetStorageSlotEquippedOwner(int32_t slotIndex);
PARABOX_API void RefreshInventoryEquippedStatus();

} // namespace ParaboxAPI
