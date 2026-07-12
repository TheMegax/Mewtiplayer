#pragma once
#include "ParaboxAPI.h"
#include "mewjector.h"
#include <windows.h>

PARABOX_API uintptr_t FindPattern(uintptr_t base, const char *signature);
PARABOX_API uintptr_t ResolveCall(uintptr_t callInstruction);
PARABOX_API uintptr_t ResolveRIP(uintptr_t instruction, int offsetIndex,
                                  int instructionLength);
PARABOX_API uintptr_t ScanSignature(MewjectorAPI *mj, uintptr_t base,
                                    const char *name, const char *signature);
