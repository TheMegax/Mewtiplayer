#pragma once

#define HOOK_DEFINE(name, ret, ...)                       \
    typedef ret (__fastcall *name##_t)(__VA_ARGS__);       \
    static name##_t g_orig##name = nullptr;

#define HOOK_INSTALL(mj, base, name, sig, stolenBytes)                    \
    do {                                                                   \
        const uintptr_t _rva = ScanSignature((mj), (base), #name, (sig)); \
        if (_rva) {                                                        \
            (mj)->InstallHook(_rva, (stolenBytes),                         \
                (void*)Hook_##name, (void**)&g_orig##name, 10, MOD_NAME); \
        }                                                                  \
    } while (0)

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
