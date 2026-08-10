#pragma once
#include "mewjector.h"
#include <cstdint>

#define MOD_NAME "Mewtiplayer"
#define MOD_VERSION "1.0.0"

struct ModState {
    MewjectorAPI *mj = nullptr;
    UINT_PTR gameBase = 0;

    bool packetTesting = false;
    uint32_t simPingMs = 150;
    uint32_t simJitterMs = 20;
    float simLossRate = 5.0f; // Loss percentage (e.g. 5.0 = 5%)
    bool noSteven = false;
    bool autoLobby = false;
    bool talkative = false;
    bool autoJoin = false;
    bool evilMode = false;
    bool evilShuffler = false;
};

extern ModState g_modState;
