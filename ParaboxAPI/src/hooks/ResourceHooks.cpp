#include "hooks/ResourceHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include <string>

HOOK_DEFINE(GetFile,         void*, void*, void*, bool, bool)
HOOK_DEFINE(GetFileRawLoose, void*, void*, void*, bool, bool)
HOOK_DEFINE(GetFileRawPak,   void*, void*, void*, bool)
HOOK_DEFINE(FileExists,      uint8_t, void*, void*)

struct MsvcStringView {
    union {
        char buf[16];
        char *ptr;
    } bx;
    size_t mysize;
    size_t myres;
};

static const char *ReadMsvcString(const void *strPtr, size_t &outLen) {
    if (!strPtr) {
        outLen = 0;
        return "";
    }
    const auto msvc = (const MsvcStringView *)strPtr;
    outLen = msvc->mysize;
    if (msvc->myres >= 16 && msvc->bx.ptr) {
        return msvc->bx.ptr;
    }
    return msvc->bx.buf;
}

static void *HandleFileGetOverride(void *thisPtr, void *param_1, const char *funcName) {
    size_t len = 0;
    const char *pStr = ReadMsvcString(param_1, len);
    const std::string rawPath(pStr, len);
    const std::string normPath = ParaboxAPI::Internal::NormalizePath(rawPath.c_str());

    ParaboxAPI::FileGetEvent ev = {};
    ev.path = normPath.c_str();
    ParaboxAPI::OnFileGet.Publish(ev);

    const void *overrideData = ev.overrideData;
    size_t overrideSize = ev.overrideSize;

    if (!overrideData) {
        ParaboxAPI::Internal::LookupFileOverride(normPath, overrideData, overrideSize);
    }

    if (overrideData) {
        ParaboxAPI::Log("[RESOURCES] Serving override for '%s' (%zu bytes) via %s",
                        normPath.c_str(), overrideSize, funcName);

        void *fileMem = ParaboxAPI::GameAllocate(0x30);
        if (!fileMem) {
            fileMem = malloc(0x30);
        }
        if (fileMem) {
            memset(fileMem, 0, 0x30);

            // Filename string at 0x00..0x1F
            auto pFilename = (MsvcReleaseModeXString *)fileMem;
            GameUtils::InitXString(*pFilename, rawPath.c_str());

            // char* filedata at 0x20
            *(char **)((uintptr_t)fileMem + 0x20) = (char *)overrideData;
            // uint32_t size at 0x28
            *(uint32_t *)((uintptr_t)fileMem + 0x28) = (uint32_t)overrideSize;
            // bool owns_data at 0x2C (false so engine doesn't deallocate our buffer)
            *(uint8_t *)((uintptr_t)fileMem + 0x2C) = 0;

            // MSVC callee destruction of param_1
            GameUtils::FreeXString(*(MsvcReleaseModeXString *)param_1);
            return fileMem;
        }
    }

    return nullptr;
}

static void *__fastcall Hook_GetFile(void *thisPtr, void *param_1, bool param_2, bool param_3) {
    void *overrideFile = HandleFileGetOverride(thisPtr, param_1, "GetFile");
    if (overrideFile)
        return overrideFile;

    if (g_origGetFile) {
        return g_origGetFile(thisPtr, param_1, param_2, param_3);
    }
    GameUtils::FreeXString(*(MsvcReleaseModeXString *)param_1);
    return nullptr;
}

static void *__fastcall Hook_GetFileRawLoose(void *thisPtr, void *param_1, bool param_2, bool param_3) {
    void *overrideFile = HandleFileGetOverride(thisPtr, param_1, "GetFileRawLoose");
    if (overrideFile)
        return overrideFile;

    if (g_origGetFileRawLoose) {
        return g_origGetFileRawLoose(thisPtr, param_1, param_2, param_3);
    }
    GameUtils::FreeXString(*(MsvcReleaseModeXString *)param_1);
    return nullptr;
}

static void *__fastcall Hook_GetFileRawPak(void *thisPtr, void *param_1, bool param_2) {
    void *overrideFile = HandleFileGetOverride(thisPtr, param_1, "GetFileRawPak");
    if (overrideFile)
        return overrideFile;

    if (g_origGetFileRawPak) {
        return g_origGetFileRawPak(thisPtr, param_1, param_2);
    }
    GameUtils::FreeXString(*(MsvcReleaseModeXString *)param_1);
    return nullptr;
}

static uint8_t __fastcall Hook_FileExists(void *thisPtr, void *param_1) {
    size_t len = 0;
    const char *pStr = ReadMsvcString(param_1, len);
    std::string rawPath(pStr, len);
    std::string normPath = ParaboxAPI::Internal::NormalizePath(rawPath.c_str());

    const void *overrideData = nullptr;
    size_t overrideSize = 0;
    if (ParaboxAPI::Internal::LookupFileOverride(normPath, overrideData, overrideSize)) {
        GameUtils::FreeXString(*(MsvcReleaseModeXString *)param_1);
        return 1;
    }

    if (g_origFileExists) {
        return g_origFileExists(thisPtr, param_1);
    }
    GameUtils::FreeXString(*(MsvcReleaseModeXString *)param_1);
    return 0;
}

void ResourceHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
    HOOK_INSTALL(mj, gameBase, GetFile,         GameSymbols::FileManagerClass_GetFile,         20);
    HOOK_INSTALL(mj, gameBase, GetFileRawLoose, GameSymbols::FileManagerClass_GetFileRawLoose, 20);
    HOOK_INSTALL(mj, gameBase, GetFileRawPak,   GameSymbols::FileManagerClass_GetFileRawPak,   16);
    HOOK_INSTALL(mj, gameBase, FileExists,      GameSymbols::FileManagerClass_FileExists,      16);
}
