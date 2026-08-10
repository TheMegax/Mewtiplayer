#pragma once

#include <map>
#include <cstdint>

extern std::map<uint64_t, bool> g_lobbyReadyStates;
extern bool g_localReady;
extern void *g_activeClassChooserLambdaThis;
extern bool g_hasTriggeredProceed;

bool AreAllLobbyMembersReady();
void ResetLobbyReadyStates();
void HandleCollarSyncInternal(const void *data, const uint32_t length);
void ClassChooserHooks_UITick();
void ClassChooserHooks_Shutdown();
void CatSelectorHooks_TriggerLockInProceed();
void HostBroadcastFullCollarState(bool force = false);
void RegisterClassChooserSubscribers();
