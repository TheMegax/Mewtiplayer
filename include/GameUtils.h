#pragma once
#include <stdint.h>

namespace GameUtils {
    // Returns the base address of the game's TLS block.
    // Assumes slot 0 for the main executable.
    void* GetThreadLocalStoragePointer();

    // Sets the 32-byte Xoshiro256 state at the specific TLS offset.
    void SetRNGState(const void* seed32);

    // Reads the current 32-byte Xoshiro256 state from the TLS.
    void GetRNGState(void* outSeed32);
}
