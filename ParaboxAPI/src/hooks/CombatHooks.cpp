#include "hooks/CombatHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "Scanner.h"

HOOK_DEFINE(TurnStart, void, TurnControl*)
HOOK_DEFINE(BeginTurn, void, Character*, int)
HOOK_DEFINE(FightEnd, void, CombatResolutionState*)
HOOK_DEFINE(AbilityTrigger, void*, Ability*, TurnAction*)
HOOK_DEFINE(EnqueueAction, void*, void*, TurnAction*)
HOOK_DEFINE(FaceDirection, void, void*, uint64_t, bool, bool)
HOOK_DEFINE(SlotUpdateDynamicValue, void*, void*, void*)
HOOK_DEFINE(ProcessCombatInput, void*, CombatUIContext*, void*)
HOOK_DEFINE(RouteCombatInput, void*, CombatUIContext*, void*, void*, void*)
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

namespace ParaboxAPI {
    EnqueueResultCallback GetEnqueueResultCallback();
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

    if (const auto cb = ParaboxAPI::GetEnqueueResultCallback())
        cb(result);

    return result;
}

static void Hook_FightEnd(CombatResolutionState *combat) {
    ParaboxAPI::FightEndEvent ev = {};
    ev.combat = combat;
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

static void *__fastcall Hook_ProcessCombatInput(CombatUIContext *ctx, void *outResult) {
    ParaboxAPI::ProcessCombatInputEvent ev = {};
    ev.ctx = ctx;
    ev.outResult = outResult;
    ParaboxAPI::OnProcessCombatInput.Publish(ev);
    
    if (ev.cancelled)
        return ev.returnValue;

    return g_origProcessCombatInput(ctx, outResult);
}

static void *__fastcall Hook_RouteCombatInput(CombatUIContext *ctx, void *outResult, void *param3, void *param4) {
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
    if (!c->possessionComponent) return; // Prevent crash when possession component at offset 0xC8 is NULL

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
    HOOK_INSTALL(mj, gameBase, BeginTurn,
        "48 89 5C 24 08 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 B0 FC FF FF", 16);

    HOOK_INSTALL(mj, gameBase, FightEnd,
        "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81 EC 18 01 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 88 44 0F 29 88 78 FF FF FF 4C 8B F1", 15);

    HOOK_INSTALL(mj, gameBase, AbilityTrigger,
        "48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 68 FD FF FF", 15);

    HOOK_INSTALL(mj, gameBase, EnqueueAction,
        "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 48 89 54 24 10 57 48 83 EC 20 48 8B FA 83 3A 01", 15);

    HOOK_INSTALL(mj, gameBase, TurnStart,
        "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 28 EF FF FF B8 D8 11 00 00 E8 ? ? ? ? 48 2B E0 0F 29 B4 24 C0 11 00 00 48 8B F1", 14);

    HOOK_INSTALL(mj, gameBase, FaceDirection,
        "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 00 FD FF FF", 14);

    HOOK_INSTALL(mj, gameBase, SlotUpdateDynamicValue,
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 60 48 8B D9 4C 8B 41 10", 15);

    HOOK_INSTALL(mj, gameBase, ProcessCombatInput,
        "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 4C 89 64 24 20 55 41 56 41 57 48 8D 6C 24 B9 48 81 EC D0 00 00 00", 15);

    HOOK_INSTALL(mj, gameBase, RouteCombatInput,
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 40 48 8B 41 38", 15);

    HOOK_INSTALL(mj, gameBase, PossessionUpdate,
        "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 81 EC 40 01 00 00", 16);

    HOOK_INSTALL(mj, gameBase, CombatMenuShow,
        "48 8B C4 48 89 58 18 48 89 50 10 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 C8 FD FF FF", 15);

    HOOK_INSTALL(mj, gameBase, CombatMenuHide,
        "48 89 5C 24 18 48 89 6C 24 20 57 41 56 41 57 48 83 EC 50 80 B9 40 01 00 00 00", 15);

    SCAN_SET(mj, gameBase, ShowCombatPopup,
        "4C 89 44 24 18 48 89 54 24 10 53 56 57 48 83 EC 60 49 8B F8 48 8B DA 4D 85 C0",
        g_fnShowCombatPopup);

    SCAN_SET(mj, gameBase, PostPopupAnim,
        "48 89 5C 24 08 48 89 74 24 20 48 89 54 24 10 57 48 83 EC 70 48 8B DA 33 F6 48 8B 79 38",
        g_fnPostPopupAnim);
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
    const int countBefore = scene ? scene->damageNumberCount : 0;

    g_fnShowCombatPopup(character, &xs, character);

    if (g_fnPostPopupAnim) {
        MsvcReleaseModeXString emptyAnim = {};
        emptyAnim.Mysize = 0;
        emptyAnim.Myres = 15;
        emptyAnim.Bx.Buf[0] = '\0';
        g_fnPostPopupAnim(character, &emptyAnim);
    }

    if (scene && scene->damageNumberCount > countBefore && scene->damageNumbers) {
        if (DamageNumber *dn = scene->damageNumbers[scene->damageNumberCount - 1]) {
            if (dn->entity) {
                dn->entity->timescale = static_cast<double>(speedScale);
            }
        }
    }
}

}
