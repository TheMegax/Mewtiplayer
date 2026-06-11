#pragma once
#include "mewjector.h"
#include <cstdint>

void StorageHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void RefreshStorageItemsInventory();
void UpdateStorageItemSlot(int32_t slotIndex, int64_t catID);
void HandleStorageItemSyncInternal(const void *data, uint32_t length);

extern void *g_activeInventoryScreenThis;
void StorageHooks_TriggerEmbarkProceed();

