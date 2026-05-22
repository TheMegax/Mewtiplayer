#include "CrashHandler.h"
#include <cstdio>
#include <windows.h>
#include <dbghelp.h>
#include <ctime>

#pragma comment(lib, "dbghelp.lib")

namespace CrashHandler {
    static PVOID g_vehHandler = nullptr;

    // Helper to get module name and RVA from an address, with symbol resolution if available
    static void ResolveAddress(void *addr, char *outBuf, const size_t outBufSize) {
        HMODULE hMod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(addr), &hMod) && hMod != nullptr) {
            char path[MAX_PATH];
            if (GetModuleFileNameA(hMod, path, sizeof(path))) {
                // Get only filename
                char* filename = strrchr(path, '\\');
                if (filename) filename++;
                else filename = path;

                const uintptr_t rva = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(hMod);

                // Attempt to resolve function name using DbgHelp
                DWORD64 displacement = 0;
                alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)];
                PSYMBOL_INFO pSymbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
                pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                pSymbol->MaxNameLen = MAX_SYM_NAME;

                if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(addr), &displacement, pSymbol)) {
                    // Symbol found! Try to get source file and line number
                    DWORD lineDisplacement = 0;
                    IMAGEHLP_LINE64 line;
                    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                    if (SymGetLineFromAddr64(GetCurrentProcess(), reinterpret_cast<DWORD64>(addr), &lineDisplacement, &line)) {
                        // Extract only filename from the source file path
                        const char* srcFile = strrchr(line.FileName, '\\');
                        if (srcFile) srcFile++;
                        else srcFile = line.FileName;

                        snprintf(outBuf, outBufSize, "%s + 0x%llX (%s in %s:%lu) (base: %p)",
                                 filename, static_cast<unsigned long long>(rva), pSymbol->Name, srcFile, line.LineNumber, hMod);
                    } else {
                        snprintf(outBuf, outBufSize, "%s + 0x%llX (%s) (base: %p)",
                                 filename, static_cast<unsigned long long>(rva), pSymbol->Name, hMod);
                    }
                } else {
                    snprintf(outBuf, outBufSize, "%s + 0x%llX (base: %p)", filename, static_cast<unsigned long long>(rva), hMod);
                }
                return;
            }
        }
        snprintf(outBuf, outBufSize, "unknown module @ %p", addr);
    }

    LONG VectoredHandler(const PEXCEPTION_POINTERS ExceptionInfo) {
        const DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;

        // Skip non-fatal exceptions
        if (code == 0x40010006 || code == 0x406D0012 || code == 0xE06D7363) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Open crash log file
        CreateDirectoryA("mod_logs", nullptr);
        CreateDirectoryA("mod_logs\\crashes", nullptr);

        FILE* f = fopen("mod_logs\\crashes\\mewtiplayer_crash.txt", "w");
        if (!f) {
            // Fallback to game root
            f = fopen("mewtiplayer_crash.txt", "w");
        }

        if (f) {
            const time_t t = time(nullptr);
            const tm* tm_info = localtime(&t);
            char timeBuf[64];
            if (tm_info) {
                strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_info);
            } else {
                snprintf(timeBuf, sizeof(timeBuf), "Unknown Time");
            }

            fprintf(f, "========================================\n");
            fprintf(f, "MEWTIPLAYER CRASH REPORT - %s\n", timeBuf);
            fprintf(f, "========================================\n\n");

            // Exception details
            fprintf(f, "Exception Code: 0x%08lX\n", code);
            
            char crashAddrStr[512];
            ResolveAddress(ExceptionInfo->ExceptionRecord->ExceptionAddress, crashAddrStr, sizeof(crashAddrStr));
            fprintf(f, "Fault Address: %s\n", crashAddrStr);

            if (code == EXCEPTION_ACCESS_VIOLATION) {
                const ULONG_PTR readWrite = ExceptionInfo->ExceptionRecord->ExceptionInformation[0];
                const ULONG_PTR targetAddr = ExceptionInfo->ExceptionRecord->ExceptionInformation[1];
                fprintf(f, "Details: Access Violation (attempted to %s memory at address %p)\n",
                        readWrite == 0 ? "read" : readWrite == 1 ? "write" : "execute", reinterpret_cast<void *>(targetAddr));
            }
            fprintf(f, "\n");

            // Registers (64-bit)
            if (const PCONTEXT ctx = ExceptionInfo->ContextRecord) {
                fprintf(f, "CPU Registers (x64):\n");
                fprintf(f, "  RAX: 0x%016llX   RBX: 0x%016llX\n", static_cast<unsigned long long>(ctx->Rax), static_cast<unsigned long long>(ctx->Rbx));
                fprintf(f, "  RCX: 0x%016llX   RDX: 0x%016llX\n", static_cast<unsigned long long>(ctx->Rcx), static_cast<unsigned long long>(ctx->Rdx));
                fprintf(f, "  RSI: 0x%016llX   RDI: 0x%016llX\n", static_cast<unsigned long long>(ctx->Rsi), static_cast<unsigned long long>(ctx->Rdi));
                fprintf(f, "  RBP: 0x%016llX   RSP: 0x%016llX\n", static_cast<unsigned long long>(ctx->Rbp), static_cast<unsigned long long>(ctx->Rsp));
                fprintf(f, "  RIP: 0x%016llX\n", static_cast<unsigned long long>(ctx->Rip));
                fprintf(f, "  R8:  0x%016llX   R9:  0x%016llX\n", static_cast<unsigned long long>(ctx->R8), static_cast<unsigned long long>(ctx->R9));
                fprintf(f, "  R10: 0x%016llX   R11: 0x%016llX\n", static_cast<unsigned long long>(ctx->R10), static_cast<unsigned long long>(ctx->R11));
                fprintf(f, "  R12: 0x%016llX   R13: 0x%016llX\n", static_cast<unsigned long long>(ctx->R12), static_cast<unsigned long long>(ctx->R13));
                fprintf(f, "  R14: 0x%016llX   R15: 0x%016llX\n", static_cast<unsigned long long>(ctx->R14), static_cast<unsigned long long>(ctx->R15));
                fprintf(f, "  EFLAGS: 0x%08lX\n", ctx->EFlags);
                fprintf(f, "\n");
            }

            // Call stack backtrace
            fprintf(f, "Call Stack (Backtrace):\n");
            void* backtrace[32];
            const USHORT frames = CaptureStackBackTrace(0, 32, backtrace, nullptr);
            for (USHORT i = 0; i < frames; i++) {
                char frameStr[512];
                ResolveAddress(backtrace[i], frameStr, sizeof(frameStr));
                fprintf(f, "  [%d] %s\n", i, frameStr);
            }
            fprintf(f, "\n");
            
            fprintf(f, "========================================\n");
            fclose(f);
        }

        // Also print to stderr/stdout for terminal capture
        fprintf(stderr, "\n!!! MEWTIPLAYER CRASH DETECTED (Code: 0x%08lX) !!!\n", code);
        char crashAddrStr[512];
        ResolveAddress(ExceptionInfo->ExceptionRecord->ExceptionAddress, crashAddrStr, sizeof(crashAddrStr));
        fprintf(stderr, "Fault Address: %s\n", crashAddrStr);
        fflush(stderr);

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void Register() {
        if (!g_vehHandler) {
            // Retrieve the path of Mewtiplayer.dll to use as search path
            char path[MAX_PATH] = {0};
            HMODULE hMod = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(Register), &hMod) && hMod != nullptr) {
                GetModuleFileNameA(hMod, path, sizeof(path));
                char* lastSlash = strrchr(path, '\\');
                if (lastSlash) {
                    *lastSlash = '\0';
                }
            }

            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            SymInitialize(GetCurrentProcess(), path[0] != '\0' ? path : nullptr, TRUE);
            g_vehHandler = AddVectoredExceptionHandler(1, VectoredHandler);
        }
    }

    void Unregister() {
        if (g_vehHandler) {
            RemoveVectoredExceptionHandler(g_vehHandler);
            g_vehHandler = nullptr;
            SymCleanup(GetCurrentProcess());
        }
    }
}
