#pragma once
#include "mewjector.h"
#include <cstdint>

void WorldEventHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void TriggerWorldEventSelectOption(int64_t catUID, uint32_t optionIndex);
void TriggerWorldEventSelectCat(int64_t selectedCatUID);
void TriggerWorldEventClickEnd(uint8_t buttonType);
