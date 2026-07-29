#pragma once

#include <cstdint>

void RegisterWorldEventSubscribers();
void TriggerWorldEventSelectOption(int64_t catUID, uint32_t optionIndex);
void TriggerWorldEventSelectCat(int64_t selectedCatUID);
void TriggerWorldEventClickEnd(uint8_t buttonType);
