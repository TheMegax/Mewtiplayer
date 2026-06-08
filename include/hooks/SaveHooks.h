#pragma once
#include "mewjector.h"
#include <cstdint>
#include <map>

extern std::map<int64_t, uint64_t> g_catIdToOwnerSteamID;

void SaveHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
