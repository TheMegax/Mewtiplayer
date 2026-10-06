#include "hooks/CombatHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "Scanner.h"

HOOK_DEFINE(TurnStart, void, TurnControl*)
HOOK_DEFINE(BeginTurn, void, Character*, int)
HOOK_DEFINE(FightEnd, void, Level*)
HOOK_DEFINE(AbilityTrigger, void*, Ability*, TurnAction*)
HOOK_DEFINE(EnqueueAction, void*, void*, TurnAction*)
HOOK_DEFINE(FaceDirection, void, void*, uint64_t, bool, bool)
HOOK_DEFINE(SlotUpdateDynamicValue, void*, void*, void*)
HOOK_DEFINE(ProcessCombatInput, void*, PlayerBrain*, void*)
HOOK_DEFINE(RouteCombatInput, void*, PlayerBrain*, void*, void*, void*)
HOOK_DEFINE(PossessionUpdate, void, void*, unsigned char)
HOOK_DEFINE(CombatMenuShow, void, void*, void*, void*)
HOOK_DEFINE(CombatMenuHide, void, void*)

using ShowCombatPopup_t = void(__fastcall *)(void *self, MsvcReleaseModeXString *text, void *entityOverride);
using PostPopupAnim_t   = void(__fastcall *)(void *self, MsvcReleaseModeXString *animName);

static ShowCombatPopup_t g_fnShowCombatPopup = nullptr;
static PostPopupAnim_t   g_fnPostPopupAnim = nullptr;

static void Hook_TurnStart(TurnControl *tc) {
    ParaboxAPI::TurnStartEvent ev = {};
    ev.tc = tc;
    ParaboxAPI::OnTurnStart.Publish(ev);
    if (!ev.cancelled && g_origTurnStart)
        g_origTurnStart(tc);
}

static void Hook_BeginTurn(Character *character, int kind) {
    ParaboxAPI::BeginTurnEvent ev = {};
    ev.character = character;
    ev.kind = kind;
    ParaboxAPI::OnBeginTurn.Publish(ev);
    if (!ev.cancelled && g_origBeginTurn)
        g_origBeginTurn(character, kind);
}

void __fastcall Hook_AbilityTrigger(Ability *ability, TurnAction *turnAction) {
    ParaboxAPI::AbilityTriggerEvent ev = {};
    ev.ability = ability;
    ev.turnAction = turnAction;
    ParaboxAPI::OnAbilityTrigger.Publish(ev);
    if (!ev.cancelled && g_origAbilityTrigger)
        g_origAbilityTrigger(ability, turnAction);
}

void ParaboxAPI::ForceAbilityTrigger(Ability *ability, TurnAction *turnAction) {
    if (g_origAbilityTrigger)
        g_origAbilityTrigger(ability, turnAction);
}

static void *__fastcall Hook_EnqueueAction(void *queue, TurnAction *actionData) {
    ParaboxAPI::EnqueueActionEvent ev = {};
    ev.queue = queue;
    ev.actionData = actionData;
    ParaboxAPI::OnEnqueueAction.Publish(ev);
    
    if (ev.cancelled)
        return ev.returnValue;

    void *result = nullptr;
    if (g_origEnqueueAction)
        result = g_origEnqueueAction(queue, actionData);

    return result;
}

static void Hook_FightEnd(Level *combat) {
    ParaboxAPI::FightEndEvent ev = {};
    ev.level = combat;
    ParaboxAPI::OnFightEnd.Publish(ev);
    if (!ev.cancelled && g_origFightEnd)
        g_origFightEnd(combat);
}

static void Hook_FaceDirection(void *character, const uint64_t target_packed,
                               const bool play_animation, const bool force) {
    ParaboxAPI::FaceDirectionEvent ev = {};
    ev.character = character;
    ev.target_packed = target_packed;
    ev.play_animation = play_animation;
    ev.force = force;
    ParaboxAPI::OnFaceDirection.Publish(ev);
    if (!ev.cancelled && g_origFaceDirection)
        g_origFaceDirection(character, target_packed, play_animation, force);
}

static void Hook_SlotUpdateDynamicValue(void *rcx, void *rdx) {
    ParaboxAPI::SlotUpdateDynamicValueEvent ev = {};
    ev.rcx = rcx;
    ev.rdx = rdx;
    ParaboxAPI::OnSlotUpdateDynamicValue.Publish(ev);
    
    if (!ev.cancelled && g_origSlotUpdateDynamicValue) {
        g_origSlotUpdateDynamicValue(rcx, rdx);
    }
}

static void *__fastcall Hook_ProcessCombatInput(PlayerBrain *ctx, void *outResult) {
    ParaboxAPI::ProcessCombatInputEvent ev = {};
    ev.ctx = ctx;
    ev.outResult = outResult;
    ParaboxAPI::OnProcessCombatInput.Publish(ev);
    
    if (ev.cancelled)
        return ev.returnValue;

    void *result = g_origProcessCombatInput(ctx, outResult);

    ParaboxAPI::PostProcessCombatInputEvent postEv = {};
    postEv.ctx = ctx;
    postEv.actionData = static_cast<TurnAction*>(result ? result : outResult);
    ParaboxAPI::OnPostProcessCombatInput.Publish(postEv);

    return result;
}

static void *__fastcall Hook_RouteCombatInput(PlayerBrain *ctx, void *outResult, void *param3, void *param4) {
    ParaboxAPI::RouteCombatInputEvent ev = {};
    ev.ctx = ctx;
    ev.outResult = outResult;
    ev.param3 = param3;
    ev.param4 = param4;
    ParaboxAPI::OnRouteCombatInput.Publish(ev);
    
    if (ev.cancelled)
        return ev.returnValue;
        
    return g_origRouteCombatInput(ctx, outResult, param3, param4);
}

static void __fastcall Hook_PossessionUpdate(void *character, unsigned char param_2) {
    if (!character) return;
    auto *c = static_cast<Character *>(character);
    if (!c->current_ability) return; // Prevent crash when ability at offset 0xC8 is NULL

    if (g_origPossessionUpdate) {
        g_origPossessionUpdate(character, param_2);
    }
}

static void __fastcall Hook_CombatMenuShow(void *menu, void *actions, void *param3) {
    ParaboxAPI::CombatMenuShowEvent ev = {};
    ev.menu = menu;
    ev.actions = actions;
    ev.param3 = param3;
    ParaboxAPI::OnCombatMenuShow.Publish(ev);

    if (ev.cancelled)
        return;

    if (g_origCombatMenuShow)
        g_origCombatMenuShow(menu, actions, param3);
}

static void __fastcall Hook_CombatMenuHide(void *menu) {
    ParaboxAPI::CombatMenuHideEvent ev = {};
    ev.menu = menu;
    ParaboxAPI::OnCombatMenuHide.Publish(ev);

    if (ev.cancelled)
        return;

    if (g_origCombatMenuHide)
        g_origCombatMenuHide(menu);
}

void CombatHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
    HOOK_INSTALL(mj, gameBase, BeginTurn, GameSymbols::Character_BeginTurn, 16);
    HOOK_INSTALL(mj, gameBase, FightEnd, GameSymbols::Level_update, 15);
    HOOK_INSTALL(mj, gameBase, AbilityTrigger, GameSymbols::Ability_trigger, 15);
    HOOK_INSTALL(mj, gameBase, EnqueueAction, GameSymbols::TurnControl_QueueAction, 15);
    HOOK_INSTALL(mj, gameBase, TurnStart, GameSymbols::TurnControl_NextTurn, 14);
    HOOK_INSTALL(mj, gameBase, FaceDirection, GameSymbols::Character_Face, 14);
    HOOK_INSTALL(mj, gameBase, SlotUpdateDynamicValue, GameSymbols::Ability_ComputeX, 15);
    HOOK_INSTALL(mj, gameBase, ProcessCombatInput, GameSymbols::PlayerBrain_OnRequestAction, 15);
    HOOK_INSTALL(mj, gameBase, RouteCombatInput, GameSymbols::MountBrain_OnRequestAction, 15);
    HOOK_INSTALL(mj, gameBase, PossessionUpdate, GameSymbols::Character_CompleteAbilityNow, 16);
    HOOK_INSTALL(mj, gameBase, CombatMenuShow, GameSymbols::CombatMenu_show, 15);
    HOOK_INSTALL(mj, gameBase, CombatMenuHide, GameSymbols::CombatMenu_hide, 15);

    RESOLVE_FUNC(gameBase, GameSymbols::Passive_DisplayText, g_fnShowCombatPopup);
    RESOLVE_FUNC(gameBase, GameSymbols::Passive_TickSound, g_fnPostPopupAnim);
}

namespace ParaboxAPI {
PARABOX_API void ForceFaceDirection(void* character, uint64_t packed, bool anim, bool force) {
    if (g_origFaceDirection) {
        g_origFaceDirection(character, packed, anim, force);
    }
}

PARABOX_API void ForceSlotUpdateDynamicValue(void* rcx, void* rdx) {
    if (g_origSlotUpdateDynamicValue) {
        g_origSlotUpdateDynamicValue(rcx, rdx);
    }
}

PARABOX_API void ForceCombatMenuHide(void *menu) {
    if (g_origCombatMenuHide) {
        g_origCombatMenuHide(menu);
    }
}

PARABOX_API void ForceCombatMenuShow(void *menu, void *actions, void *param3) {
    if (g_origCombatMenuShow) {
        g_origCombatMenuShow(menu, actions, param3);
    }
}

static void MakeInlineXString(MsvcReleaseModeXString &xs, const char *str, size_t len) {
    xs.Mysize = len;
    if (len < 16) {
        xs.Myres = 15;
        memcpy(xs.Bx.Buf, str, len);
        xs.Bx.Buf[len] = '\0';
    } else {
        auto buf = static_cast<char *>(GameAllocate(len + 1));
        memcpy(buf, str, len);
        buf[len] = '\0';
        xs.Bx.Ptr = buf;
        xs.Myres = len;
    }
}

PARABOX_API void ShowCombatPopup(Character *character, const char *text, float speedScale) {
    if (!g_fnShowCombatPopup || !character || !text || text[0] == '\0') return;

    const size_t len = strlen(text);
    MsvcReleaseModeXString xs = {};
    MakeInlineXString(xs, text, len);

    const auto *comp = reinterpret_cast<const Component *>(character);
    Scene *scene = (comp && comp->entity) ? comp->entity->scene : nullptr;
    const uint32_t countBefore = scene ? scene->CompList_update.unsorted_.size_ : 0;

    g_fnShowCombatPopup(character, &xs, character);

    if (g_fnPostPopupAnim) {
        MsvcReleaseModeXString emptyAnim = {};
        emptyAnim.Mysize = 0;
        emptyAnim.Myres = 15;
        emptyAnim.Bx.Buf[0] = '\0';
        g_fnPostPopupAnim(character, &emptyAnim);
    }

    if (scene && scene->CompList_update.unsorted_.size_ > countBefore && scene->CompList_update.unsorted_.data_) {
        if (Component *c = scene->CompList_update.unsorted_.data_[scene->CompList_update.unsorted_.size_ - 1]) {
            if (c->entity) {
                c->entity->timescale = static_cast<double>(speedScale);
            }
        }
    }
}

}
