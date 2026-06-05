#pragma once
#include "mewjector.h"
#include "NetworkManager.h"
#include <cstdint>
#include <deque>

typedef void(__fastcall *FaceDirection_t)(void*, uint64_t, bool, bool);
extern FaceDirection_t g_origFaceDirection;
extern std::deque<ActionPacket> g_pendingInjections;

void CombatHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);
