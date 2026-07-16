#include "Scanner.h"
#include "ParaboxAPI.h"

uintptr_t FindPattern(const uintptr_t base, const char *signature) {
  if (!base)
    return 0;

  const auto dosHeader = (PIMAGE_DOS_HEADER)base;
  auto ntHeaders = (PIMAGE_NT_HEADERS)(base + dosHeader->e_lfanew);

  auto section = IMAGE_FIRST_SECTION(ntHeaders);
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

  auto hexToByte = [](const char c) -> uint8_t {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return 0;
  };

  uint8_t patternBytes[512];
  bool patternMask[512];
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
      if (patternLen >= 512) {
        ParaboxAPI::Log("[PARABOX] [ERR] Signature too long in FindPattern!");
        return 0;
      }
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

uintptr_t ResolveCall(const uintptr_t callInstruction) {
  if (!callInstruction)
    return 0;
  const int32_t offset = *(int32_t *)(callInstruction + 1);
  return callInstruction + 5 + offset;
}

uintptr_t ResolveRIP(const uintptr_t instruction, const int offsetIndex,
                     const int instructionLength) {
  if (!instruction)
    return 0;
  const int32_t offset = *(int32_t *)(instruction + offsetIndex);
  return instruction + instructionLength + offset;
}

uintptr_t ScanSignature(MewjectorAPI *mj, const uintptr_t base, const char *name,
                        const char *signature) {
  const uintptr_t addr = FindPattern(base, signature);
  if (!addr) {
    ParaboxAPI::Log("FAILED to find %s signature", name);
    return 0;
  }
  const uintptr_t rva = addr - base;
  ParaboxAPI::Log("Found %s at RVA 0x%p", name, (void *)rva);
  return rva;
}
