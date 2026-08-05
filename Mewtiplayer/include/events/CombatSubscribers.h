#pragma once

#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include <deque>
#include <unordered_set>
#include <map>

extern TurnControl *g_currentTurnControl;
extern bool g_isCombatUIProcessing;
extern std::unordered_set<void *> g_castableAbilities;

extern bool g_waitingForPlayerAction;
extern bool g_isSyncActionPending;

extern bool g_deferredBroadcastPending;
extern TurnActionPacket g_deferredActionPkt;
extern uint32_t g_deferredActorNUID;

extern bool g_isMainActionActive;
extern uint32_t g_activeMainActionActorNUID;
extern Ability *g_activeMainActionAbilityPtr;
extern std::string g_activeMainActionAbilityName;

extern bool g_startedCombat;
extern bool g_isQueueEmpty;
extern bool g_inCombatDetected;

extern std::deque<ActionPacket> g_pendingInjections;
extern ActionPacket g_lastSentTurnPackage;
extern void *g_lastActionQueue;

extern std::map<glaiel::LevelUpScreen*, PersistentCharacter*> g_levelUpScreenToCat;
extern std::map<glaiel::AbilityChooser*, PersistentCharacter*> g_abilityChooserToCat;

extern ActionPacket g_lastInjectedActionPacket;
extern bool g_injectedInCurrentCall;

void NotifyEnqueueResult(void* result);
void ResetCombatSubscribersState();
void RegisterCombatSubscribers();

void RecordRecentlyExecutedPassive(uint32_t nuid, const std::string& abilityName);
bool IsPassiveRecentlyExecuted(uint32_t nuid, const std::string& abilityName);
void ClearRecentlyExecutedPassive(uint32_t nuid, const std::string& abilityName);
