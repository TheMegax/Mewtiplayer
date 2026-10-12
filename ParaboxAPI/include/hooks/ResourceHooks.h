#pragma once
#include "ParaboxAPI.h"
#include "mewjector.h"
#include <string>

void ResourceHooks_Init(MewjectorAPI *mj, uintptr_t gameBase);

namespace ParaboxAPI {
namespace Internal {
    bool LookupFileOverride(const std::string &normalizedPath, const void *&outData, size_t &outSize);
    std::string NormalizePath(const char *path);
}
}
