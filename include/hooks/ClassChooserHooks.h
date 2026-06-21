#pragma once
#include "mewjector.h"
#include "MewgenicsTypes.h"
#include <cstdint>
#include <map>

extern std::map<uint64_t, bool> g_lobbyReadyStates;
extern bool g_localReady;
extern bool g_hasTriggeredProceed;
extern void *g_activeClassChooserLambdaThis;
extern void *g_activeCatSelector;

void CatSelectorHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
bool IsCatSelectorValid(const void *selector);
bool AreAllLobbyMembersReady();
void RefreshCatSelectorUI();
PersistentCharacter *GetPersistentCharacterById(int64_t catID);
void ResetLobbyReadyStates();
void CatSelectorHooks_TriggerLockInProceed();
void ApplyCollarToCharacter(PersistentCharacter *cat, const char *collarName);
void RefreshClassChooserInventory();
void UpdateClassChooserTagBoxes(int64_t catID, int32_t collarIndex);
void HandleCollarSyncInternal(const void *data, uint32_t length);
void ClassChooserHooks_UITick();
void ClassChooserHooks_Shutdown();
