#include "hooks/ClassChooserHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "MewgenicsTypes.h"
#include "Scanner.h"
#include "ParaboxAPI.h"

static void *g_activeCatSelector = nullptr;

typedef void *(__fastcall *LookupCatData_t)(void *pedigreeState, int64_t catID);
typedef void (__fastcall *RefreshCatSelectorUI_t)(void *selector);
typedef void (__fastcall *ApplyCollar_t)(CatData *cat, void *collarNameStr);
typedef void (__fastcall *RefreshInventoryGrid_t)(void *classChooser);
typedef void (__fastcall *ClassChooserResetCat_t)(ClassChooser *chooser, int64_t catID, bool param_2);
typedef void (__fastcall *ClassChooserClose_t)(ClassChooser *chooser);

static LookupCatData_t g_LookupCatData = nullptr;
static RefreshCatSelectorUI_t g_RefreshCatSelectorUI = nullptr;
static ApplyCollar_t g_ApplyCollar = nullptr;
static RefreshInventoryGrid_t g_RefreshInventoryGrid = nullptr;
static ClassChooserResetCat_t g_ClassChooserResetCat = nullptr;
static ClassChooserClose_t g_ClassChooserClose = nullptr;

HOOK_DEFINE(CatSelector_init, void, void *, int64_t)
HOOK_DEFINE(ClassTagBox_Click, void, void *)
HOOK_DEFINE(ClassChooser_LockIn, void, void *)

namespace ParaboxAPI {

PARABOX_API void ClearClassTagBoxes() {
  // Deprecated stub: tag boxes are queried directly from ClassChooser::boxes
}

PARABOX_API void RegisterClassTagBox(void *comp) {
  // Deprecated stub: tag boxes are queried directly from ClassChooser::boxes
}

PARABOX_API ClassChooser* GetActiveClassChooser() {
  for (const Scene *scene : GameUtils::GetCurrentScenes()) {
    if (!scene) continue;
    for (const Component *comp : GameUtils::GetSceneComponents(scene)) {
      if (!comp || comp->deleted) continue;
      MsvcReleaseModeXString name = {};
      if (GameUtils::SafeGetComponentName(comp, &name)) {
        const bool match = name.as_native_string_view() == "ClassChooser";
        GameUtils::FreeXString(name);
        if (match) return const_cast<ClassChooser *>(reinterpret_cast<const ClassChooser *>(comp));
      }
    }
  }
  return nullptr;
}

PARABOX_API void ResetCatOnClassChooser(ClassChooser *chooser, const int64_t catID, const bool restoreDefaults) {
  if (!chooser) chooser = GetActiveClassChooser();
  if (g_ClassChooserResetCat && chooser) {
    g_ClassChooserResetCat(chooser, catID, restoreDefaults);
  }
}

PARABOX_API void ForceClassChooserClose(ClassChooser *chooser) {
  if (!chooser) chooser = GetActiveClassChooser();
  if (g_ClassChooserClose && chooser) {
    g_ClassChooserClose(chooser);
  }
}

int64_t ResolveCatIDFromTagBox(void *tagBox) {
  const auto *box = static_cast<ClassTagBox *>(tagBox);
  if (!box) return -1;
  int64_t catID = box->equipped_cat_id;
  if (catID == -1) {
    if (const auto *classChooser = box->parent ? box->parent : GetActiveClassChooser()) {
      catID = classChooser->current_cat;
    }
  }
  return catID;
}

int32_t FindTagBoxIndex(const void *tagBoxPtr, const void *classChooserPtr) {
  const auto *classChooser = static_cast<const ClassChooser *>(classChooserPtr);
  if (!classChooser) {
    classChooser = GetActiveClassChooser();
  }
  if (!classChooser || !classChooser->boxes.data_) {
    return -1;
  }
  for (uint32_t i = 0; i < classChooser->boxes.size(); i++) {
    if (classChooser->boxes.data_[i] == tagBoxPtr) {
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
  const auto *classChooser = box ? box->parent : nullptr;
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
  if (auto *chooser = GetActiveClassChooser()) {
    g_RefreshInventoryGrid(chooser);
  }
}

PARABOX_API void UpdateClassChooserTagBoxes(const int64_t catID, const int32_t collarIndex) {
  auto *chooser = GetActiveClassChooser();
  if (!chooser || !chooser->boxes.data_) return;

  if (collarIndex != -1) {
    if (collarIndex >= 0 && static_cast<uint32_t>(collarIndex) < chooser->boxes.size()) {
      // clear cat from any other tag box
      for (uint32_t j = 0; j < chooser->boxes.size(); j++) {
        ClassTagBox *otherBox = chooser->boxes.data_[j];
        if (otherBox && otherBox->equipped_cat_id == catID) {
          otherBox->equipped_cat_id = -1;
          otherBox->equipped = false;
        }
      }
      ClassTagBox *targetBox = chooser->boxes.data_[collarIndex];
      if (targetBox) {
        targetBox->equipped_cat_id = catID;
        targetBox->equipped = true;
      }
    }
  } else {
    // clear tag box bound to this cat
    for (uint32_t i = 0; i < chooser->boxes.size(); i++) {
      ClassTagBox *box = chooser->boxes.data_[i];
      if (box && box->equipped_cat_id == catID) {
        box->equipped_cat_id = -1;
        box->equipped = false;
        break;
      }
    }
  }
}

PARABOX_API const char *ResolveCollarNameFromIndex(const int32_t collarIndex) {
  if (collarIndex == -1) {
    return "Colorless";
  }

  const auto *chooser = GetActiveClassChooser();
  if (chooser && chooser->boxes.data_ && collarIndex >= 0 && static_cast<uint32_t>(collarIndex) < chooser->boxes.size()) {
    const ClassTagBox *box = chooser->boxes.data_[collarIndex];
    if (box && box->cat_class.is_valid()) {
      return box->cat_class.begin();
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
  if (const auto *chooser = GetActiveClassChooser()) {
    return static_cast<int32_t>(chooser->boxes.size());
  }
  return 0;
}

} // namespace ParaboxAPI

void CatSelectorHooks_Init(MewjectorAPI *mj, const uintptr_t gameBase) {
  RESOLVE_FUNC(gameBase, GameSymbols::ClassChooser_refresh_item_locations, g_RefreshInventoryGrid);
  RESOLVE_FUNC(gameBase, GameSymbols::CatData_set_class_preview, g_ApplyCollar);
  RESOLVE_FUNC(gameBase, GameSymbols::CatDatabase_get_cat, g_LookupCatData);
  RESOLVE_FUNC(gameBase, GameSymbols::CatSelector_RefreshAll, g_RefreshCatSelectorUI);
  RESOLVE_FUNC(gameBase, GameSymbols::ClassChooser_ResetCat, g_ClassChooserResetCat);
  RESOLVE_FUNC(gameBase, GameSymbols::ClassChooser_close, g_ClassChooserClose);

  HOOK_INSTALL(mj, gameBase, CatSelector_init, GameSymbols::CatSelector_init, 0);
  HOOK_INSTALL(mj, gameBase, ClassTagBox_Click, GameSymbols::ClassTagBox_click, 0);
  HOOK_INSTALL(mj, gameBase, ClassChooser_LockIn, GameSymbols::ClassChooser_init_embark, 0);
}