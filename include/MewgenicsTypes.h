#pragma once
#include <stdint.h>
#include <string>
#include <string_view>
#include <windows.h>

// Definitions adapted from polymeric's mewgenics_analysis
// Source: https://github.com/p0lymeric/mewgenics_analysis
// Credit: polymeric 2026

// MSVC Vector (std::vector), laid out as compiled in Release mode
template <class _Value_type> struct MsvcReleaseModeVector {
  _Value_type *_Myfirst;
  _Value_type *_Mylast;
  _Value_type *_Myend;

  const _Value_type *begin() const { return _Myfirst; }
  const _Value_type *end() const { return _Mylast; }
  _Value_type *begin() { return _Myfirst; }
  _Value_type *end() { return _Mylast; }
  size_t size() const { return this->_Mylast - this->_Myfirst; }
};

// podvector
template <typename T> struct podvector {
  uint32_t capacity_;
  uint32_t size_;
  T *data_;

  T *begin() { return data_; }
  T *end() { return data_ + size_; }
  const T *begin() const { return data_; }
  const T *end() const { return data_ + size_; }
  uint32_t size() const { return size_; }
};

// MSVC XString (std::string), laid out as compiled in Release mode
struct MsvcReleaseModeXString {
  union {
    char _Buf[16];
    char *_Ptr;
  } _Bx;
  uint64_t _Mysize;
  uint64_t _Myres;

  const char *begin() const {
    if (this->_Myres < 16)
      return &this->_Bx._Buf[0];
    else
      return this->_Bx._Ptr;
  }

  const char *end() const {
    if (this->_Myres < 16)
      return &this->_Bx._Buf[this->_Mysize];
    else
      return this->_Bx._Ptr + this->_Mysize;
  }

  std::string copy_to_native_string() const {
    return std::string(this->begin(), this->end());
  }

  std::string_view as_native_string_view() const {
    return std::string_view(this->begin(), this->_Mysize);
  }
};

// MSVC XString (std::wstring), laid out as compiled in Release mode
struct MsvcReleaseModeWString {
  union {
    wchar_t _Buf[8];
    wchar_t *_Ptr;
  } _Bx;
  uint64_t _Mysize;
  uint64_t _Myres;

  const wchar_t *begin() const {
    if (this->_Myres < 8)
      return &this->_Bx._Buf[0];
    else
      return this->_Bx._Ptr;
  }

  const wchar_t *end() const {
    if (this->_Myres < 8)
      return &this->_Bx._Buf[this->_Mysize];
    else
      return this->_Bx._Ptr + this->_Mysize;
  }

  std::wstring copy_to_native_wstring() const {
    return std::wstring(this->begin(), this->end());
  }

  std::string to_utf8() const {
    std::wstring ws = copy_to_native_wstring();
    if (ws.empty())
      return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(),
                                          NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &ws[0], (int)ws.size(), &strTo[0],
                        size_needed, NULL, NULL);
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
struct CombatContext;
struct TurnControl;

template <typename T> struct ComponentVTable {
  MsvcReleaseModeXString *(__cdecl *GetObjectTypeSTR)(
      const T *thiss, MsvcReleaseModeXString *__return);
  int32_t(__cdecl *GetObjectType)(const T *thiss);
  bool(__cdecl *TypeInHierarchy)(const T *thiss, MsvcReleaseModeXString *type);
  void *_reserved1;
  void *_reserved2;
  void *_reserved3;
  // other vtable entries omitted for brevity
};

struct Component {
  const ComponentVTable<Component> *vtable;
  uint32_t _objid;
  uint8_t override_tags_B0;
  uint8_t override_tags_B1;
  bool entity_enabled;
  bool deleted;
  bool enabled;
  bool started;
  char _12[6];
  Entity *entity;
  Scene *scene;
  Director *director;
  double timescale;
};
static_assert(sizeof(Component) == 56, "Component size mismatch");

struct Scene {
  Director *director;                     // 0
  podvector<Entity *> Entities;           // 8
  podvector<Component *> *ComponentLists; // 24
  void *CachedActiveComponentLists;       // 32
  char _padding_to_1200[1200 - 40];       // 40
  bool doing_scene_destruction;           // 1200
  char _padding_to_name[7];               // 1201
  MsvcReleaseModeXString name;            // 1208
  char _padding_to_petlist[1468 - 1208 - sizeof(MsvcReleaseModeXString)];
  int32_t petListCount;             // 1468
  PersistentCharacter **petListPtr; // 1472
  char _padding_to_catlist2[1604 - 1472 - 8];
  int32_t catList2Count;             // 1604
  PersistentCharacter **catList2Ptr; // 1608
};
static_assert(offsetof(Scene, doing_scene_destruction) == 1200,
              "Scene offset mismatch");
static_assert(offsetof(Scene, name) == 1208, "Scene offset mismatch");
static_assert(offsetof(Scene, petListCount) == 1468, "Scene offset mismatch");
static_assert(offsetof(Scene, catList2Count) == 1604, "Scene offset mismatch");

struct Entity {
  EntityVTable *vtable;
  Scene *scene;
  double timescale;
  bool deleted;
  bool enabled;
  char _padding[6];
  podvector<Component *> components;
  podvector<void *> unknown_0;
  char _padding_1[128 - 64];
  int64_t catID; // 128
  bool isActive; // 136
  char _padding_2[232 - 136 - 1];
  void *container; // 232
};
static_assert(offsetof(Entity, catID) == 128, "Entity offset mismatch");
static_assert(offsetof(Entity, container) == 232, "Entity offset mismatch");

struct EntityVTable {
  void *(__cdecl *VDtor)(Entity *thiss, uint32_t flags);
};

struct House {
  char _padding_house_0[28];
  int32_t absoluteDay; // 28
  char _padding_house_1[60 - 28 - 4];
  int32_t furnitureCount; // 60
  void *furniturePtr;     // 64
  char _padding_house_2[176 - 64 - 8];
  int32_t food;             // 176
  int32_t gold;             // 180
  int32_t blankCollars;     // 184
  int32_t storageExpansion; // 188
};
static_assert(offsetof(House, food) == 176, "House offset mismatch");
static_assert(offsetof(House, absoluteDay) == 28, "House offset mismatch");

struct Director {
  MsvcReleaseModeVector<Scene *> scenes;
};

struct MewDirector {
  char _padding_md_0[40];
  Director *director; // 40
  char _padding_md_1[1408 - 40 - 8];
  int32_t currentDay; // 1408
  char _padding_md_2[1440 - 1408 - 4];
  void *inventoryPtr;  // 1440
  struct House *house; // 1448
  char _padding_md_3[1464 - 1448 - 8];
  int32_t partyCapacity;           // 1464
  int32_t partyCount;              // 1468
  PersistentCharacter **partyData; // 1472
  char _padding_md_4[1744 - 1472 - 8];
  int32_t eventDifficulty; // 1744
  char _padding_md_5[1752 - 1744 - 4];
  int32_t gamePhase;    // 1752
  int32_t chapterState; // 1756
  int32_t chapterNum;   // 1760
  char _padding_md_6[1768 - 1760 - 4];
  MsvcReleaseModeXString chapterName; // 1768
  char _padding_md_7[1812 - 1768 - sizeof(MsvcReleaseModeXString)];
  bool inCombat;         // 1812
  bool isProductionSave; // 1813
};
static_assert(offsetof(MewDirector, director) == 40,
              "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, currentDay) == 1408,
              "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, house) == 1448,
              "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, partyCount) == 1468,
              "MewDirector offset mismatch");
static_assert(offsetof(MewDirector, inCombat) == 1812,
              "MewDirector offset mismatch");

struct HouseCat : Component {
  char _padding_to_sql_key[128 - sizeof(Component)];
  int64_t sql_key;
};
static_assert(offsetof(HouseCat, sql_key) == 128, "HouseCat offset mismatch");

struct PersistentCharacter {
  char _padding_0[24];
  MsvcReleaseModeWString name; // 24
  char _padding_1[128 - 24 - sizeof(MsvcReleaseModeXString)];
  int64_t sql_key; // 128
  char _padding_2[3032 - 128 - 8];
  int64_t parentID; // 3032
  char _padding_3[3064 - 3032 - 8];
  int32_t state; // 3064
  char _padding_4[3088 - 3064 - 4];
  MsvcReleaseModeXString className; // 3088
  int32_t level;                    // 3120
  char _padding_6[3128 - 3120 - 4];
  int32_t birthDay; // 3128
  char _padding_7[3144 - 3128 - 4];
  int64_t catID; // 3144
};
static_assert(offsetof(PersistentCharacter, name) == 24,
              "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, sql_key) == 128,
              "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, className) == 3088,
              "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, level) == 3120,
              "PersistentCharacter offset mismatch");
static_assert(offsetof(PersistentCharacter, catID) == 3144,
              "PersistentCharacter offset mismatch");

struct AbilityDefinition {
  char _padding_0[136];
  MsvcReleaseModeXString name; // 136
};

struct Ability {
  char _padding_0[16];
  struct Character *owner; // 16
  char _padding_1[40 - 16 - 8];
  AbilityDefinition *definition; // 40
};

struct FighterList {
  char _padding_0[12];
  uint32_t count;          // 12
  struct Character **data; // 16
};

struct CombatStateBlock {
  char _padding_0[8048];
  FighterList *fighters; // 8048
};

struct CombatEntityManager {
  char _padding_0[32];
  CombatStateBlock *stateBlock; // 32
};

struct CombatContext {
  char _padding_0[8];
  CombatEntityManager *entityManager; // 8
};

struct TurnControl {
  char _padding_0[24];
  CombatContext *context; // 24
  char _padding_1[344 - 24 - 8];
  int32_t turnCount; // 344
};

struct Character {
  void *vtable; // 0
  char _padding_0[136 - 8];
  PersistentCharacter *persistentChar; // 136
  char _padding_1[656 - 136 - 8];
  MsvcReleaseModeWString name; // 656
  char _padding_2[1161 - 656 - sizeof(MsvcReleaseModeXString)];
  uint8_t isPlayerCat; // 1161
  char _padding_3[1200 - 1161 - 1];
  int32_t currentHP; // 1200
  int32_t barrierHP; // 1204
  int32_t lives;     // 1208
  int32_t maxHP;     // 1212
  char _padding_4[1218 - 1212 - 4];
  bool isDead; // 1218
  char _padding_5[1440 - 1218 - 1];
  int32_t strength;     // 1440
  int32_t dexterity;    // 1444
  int32_t constitution; // 1448
  int32_t intelligence; // 1452
  int32_t speed;        // 1456
  int32_t charisma;     // 1460
  int32_t luck;         // 1464
  char _padding_6[1496 - 1464 - 4];
  int32_t baseMovement;    // 1496
  int32_t baseInitiative;  // 1500
  int32_t baseManaRegen;   // 1504
  int32_t baseHealthRegen; // 1508
  char _padding_7[1520 - 1508 - 4];
  int32_t baseMana;        // 1520
  int32_t baseInitialMana; // 1524
  char _padding_8[1572 - 1524 - 4];
  int32_t baseCritChance; // 1572
  char _padding_9[1776 - 1572 - 4];
  int32_t buffedStr; // 1776
  int32_t buffedDex; // 1780
  int32_t buffedCon; // 1784
  int32_t buffedInt; // 1788
  int32_t buffedSpd; // 1792
  char _padding_10[3290 - 1792 - 4];
  bool isStatic;               // 3290
  bool isSpeculativeInanimate; // 3291
  char _padding_11[3294 - 3291 - 1];
  bool isChampion; // 3294
  bool isElite;    // 3295
  char _padding_12[3316 - 3295 - 1];
  int32_t characterType; // 3316
};
static_assert(offsetof(Character, name) == 656, "Character offset mismatch");
static_assert(offsetof(Character, currentHP) == 1200,
              "Character offset mismatch");
static_assert(offsetof(Character, maxHP) == 1212, "Character offset mismatch");
static_assert(offsetof(Character, strength) == 1440,
              "Character offset mismatch");
static_assert(offsetof(Character, buffedStr) == 1776,
              "Character offset mismatch");
static_assert(offsetof(Character, characterType) == 3316,
              "Character offset mismatch");

struct TurnAction {
  int32_t type; // 0
  char _padding_0[4];
  Character *character; // 8
  int32_t targetX;      // 16
  int32_t targetY;      // 20
  int32_t target2X;     // 24
  int32_t target2Y;     // 28
  char _padding_1[128 - 32];
  void (*callback)(void *context, bool arg2); // 128
  void *callback_context;                     // 136
};
static_assert(sizeof(TurnAction) == 144, "TurnAction size mismatch");
