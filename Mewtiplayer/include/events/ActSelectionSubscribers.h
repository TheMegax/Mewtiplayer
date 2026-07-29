#pragma once

#include <cstdint>

extern uint32_t g_pendingActSelectIndex;

void RegisterActSelectionSubscribers();
void TriggerActSelect(uint32_t actIndex);
