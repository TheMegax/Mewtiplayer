#pragma once

#include <cstdint>

extern uint32_t g_pendingMapNodeSyncIndex;
extern bool g_isHandlingNetworkMapNodeSync;

void MapHooks_UITick();
void MapHooks_Shutdown();
void ForceMapInventoryOpen();
void RegisterMapSubscribers();
void TriggerMapNodeSync(uint32_t nodeIndex);
