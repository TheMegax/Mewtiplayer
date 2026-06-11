#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <windows.h>

// Scene definitions adapted from polymeric's mewgenics_analysis
// Source: https://github.com/p0lymeric/mewgenics_analysis
// Credit: polymeric 2026

// MSVC Vector (std::vector), laid out as compiled in Release mode
template <class Value_type> struct MsvcReleaseModeVector {
  Value_type *Myfirst;
  Value_type *Mylast;
  Value_type *Myend;

  [[nodiscard]] const Value_type *begin() const { return Myfirst; }
  [[nodiscard]] const Value_type *end() const { return Mylast; }
  Value_type *begin() { return Myfirst; }
  Value_type *end() { return Mylast; }
  [[nodiscard]] size_t size() const { return this->Mylast - this->Myfirst; }
};

// podvector
template <typename T> struct podvector {
  uint32_t capacity_;
  uint32_t size_;
  T *data_;

  T *begin() { return data_; }
  T *end() { return data_ + size_; }
  [[nodiscard]] const T *begin() const { return data_; }
  [[nodiscard]] const T *end() const { return data_ + size_; }
  [[nodiscard]] uint32_t size() const { return size_; }
};

// MSVC XString (std::string), laid out as compiled in Release mode
struct MsvcReleaseModeXString {
  union {
    char Buf[16];
    char *Ptr;
  } Bx;
  uint64_t Mysize;
  uint64_t Myres;

  [[nodiscard]] bool is_valid() const {
    if (this->Mysize >= 1024)
      return false;
    if (this->Myres < this->Mysize)
      return false;
    if (this->Myres >= 16) {
      if ((uintptr_t)this->Bx.Ptr <= 0x10000 || (uintptr_t)this->Bx.Ptr >= 0x7FFFFFFFFFFF)
        return false;
    }
    return true;
  }

  [[nodiscard]] const char *begin() const {
    if (!this->is_valid())
      return "";
    if (this->Myres < 16)
      return &this->Bx.Buf[0];
    return this->Bx.Ptr;
  }

  [[nodiscard]] const char *end() const {
    if (!this->is_valid())
      return "";
    if (this->Myres < 16)
      return &this->Bx.Buf[this->Mysize];
    return this->Bx.Ptr + this->Mysize;
  }

  [[nodiscard]] std::string copy_to_native_string() const {
    if (!this->is_valid())
      return "";
    return std::string(this->begin(), this->end());
  }

  [[nodiscard]] std::string_view as_native_string_view() const {
    if (!this->is_valid())
      return "";
    return std::string_view(this->begin(), this->Mysize);
  }
};

// MSVC XString (std::wstring), laid out as compiled in Release mode
struct MsvcReleaseModeWString {
  union {
    wchar_t Buf[8];
    wchar_t *Ptr;
  } Bx;
  uint64_t Mysize;
  uint64_t Myres;

  [[nodiscard]] bool is_valid() const {
    if (this->Mysize >= 1024)
      return false;
    if (this->Myres < this->Mysize)
      return false;
    if (this->Myres >= 8) {
      if ((uintptr_t)this->Bx.Ptr <= 0x10000 || (uintptr_t)this->Bx.Ptr >= 0x7FFFFFFFFFFF)
        return false;
    }
    return true;
  }

  [[nodiscard]] const wchar_t *begin() const {
    if (!this->is_valid())
      return L"";
    if (this->Myres < 8)
      return &this->Bx.Buf[0];
    return this->Bx.Ptr;
  }

  [[nodiscard]] const wchar_t *end() const {
    if (!this->is_valid())
      return L"";
    if (this->Myres < 8)
      return &this->Bx.Buf[this->Mysize];
    return this->Bx.Ptr + this->Mysize;
  }

  [[nodiscard]] std::wstring copy_to_native_wstring() const {
    if (!this->is_valid())
      return L"";
    return std::wstring(this->begin(), this->end());
  }

  [[nodiscard]] std::string to_utf8() const {
    if (!this->is_valid())
      return "";
    const std::wstring ws = copy_to_native_wstring();
    if (ws.empty())
      return "";
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(),
                                                nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(), &strTo[0],
                        size_needed, nullptr, nullptr);
    return strTo;
  }
};

struct Entity;
struct Scene;
struct Director;
struct EntityVTable;
struct PersistentCharacter;
struct Character;
struct House;
struct AbilityDefinition;
struct Ability;
struct FighterList;
struct CombatStateBlock;
struct CombatEntityManager;
struct CombatUISlotManager;
struct CombatContext;
struct CombatUIContext;
struct TurnControl;
struct CombatResolutionState;
struct UIAbilitySlotVTable;
struct UIAbilitySlot;

template <typename T> struct ComponentVTable {
  MsvcReleaseModeXString *(__cdecl *GetObjectTypeSTR)(
      const T *thiss, MsvcReleaseModeXString *returns);
  int32_t(__cdecl *GetObjectType)(const T *thiss);
  bool(__cdecl *TypeInHierarchy)(const T *thiss, MsvcReleaseModeXString *type);
  void *_reserved1;
  void *_reserved2;
  void *_reserved3;
};

struct Component {
  const ComponentVTable<Component> *vtable; // 0x00
  uint32_t _objid;                          // 0x08
  uint8_t override_tags_B0;                 // 0x0C
  uint8_t override_tags_B1;                 // 0x0D
  bool entity_enabled;                      // 0x0E
  bool deleted;                             // 0x0F
  bool enabled;                             // 0x10
  bool started;                             // 0x11
  char _12[6];                              // 0x12
  Entity *entity;                           // 0x18
  Scene *scene;                             // 0x20
  Director *director;                       // 0x28
  double timescale;                         // 0x30
};
static_assert(sizeof(Component) == 0x38, "Component size mismatch");

struct Scene {
  Director *director;                      // 0x000
  podvector<Entity *> Entities;            // 0x008
  podvector<Component *> *ComponentLists;  // 0x018
  void *CachedActiveComponentLists;        // 0x020
  char _padding_1[0x488];                  // 0x028
  bool doing_scene_destruction;            // 0x4B0
  char _padding_2[0x7];                    // 0x4B1
  MsvcReleaseModeXString name;             // 0x4B8
  char _padding_3[0xE4];                   // 0x4D8
  int32_t petListCount;                    // 0x5BC
  PersistentCharacter **petListPtr;        // 0x5C0
  char _padding_4[0x7C];                   // 0x5C8
  int32_t catList2Count;                   // 0x644
  PersistentCharacter **catListPtr;        // 0x648
};
static_assert(offsetof(Scene, doing_scene_destruction) == 0x4B0, "Scene offset mismatch");
static_assert(offsetof(Scene, name) == 0x4B8, "Scene offset mismatch");
static_assert(offsetof(Scene, petListCount) == 0x5BC, "Scene offset mismatch");
static_assert(offsetof(Scene, catList2Count) == 0x644, "Scene offset mismatch");

struct Entity {
  EntityVTable *vtable;              // 0x00
  Scene *scene;                      // 0x08
  double timescale;                  // 0x10
  bool deleted;                      // 0x18
  bool enabled;                      // 0x19
  char _padding[0x6];                // 0x1A
  podvector<Component *> components; // 0x20
  podvector<void *> unknown_0;       // 0x30
  char _padding_1[0x40];             // 0x40
  int64_t catID;                     // 0x80
  bool isActive;                     // 0x88
  char _padding_2[0x5F];             // 0x89
  void *container;                   // 0xE8
};
static_assert(offsetof(Entity, catID) == 0x80, "Entity offset mismatch");
static_assert(offsetof(Entity, container) == 0xE8, "Entity offset mismatch");

struct EntityVTable {
  void *(__cdecl *VDtor)(Entity *thiss, uint32_t flags);
};

struct House {
  char _padding_0[0x1C];    // 0x00
  int32_t absoluteDay;      // 0x1C
  char _padding_1[0x1C];    // 0x20
  int32_t furnitureCount;   // 0x3C
  void *furniturePtr;       // 0x40
  char _padding_2[0x68];    // 0x48
  int32_t food;             // 0xB0
  int32_t gold;             // 0xB4
  int32_t blankCollars;     // 0xB8
  int32_t storageExpansion; // 0xBC
  char _padding_3[0x350];   // 0xC0
  int32_t difficultyMod1;   // 0x410
  char _padding_4[0x14];    // 0x414
  int32_t difficultyMod2;   // 0x428
  char _padding_5[0x14];    // 0x42C
  int32_t difficultyMod3;   // 0x440
};
static_assert(offsetof(House, food) == 0xB0, "House offset mismatch");
static_assert(offsetof(House, absoluteDay) == 0x1C, "House offset mismatch");

struct Director {
  MsvcReleaseModeVector<Scene *> scenes;
};

struct MewDirector {
  char _padding_0[0x18];                // 0x000
  void* contextData;                    // 0x018
  void* sceneManager;                   // 0x020
  Director *director;                   // 0x028
  char _padding_1[0x8];                 // 0x030
  char gameStateMap[0x470];             // 0x038
  void* sqlSaveFile;                    // 0x4A8
  MsvcReleaseModeXString saveNameStr;   // 0x4B0
  char _padding_1a[0xB0];               // 0x4D0
  int32_t currentDay;                   // 0x580
  char _padding_2[0x14];                // 0x584
  void* pedigreeState;                  // 0x598
  void *inventoryPtr;                   // 0x5A0
  House *house;                         // 0x5A8
  char _padding_3[0x8];                 // 0x5B0
  int32_t partyCapacity;                // 0x5B8
  int32_t partyCount;                   // 0x5BC
  PersistentCharacter **partyData;      // 0x5C0
  char _padding_4[0x108];               // 0x5C8
  int32_t eventDifficulty;              // 0x6D0
  char _padding_5[0x4];                 // 0x6D4
  int32_t gamePhase;                    // 0x6D8
  int32_t chapterState;                 // 0x6DC
  int32_t chapterNum;                   // 0x6E0
  char _padding_6[0x4];                 // 0x6E4
  MsvcReleaseModeXString chapterName;   // 0x6E8
  char _padding_7[0xC];                 // 0x708
  bool inCombat;                        // 0x714
  bool isProductionSave;                // 0x715
  char _padding_8[0x7A];                // 0x716
};
static_assert(offsetof(MewDirector, director) == 0x028, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, gameStateMap) == 0x038, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, sqlSaveFile) == 0x4A8, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, saveNameStr) == 0x4B0, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, currentDay) == 0x580, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, pedigreeState) == 0x598, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, house) == 0x5A8, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, partyCount) == 0x5BC, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, inCombat) == 0x714, "MewDirector offset mismatch");
static_assert(sizeof(MewDirector) == 0x790, "MewDirector size mismatch");

namespace glaiel {
struct SQLSaveFile {
  void* db;                              // 0x00
  MsvcReleaseModeXString db_path_string; // 0x08
  char padding[128];                     // 0x28 (safe padding)
};
}

struct MewSaveFile {
  char _padding_0[0x470];       // 0x000
  glaiel::SQLSaveFile sqlFile;  // 0x470
  char _padding_1[0xE8];        // 0x518
};
static_assert(sizeof(MewSaveFile) == 0x600, "MewSaveFile size mismatch");

struct GameTLSLayout {
  char padding[0x178];
  uint32_t rngState[4];  // 0x178
  uint64_t rngState4;    // 0x188
  uint64_t rngState5;    // 0x190
};

struct HouseCat : Component {
  char _padding_0[0x48];  // 0x38
  int64_t sql_key;        // 0x80
};

struct PersistentCharacter {
  char _padding_0[0x18];              // 0x000
  MsvcReleaseModeWString name;        // 0x018
  char _padding_1[0x48];              // 0x038
  int64_t sql_key;                    // 0x080
  char _padding_2[0x54];              // 0x088
  bool onAdventure;                   // 0x0DC
  char _padding_2a[0xAFB];            // 0x0DD
  int64_t parentID;                   // 0xBD8
  char _padding_3[0x18];              // 0xBE0
  int32_t state;                      // 0xBF8
  char _padding_4[0x14];              // 0xBFC
  MsvcReleaseModeXString className;   // 0xC10
  int32_t level;                      // 0xC30
  char _padding_6[0x4];               // 0xC34
  int32_t birthDay;                   // 0xC38
  char _padding_7[0x4];               // 0xC3C
  int64_t deathDay;                   // 0xC40
  int64_t catID;                      // 0xC48
};
static_assert(offsetof(PersistentCharacter, name) == 0x018, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, sql_key) == 0x080, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, onAdventure) == 0x0DC, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, className) == 0xC10, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, level) == 0xC30, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, deathDay) == 0xC40, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, catID) == 0xC48, "PersistentCharacter offset mismatch");

struct AbilityDefinition {
  char _padding_0[0x88];        // 0x00
  MsvcReleaseModeXString name;  // 0x88
};

struct Ability {
  void *vtable;                   // 0x00
  AbilityDefinition *definition;  // 0x08
  Character *owner;               // 0x10
  char _padding_1[0x8];           // 0x18
};

struct FighterList {
  char _padding_0[0xC];   // 0x00
  uint32_t count;         // 0x0C
  Character **data;       // 0x10
};

struct CombatStateBlock {
  char _padding_0[0x1F90]; // 0x0000
  FighterList *fighters;   // 0x1F90
};

struct CombatEntityManager {
  char _padding_0[0x20];         // 0x00
  CombatStateBlock *stateBlock;  // 0x20
};

struct TacticsTile : Component {
  char _padding_0[0x10];  // 0x38
  int32_t x;              // 0x48
  int32_t y;              // 0x4C
};

struct GridPositionComponent : Component {
  char _padding_0[0x40];      // 0x38
  TacticsTile *currentNode;   // 0x78
  TacticsTile *targetNode;    // 0x80
};

struct CombatContext {
  char _padding_0[0x8];                 // 0x00
  CombatEntityManager *entityManager;   // 0x08
};

struct CombatUIContext {
  char _padding_0[0x38];                 // 0x000
  CombatUISlotManager *entityManager;    // 0x038
  char _padding_1[0x280];                // 0x040
  int32_t pendingActionCount;            // 0x2C0
  char _padding_2[0x10C];                // 0x2C4
  bool isInputDirty;                     // 0x3D0
  char _padding_3[0x2];                  // 0x3D1
  bool isDragging;                       // 0x3D3
};
static_assert(offsetof(CombatUIContext, entityManager) == 0x038, "BattleUIContext offset mismatch");
static_assert(offsetof(CombatUIContext, pendingActionCount) == 0x2C0, "BattleUIContext offset mismatch");
static_assert(offsetof(CombatUIContext, isInputDirty) == 0x3D0, "BattleUIContext offset mismatch");
static_assert(offsetof(CombatUIContext, isDragging) == 0x3D3, "BattleUIContext offset mismatch");

struct TurnControl {
  char _padding_0[0x18];    // 0x00
  CombatContext *context;   // 0x18
  char _padding_1[0x138];   // 0x20
  int32_t turnCount;        // 0x158
};

struct CombatResolutionState {
  char _padding_0[0x35];  // 0x000
  bool defeat;            // 0x035
  char _padding_1[0x174]; // 0x036
  bool victory;           // 0x1AA
};
static_assert(offsetof(CombatResolutionState, defeat) == 0x35, "CombatResolutionState offset mismatch");
static_assert(offsetof(CombatResolutionState, victory) == 0x1AA, "CombatResolutionState offset mismatch");

struct Character {
  void *vtable;                         // 0x000
  char _padding_0[0x80];                // 0x008
  PersistentCharacter *persistentChar;  // 0x088
  char _padding_1[0x38];                // 0x090
  Ability *ability0;                    // 0x0C8 (It is only valid for the first ability cast then disappears)
  Ability *defaultMove;                 // 0x0D0
  Ability *basicAttack;                 // 0x0D8
  char _padding_1a[0x10];               // 0x0E0
  Ability **spells;                     // 0x0F0
  char _padding_1b[0x198];              // 0x0F8 -- TODO: Check item actions later, they may be an array too
  MsvcReleaseModeWString name;          // 0x290
  char _padding_2[0x1D9];               // 0x2B0
  uint8_t isPlayerCat;                  // 0x489
  char _padding_3[0x26];                // 0x48A
  int32_t currentHP;                    // 0x4B0
  int32_t barrierHP;                    // 0x4B4
  int32_t lives;                        // 0x4B8
  int32_t maxHP;                        // 0x4BC
  char _padding_4[0x2];                 // 0x4C0
  bool isDead;                          // 0x4C2
  char _padding_5[0xDD];                // 0x4C3
  int32_t strength;                     // 0x5A0
  int32_t dexterity;                    // 0x5A4
  int32_t constitution;                 // 0x5A8
  int32_t intelligence;                 // 0x5AC
  int32_t speed;                        // 0x5B0
  int32_t charisma;                     // 0x5B4
  int32_t luck;                         // 0x5B8
  char _padding_6[0x1C];                // 0x5BC
  int32_t baseMovement;                 // 0x5D8
  int32_t baseInitiative;               // 0x5DC
  int32_t baseManaRegen;                // 0x5E0
  int32_t baseHealthRegen;              // 0x5E4
  char _padding_7[0x8];                 // 0x5E8
  int32_t baseMana;                     // 0x5F0
  int32_t baseInitialMana;              // 0x5F4
  char _padding_8[0x2C];                // 0x5F8
  int32_t baseCritChance;               // 0x624
  char _padding_9[0xC8];                // 0x628
  int32_t buffedStr;                    // 0x6F0
  int32_t buffedDex;                    // 0x6F4
  int32_t buffedCon;                    // 0x6F8
  int32_t buffedInt;                    // 0x6FC
  int32_t buffedSpd;                    // 0x700
  char _padding_10[0x5D6];              // 0x704
  bool isStatic;                        // 0xCDA
  bool isInanimate;                     // 0xCDB
  char _padding_11[0x2];                // 0xCDC
  bool isChampion;                      // 0xCDE
  bool isElite;                         // 0xCDF
  char _padding_12[0x14];               // 0xCE0
  int32_t characterType;                // 0xCF4 (Enemy, Small, Boss, Cat, Object)
};
static_assert(offsetof(Character, name) == 0x290, "Character offset mismatch");
static_assert(offsetof(Character, currentHP) == 0x4B0, "Character offset mismatch");
static_assert(offsetof(Character, maxHP) == 0x4BC, "Character offset mismatch");
static_assert(offsetof(Character, strength) == 0x5A0, "Character offset mismatch");
static_assert(offsetof(Character, buffedStr) == 0x6F0, "Character offset mismatch");
static_assert(offsetof(Character, characterType) == 0xCF4, "Character offset mismatch");

struct UIAbilitySlotVTable {
  void *unk0;
  void *unk1;
  uint64_t(__fastcall *IsCastable)(UIAbilitySlot *thiss, uint64_t param);
};

struct UIAbilitySlot {
  UIAbilitySlotVTable *vtable;
};

struct CombatUISlotManager {
  char _padding_0[0xD0];           // 0x00
  UIAbilitySlot *primaryEntity;    // 0xD0
  UIAbilitySlot *secondaryEntity;  // 0xD8
  UIAbilitySlot *tertiaryEntity;   // 0xE0
  char _padding_1[0x4];            // 0xE8
  uint32_t extraCount;             // 0xEC
  UIAbilitySlot **extraArray;      // 0xF0
};
static_assert(offsetof(CombatUISlotManager, primaryEntity) == 0xD0, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, secondaryEntity) == 0xD8, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, tertiaryEntity) == 0xE0, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, extraCount) == 0xEC, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, extraArray) == 0xF0, "BattleUISlotManager offset mismatch");

struct TurnAction {
  int32_t type;       // 0x00
  int32_t _pad0;      // 0x04
  Ability *ability;   // 0x08
  int32_t targetX;    // 0x10
  int32_t targetY;    // 0x14
  int32_t target2X;   // 0x18
  int32_t target2Y;   // 0x1C
  Character *actor;   // 0x20
  int32_t unk_28;     // 0x28
  int32_t unk_2C;     // 0x2C
  uint8_t flag_30;    // 0x30
  uint8_t flag_31;    // 0x31
  uint8_t flag_32;    // 0x32
  uint8_t flag_33;    // 0x33
  uint8_t flag_34;    // 0x34
  uint8_t flag_35;    // 0x35
  uint8_t flag_36;    // 0x36
  uint8_t flag_37;    // 0x37
  int32_t _field_38;  // 0x38
  int32_t _field_3C;  // 0x3C
  void *staticPtr40;  // 0x40
  void *staticPtr48;  // 0x48
  void *dynamicPtr50; // 0x50
  void *dynamicPtr58; // 0x58 (Unique heap token)
  void *staticPtr60;  // 0x60
  void *staticPtr68;  // 0x68
  void *callback;     // 0x70
  void *_field_78;    // 0x78
  int32_t _field_80;  // 0x80
  int32_t magic84;    // 0x84 ("AULT")
};

struct vec2 {
  double x;
  double y;
};

struct MovieClip {
  char _padding_0[0x80]; // 0x00
  double x;              // 0x80
  double y;              // 0x88
};

struct Button : Component {
  char _padding_0[0x1C0];             // 0x038
  MsvcReleaseModeXString roleName;    // 0x1F8
  char _padding_1[0xD8];              // 0x218
  int32_t state;                      // 0x2F0
};

struct PauseMenuScene {
  char _padding_0[0x28];          // 0x00
  void *sceneManager;             // 0x28
  char _padding_1[0x78];          // 0x30
  int32_t pauseTimer;             // 0xA4
};

struct ButchBox : Component {
  void *movieClip;                      // 0x38
  char _padding_0[0xA0];                // 0x40
  void *butchBoxMovieClip;              // 0xE0
  void *butchBoxMaskMovieClip;          // 0xE8
  bool tutorialFlag;                    // 0xF0
  char _padding_1[0x7];                 // 0xF1
  podvector<PersistentCharacter*> cats; // 0xF8
  podvector<vec2> positions;            // 0x108
};
