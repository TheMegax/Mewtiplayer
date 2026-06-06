#include "hooks/AdventureBoxHooks.h"
#include "hooks/HookMacros.h"
#include "hooks/ModState.h"
#include "MewgenicsTypes.h"
#include "Overlay.h"
#include "Scanner.h"
#include <algorithm>
#include <vector>

static int g_adventureCapacity = 4; // TODO: Pretty glitchy to have >4
static HANDLE* g_pCRTHeap = nullptr;

static HANDLE GetCRTHeap() {
  if (g_pCRTHeap) {
    return *g_pCRTHeap;
  }
  return GetProcessHeap();
}

struct LoadAdventureArgs {
  ButchBox *butchBox;
  podvector<int64_t> catIDs;
};

HOOK_DEFINE(ButchBox_Init, void, ButchBox*, bool)
HOOK_DEFINE(ButchBox_TryPlaceCat, bool, ButchBox*, void*)
HOOK_DEFINE(ButchBox_ConfigCats, void, ButchBox*)
HOOK_DEFINE(ButchBox_TryRemoveCat, void, ButchBox*, void*)
HOOK_DEFINE(ButchBox_Depart, bool, ButchBox*)
HOOK_DEFINE(LoadAdventure, void, LoadAdventureArgs*)

void __fastcall Hook_ButchBox_Init(ButchBox *self, const bool param2) {
  if (g_origButchBox_Init) {
    g_origButchBox_Init(self, param2);
  }

  if (!self || g_adventureCapacity == 4) return;

  // ReSharper disable once CppLocalVariableMayBeConst
  HANDLE heap = GetCRTHeap();

  // Reallocate cats vector
  if (self->cats.data_) {
    if (void* newCats = HeapReAlloc(heap, HEAP_ZERO_MEMORY, self->cats.data_, g_adventureCapacity * sizeof(PersistentCharacter*))) {
      self->cats.data_ = (PersistentCharacter**)newCats;
      if (g_adventureCapacity > 4) {
        memset(self->cats.data_ + 4, 0, (g_adventureCapacity - 4) * sizeof(PersistentCharacter*));
      }
      self->cats.capacity_ = g_adventureCapacity;
      self->cats.size_ = g_adventureCapacity;
    }
  }

  // Reallocate positions vector
  if (self->positions.data_) {
    if (void* newPositions = HeapReAlloc(heap, HEAP_ZERO_MEMORY, self->positions.data_, g_adventureCapacity * sizeof(vec2))) {
      self->positions.data_ = (vec2*)newPositions;

      // Positioning it ourselves
      const double startX = self->positions.data_[0].x;
      const double startY = self->positions.data_[0].y;
      const double step = g_adventureCapacity > 1 ? (3.0 / (double)(g_adventureCapacity - 1)) : 0.0;
      for (int i = 0; i < g_adventureCapacity; ++i) {
        self->positions.data_[i].x = startX + (double)i * step;
        self->positions.data_[i].y = startY;
      }
      self->positions.capacity_ = g_adventureCapacity;
      self->positions.size_ = g_adventureCapacity;
    }
  }
}

bool __fastcall Hook_ButchBox_TryPlaceCat(ButchBox *self, void *cat) {
  if (!self || !cat) return false;

  int best_slot = -1;
  auto* catMc = *(MovieClip**)((char*)cat + 0x40);
  auto* boxMc = (MovieClip*)self->movieClip;

  if (catMc && boxMc) {
    const double catX = catMc->x;
    const double catY = catMc->y;
    const double boxX = boxMc->x;
    const double boxY = boxMc->y;

    const double relX = catX - boxX;
    const double relY = catY - boxY;

    double min_dist = 1e9;
    for (int i = 0; i < g_adventureCapacity; ++i) {
      if (self->cats.data_ && self->cats.data_[i] == nullptr) {
        const double slotX = self->positions.data_[i].x;
        const double slotY = self->positions.data_[i].y;
        const double dx = relX - slotX;
        const double dy = relY - slotY;
        const double dist = dx * dx + dy * dy;
        if (dist < min_dist) {
          min_dist = dist;
          best_slot = i;
        }
      }
    }
  }

  // Fallback to first empty slot
  if (best_slot == -1) {
    for (int i = 0; i < g_adventureCapacity; ++i) {
      if (self->cats.data_ && self->cats.data_[i] == nullptr) {
        best_slot = i;
        break;
      }
    }
  }

  if (best_slot == -1) { // Adventure box is full
    return false;
  }

  bool result = false;
  if (best_slot >= 4) {
    // Swap empty slot with slot 0 so the original function places it there
    std::swap(self->cats.data_[0], self->cats.data_[best_slot]);
    std::swap(self->positions.data_[0], self->positions.data_[best_slot]);

    if (g_origButchBox_TryPlaceCat) {
      result = g_origButchBox_TryPlaceCat(self, cat);
    }

    // Swap back
    std::swap(self->cats.data_[0], self->cats.data_[best_slot]);
    std::swap(self->positions.data_[0], self->positions.data_[best_slot]);
  } else {
    if (g_origButchBox_TryPlaceCat) {
      result = g_origButchBox_TryPlaceCat(self, cat);
    }
  }

  return result;
}

void __fastcall Hook_ButchBox_ConfigCats(ButchBox *self) {
  if (!self) return;

  // Run config_cats in virtual chunks of 4
  for (int start = 0; start < g_adventureCapacity; start += 4) {
    if (start == 0) {
      if (g_origButchBox_ConfigCats) {
        g_origButchBox_ConfigCats(self);
      }
    } else {
      const int count = std::min(4, g_adventureCapacity - start);
      for (int i = 0; i < count; ++i) {
        std::swap(self->cats.data_[i], self->cats.data_[start + i]);
        std::swap(self->positions.data_[i], self->positions.data_[start + i]);
      }

      if (g_origButchBox_ConfigCats) {
        g_origButchBox_ConfigCats(self);
      }

      // Swap back
      for (int i = 0; i < count; ++i) {
        std::swap(self->cats.data_[i], self->cats.data_[start + i]);
        std::swap(self->positions.data_[i], self->positions.data_[start + i]);
      }
    }
  }
}

void __fastcall Hook_ButchBox_TryRemoveCat(ButchBox *self, void *cat) {
  if (!self || !cat) return;

  int idx = -1;
  for (int i = 0; i < g_adventureCapacity; ++i) {
    if (self->cats.data_[i] == cat) {
      idx = i;
      break;
    }
  }

  if (idx != -1) {
    if (idx >= 4) {
      // Swap with slot 0
      std::swap(self->cats.data_[0], self->cats.data_[idx]);
      std::swap(self->positions.data_[0], self->positions.data_[idx]);

      if (g_origButchBox_TryRemoveCat) {
        g_origButchBox_TryRemoveCat(self, cat);
      }

      // Swap back
      std::swap(self->cats.data_[0], self->cats.data_[idx]);
      std::swap(self->positions.data_[0], self->positions.data_[idx]);
    } else {
      if (g_origButchBox_TryRemoveCat) {
        g_origButchBox_TryRemoveCat(self, cat);
      }
    }
  }
}

bool __fastcall Hook_ButchBox_Depart(ButchBox *self) {
  if (!self) return false;

  std::vector<int64_t> activeKeys;
  for (int i = 0; i < g_adventureCapacity; ++i) {
    if (self->cats.data_ && self->cats.data_[i]) {
      activeKeys.push_back(self->cats.data_[i]->sql_key);
    }
  }

  if (activeKeys.empty()) { // No cats in box, can't depart.
    return false;
  }

  if (g_adventureCapacity == 4 && activeKeys.size() < 4) {
    if (g_origButchBox_Depart) {
      return g_origButchBox_Depart(self);
    }
  }

  // ReSharper disable once CppLocalVariableMayBeConst
  HANDLE heap = GetCRTHeap();
  const auto data = (int64_t*)HeapAlloc(heap, HEAP_ZERO_MEMORY, activeKeys.size() * sizeof(int64_t));
  if (!data) return false;

  for (size_t i = 0; i < activeKeys.size(); ++i) {
    data[i] = activeKeys[i];
  }

  LoadAdventureArgs args = {};
  args.butchBox = self;
  args.catIDs.capacity_ = (uint32_t)activeKeys.size();
  args.catIDs.size_ = (uint32_t)activeKeys.size();
  args.catIDs.data_ = data;

  if (g_origLoadAdventure) {
    g_origLoadAdventure(&args);
  }

  HeapFree(heap, 0, data);
  return true;
}

void __fastcall Hook_LoadAdventure(LoadAdventureArgs *args) {
  if (g_origLoadAdventure) {
    g_origLoadAdventure(args);
  }

  if (args && args->butchBox) {
    const ButchBox *self = args->butchBox;
    int markedCount = 0;
    for (int i = 4; i < g_adventureCapacity; ++i) {
      if (self->cats.data_ && self->cats.data_[i]) {
        PersistentCharacter *cat = self->cats.data_[i];
        cat->onAdventure = true;
        markedCount++;
      }
    }
  }
}

void AdventureBoxHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
    SCAN_RESOLVE(mj, gameBase, CRTFree,
        "48 85 C9 74 36 53 48 83 EC 20 4C 8B C1 33 D2 48 8B 0D",
        g_pCRTHeap, 15, 3, 7);
    if (g_pCRTHeap)
        Overlay::Log("Found CRTHeap at RVA %p", (void*)g_pCRTHeap);

    HOOK_INSTALL(mj, gameBase, ButchBox_Init,
        "48 8B C4 55 53 56 57 41 56 48 8D 68 A1 48 81 EC E0 00 00 00", 20);

    HOOK_INSTALL(mj, gameBase, ButchBox_TryPlaceCat,
        "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 4C 8B FA 4C 8B E9 80 B9 F0 00 00 00 00", 27);

    HOOK_INSTALL(mj, gameBase, ButchBox_ConfigCats,
        "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 40 33 DB", 20);

    HOOK_INSTALL(mj, gameBase, ButchBox_TryRemoveCat,
        "48 8B 81 00 01 00 00 45 33 C9 48 39 10 75 03 4C 89 08 48 8B 81 00 01 00 00 48 39 50 08 75 04 4C 89 48 08", 35);

    HOOK_INSTALL(mj, gameBase, ButchBox_Depart,
        "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E8 48 81 EC 18 01 00 00", 29);

    HOOK_INSTALL(mj, gameBase, LoadAdventure,
        "48 89 5C 24 08 48 89 7C 24 20 55 48 8D 6C 24 C0 48 81 EC 40 01 00 00", 23);
}
