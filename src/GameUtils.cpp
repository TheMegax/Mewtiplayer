#include "GameUtils.h"
#include <intrin.h>

namespace GameUtils {

void *GetThreadLocalStoragePointer() {
  // On Windows x64, the Thread Environment Block (TEB) contains a pointer
  // to the Thread Local Storage (TLS) array at gs:[0x58].
  void **tlsArray = (void **)__readgsqword(0x58);
  if (!tlsArray)
    return nullptr;
  return tlsArray[0];
}

void SetRNGState(const void *seed32) {
  uint8_t *tls = (uint8_t *)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  const uint32_t *seed = (const uint32_t *)seed32;

  // Inject the 256-bit seed directly into the Thread Local RNG State!
  *(uint32_t *)(tls + 0x178) = seed[0];
  *(uint32_t *)(tls + 0x17c) = seed[1];
  *(uint32_t *)(tls + 0x180) = seed[2];
  *(uint32_t *)(tls + 0x184) = seed[3];

  // The last 16 bytes are written as two 64-bit integers.
  // Offset 400 (decimal) is 0x190 (hex).
  *(uint64_t *)(tls + 0x188) = *(uint64_t *)(seed + 4);
  *(uint64_t *)(tls + 0x190) = *(uint64_t *)(seed + 6);
}

void GetRNGState(void *outSeed32) {
  uint8_t *tls = (uint8_t *)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  uint32_t *out = (uint32_t *)outSeed32;

  out[0] = *(uint32_t *)(tls + 0x178);
  out[1] = *(uint32_t *)(tls + 0x17c);
  out[2] = *(uint32_t *)(tls + 0x180);
  out[3] = *(uint32_t *)(tls + 0x184);

  *(uint64_t *)(out + 4) = *(uint64_t *)(tls + 0x188);
  *(uint64_t *)(out + 6) = *(uint64_t *)(tls + 0x190);
}

} // namespace GameUtils
