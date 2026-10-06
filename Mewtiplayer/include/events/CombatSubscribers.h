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

extern bool g_startedCombat;
extern bool g_isQueueEmpty;
extern bool g_inCombatDetected;

extern std::deque<ActionPacket> g_pendingInjections;
extern ActionPacket g_lastSentTurnPackage;
extern void *g_lastActionQueue;

extern std::map<LevelUpScreen*, CatData*> g_levelUpScreenToCat;
extern std::map<AbilityChooser*, CatData*> g_abilityChooserToCat;

void ResetCombatSubscribersState();
void RegisterCombatSubscribers();
