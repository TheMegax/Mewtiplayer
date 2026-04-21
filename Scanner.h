#pragma once
#include <stdint.h>
#include <windows.h>
#include "mewjector.h"

uintptr_t FindPattern(uintptr_t base, const char *signature);
uintptr_t ResolveCall(uintptr_t callInstruction);
uintptr_t ScanSignature(MewjectorAPI *mj, uintptr_t base, const char *name, const char *signature);
