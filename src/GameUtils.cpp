#include "GameUtils.h"
#include "Overlay.h"
#include <windows.h>

#ifndef _MSC_VER
#define __try try // NOLINT(*-reserved-identifier)
#define __except(x) catch(...) // NOLINT(*-reserved-identifier)
#endif

namespace GameUtils {

static MewDirector **g_pMewDirectorPtr = nullptr;
static TurnControl **g_pTurnControlPtr = nullptr;

void SetMewDirectorSingletonPtr(MewDirector **ptr) { g_pMewDirectorPtr = ptr; }
void SetTurnControlPtr(TurnControl **ptr) { g_pTurnControlPtr = ptr; }

TurnControl *GetTurnControl() {
  if (g_pTurnControlPtr)
    return *g_pTurnControlPtr;
  return nullptr;
}

MewDirector *GetMewDirectorSingleton() {
  if (g_pMewDirectorPtr)
    return *g_pMewDirectorPtr;
  return nullptr;
}

static ExecSQL_t g_ExecSQL = nullptr;
void SetExecSQLPtr(const ExecSQL_t ptr) {
  g_ExecSQL = ptr;
}
ExecSQL_t GetExecSQLPtr() {
  return g_ExecSQL;
}

void ExecuteSQL(const char* query) {
  if (!g_ExecSQL) return;
  MewDirector* dir = GetMewDirectorSingleton();
  if (!dir) return;

  void* sqlSaveFile = (char*)dir + 0x4a8;

  MsvcReleaseModeXString queryStr;
  size_t len = strlen(query);
  if (len < 16) {
    memcpy(queryStr.Bx.Buf, query, len + 1);
    queryStr.Myres = 15;
  } else {
    queryStr.Bx.Ptr = (char*)malloc(len + 1);
    memcpy(queryStr.Bx.Ptr, query, len + 1);
    queryStr.Myres = len;
  }
  queryStr.Mysize = len;

  void* dummyFunc[8] = {}; // Dummy std::function block (64 bytes)
  g_ExecSQL(sqlSaveFile, &queryStr, dummyFunc);

  if (len >= 16 && queryStr.Bx.Ptr) {
    free(queryStr.Bx.Ptr);
  }
}

std::string SanitizeSQLString(const std::string& input) {
  std::string output;
  for (char c : input) {
    if (c == '\'') {
      output += "''";
    } else {
      output += c;
    }
  }
  return output;
}

void SetSaveProperty(const std::string& key, int value) {
  std::string safeKey = SanitizeSQLString(key);
  char query[512];
  snprintf(query, sizeof(query), "INSERT OR REPLACE INTO properties VALUES ('%s', %d);", safeKey.c_str(), value);
  ExecuteSQL(query);
}

static ContinueFile_t g_ContinueFile = nullptr;
void SetContinueFilePtr(const ContinueFile_t ptr) {
  g_ContinueFile = ptr;
}

static StartRun_t g_StartRun = nullptr;
void SetStartRunPtr(const StartRun_t ptr) {
  g_StartRun = ptr;
}

struct FakeSaveSelection {
  char pad0[0x18];    // 0x00
  Entity* entity;     // 0x18
  Scene* scene;       // 0x20
  Director* director; // 0x28
  char pad3[0x8];     // 0x30
  MsvcReleaseModeXString* saveStrings_first; // 0x38
  MsvcReleaseModeXString* saveStrings_last;  // 0x40
  MsvcReleaseModeXString* saveStrings_end;   // 0x48
};

static FakeSaveSelection g_fakeSaveSelection = {};
static MsvcReleaseModeXString g_fakeSaveStrings[4] = {};

bool g_injectCustomSaveData = false;
int g_customTeamSize = 4;
int g_customDifficulty = 0;
int g_customCollarIndex = 0;
bool g_startCustomRunPending = false;

void LoadSaveFile(const char *saveName) {
  g_injectCustomSaveData = true;
  // This will initiate a fadeout sequence. At the end of it, it will initiate the save file with the given name,
  // creating a new mewdirector. It later takes the scene pointer from the fake save selection and destroys it,
  // loading the new save's scenes in its place.

  if (!g_ContinueFile) {
    Overlay::Log("[SAVE] ContinueFile not hooked!");
    return;
  }

  Overlay::Log("[SAVE] Triggering mod save load sequence...");
  const MewDirector* md = GetMewDirectorSingleton();
  if (!md || !md->director) {
    Overlay::Log("[SAVE] MewDirector or Director is null!");
    return;
  }

  std::vector<Scene*> scenes = GetCurrentScenes();
  int baseIndex = -1;
  int tutorialIndex = -1;

  // Instanced scenes are loaded between Base and Tutorial
  // All other scenes are globals and should *not* be deleted!
  for (int i = 0; i < (int)scenes.size(); i++) {
    if (scenes[i]) {
      std::string name(scenes[i]->name.as_native_string_view());
      if (name == "Base") baseIndex = i;
      if (name == "Tutorial") tutorialIndex = i;
    }
  }

  if (baseIndex == -1 || tutorialIndex == -1 || tutorialIndex <= baseIndex) {
    Overlay::Log("[SAVE] Error: Could not find valid Base and Tutorial scenes!");
    return;
  }
  Scene* targetScene = scenes[tutorialIndex - 1];

  // Deconstruct scenes between Base and the targetScene
  for (int i = baseIndex + 1; i < tutorialIndex - 1; i++) {
    if (scenes[i]) {
      scenes[i]->doing_scene_destruction = 1;
    }
  }

  // Find ANY valid component to proxy the Entity/Director
  Component* validComp = nullptr;
  for (int i = (int)scenes.size() - 1; i >= 0; i--) {
    std::vector<Component*> comps = GetSceneComponents(scenes[i]);
    if (!comps.empty()) {
      validComp = comps[0];
      break;
    }
  }

  if (!validComp) { // WTF?!
    Overlay::Log("[SAVE] Error: Could not find any valid components to proxy!");
    return;
  }

  memset(&g_fakeSaveSelection, 0, sizeof(g_fakeSaveSelection));
  memset(g_fakeSaveStrings, 0, sizeof(g_fakeSaveStrings));

  g_fakeSaveSelection.entity = validComp->entity;
  g_fakeSaveSelection.scene = targetScene;
  g_fakeSaveSelection.director = validComp->director;

  g_fakeSaveSelection.saveStrings_first = g_fakeSaveStrings;
  g_fakeSaveSelection.saveStrings_last = g_fakeSaveStrings + 4;
  g_fakeSaveSelection.saveStrings_end = g_fakeSaveStrings + 4;

  size_t len = strlen(saveName);
  if (len < 16) {
    memcpy(g_fakeSaveStrings[1].Bx.Buf, saveName, len + 1);
    g_fakeSaveStrings[1].Myres = 15;
  } else {
    const auto heapBuf = (char*)malloc(len + 1);
    memcpy(heapBuf, saveName, len + 1);
    g_fakeSaveStrings[1].Bx.Ptr = heapBuf;
    g_fakeSaveStrings[1].Myres = len;
  }
  g_fakeSaveStrings[1].Mysize = len;

  g_ContinueFile(&g_fakeSaveSelection, 1, false);
}

void StartCustomRun(int teamSize, int difficulty, int collarIndex) {
  if (!g_StartRun) {
    Overlay::Log("[RUN] StartRun not hooked!");
    return;
  }

  MewDirector* dir = GetMewDirectorSingleton();
  if (!dir) {
    Overlay::Log("[RUN] MewDirector is null!");
    return;
  }

  // Offset 1448 (0x5a8) is ProgressState
  void* progressState = *(void**)((char*)dir + 1448);
  if (!progressState) {
    Overlay::Log("[RUN] ProgressState is null!");
    return;
  }

  // Set difficulty mod
  *(int32_t*)((char*)progressState + 0x410) = difficulty;
  *(int32_t*)((char*)progressState + 0x428) = difficulty;
  *(int32_t*)((char*)progressState + 0x440) = difficulty;

  const char* mapName = "alley.gon";

  MsvcReleaseModeXString mapStr = {};
  mapStr.Mysize = strlen(mapName);
  mapStr.Myres = 15;
  memcpy(mapStr.Bx.Buf, mapName, mapStr.Mysize + 1);

  Overlay::Log("[RUN] Starting custom run: TeamSize=%d, Difficulty=%d, CollarIndex=%d", teamSize, difficulty, collarIndex);

  g_StartRun(dir, &mapStr, collarIndex, teamSize, 1);
}

Scene *GetSceneByName(const char *name) {
  const MewDirector *p_md = GetMewDirectorSingleton();
  if (!p_md || !p_md->director)
    return nullptr;

  for (Scene *p_scene : p_md->director->scenes) {
    if (!p_scene)
      continue;
    if (p_scene->name.as_native_string_view() == name) {
      return p_scene;
    }
  }
  return nullptr;
}

std::vector<Scene *> GetCurrentScenes() {
  std::vector<Scene *> result;
  const MewDirector *p_md = GetMewDirectorSingleton();
  if (!p_md || !p_md->director)
    return result;

  for (Scene *p_scene : p_md->director->scenes) {
    if (p_scene) {
      result.push_back(p_scene);
    }
  }
  return result;
}

std::vector<Character *> GetAllEntities() {
  std::vector<Character *> result;

  const TurnControl *tc = GetTurnControl();
  if (!tc) {
    Overlay::Log("GetFighters: TurnControl is null");
    return result;
  }

  if (!tc->context) {
    Overlay::Log("GetFighters: tc->context is null");
    return result;
  }

  const CombatContext *ctx = tc->context;
  if (!ctx->entityManager) {
    Overlay::Log("GetFighters: ctx->entityManager is null");
    return result;
  }

  const CombatEntityManager *mgr = ctx->entityManager;
  if (!mgr->stateBlock) {
    Overlay::Log("GetFighters: mgr->stateBlock is null");
    return result;
  }

  const CombatStateBlock *sb = mgr->stateBlock;
  if (!sb->fighters) {
    Overlay::Log("GetFighters: sb->fighters is null");
    return result;
  }

  const FighterList *list = sb->fighters;
  if (!list->data) {
    Overlay::Log("GetFighters: list->data is null");
    return result;
  }

  if (list->count == 0) {
    Overlay::Log("GetFighters: list->count is 0");
    return result;
  }

  for (uint32_t i = 0; i < list->count; i++) {
    if (Character *c = list->data[i]) {
      result.push_back(c);
    }
  }

  return result;
}

std::vector<Character *> GetFighters() {
  const std::vector<Character *> all = GetAllEntities();
  std::vector<Character *> fighters;

  for (Character *c : all) {
    if (c->isStatic || c->isInanimate || c->characterType == 4) {
      continue;
    }

    fighters.push_back(c);
  }

  return fighters;
}

static std::string SafeGetNativeString(const MsvcReleaseModeXString& xstr) {
  if (xstr.Mysize >= 1024 || xstr.Myres < xstr.Mysize) return "";
  
  std::string result(xstr.Mysize, '\0');
  if (xstr.Myres < 16) {
    memcpy(&result[0], xstr.Bx.Buf, xstr.Mysize);
  } else {
    if ((uintptr_t)xstr.Bx.Ptr <= 0x10000 || (uintptr_t)xstr.Bx.Ptr >= 0x7FFFFFFFFFFF) return "";
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), xstr.Bx.Ptr, &result[0], xstr.Mysize, &bytesRead) || bytesRead != xstr.Mysize) {
      return "";
    }
  }
  return result;
}

std::string GetAbilityName(Ability *ability) {
  if (!ability || (uintptr_t)ability <= 0x10000 || (uintptr_t)ability >= 0x7FFFFFFFFFFF
      || ((uintptr_t)ability & 0xF))
    return "NULL";

  auto check_definition = [](void *p) -> std::string {
    if (!p || (uintptr_t)p <= 0x10000 || (uintptr_t)p >= 0x7FFFFFFFFFFF || ((uintptr_t)p & 0xF)) return "";
    AbilityDefinition def;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), p, &def, sizeof(AbilityDefinition), &bytesRead) || bytesRead != sizeof(AbilityDefinition)) {
        return "";
    }
    std::string name = SafeGetNativeString(def.name);
    if (!name.empty() && name.length() < 128) {
      bool printable = true;
      for (const char c : name) {
        if (c < 32 || c > 126) {
          printable = false;
          break;
        }
      }
      if (printable) {
        return name;
      }
    }
    return "";
  };

  std::string result = check_definition(ability->definition);
  if (!result.empty()) return result;

  // Fallback: only scan safe offsets if definition lookup fails.
  // Explicitly skip 0 (vtable) and 16 (owner pointer).
  for (int i = 8; i < 64; i += 8) {
    if (i == 16)
      continue; // Skip owner pointer (Character*)
      
    void *p = nullptr;
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)ability + i), &p, sizeof(void*), &bytesRead) && bytesRead == sizeof(void*)) {
        result = check_definition(p);
        if (!result.empty()) return result;
    }
  }

  return "UNKNOWN";
}

Ability *FindCharacterAbility(const Character *actor, const std::string &targetName) {
  if (!actor)
    return nullptr;

  // Check direct ability pointers
  Ability *directAbilities[] = {actor->defaultMove, actor->basicAttack};
  for (Ability *directAbility : directAbilities) {
    if (directAbility && (uintptr_t)directAbility > 0x10000
        && !((uintptr_t)directAbility & 0xF)) {
      __try {
        if (directAbility->owner == actor &&
            GetAbilityName(directAbility) == targetName) {
          return directAbility;
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }
  }

  // Iterate spells
  if (actor != nullptr && actor->spells) {
    for (int i = 0; i < 5; i++) {
      __try {
        Ability *a = actor->spells[i];
        if (a && (uintptr_t)a > 0x10000 && (uintptr_t)a < 0x7FFFFFFFFFFF
            && !((uintptr_t)a & 0xF)) {
          if (a->owner == actor && GetAbilityName(a) == targetName) {
            return a;
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        break;
      }
    }
  }
  return nullptr;
}

std::vector<Component *> GetSceneComponents(const Scene *scene) {
  std::vector<Component *> result;
  if (!scene)
    return result;
  if (scene->doing_scene_destruction)
    return result;
  if (!scene->ComponentLists)
    return result;

  const podvector<Component *> &list = *scene->ComponentLists;
  for (uint32_t i = 0; i < list.size_; i++) {
    Component *p_component = list.data_[i];
    if (!p_component)
      continue;
    if (p_component->deleted)
      continue;
    result.push_back(p_component);
  }
  return result;
}

std::vector<Component *> GetEntityComponents(Entity *entity) {
  std::vector<Component *> result;
  if (!entity)
    return result;

  const podvector<Component *> &comps = entity->components;
  for (uint32_t i = 0; i < comps.size_; i++) {
    Component *p_component = comps.data_[i];
    if (!p_component)
      continue;
    if (p_component->deleted)
      continue;
    result.push_back(p_component);
  }
  return result;
}

Component *FindComponentByTypeName(const Scene *scene, const char *typeName) {
  const std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *p_component : components) {
    MsvcReleaseModeXString name = {};
    if (SafeGetComponentName(p_component, &name)) {
      if (name.as_native_string_view() == typeName) {
        return p_component;
      }
    }
  }
  return nullptr;
}

bool SafeGetComponentName(const Component *p_component,
                          MsvcReleaseModeXString *out_name) {
  __try {
    if (!p_component || !p_component->vtable ||
        !p_component->vtable->GetObjectTypeSTR)
      return false;
    p_component->vtable->GetObjectTypeSTR(p_component, out_name);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

ButtonState GetButtonState(Component *button) {
  if (!button)
    return ButtonState_Invalid;
  __try {
    return (ButtonState) * (int32_t *)((uintptr_t)button + 0x2F0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return ButtonState_Invalid;
  }
}

bool GetButtonRoleName(Component *button, char *outBuf, const size_t bufSize) {
  if (!button || !outBuf || bufSize == 0)
    return false;
  __try {
    const auto *role = (MsvcReleaseModeXString *)((uintptr_t)button + 0x1F8);
    if (role->Mysize > 0 && role->Mysize < 256) {
      const auto sv = role->as_native_string_view();
      const size_t len = sv.size() < bufSize - 1 ? sv.size() : bufSize - 1;
      memcpy(outBuf, sv.data(), len);
      outBuf[len] = '\0';
      return true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  if (outBuf != nullptr) {
    outBuf[0] = '\0';
  }
  return false;
}

Component *FindButton(const Scene *scene, const char *roleName) {
  const std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *c : components) {
    MsvcReleaseModeXString tn = {};
    if (!SafeGetComponentName(c, &tn))
      continue;
    if (tn.as_native_string_view() != "Button")
      continue;
    __try {
      auto *role = (MsvcReleaseModeXString *)((uintptr_t)c + 0x1F8);
      if (role->Mysize > 0 && role->Mysize < 256 &&
          role->as_native_string_view() == roleName) {
        return c;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  return nullptr;
}

std::vector<Component *> FindAllButtons(const Scene *scene, const char *roleName) {
  std::vector<Component *> result;
  const std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *c : components) {
    MsvcReleaseModeXString tn = {};
    if (!SafeGetComponentName(c, &tn))
      continue;
    if (tn.as_native_string_view() != "Button")
      continue;
    __try {
      const auto *role = (MsvcReleaseModeXString *)((uintptr_t)c + 0x1F8);
      if (role->Mysize > 0 && role->Mysize < 256 &&
          role->as_native_string_view() == roleName) {
        result.push_back(c);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  return result;
}

void ButtonGroupTracker::Init(const Scene *scene, const char *role) {
  roleName = role;
  buttons = FindAllButtons(scene, role);
  lastStates.assign(buttons.size(), ButtonState_Invalid);
}

std::vector<ButtonChange> ButtonGroupTracker::Poll() {
  std::vector<ButtonChange> changed;
  for (size_t i = 0; i < buttons.size(); i++) {
    const ButtonState state = GetButtonState(buttons[i]);
    if (state != lastStates[i]) {
      changed.push_back({static_cast<int>(i), lastStates[i], state});
      lastStates[i] = state;
    }
  }
  return changed;
}

void ButtonGroupTracker::Reset() {
  roleName = nullptr;
  buttons.clear();
  lastStates.clear();
}

void *GetThreadLocalStoragePointer() {
  // On Windows x64, the Thread Environment Block (TEB) contains a pointer
  // to the Thread Local Storage (TLS) array at gs:[0x58].
  const auto tlsArray = (void **)__readgsqword(0x58);
  if (!tlsArray)
    return nullptr;
  return tlsArray[0];
}

void SetRNGState(const void *seed32) {
  const auto tls = (uint8_t *)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  const auto seed = (const uint32_t *)seed32;

  // Inject the 256-bit seed directly into the Thread Local RNG State!
  *(uint32_t *)(tls + 0x178) = seed[0];
  *(uint32_t *)(tls + 0x17c) = seed[1];
  *(uint32_t *)(tls + 0x180) = seed[2];
  *(uint32_t *)(tls + 0x184) = seed[3];

  // The last 16 bytes are written as two 64-bit integers.
  // Offset 400 (decimal) is 0x190 (hex).
  *(uint64_t *)(tls + 0x188) = *(uint64_t *)(seed + 4);
  *(uint64_t *)(tls + 0x190) = *(uint64_t *)(seed + 6);
}

void GetRNGState(void *outSeed32) {
  const auto tls = (uint8_t *)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  auto *out = (uint32_t *)outSeed32;

  out[0] = *(uint32_t *)(tls + 0x178);
  out[1] = *(uint32_t *)(tls + 0x17c);
  out[2] = *(uint32_t *)(tls + 0x180);
  out[3] = *(uint32_t *)(tls + 0x184);

  *(uint64_t *)(out + 4) = *(uint64_t *)(tls + 0x188);
  *(uint64_t *)(out + 6) = *(uint64_t *)(tls + 0x190);
}

// Note: Doesn't work, will need to look into it further
uint32_t CalculateCRC32(const void *data, size_t size) {
  uint32_t crc = 0xFFFFFFFF;
  auto p = (const uint8_t *)data;
  while (size--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++)
      crc = crc >> 1 ^ -(int32_t)(crc & 1) & 0xEDB88320;
  }
  return ~crc;
}

std::vector<UIAbilitySlot *> GetUIAbilitySlots(CombatUISlotManager *em) {
  std::vector<UIAbilitySlot *> list;
  if (!em) return list;

  if (em->primaryEntity)
    list.push_back(em->primaryEntity);
  if (em->secondaryEntity)
    list.push_back(em->secondaryEntity);

  if (em->extraArray) {
    for (uint32_t i = 0; i < em->extraCount; i++) {
      if (em->extraArray[i])
        list.push_back(em->extraArray[i]);
    }
  }

  if (em->tertiaryEntity)
    list.push_back(em->tertiaryEntity);

  return list;
}

} // namespace GameUtils
void *GameUtils::ResolveGridTile(const int x, const int y) {
  // Unused!
  const Scene *scene = GetSceneByName("Battle");
  if (!scene)
    return nullptr;

  const std::vector<Component *> components = GetSceneComponents(scene);
  std::vector<Component *> tiles;

  for (Component *c : components) {
    MsvcReleaseModeXString name = {};
    if (!SafeGetComponentName(c, &name))
      continue;
    std::string_view nameStr = name.as_native_string_view();

    // Fuzzy search for tile-like components
    if (nameStr.find("Tile") != std::string::npos) {
      tiles.push_back(c);
    }
  }

  if (tiles.empty()) {
    Overlay::Log("[GRID] [ERR] No tile-like components found in 'Battle'!");
    return nullptr;
  }

  // Deterministic counting
  const int idx = y * 10 + x;
  if (idx >= 0 && idx < (int)tiles.size()) {
    const Component *tile = tiles[idx];
    MsvcReleaseModeXString name = {};
    SafeGetComponentName(tile, &name);
    Overlay::Log("[GRID] Resolved (%d, %d) via Index -> %d, name: %s", x, y,
                 idx, name.as_native_string_view().data());
    return tiles[idx];
  }

  Overlay::Log("[GRID] [ERR] Failed to resolve (%d, %d) among %zu tiles!", x, y,
               tiles.size());
  return nullptr;
}
