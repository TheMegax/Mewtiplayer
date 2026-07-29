#pragma once

#include <cstdint>

void RegisterLevelUpSubscribers();
void TriggerLevelUpSelectOption(int64_t catUID, uint32_t optionIndex);
void TriggerLevelUpReroll(int64_t catUID);
void TriggerAbilityReplace(int64_t catUID, uint32_t slotIndex);
