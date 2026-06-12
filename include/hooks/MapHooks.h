#pragma once

#include "mewjector.h"
#include <cstdint>

void MapHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
void TriggerMapNodeSync(uint32_t nodeIndex);
