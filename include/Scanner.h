#pragma once
#include "mewjector.h"
#include <stdint.h>
#include <windows.h>

uintptr_t FindPattern(uintptr_t base, const char *signature);
uintptr_t ResolveCall(uintptr_t callInstruction);
uintptr_t ResolveRIP(uintptr_t instruction, int offsetIndex,
                     int instructionLength);
uintptr_t ScanSignature(MewjectorAPI *mj, uintptr_t base, const char *name,
                        const char *signature);
