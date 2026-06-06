#pragma once
#include "mewjector.h"
#include <vector>
#include <cstdint>

void AdventureBoxHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
std::vector<int64_t> GetButchBoxCatKeys();
int GetButchBoxCatAge(int64_t sqlKey);
int GetLocalButchBoxCatCount();
