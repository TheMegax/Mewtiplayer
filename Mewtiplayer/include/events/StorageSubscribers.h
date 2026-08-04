#pragma once

#include <cstdint>
#include "ParaboxAPI.h"

extern void *g_activeInventoryScreenThis;
using ParaboxAPI::g_isHandlingNetworkStorageItemSync;

void StorageHooks_UITick();
void StorageHooks_Shutdown();
void StorageHooks_TriggerEmbarkProceed();
void HandleStorageItemSyncInternal(const void *data, const uint32_t length);
void RegisterStorageSubscribers();
