#pragma once
#include "mewjector.h"

#define MOD_NAME "Mewtiplayer"
#define MOD_VERSION "1.0.0"

struct ModState {
    MewjectorAPI *mj = nullptr;
    UINT_PTR gameBase = 0;

    bool packetTesting = false;
    bool noSteven = false;
    bool autoLobby = false;
    bool talkative = false;
    bool autoJoin = false;
    bool evilMode = false;
};

extern ModState g_modState;
