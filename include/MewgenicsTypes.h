#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <windows.h>

// Definitions adapted from polymeric's mewgenics_analysis
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
  const ComponentVTable<Component> *vtable; // 0
  uint32_t _objid;                          // 8
  uint8_t override_tags_B0;                 // 12
  uint8_t override_tags_B1;                 // 13
  bool entity_enabled;                      // 14
  bool deleted;                             // 15
  bool enabled;                             // 16
  bool started;                             // 17
  char _12[6];                              // 18
  Entity *entity;                           // 24
  Scene *scene;                             // 32
  Director *director;                       // 40
  double timescale;                         // 48
};
static_assert(sizeof(Component) == 56, "Component size mismatch");

struct Scene {
  Director *director;                                    // 0
  podvector<Entity *> Entities;                          // 8
  podvector<Component *> *ComponentLists;                // 24
  void *CachedActiveComponentLists;                      // 32
  char _padding_1[1160];                                 // 40
  bool doing_scene_destruction;                          // 1200
  char _padding_2[7];                                    // 1201
  MsvcReleaseModeXString name;                           // 1208
  char _padding_3[260 - sizeof(MsvcReleaseModeXString)]; // 1240
  int32_t petListCount;                                  // 1468
  PersistentCharacter **petListPtr;                      // 1472
  char _padding_4[124];                                  // 1480
  int32_t catList2Count;                                 // 1604
  PersistentCharacter **catListPtr;                      // 1608
};
static_assert(offsetof(Scene, doing_scene_destruction) == 1200, "Scene offset mismatch");
static_assert(offsetof(Scene, name) == 1208, "Scene offset mismatch");
static_assert(offsetof(Scene, petListCount) == 1468, "Scene offset mismatch");
static_assert(offsetof(Scene, catList2Count) == 1604, "Scene offset mismatch");

struct Entity {
  EntityVTable *vtable;              // 0
  Scene *scene;                      // 8
  double timescale;                  // 16
  bool deleted;                      // 24
  bool enabled;                      // 25
  char _padding[6];                  // 26
  podvector<Component *> components; // 32
  podvector<void *> unknown_0;       // 48
  char _padding_1[64];               // 64
  int64_t catID;                     // 128
  bool isActive;                     // 136
  char _padding_2[95];               // 137
  void *container;                   // 232
};
static_assert(offsetof(Entity, catID) == 128, "Entity offset mismatch");
static_assert(offsetof(Entity, container) == 232, "Entity offset mismatch");

struct EntityVTable {
  void *(__cdecl *VDtor)(Entity *thiss, uint32_t flags);
};

struct House {
  char _padding_house_0[28];  // 0
  int32_t absoluteDay;        // 28
  char _padding_house_1[28];  // 32
  int32_t furnitureCount;     // 60
  void *furniturePtr;         // 64
  char _padding_house_2[104]; // 72
  int32_t food;               // 176
  int32_t gold;               // 180
  int32_t blankCollars;       // 184
  int32_t storageExpansion;   // 188
};
static_assert(offsetof(House, food) == 176, "House offset mismatch");
static_assert(offsetof(House, absoluteDay) == 28, "House offset mismatch");

struct Director {
  MsvcReleaseModeVector<Scene *> scenes;
};

struct MewDirector {
  char _padding_0[24];                // 0
  void* contextData;                  // 24
  void* sceneManager;                 // 32
  Director *director;                 // 40
  char _padding_1[1360];              // 48 (there's definitely stuff in here like holy)
  int32_t currentDay;                 // 1408
  char _padding_2[28];                // 1412
  void *inventoryPtr;                 // 1440
  House *house;                       // 1448
  char _padding_3[8];                 // 1456
  int32_t partyCapacity;              // 1464
  int32_t partyCount;                 // 1468
  PersistentCharacter **partyData;    // 1472
  char _padding_4[264];               // 1480 (more stuff in here)
  int32_t eventDifficulty;            // 1744
  char _padding_5[4];                 // 1748
  int32_t gamePhase;                  // 1752
  int32_t chapterState;               // 1756
  int32_t chapterNum;                 // 1760
  char _padding_6[4];                 // 1764
  MsvcReleaseModeXString chapterName; // 1768
  char _padding_7[12];                // 1800
  bool inCombat;                      // 1812
  bool isProductionSave;              // 1813
};
static_assert(offsetof(MewDirector, director) == 40, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, currentDay) == 1408, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, house) == 1448, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, partyCount) == 1468, "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, inCombat) == 1812, "MewDirector offset mismatch");

struct HouseCat : Component {
  char _padding_0[72];  // 56
  int64_t sql_key;      // 128
};

struct PersistentCharacter {
  char _padding_0[24];              // 0
  MsvcReleaseModeWString name;      // 24
  char _padding_1[72];              // 56
  int64_t sql_key;                  // 128
  char _padding_2[2896];            // 136
  int64_t parentID;                 // 3032
  char _padding_3[24];              // 3040
  int32_t state;                    // 3064
  char _padding_4[20];              // 3068
  MsvcReleaseModeXString className; // 3088
  int32_t level;                    // 3120
  char _padding_6[4];               // 3124
  int32_t birthDay;                 // 3128
  char _padding_7[12];              // 3132
  int64_t catID;                    // 3144
};
static_assert(offsetof(PersistentCharacter, name) == 24, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, sql_key) == 128, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, className) == 3088, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, level) == 3120, "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, catID) == 3144, "PersistentCharacter offset mismatch");

struct AbilityDefinition {
  char _padding_0[136];        // 0
  MsvcReleaseModeXString name; // 136
};

struct Ability {
  void *vtable;                         // 0
  AbilityDefinition *definition;        // 8
  Character *owner;                     // 16
  char _padding_1[8];                   // 24
};

struct FighterList {
  char _padding_0[12];  // 0
  uint32_t count;       // 12
  Character **data;     // 16
};

struct CombatStateBlock {
  char _padding_0[8080]; // 0
  FighterList *fighters; // 8080
};

struct CombatEntityManager {
  char _padding_0[32];          // 0
  CombatStateBlock *stateBlock; // 32
};

struct TacticsTile : Component {
  char _padding_0[16];  // 56
  int32_t x;            // 72
  int32_t y;            // 76
};

struct GridPositionComponent : Component {
  char _padding_0[64];      // 56
  TacticsTile *currentNode; // 120
  TacticsTile *targetNode;  // 128
};

struct CombatContext {
  char _padding_0[8];                 // 0
  CombatEntityManager *entityManager; // 8
};

struct CombatUIContext {
  char _padding_0[56];                 // 0
  CombatUISlotManager *entityManager;  // 56
  char _padding_1[640];                // 64
  int32_t pendingActionCount;          // 704
  char _padding_2[268];                // 708
  bool isInputDirty;                   // 976
  char _padding_3[2];                  // 977
  bool isDragging;                     // 979
};
static_assert(offsetof(CombatUIContext, entityManager) == 56, "BattleUIContext offset mismatch");
static_assert(offsetof(CombatUIContext, pendingActionCount) == 704, "BattleUIContext offset mismatch");
static_assert(offsetof(CombatUIContext, isInputDirty) == 976, "BattleUIContext offset mismatch");
static_assert(offsetof(CombatUIContext, isDragging) == 979, "BattleUIContext offset mismatch");

struct TurnControl {
  char _padding_0[24];    // 0
  CombatContext *context; // 24
  char _padding_1[312];   // 32
  int32_t turnCount;      // 344
};

struct CombatResolutionState {
  char _padding_0[53];  // 0
  bool defeat;          // 0x35
  char _padding_1[372]; // 0x36
  bool victory;         // 0x1AA
};
static_assert(offsetof(CombatResolutionState, defeat) == 0x35, "CombatResolutionState offset mismatch");
static_assert(offsetof(CombatResolutionState, victory) == 0x1AA, "CombatResolutionState offset mismatch");

struct Character {
  void *vtable;                         // 0
  char _padding_0[128];                 // 8
  PersistentCharacter *persistentChar;  // 136
  char _padding_1[56];                  // 144
  Ability *ability0;                    // 200 (It is only valid for the first ability cast then disappears)
  Ability *defaultMove;                 // 208
  Ability *basicAttack;                 // 216
  char _padding_1a[16];                 // 224
  Ability **spells;                     // 240
  char _padding_1b[408];                // 248 -- TODO: Check item actions later, they may be an array too
  MsvcReleaseModeWString name;          // 656
  char _padding_2[473];                 // 688
  uint8_t isPlayerCat;                  // 1161
  char _padding_3[38];                  // 1161
  int32_t currentHP;                    // 1200
  int32_t barrierHP;                    // 1204
  int32_t lives;                        // 1208
  int32_t maxHP;                        // 1212
  char _padding_4[2];                   // 1216
  bool isDead;                          // 1218
  char _padding_5[221];                 // 1219
  int32_t strength;                     // 1440
  int32_t dexterity;                    // 1444
  int32_t constitution;                 // 1448
  int32_t intelligence;                 // 1452
  int32_t speed;                        // 1456
  int32_t charisma;                     // 1460
  int32_t luck;                         // 1464
  char _padding_6[28];                  // 1468
  int32_t baseMovement;                 // 1496
  int32_t baseInitiative;               // 1500
  int32_t baseManaRegen;                // 1504
  int32_t baseHealthRegen;              // 1508
  char _padding_7[8];                   // 1512
  int32_t baseMana;                     // 1520
  int32_t baseInitialMana;              // 1524
  char _padding_8[44];                  // 1528
  int32_t baseCritChance;               // 1572
  char _padding_9[200];                 // 1576
  int32_t buffedStr;                    // 1776
  int32_t buffedDex;                    // 1780
  int32_t buffedCon;                    // 1784
  int32_t buffedInt;                    // 1788
  int32_t buffedSpd;                    // 1792
  char _padding_10[1494];               // 1796
  bool isStatic;                        // 3290
  bool isInanimate;                     // 3291
  char _padding_11[2];                  // 3292
  bool isChampion;                      // 3294
  bool isElite;                         // 3295
  char _padding_12[20];                 // 3598
  int32_t characterType;                // 3316
};
static_assert(offsetof(Character, name) == 656, "Character offset mismatch");
static_assert(offsetof(Character, currentHP) == 1200, "Character offset mismatch");
static_assert(offsetof(Character, maxHP) == 1212, "Character offset mismatch");
static_assert(offsetof(Character, strength) == 1440, "Character offset mismatch");
static_assert(offsetof(Character, buffedStr) == 1776, "Character offset mismatch");
static_assert(offsetof(Character, characterType) == 3316, "Character offset mismatch");

struct UIAbilitySlotVTable {
  void *unk0;
  void *unk1;
  uint64_t(__fastcall *IsCastable)(UIAbilitySlot *thiss, uint64_t param);
};

struct UIAbilitySlot {
  UIAbilitySlotVTable *vtable;
};

struct CombatUISlotManager {
  char _padding_0[208];           // 0
  UIAbilitySlot *primaryEntity;   // 208
  UIAbilitySlot *secondaryEntity; // 216
  UIAbilitySlot *tertiaryEntity;  // 224
  char _padding_1[4];             // 232
  uint32_t extraCount;            // 236
  UIAbilitySlot **extraArray;     // 240
};
static_assert(offsetof(CombatUISlotManager, primaryEntity) == 208, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, secondaryEntity) == 216, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, tertiaryEntity) == 224, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, extraCount) == 236, "BattleUISlotManager offset mismatch");
static_assert(offsetof(CombatUISlotManager, extraArray) == 240, "BattleUISlotManager offset mismatch");

struct TurnAction {
  int32_t type;       // 0
  int32_t _pad0;      // 4
  Ability *ability;   // 8
  int32_t targetX;    // 16
  int32_t targetY;    // 20
  int32_t target2X;   // 24
  int32_t target2Y;   // 28
  Character *actor;   // 32
  int32_t unk_28;     // 40
  int32_t unk_2C;     // 44
  uint8_t flag_30;    // 48
  uint8_t flag_31;    // 49
  uint8_t flag_32;    // 50
  uint8_t flag_33;    // 51
  uint8_t flag_34;    // 52
  uint8_t flag_35;    // 53
  uint8_t flag_36;    // 54
  uint8_t flag_37;    // 55
  int32_t _field_38;  // 56
  int32_t _field_3C;  // 60
  void *staticPtr40;  // 64
  void *staticPtr48;  // 72
  void *dynamicPtr50; // 80
  void *dynamicPtr58; // 88 (Unique heap token)
  void *staticPtr60;  // 96
  void *staticPtr68;  // 104
  void *callback;     // 112
  void *_field_78;    // 120
  int32_t _field_80;  // 128
  int32_t magic84;    // 132 ("AULT")
};
