#pragma once
#include <stdint.h>
#include <string>
#include <string_view>

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

struct Entity;
struct Scene;
struct Director;
struct EntityVTable;

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
  Director *director;                     // 0   (8 bytes)
  podvector<Entity *> Entities;           // 8   (16 bytes, was 24)
  podvector<Component *> *ComponentLists; // 24  (8 byte pointer)
  void *CachedActiveComponentLists;       // 32  (8 bytes)
  char _padding_to_1200[1200 - 40];       // 40  -> 1200
  bool doing_scene_destruction;           // 1200
  char _padding_to_name[7];               // 1201
  MsvcReleaseModeXString name;            // 1208
};
static_assert(offsetof(Scene, doing_scene_destruction) == 1200,
              "Scene offset mismatch");
static_assert(offsetof(Scene, name) == 1208, "Scene offset mismatch");

struct Entity {
  EntityVTable *vtable;
  Scene *scene;
  double timescale;
  bool deleted;
  bool enabled;
  char _padding[6];
  podvector<Component *> components;
  podvector<void *> unknown_0;
};
static_assert(sizeof(Entity) == 0x40, "Entity size mismatch");

struct EntityVTable {
  void *(__cdecl *VDtor)(Entity *thiss, uint32_t flags);
};

struct Director {
  MsvcReleaseModeVector<Scene *> scenes;
};

struct MewDirector {
  char _padding[40];
  Director *director;
};
static_assert(offsetof(MewDirector, director) == 40,
              "MewDirector offset mismatch");

struct HouseCat : Component {
  char _padding_to_sql_key[128 - sizeof(Component)];
  int64_t sql_key;
};
static_assert(offsetof(HouseCat, sql_key) == 128, "HouseCat offset mismatch");

struct House : Component {};
