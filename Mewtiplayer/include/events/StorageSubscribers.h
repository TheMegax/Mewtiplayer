#pragma once

#include <cstdint>

extern void *g_activeInventoryScreenThis;

void StorageHooks_UITick();
void StorageHooks_Shutdown();
void StorageHooks_TriggerEmbarkProceed();
void HandleStorageItemSyncInternal(const void *data, const uint32_t length);
void RegisterStorageSubscribers();
