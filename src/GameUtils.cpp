#include "GameUtils.h"
#include "Overlay.h"
#include <intrin.h>
#include <windows.h>

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

Scene *GetSceneByName(const char *name) {
  MewDirector *p_md = GetMewDirectorSingleton();
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
  MewDirector *p_md = GetMewDirectorSingleton();
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

  TurnControl *tc = GetTurnControl();
  if (!tc) {
    Overlay::Log("GetFighters: TurnControl is null");
    return result;
  }

  if (!tc->context) {
    Overlay::Log("GetFighters: tc->context is null");
    return result;
  }

  CombatContext *ctx = tc->context;
  if (!ctx->entityManager) {
    Overlay::Log("GetFighters: ctx->entityManager is null");
    return result;
  }

  CombatEntityManager *mgr = ctx->entityManager;
  if (!mgr->stateBlock) {
    Overlay::Log("GetFighters: mgr->stateBlock is null");
    return result;
  }

  CombatStateBlock *sb = mgr->stateBlock;
  if (!sb->fighters) {
    Overlay::Log("GetFighters: sb->fighters is null");
    return result;
  }

  FighterList *list = sb->fighters;
  if (!list->data) {
    Overlay::Log("GetFighters: list->data is null");
    return result;
  }

  if (list->count == 0) {
    Overlay::Log("GetFighters: list->count is 0");
    return result;
  }

  for (uint32_t i = 0; i < list->count; i++) {
    Character *c = list->data[i];
    if (c) {
      result.push_back(c);
    }
  }

  return result;
}

std::vector<Character *> GetFighters() {
  std::vector<Character *> all = GetAllEntities();
  std::vector<Character *> fighters;

  for (Character *c : all) {
    if (c->isStatic || c->isSpeculativeInanimate || c->characterType == 4) {
      continue;
    }

    fighters.push_back(c);
  }

  return fighters;
}

std::string GetAbilityName(Ability *ability) {
  if (!ability)
    return "NULL";

  for (int i = 0; i < 64; i += 8) {
    __try {
      void *p = *(void **)((uintptr_t)ability + i);
      if (p && (uintptr_t)p > 0x10000) {
        AbilityDefinition *def = (AbilityDefinition *)p;
        std::string name = def->name.copy_to_native_string();
        if (!name.empty() && name.length() < 128) {
          bool printable = true;
          for (char c : name) {
            if (c < 32 || c > 126) {
              printable = false;
              break;
            }
          }
          if (printable) {
            return name;
          }
        }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }

  return "UNKNOWN";
}

Ability *FindCharacterAbility(Character *actor, const std::string &targetName) {
  if (!actor)
    return nullptr;

  // Check direct ability pointers
  Ability *directAbilities[] = {actor->defaultMove, actor->basicAttack};
  for (Ability *directAbility : directAbilities) {
    if (directAbility && (uintptr_t)directAbility > 0x10000) {
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
  if (actor->spells) {
    for (int i = 0; i < 5; i++) {
      __try {
        Ability *a = actor->spells[i];
        if (a && (uintptr_t)a > 0x10000 && (uintptr_t)a < 0x7FFFFFFFFFFF) {
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

std::vector<Component *> GetSceneComponents(Scene *scene) {
  std::vector<Component *> result;
  if (!scene)
    return result;
  if (scene->doing_scene_destruction)
    return result;
  if (!scene->ComponentLists)
    return result;

  podvector<Component *> &list = *scene->ComponentLists;
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

  podvector<Component *> &comps = entity->components;
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

Component *FindComponentByTypeName(Scene *scene, const char *typeName) {
  std::vector<Component *> components = GetSceneComponents(scene);
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

bool SafeGetComponentName(Component *p_component,
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

bool GetButtonRoleName(Component *button, char *outBuf, size_t bufSize) {
  if (!button || !outBuf || bufSize == 0)
    return false;
  __try {
    auto *role = (MsvcReleaseModeXString *)((uintptr_t)button + 0x1F8);
    if (role->_Mysize > 0 && role->_Mysize < 256) {
      auto sv = role->as_native_string_view();
      size_t len = sv.size() < bufSize - 1 ? sv.size() : bufSize - 1;
      memcpy(outBuf, sv.data(), len);
      outBuf[len] = '\0';
      return true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  outBuf[0] = '\0';
  return false;
}

Component *FindButton(Scene *scene, const char *roleName) {
  std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *c : components) {
    MsvcReleaseModeXString tn = {};
    if (!SafeGetComponentName(c, &tn))
      continue;
    if (tn.as_native_string_view() != "Button")
      continue;
    __try {
      auto *role = (MsvcReleaseModeXString *)((uintptr_t)c + 0x1F8);
      if (role->_Mysize > 0 && role->_Mysize < 256 &&
          role->as_native_string_view() == roleName) {
        return c;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  return nullptr;
}

std::vector<Component *> FindAllButtons(Scene *scene, const char *roleName) {
  std::vector<Component *> result;
  std::vector<Component *> components = GetSceneComponents(scene);
  for (Component *c : components) {
    MsvcReleaseModeXString tn = {};
    if (!SafeGetComponentName(c, &tn))
      continue;
    if (tn.as_native_string_view() != "Button")
      continue;
    __try {
      auto *role = (MsvcReleaseModeXString *)((uintptr_t)c + 0x1F8);
      if (role->_Mysize > 0 && role->_Mysize < 256 &&
          role->as_native_string_view() == roleName) {
        result.push_back(c);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }
  return result;
}

void ButtonGroupTracker::Init(Scene *scene, const char *role) {
  roleName = role;
  buttons = FindAllButtons(scene, role);
  lastStates.assign(buttons.size(), ButtonState_Invalid);
}

std::vector<ButtonChange> ButtonGroupTracker::Poll() {
  std::vector<ButtonChange> changed;
  for (size_t i = 0; i < buttons.size(); i++) {
    ButtonState state = GetButtonState(buttons[i]);
    if (state != lastStates[i]) {
      changed.push_back({(int)i, lastStates[i], state});
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
  void **tlsArray = (void **)__readgsqword(0x58);
  if (!tlsArray)
    return nullptr;
  return tlsArray[0];
}

void SetRNGState(const void *seed32) {
  uint8_t *tls = (uint8_t *)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  const uint32_t *seed = (const uint32_t *)seed32;

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
  uint8_t *tls = (uint8_t *)GetThreadLocalStoragePointer();
  if (!tls)
    return;

  uint32_t *out = (uint32_t *)outSeed32;

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
  const uint8_t *p = (const uint8_t *)data;
  while (size--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++)
      crc = (crc >> 1) ^ (-(int32_t)(crc & 1) & 0xEDB88320);
  }
  return ~crc;
}

} // namespace GameUtils
void *GameUtils::ResolveGridTile(int x, int y) {
  Scene *scene = GetSceneByName("Battle");
  if (!scene)
    return nullptr;

  std::vector<Component *> components = GetSceneComponents(scene);
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
  int idx = (y * 10) + x;
  if (idx >= 0 && idx < (int)tiles.size()) {
    Component *tile = tiles[idx];
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
