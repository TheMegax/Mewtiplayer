#include "hooks/AdventureBoxHooks.h"
#include "hooks/HookMacros.h"
#include "MewgenicsTypes.h"
#include "GameUtils.h"
#include "Scanner.h"
#include "ParaboxAPI.h"
#include <algorithm>
#include <vector>

static int g_adventureCapacity = 4;
static HANDLE* g_pCRTHeap = nullptr;

typedef void* (__fastcall *LookupCatData_t)(void* pedigreeState, int64_t catID);
static LookupCatData_t g_LookupCatData = nullptr;

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

static ButchBox* g_activeButchBox = nullptr;

void __fastcall Hook_ButchBox_Init(ButchBox *self, const bool param2) {
  g_activeButchBox = self;
  if (g_origButchBox_Init) {
    g_origButchBox_Init(self, param2);
  }

  if (self && g_adventureCapacity > 4) {
    HANDLE heap = GetCRTHeap();

    // Reallocate slots vector
    if (self->slots.data_) {
      if (void* newCats = HeapReAlloc(heap, HEAP_ZERO_MEMORY, self->slots.data_, g_adventureCapacity * sizeof(HouseCat*))) {
        self->slots.data_ = (HouseCat**)newCats;
        if (g_adventureCapacity > 4) {
          memset(self->slots.data_ + 4, 0, (g_adventureCapacity - 4) * sizeof(HouseCat*));
        }
        self->slots.capacity_ = g_adventureCapacity;
        self->slots.size_ = g_adventureCapacity;
      }
    }

    // Reallocate slot_offsets vector
    if (self->slot_offsets.data_) {
      if (void* newPositions = HeapReAlloc(heap, HEAP_ZERO_MEMORY, self->slot_offsets.data_, g_adventureCapacity * sizeof(Vec2D))) {
        self->slot_offsets.data_ = (Vec2D*)newPositions;

        // Positioning it ourselves
        const double startX = self->slot_offsets.data_[0].x;
        const double startY = self->slot_offsets.data_[0].y;
        const double step = g_adventureCapacity > 1 ? (3.0 / (double)(g_adventureCapacity - 1)) : 0.0;
        for (int i = 0; i < g_adventureCapacity; ++i) {
          self->slot_offsets.data_[i].x = startX + (double)i * step;
          self->slot_offsets.data_[i].y = startY;
        }
        self->slot_offsets.capacity_ = g_adventureCapacity;
        self->slot_offsets.size_ = g_adventureCapacity;
      }
    }
  }

  ParaboxAPI::ButchBoxInitEvent ev = {};
  ev.box = self;
  ev.param2 = param2;
  ParaboxAPI::OnButchBoxInit.Publish(ev);
}

bool __fastcall Hook_ButchBox_TryPlaceCat(ButchBox *self, void *cat) {
  if (!self || !cat) return false;

  int best_slot = -1;
  auto* houseCat = static_cast<HouseCat*>(cat);
  auto* catTransform = houseCat ? houseCat->transform : nullptr;
  auto* boxTransform = self->transform;

  if (catTransform && boxTransform) {
    const double catX = catTransform->position.x;
    const double catY = catTransform->position.y;
    const double boxX = boxTransform->position.x;
    const double boxY = boxTransform->position.y;

    const double relX = catX - boxX;
    const double relY = catY - boxY;

    double min_dist = 1e9;
    for (int i = 0; i < g_adventureCapacity; ++i) {
      if (self->slots.data_ && self->slots.data_[i] == nullptr) {
        const double slotX = self->slot_offsets.data_[i].x;
        const double slotY = self->slot_offsets.data_[i].y;
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
      if (self->slots.data_ && self->slots.data_[i] == nullptr) {
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
    std::swap(self->slots.data_[0], self->slots.data_[best_slot]);
    std::swap(self->slot_offsets.data_[0], self->slot_offsets.data_[best_slot]);

    if (g_origButchBox_TryPlaceCat) {
      result = g_origButchBox_TryPlaceCat(self, cat);
    }

    // Swap back
    std::swap(self->slots.data_[0], self->slots.data_[best_slot]);
    std::swap(self->slot_offsets.data_[0], self->slot_offsets.data_[best_slot]);
  } else {
    if (g_origButchBox_TryPlaceCat) {
      result = g_origButchBox_TryPlaceCat(self, cat);
    }
  }

  ParaboxAPI::ButchBoxTryPlaceCatEvent ev = {};
  ev.box = self;
  ev.cat = cat;
  ev.returnValue = result;
  ParaboxAPI::OnButchBoxTryPlaceCat.Publish(ev);

  return ev.returnValue;
}

void __fastcall Hook_ButchBox_ConfigCats(ButchBox *self) {
  if (!self) return;

  for (int start = 0; start < g_adventureCapacity; start += 4) {
    if (start == 0) {
      if (g_origButchBox_ConfigCats) {
        g_origButchBox_ConfigCats(self);
      }
    } else {
      const int count = std::min(4, g_adventureCapacity - start);
      for (int i = 0; i < count; ++i) {
        std::swap(self->slots.data_[i], self->slots.data_[start + i]);
        std::swap(self->slot_offsets.data_[i], self->slot_offsets.data_[start + i]);
      }

      if (g_origButchBox_ConfigCats) {
        g_origButchBox_ConfigCats(self);
      }

      for (int i = 0; i < count; ++i) {
        std::swap(self->slots.data_[i], self->slots.data_[start + i]);
        std::swap(self->slot_offsets.data_[i], self->slot_offsets.data_[start + i]);
      }
    }
  }

  ParaboxAPI::ButchBoxConfigCatsEvent ev = {};
  ev.box = self;
  ParaboxAPI::OnButchBoxConfigCats.Publish(ev);
}

void __fastcall Hook_ButchBox_TryRemoveCat(ButchBox *self, void *cat) {
  if (!self || !cat) return;

  int idx = -1;
  for (int i = 0; i < g_adventureCapacity; ++i) {
    if (self->slots.data_[i] == cat) {
      idx = i;
      break;
    }
  }

  if (idx != -1) {
    if (idx >= 4) {
      std::swap(self->slots.data_[0], self->slots.data_[idx]);
      std::swap(self->slot_offsets.data_[0], self->slot_offsets.data_[idx]);

      if (g_origButchBox_TryRemoveCat) {
        g_origButchBox_TryRemoveCat(self, cat);
      }

      std::swap(self->slots.data_[0], self->slots.data_[idx]);
      std::swap(self->slot_offsets.data_[0], self->slot_offsets.data_[idx]);
    } else {
      if (g_origButchBox_TryRemoveCat) {
        g_origButchBox_TryRemoveCat(self, cat);
      }
    }
    
    ParaboxAPI::ButchBoxTryRemoveCatEvent ev = {};
    ev.box = self;
    ev.cat = cat;
    ParaboxAPI::OnButchBoxTryRemoveCat.Publish(ev);
  }
}

bool __fastcall Hook_ButchBox_Depart(ButchBox *self) {
  if (!self) return false;

  std::vector<int64_t> activeKeys;
  for (int i = 0; i < g_adventureCapacity; ++i) {
    if (self->slots.data_ && self->slots.data_[i]) {
      activeKeys.push_back(self->slots.data_[i]->cat_id);
    }
  }

  if (activeKeys.empty()) {
    return false;
  }

  bool result = false;
  if (g_adventureCapacity == 4 && activeKeys.size() < 4) {
    if (g_origButchBox_Depart) {
      result = g_origButchBox_Depart(self);
    }
  } else {
    HANDLE heap = GetCRTHeap();
    const auto data = (int64_t*)HeapAlloc(heap, HEAP_ZERO_MEMORY, activeKeys.size() * sizeof(int64_t));
    if (data) {
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
      result = true;
    }
  }

  ParaboxAPI::ButchBoxDepartEvent ev = {};
  ev.box = self;
  ev.returnValue = result;
  ParaboxAPI::OnButchBoxDepart.Publish(ev);
  
  return ev.returnValue;
}

void __fastcall Hook_LoadAdventure(LoadAdventureArgs *args) {
  if (g_origLoadAdventure) {
    g_origLoadAdventure(args);
  }

  if (args && args->butchBox) {
    const ButchBox *self = args->butchBox;
    for (int i = 4; i < g_adventureCapacity; ++i) {
      if (self->slots.data_ && self->slots.data_[i]) {
        self->slots.data_[i]->departed = true;
      }
    }
  }
  
  ParaboxAPI::LoadAdventureEvent ev = {};
  ev.args = args;
  ParaboxAPI::OnLoadAdventure.Publish(ev);
}

PARABOX_API void SetAdventureCapacity(int capacity) {
    g_adventureCapacity = capacity;
}

void AdventureBoxHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
    RESOLVE_DATA(gameBase, GameSymbols::acrt_heap, g_pCRTHeap);

    HOOK_INSTALL(mj, gameBase, ButchBox_Init, GameSymbols::ButchBox_init, 0);
    HOOK_INSTALL(mj, gameBase, ButchBox_TryPlaceCat, GameSymbols::ButchBox_try_place_cat, 0);
    HOOK_INSTALL(mj, gameBase, ButchBox_ConfigCats, GameSymbols::ButchBox_late_update, 0);
    HOOK_INSTALL(mj, gameBase, ButchBox_TryRemoveCat, GameSymbols::ButchBox_try_remove_cat, 0);
    HOOK_INSTALL(mj, gameBase, ButchBox_Depart, GameSymbols::ButchBox_depart, 0);
    HOOK_INSTALL(mj, gameBase, LoadAdventure, GameSymbols::ButchBox_depart_transition, 0);

    RESOLVE_FUNC(gameBase, GameSymbols::CatDatabase_get_cat, g_LookupCatData);
}

PARABOX_API ParaboxAPI::Array<int64_t> GetButchBoxCatKeys() {
  std::vector<int64_t> keys;
  const ButchBox* box = nullptr;
  for (const Scene* scene : GameUtils::GetCurrentScenes()) {
    if (scene) {
      box = (ButchBox*)GameUtils::FindComponentByTypeName(scene, "ButchBox");
      if (box) break;
    }
  }

  if (!box) {
    box = g_activeButchBox;
  }

  if (!box) return ParaboxAPI::MakeArray(keys);

  for (int i = 0; i < g_adventureCapacity; ++i) {
    if (box->slots.data_ && box->slots.data_[i]) {
      keys.push_back(box->slots.data_[i]->cat_id);
    }
  }
  return ParaboxAPI::MakeArray(keys);
}

PARABOX_API int GetButchBoxCatAge(const int64_t sqlKey) {
  const ButchBox* box = nullptr;
  for (const Scene* scene : GameUtils::GetCurrentScenes()) {
    if (scene) {
      box = (ButchBox*)GameUtils::FindComponentByTypeName(scene, "ButchBox");
      if (box) break;
    }
  }

  if (!box) {
    box = g_activeButchBox;
  }

  if (!box) return 0;

  MewDirector* director = GameUtils::GetMewDirectorSingleton();
  if (!director) return 0;

  void* pedigreeState = director->cat_db;
  if (!pedigreeState || !g_LookupCatData) return 0;

  for (int i = 0; i < g_adventureCapacity; ++i) {
    if (box->slots.data_ && box->slots.data_[i]) {
      const int64_t catID = box->slots.data_[i]->cat_id;
      if (catID == sqlKey) {
        if (const auto* cat = (CatData*)g_LookupCatData(pedigreeState, catID)) {
          int64_t deathDay = cat->deathday;
          if (deathDay == -1) {
            deathDay = director->current_day;
          }
          return (int)(deathDay - cat->birthday);
        }
      }
    }
  }
  return 0;
}

PARABOX_API int GetLocalButchBoxCatCount() {
  const ButchBox* box = nullptr;
  for (const Scene* scene : GameUtils::GetCurrentScenes()) {
    if (scene) {
      box = (ButchBox*)GameUtils::FindComponentByTypeName(scene, "ButchBox");
      if (box) break;
    }
  }

  if (!box) {
    box = g_activeButchBox;
  }

  if (!box) return 0;

  int count = 0;
  for (int i = 0; i < g_adventureCapacity; ++i) {
    if (box->slots.data_ && box->slots.data_[i]) {
      count++;
    }
  }
  return count;
}
