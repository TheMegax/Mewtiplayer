#pragma once
#include <cstdint>
#include <type_traits>
#include "GameSymbols.h"
#include "ParaboxAPI.h"
#include "mewjector.h"
#include "Scanner.h"

// Tag used to identify hooks installed by this module.
// Consumers (e.g. Mewtiplayer) can override this before including HookMacros.h.
#ifndef PARABOX_MOD_TAG
#define PARABOX_MOD_TAG "ParaboxAPI"
#endif

template <typename T>
inline uintptr_t HookResolveTargetRva(MewjectorAPI *mj, uintptr_t base, const char *name, T val) {
    if constexpr (std::is_integral_v<T>) {
        return static_cast<uintptr_t>(val);
    } else {
        return ScanSignature(mj, base, name, val);
    }
}

#define HOOK_DEFINE(name, retType, ...)                                        \
  typedef retType(__fastcall *name##_t)(__VA_ARGS__);                          \
  static name##_t g_orig##name = nullptr;

#define HOOK_INSTALL(mj, base, name, rva, stolenBytes)                         \
  do {                                                                         \
    const uintptr_t _rva = HookResolveTargetRva((mj), (base), #name, (rva));   \
    ParaboxAPI::Log("[HOOK] Installing " #name " at RVA 0x%p", (void*)_rva);   \
    (mj)->InstallHook(_rva, (stolenBytes),                                     \
        (void*)Hook_##name, (void**)&g_orig##name, 10,                         \
        PARABOX_MOD_TAG);                                                      \
  } while (0)

#define RESOLVE_FUNC(base, rva, outPtr)                                        \
  do {                                                                         \
    outPtr = reinterpret_cast<decltype(outPtr)>((base) + (rva));               \
    ParaboxAPI::Log("[RESOLVE] " #outPtr " at 0x%p (RVA 0x%p)",                \
        (void*)outPtr, (void*)(rva));                                          \
  } while (0)

#define RESOLVE_DATA(base, rva, outPtr)                                        \
  do {                                                                         \
    outPtr = reinterpret_cast<decltype(outPtr)>((base) + (rva));               \
    ParaboxAPI::Log("[DATA] " #outPtr " at 0x%p (RVA 0x%p)",                   \
        (void*)outPtr, (void*)(rva));                                          \
  } while (0)

// Legacy transition helpers
#define SCAN_RESOLVE(mj, base, name, sig, outPtr, instrSkip, ripOff, ripLen) \
    do {                                                                      \
        const uintptr_t _rva = ScanSignature((mj), (base), #name, (sig));    \
        if (_rva) {                                                           \
            (outPtr) = (decltype(outPtr))ResolveRIP(                          \
                (base) + _rva + (instrSkip), (ripOff), (ripLen));             \
        }                                                                     \
    } while (0)

#define SCAN_SET(mj, base, name, sig, outPtr)                             \
    do {                                                                   \
        const uintptr_t _rva = ScanSignature((mj), (base), #name, (sig)); \
        if (_rva) {                                                        \
            (outPtr) = (decltype(outPtr))((base) + _rva);                  \
        }                                                                  \
    } while (0)
