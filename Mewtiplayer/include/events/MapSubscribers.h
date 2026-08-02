#pragma once

#include <cstdint>
#include "ParaboxAPI.h"

extern uint32_t g_pendingMapNodeSyncIndex;
using ParaboxAPI::g_isHandlingNetworkMapNodeSync;

void MapHooks_UITick();
void MapHooks_Shutdown();
void ForceMapInventoryOpen();
void RegisterMapSubscribers();
void TriggerMapNodeSync(uint32_t nodeIndex);
