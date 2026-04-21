#include "Scanner.h"
#include "Overlay.h"
#include <string.h>

#define MOD_NAME "Multigenics"

uintptr_t FindPattern(uintptr_t base, const char *signature) {
  if (!base)
    return 0;

  PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)base;
  PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(base + dosHeader->e_lfanew);

  PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
  uintptr_t startAddress = 0;
  size_t scanSize = 0;

  for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
    if (memcmp(section->Name, ".text", 5) == 0) {
      startAddress = base + section->VirtualAddress;
      scanSize = section->Misc.VirtualSize;
      break;
    }
  }

  if (startAddress == 0) {
    startAddress = base + ntHeaders->OptionalHeader.BaseOfCode;
    scanSize = ntHeaders->OptionalHeader.SizeOfCode;
  }

  auto hexToByte = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return 0;
  };

  uint8_t patternBytes[128];
  bool patternMask[128];
  size_t patternLen = 0;

  for (const char *p = signature; *p; p++) {
    if (*p == ' ')
      continue;
    if (*p == '?') {
      patternBytes[patternLen] = 0;
      patternMask[patternLen] = false;
      patternLen++;
      if (*(p + 1) == '?')
        p++;
    } else {
      patternBytes[patternLen] = (hexToByte(*p) << 4) | hexToByte(*(p + 1));
      patternMask[patternLen] = true;
      patternLen++;
      p++;
    }
  }

  for (size_t i = 0; i < scanSize - patternLen; i++) {
    bool found = true;
    for (size_t j = 0; j < patternLen; j++) {
      if (patternMask[j] &&
          ((uint8_t *)(startAddress + i))[j] != patternBytes[j]) {
        found = false;
        break;
      }
    }
    if (found) {
      return startAddress + i;
    }
  }
  return 0;
}

uintptr_t ResolveCall(uintptr_t callInstruction) {
  if (!callInstruction)
    return 0;
  int32_t offset = *(int32_t *)(callInstruction + 1);
  return callInstruction + 5 + offset;
}

uintptr_t ScanSignature(MewjectorAPI *mj, uintptr_t base, const char *name,
                        const char *signature) {
  uintptr_t addr = FindPattern(base, signature);
  if (!addr) {
    Overlay::Log("FAILED to find %s signature", name);
    return 0;
  }
  uintptr_t rva = addr - base;
  Overlay::Log("Found %s at RVA 0x%p", name, (void *)rva);
  return rva;
}
