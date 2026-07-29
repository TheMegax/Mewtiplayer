#pragma once

#include <map>
#include <cstdint>

extern std::map<int64_t, uint64_t> g_catIdToOwnerSteamID;
extern bool g_isHandlingNetworkMapInventoryOpen;

void RegisterSaveSubscribers();
