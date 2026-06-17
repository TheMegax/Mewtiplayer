#pragma once
#include "mewjector.h"
#include <cstdint>

void ActSelectionHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase);
void TriggerActSelect(uint32_t actIndex);
