#pragma once
#include "mewjector.h"
#include <cstdint>

void LevelUpHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase);

void TriggerLevelUpSelectOption(int64_t catUID, uint32_t optionIndex);
void TriggerLevelUpReroll(int64_t catUID);
void TriggerAbilityReplace(int64_t catUID, uint32_t slotIndex);
