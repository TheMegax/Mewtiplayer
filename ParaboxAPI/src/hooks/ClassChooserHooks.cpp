#include "hooks/ClassChooserHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "Scanner.h"
#include "ParaboxAPI.h"

struct ClassTagBox : Component  {
  void *classChooser;                     // 0x38
  [[maybe_unused]] char _padding_0[0x18]; // 0x40
  MsvcReleaseModeXString boxName;         // 0x58
  [[maybe_unused]] char _padding_1[0x18]; // 0x78
  int64_t catID;                          // 0x90
};

struct ClassChooser : Component {
  [[maybe_unused]] char _padding_0[0x64]; // 0x38
  uint32_t numTagBoxes;                   // 0x9c
  ClassTagBox **tagBoxes;                 // 0xa0
  [[maybe_unused]] char _padding_1[0x28]; // 0xa8
  int64_t catID;                          // 0xd0
};

static void *g_activeCatSelector = nullptr;

typedef void *(__fastcall *LookupCatData_t)(void *pedigreeState, int64_t catID);
typedef void (__fastcall *RefreshCatSelectorUI_t)(void *selector);
typedef void (__fastcall *ApplyCollar_t)(CatData *cat, void *collarNameStr);
typedef void (__fastcall *RefreshInventoryGrid_t)(void *classChooser);

static LookupCatData_t g_LookupCatData = nullptr;
static RefreshCatSelectorUI_t g_RefreshCatSelectorUI = nullptr;
static ApplyCollar_t g_ApplyCollar = nullptr;
static RefreshInventoryGrid_t g_RefreshInventoryGrid = nullptr;

HOOK_DEFINE(CatSelector_init, void, void *, int64_t)
HOOK_DEFINE(ClassTagBox_Click, void, void *)
HOOK_DEFINE(ClassChooser_LockIn, void, void *)

static std::vector<void *>* g_classTagBoxes = new std::vector<void *>();

namespace ParaboxAPI {

PARABOX_API void ClearClassTagBoxes() {
  g_classTagBoxes->clear();
}

PARABOX_API void RegisterClassTagBox(void *comp) {
  if (comp) {
    g_classTagBoxes->push_back(comp);
  }
}

int64_t ResolveCatIDFromTagBox(void *tagBox) {
  const auto *box = static_cast<ClassTagBox *>(tagBox);
  int64_t catID = box->catID;
  if (catID == -1) {
    if (const auto *classChooser = static_cast<ClassChooser *>(box->classChooser)) {
      catID = classChooser->catID;
    }
  }
  return catID;
}

int32_t FindTagBoxIndex(const void *tagBoxPtr, const void *classChooserPtr) {
  for (size_t i = 0; i < g_classTagBoxes->size(); i++) {
    if ((*g_classTagBoxes)[i] == tagBoxPtr) {
      return static_cast<int32_t>(i);
    }
  }

  const ClassTagBox *tagBox = static_cast<const ClassTagBox *>(tagBoxPtr);
  const ClassChooser *classChooser = static_cast<const ClassChooser *>(classChooserPtr);

  if (!classChooser || !classChooser->tagBoxes) {
    return -1;
  }
  for (uint32_t i = 0; i < classChooser->numTagBoxes; i++) {
    if (classChooser->tagBoxes[i] == tagBox) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

} // namespace ParaboxAPI

static void __fastcall Hook_CatSelector_init(void *self, const int64_t param2) {
  g_activeCatSelector = self;
  if (g_origCatSelector_init) {
    g_origCatSelector_init(self, param2);
  }

  ParaboxAPI::CatSelectorInitEvent ev = {};
  ev.selector = self;
  ev.param2 = param2;
  ParaboxAPI::OnCatSelectorInit.Publish(ev);
}

static void __fastcall Hook_ClassTagBox_Click(void *tagBox) {
  const auto *box = static_cast<ClassTagBox *>(tagBox);
  const auto *classChooser = box ? static_cast<ClassChooser *>(box->classChooser) : nullptr;
  const int32_t clickedIndex = classChooser ? ParaboxAPI::FindTagBoxIndex(box, classChooser) : -1;
  const int64_t catID = ParaboxAPI::ResolveCatIDFromTagBox(tagBox);

  ParaboxAPI::ClassTagBoxClickEvent ev = {};
  ev.tagBox = tagBox;
  ev.catID = catID;
  ev.clickedIndex = clickedIndex;
  ParaboxAPI::OnClassTagBoxClick.Publish(ev);

  if (ev.cancelled) return;

  if (g_origClassTagBox_Click) {
    g_origClassTagBox_Click(tagBox);
  }
}

static void __fastcall Hook_ClassChooser_LockIn(void *lambdaThis) {
  ParaboxAPI::ClassChooserLockInEvent ev = {};
  ev.lambdaThis = lambdaThis;
  ParaboxAPI::OnClassChooserLockIn.Publish(ev);

  if (ev.cancelled) return;

  if (g_origClassChooser_LockIn) {
    g_origClassChooser_LockIn(lambdaThis);
  }
}

namespace ParaboxAPI {

PARABOX_API void* GetActiveCatSelector() {
  return g_activeCatSelector;
}

PARABOX_API bool IsCatSelectorValid(const void *selector) {
  if (!selector) {
    return false;
  }

  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) {
      continue;
    }
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      if (comp == selector && !comp->deleted) {
        return true;
      }
    }
  }
  return false;
}

PARABOX_API void RefreshCatSelectorUI() {
  if (g_RefreshCatSelectorUI && IsCatSelectorValid(g_activeCatSelector)) {
    g_RefreshCatSelectorUI(g_activeCatSelector);
  }
}

PARABOX_API CatData *GetCatDataById(const int64_t catID) {
  if (!g_LookupCatData || catID == -1) {
    return nullptr;
  }

  const MewDirector *director = GameUtils::GetMewDirectorSingleton();
  void *pedigreeState = director ? director->cat_db : nullptr;
  if (!pedigreeState) {
    return nullptr;
  }

  return (CatData *)g_LookupCatData(pedigreeState, catID);
}

PARABOX_API void ApplyCollarToCharacter(CatData *cat, const char *collarName) {
  if (!g_ApplyCollar || !cat || !collarName) {
    return;
  }
  MsvcReleaseModeXString nameStr = {};
  GameUtils::InitXString(nameStr, collarName);
  g_ApplyCollar(cat, &nameStr);
}

PARABOX_API void RefreshClassChooserInventory() {
  if (!g_RefreshInventoryGrid) {
    return;
  }
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "ClassChooser";
        GameUtils::FreeXString(name);
        if (match) {
          g_RefreshInventoryGrid(const_cast<void *>(reinterpret_cast<const void *>(comp)));
          return;
        }
      }
    }
  }
}

PARABOX_API void UpdateClassChooserTagBoxes(const int64_t catID, const int32_t collarIndex) {
  if (!g_classTagBoxes->empty()) {
    if (collarIndex != -1) {
      if (collarIndex >= 0 && static_cast<size_t>(collarIndex) < g_classTagBoxes->size()) {
        for (size_t j = 0; j < g_classTagBoxes->size(); j++) {
          auto *box = static_cast<ClassTagBox *>((*g_classTagBoxes)[j]);
          if (box->catID == catID) {
            box->catID = -1;
          }
        }
        auto *targetBox = static_cast<ClassTagBox *>((*g_classTagBoxes)[collarIndex]);
        targetBox->catID = catID;
        return;
      }
    } else {
      for (size_t i = 0; i < g_classTagBoxes->size(); i++) {
        auto *box = static_cast<ClassTagBox *>((*g_classTagBoxes)[i]);
        if (box->catID == catID) {
          box->catID = -1;
          break;
        }
      }
      return;
    }
  }

  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "ClassChooser";
        GameUtils::FreeXString(name);
        if (!match) continue;
      } else {
        continue;
      }

      const auto *classChooser = static_cast<ClassChooser *>(const_cast<Component *>(comp)); // NOLINT(*-pro-type-static-cast-downcast)
      if (!classChooser->tagBoxes || classChooser->numTagBoxes == 0) continue;

      if (collarIndex != -1) {
        if (collarIndex >= 0 && static_cast<uint32_t>(collarIndex) < classChooser->numTagBoxes) {
          // Remove this cat from any other tag box first
          for (uint32_t j = 0; j < classChooser->numTagBoxes; j++) {
            ClassTagBox *otherBox = classChooser->tagBoxes[j];
            if (otherBox->catID == catID) {
              otherBox->catID = -1;
            }
          }
          classChooser->tagBoxes[collarIndex]->catID = catID;
        }
      } else {
        // Clear any tag box that has this cat
        for (uint32_t i = 0; i < classChooser->numTagBoxes; i++) {
          ClassTagBox *box = classChooser->tagBoxes[i];
          if (box->catID == catID) {
            box->catID = -1;
            break;
          }
        }
      }
      return;
    }
  }
}

PARABOX_API const char *ResolveCollarNameFromIndex(const int32_t collarIndex) {
  if (collarIndex == -1) {
    return "Colorless";
  }

  if (collarIndex >= 0 && static_cast<size_t>(collarIndex) < g_classTagBoxes->size()) {
    const auto *box = static_cast<const ClassTagBox *>((*g_classTagBoxes)[collarIndex]);
    return box->boxName.is_valid() ? box->boxName.begin() : "Colorless";
  }

  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      MsvcReleaseModeXString compName = {};
      if (GameUtils::SafeGetComponentName(comp, &compName)) {
        const bool match = compName.as_native_string_view() == "ClassChooser";
        GameUtils::FreeXString(compName);
        if (!match) continue;
      } else {
        continue;
      }

      const auto *classChooser = static_cast<ClassChooser *>(const_cast<Component *>(comp)); // NOLINT(*-pro-type-static-cast-downcast)
      if (!classChooser->tagBoxes || collarIndex < 0 || static_cast<uint32_t>(collarIndex) >= classChooser->numTagBoxes) {
        return "Colorless";
      }
      const ClassTagBox *box = classChooser->tagBoxes[collarIndex];
      return box->boxName.is_valid() ? box->boxName.begin() : "Colorless";
    }
  }
  return "Colorless";
}

PARABOX_API void ForceClassChooserLockIn(void *lambdaThis) {
  if (g_origClassChooser_LockIn) {
    g_origClassChooser_LockIn(lambdaThis);
  }
}

PARABOX_API int32_t GetClassTagBoxCount() {
  return g_classTagBoxes ? static_cast<int32_t>(g_classTagBoxes->size()) : 0;
}

} // namespace ParaboxAPI

void CatSelectorHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  RESOLVE_FUNC(gameBase, GameSymbols::ClassChooser_refresh_item_locations, g_RefreshInventoryGrid);
  RESOLVE_FUNC(gameBase, GameSymbols::CatData_set_class_preview, g_ApplyCollar);
  RESOLVE_FUNC(gameBase, GameSymbols::CatDatabase_get_cat, g_LookupCatData);
  RESOLVE_FUNC(gameBase, GameSymbols::CatSelector_RefreshAll, g_RefreshCatSelectorUI);

  HOOK_INSTALL(mj, gameBase, CatSelector_init, GameSymbols::CatSelector_init, 0);
  HOOK_INSTALL(mj, gameBase, ClassTagBox_Click, GameSymbols::ClassTagBox_click, 0);
  HOOK_INSTALL(mj, gameBase, ClassChooser_LockIn, GameSymbols::ClassChooser_init_embark, 0);
}