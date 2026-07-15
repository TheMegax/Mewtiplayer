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

    if (g_origEnqueueAction)
        return g_origEnqueueAction(queue, actionData);
    return nullptr;
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

}
