#pragma once
#include "ParaboxAPI.h"
#include "mewjector.h"
#include <vector>
#include <cstdint>

PARABOX_API void AdventureBoxHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
PARABOX_API void SetAdventureCapacity(int capacity);

PARABOX_API std::vector<int64_t> GetButchBoxCatKeys();
PARABOX_API int GetButchBoxCatAge(int64_t sqlKey);
PARABOX_API int GetLocalButchBoxCatCount();
