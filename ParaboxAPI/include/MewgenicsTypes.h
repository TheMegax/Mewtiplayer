#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <windows.h>

// Standard Library / Engine Containers

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

template <typename T> struct flatset {
  podvector<T> sorted_;
  podvector<T> back_;
  podvector<T> unsorted_;
  podvector<T> append_;
  bool needs_flatten;
  uint8_t _pad[7];
};

template <typename T> struct Ref {
  T *obj;
  uint64_t generation;
};

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

  [[nodiscard]] std::wstring_view as_native_wstring_view() const {
    if (!this->is_valid())
      return L"";
    return std::wstring_view(this->begin(), this->Mysize);
  }

  [[nodiscard]] std::string to_utf8() const {
    if (!this->is_valid() || this->Mysize == 0)
      return "";
    const int len = WideCharToMultiByte(CP_UTF8, 0, this->begin(), static_cast<int>(this->Mysize), nullptr, 0, nullptr, nullptr);
    if (len <= 0)
      return "";
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, this->begin(), static_cast<int>(this->Mysize), &result[0], len, nullptr, nullptr);
    return result;
  }
};

struct MsvcReleaseModeStdFunction {
  char _storage[64];
};

// Math And Geometry Types

#ifndef IVEC2D_DEFINED
#define IVEC2D_DEFINED
struct iVec2D {
  int32_t x;
  int32_t y;
};
#endif

struct Vec2D {
  double x;
  double y;
};

struct Vec3D {
  double x;
  double y;
  double z;
};

struct Matrix3x2 {
  float scalex;
  float scaley;
  float rotateskewx;
  float rotateskewy;
  float translatex;
  float translatey;
};

struct ColorTransform {
  float mul_r;
  float mul_g;
  float mul_b;
  float mul_a;
  float add_r;
  float add_g;
  float add_b;
  float add_a;
};

struct ElementList {
  uint64_t flags;
};

// Enums

enum class TurnKind : uint32_t {
  MAIN = 0,
  DISPERSED_BONUS = 1,
  STACKED_BONUS = 2,
  ROUND_START_BONUS = 3,
  ROUND_END_BONUS = 4,
  ABILITY = 5,
  FAKE_TURN = 6,
  DUMMY = 7,
  ROUND_UPKEEP = 8,
  NONE = 9
};

enum class CharacterType : uint32_t {
  NORMAL = 0,
  SMALL = 1,
  BOSS = 2,
  CAT = 3,
  OBJECT = 4
};

enum class MapNodeType : uint32_t {
  NONE = 0,
  NPC = 1,
  ENTER = 2,
  EXIT = 3,
  HOME = 4,
  BATTLE = 5,
  HARD = 6,
  MINIBOSS = 7,
  BOSS = 8,
  EVENT = 9,
  OPTIONAL_EVENT = 10,
  SPECIAL_EVENT = 11,
  SHOP = 12,
  TREASURE = 13,
  TREASURE_FURNITURE = 14,
  TREASURE_FOOD = 15,
  BONUS = 16,
  EMPTY = 17,
  ANY = 18,
  NUM_TYPES = 19
};

enum class Area : uint32_t {
  DEBUGAREA = 0,
  TUTORIAL = 1,
  ALLEY = 2,
  SEWERS = 3,
  JUNKYARD = 4,
  CAVES = 5,
  BONEYARD = 6,
  MEATWORLD = 7,
  DESERT = 8,
  CRATER = 9,
  BUNKER = 10,
  MOON = 11,
  CORE = 12,
  DIMENSIONX = 13,
  LAB = 14,
  ICEAGE = 15,
  FUTURE = 16,
  JURASSIC = 17,
  THEEND = 18,
  THEINFINITE = 19
};

enum class Gender : uint32_t {
  MALE = 0,
  FEMALE = 1,
  NEUTRAL = 2,
  NONE = 3
};

enum class LifeStage : uint32_t {
  YOUNG = 0,
  AGING = 1,
  OLD = 2
};

enum class Condition : uint32_t {
  BRAND_NEW = 0,
  GOOD = 1,
  REPAIRED = 2,
  WORN = 3,
  VERYWORN = 4,
  BROKEN = 5
};

enum class SaveScumLocation : uint32_t {
  NONE = 0,
  BATTLE = 1,
  HOUSEBOSS = 2,
  EVENT = 3,
  OTHER = 4
};

enum class Animate : uint32_t {
  DEFAULT = 0,
  ANIMATE = 1,
  IMMEDIATE = 2
};

enum class ActionKind : uint32_t {
  NONE = 0,
  WAITING = 1,
  ABILITY = 2,
  END_TURN = 3,
  END_TURN_MANUAL = 4,
  RUN_AWAY = 5,
  ROUND_UPKEEP = 6,
  CUSTOM = 7
};

// Forward Declarations

struct Scene;
struct Director;
struct Entity;
struct Component;
struct DamageNumber;
struct Character;
struct Ability;
struct TacticsObject;
struct TacticsGrid;
struct TacticsTile;
struct Passive;
struct GonObject;
struct CatData;
struct CatParts;
struct TurnControl;
struct Level;
struct Brain;
struct PlayerBrain;
struct ButchBox;
struct HouseCat;
struct MapNode;
struct MapScreen;
struct MapMarker;
struct MewDirector;
struct HouseInventory;
struct GlobalProgressionData;
struct CatSelector;
struct Transform;
struct CatPlacementArea;
struct CombatMenu;

struct SQLSaveFile;
struct LevelUpOption;
struct LevelUpScreen;
struct WorldEventOption;
struct WorldEvent;
struct WorldEventClickEvent;
struct WorldEventCatButton;
struct AbilityChooser;
struct ShopItem;
struct Shop;
struct InventoryItemBox;
struct InventoryScreen2;
struct ClassTagBox;
struct ClassChooser;

// Core Engine Hierarchy
struct ComponentVTable {
  void *(__cdecl *GetObjectTypeSTR)(Component *thiss, MsvcReleaseModeXString *out_name);
  int32_t (__cdecl *GetObjectType)(Component *thiss);
  bool (__cdecl *TypeInHierarchy)(Component *thiss, MsvcReleaseModeXString *type);
  void *(__cdecl *GetObjectHierarchy)(Component *thiss);
  void *unk4;
  void *(__cdecl *VDtor)(Component *thiss, uint32_t flags);
  void *unk6;
  void *unk7;
  void *unk8;
  void *unk9;
  void *unk10;
  void *unk11;
  void *unk12;
  void *unk13;
  void *unk14;
  void *unk15;
  void *unk16;
  void *unk17;
  void *unk18;
  void *unk19;
  void *unk20;
  void *unk21;
  void *unk22;
  void *unk23;
  void *unk24;
  void *unk25;
  void *unk26;
  void *unk27;
  void *unk28;
  void *unk29;
  void *unk30;
  void *unk31;
  void *unk32;
  void *unk33;
  void *unk34;
  void *unk35;
  void *unk36;
  void *unk37;
  void *unk38;
  void *unk39;
  void *unk40;
  void *unk41;
  void *unk42;
  void *unk43;
  void *unk44;
  void *unk45;
  void *unk46;
  void *unk47;
  void *unk48;
  void *unk49;
  void *unk50;
  void *unk51;
  void *unk52;
  void *unk53;
  void *unk54;
  void *unk55;
  void *unk56;
  void *unk57;
  void *unk58;
  void *unk59;
  void *unk60;
  void *unk61;
  void *unk62;
  void *unk63;
  void *unk64;
  void *unk65;
  void *unk66;
  void *unk67;
  void *unk68;
  void *unk69;
  void *unk70;
  void *unk71;
  void *unk72;
  void *unk73;
  void *unk74;
  void *unk75;
  void *unk76;
  void *unk77;
  void *unk78;
  void *unk79;
  void *unk80;
  void *unk81;
  void *unk82;
  void *unk83;
  void *unk84;
  void *unk85;
  void *unk86;
  void *unk87;
  void *unk88;
  void *unk89;
  void *unk90;
  void *unk91;
  void *unk92;
  void *unk93;
  void *unk94;
  void *unk95;
  void *unk96;
  void *unk97;
  void *unk98;
  void *unk99;
  void *unk100;
  void *unk101;
  void *unk102;
  void *unk103;
  void *unk104;
  void *unk105;
  void *unk106;
  void *unk107;
  void *unk108;
  void *unk109;
  void *unk110;
  void *unk111;
  void *unk112;
  void *unk113;
  void *unk114;
  void *unk115;
  void *unk116;
  void *unk117;
  void *unk118;
  void *unk119;
  void *unk120;
  void *unk121;
  void *unk122;
  void *unk123;
  void *unk124;
  void *unk125;
  void *unk126;
  void *unk127;
  void *unk128;
  void *unk129;
  void *unk130;
  void *unk131;
  void *unk132;
  void *unk133;
  void *unk134;
  void *unk135;
  void *unk136;
  void *unk137;
  void *unk138;
  void *unk139;
  void *unk140;
  void *unk141;
  void *unk142;
  void *unk143;
  void *unk144;
  void *unk145;
  void *unk146;
  void *unk147;
  void *unk148;
  void *unk149;
  void *unk150;
  void *unk151;
  void *unk152;
  void *unk153;
  void *unk154;
  void *unk155;
  void *unk156;
  void *unk157;
  void *unk158;
  void *unk159;
  void *unk160;
  void *unk161;
  void *unk162;
  void *unk163;
  void *unk164;
  void *unk165;
  void *unk166;
  void *unk167;
  void *unk168;
  void *unk169;
  void *unk170;
  void *unk171;
  void *unk172;
  void *unk173;
  void *unk174;
  void *unk175;
  void *unk176;
  void *unk177;
  void *unk178;
  void *unk179;
  void *unk180;
  void *unk181;
  void *unk182;
  void *unk183;
  void *unk184;
  void *unk185;
  void *unk186;
  void *unk187;
  void *unk188;
  void *unk189;
  void *unk190;
  void *unk191;
  void *unk192;
  void *unk193;
  void *unk194;
  void *unk195;
  void *unk196;
  void *unk197;
  void *unk198;
  void *unk199;
  void *unk200;
  void *unk201;
  void *unk202;
  void *unk203;
  void *unk204;
  void *unk205;
  void *unk206;
  void *unk207;
  void *unk208;
  void *unk209;
  void *unk210;
  void *unk211;
  void *unk212;
  void *unk213;
  void *unk214;
  void *unk215;
  void *unk216;
  void *unk217;
  void *unk218;
  void *unk219;
  void *unk220;
  void *unk221;
  void *unk222;
  void *unk223;
  void *unk224;
  void *unk225;
  void *unk226;
  void *unk227;
  void *unk228;
  void *unk229;
  void *unk230;
  void *unk231;
  void *unk232;
  void *unk233;
  void *unk234;
  void *unk235;
  void *unk236;
  void *unk237;
  void *unk238;
  void *unk239;
  void *unk240;
  void *unk241;
  void *unk242;
  void *unk243;
  void *unk244;
  void *unk245;
  void *unk246;
  void *unk247;
  void *unk248;
  void *unk249;
  void *unk250;
  void *unk251;
  void *unk252;
  void *unk253;
  void *unk254;
  void *unk255;
  void *unk256;
  void *unk257;
  void *unk258;
  void *unk259;
  void *unk260;
  void *unk261;
  void *unk262;
  void *unk263;
  void *unk264;
  void *unk265;
  void *unk266;
  void *unk267;
  void *unk268;
  void *unk269;
  void *unk270;
  void *unk271;
  void *unk272;
  void *unk273;
  void *unk274;
  void *unk275;
  void *unk276;
  void *unk277;
  void *unk278;
  void *unk279;
  void *unk280;
  void *unk281;
  void *unk282;
  void *unk283;
  void *unk284;
  void *unk285;
  void *unk286;
  void *unk287;
  void *unk288;
  void *unk289;
  void *unk290;
  void *unk291;
  void *unk292;
  void *unk293;
  void *unk294;
  void *unk295;
  void *unk296;
  void *unk297;
  void *unk298;
  void *unk299;
  void *unk300;
  void *unk301;
  void *unk302;
  void *unk303;
  void *unk304;
  void *unk305;
  void *unk306;
  void *unk307;
  void *unk308;
  void *unk309;
  void *unk310;
  void *unk311;
  void *unk312;
  void *unk313;
  void *unk314;
  void *unk315;
  void *unk316;
  void *unk317;
  void *unk318;
  void *unk319;
  void *unk320;
  void *unk321;
  void *unk322;
  void *unk323;
  void *unk324;
  void *unk325;
  void *unk326;
  void *unk327;
  void *unk328;
  void *unk329;
  void *unk330;
  void *unk331;
  void *unk332;
  void *unk333;
  void *unk334;
  void *unk335;
  void *unk336;
  void *unk337;
  void *unk338;
  void *unk339;
  void *unk340;
  void *unk341;
  void *unk342;
  void *unk343;
  void *unk344;
  void *unk345;
  void *unk346;
  void *unk347;
  void *unk348;
  void *unk349;
  void *unk350;
  void *unk351;
  void *unk352;
  void *unk353;
  void *unk354;
  void *unk355;
  void *unk356;
  void *unk357;
  void *unk358;
  void *unk359;
  void *unk360;
  void *unk361;
  void *unk362;
  void *unk363;
  void *unk364;
  void *unk365;
  void *unk366;
  void *unk367;
  void *unk368;
  void *unk369;
  void *unk370;
  void *unk371;
  void *unk372;
  void *unk373;
  void *unk374;
  void *unk375;
  void *unk376;
  void *unk377;
  void *unk378;
  void *unk379;
  void *unk380;
  void *unk381;
  void *unk382;
  void *unk383;
  void *unk384;
  void *unk385;
  void *unk386;
  void *unk387;
  void *unk388;
  void *unk389;
  void *unk390;
  void *unk391;
  void *unk392;
  void *unk393;
  void *unk394;
  void *unk395;
  void *unk396;
  void *unk397;
  void *unk398;
  void *unk399;
  void *unk400;
  void *unk401;
  void *unk402;
  void *unk403;
  void *unk404;
  void *unk405;
  void *unk406;
  void *unk407;
  void *unk408;
  void *unk409;
  void *unk410;
  void *unk411;
  void *unk412;
  void *unk413;
  void *unk414;
  void *unk415;
  void *unk416;
  void *unk417;
  void *unk418;
  void *unk419;
  void *unk420;
  void *unk421;
  void *unk422;
  void *unk423;
  void *unk424;
  void *unk425;
  void *unk426;
  void *unk427;
  void *unk428;
  void *unk429;
  void *unk430;
  void *unk431;
  void *unk432;
  void *unk433;
  void *unk434;
  void *unk435;
  void *unk436;
  void *unk437;
  void *unk438;
  void *unk439;
  void *unk440;
  void *unk441;
  void *unk442;
  void *unk443;
  void *unk444;
  void *unk445;
  void *unk446;
  void *unk447;
  void *unk448;
  void *unk449;
  void *unk450;
  void *unk451;
  void *unk452;
  void *unk453;
  void *unk454;
  void *unk455;
  void *unk456;
  void *unk457;
  void *unk458;
  void *unk459;
  void *unk460;
  void *unk461;
  void *unk462;
  void *unk463;
  void *unk464;
  void *unk465;
  void *unk466;
  void *unk467;
  void *unk468;
  void *unk469;
  void *unk470;
  void *unk471;
  void *unk472;
  void *unk473;
  void *unk474;
  void *unk475;
  void *unk476;
  void *unk477;
  void *unk478;
  void *unk479;
  void *unk480;
  void *unk481;
  void *unk482;
  void *unk483;
  void *unk484;
  void *unk485;
  void *unk486;
  void *unk487;
  void *unk488;
  void *unk489;
  void *unk490;
  void *unk491;
  void *unk492;
  void *unk493;
  void *unk494;
  void *unk495;
  void *unk496;
  void *unk497;
  void *unk498;
  void *unk499;
  void *unk500;
  void *unk501;
  void *unk502;
  void *unk503;
  void *unk504;
  void *unk505;
  void *unk506;
  void *unk507;
  void *unk508;
  void *unk509;
  void *unk510;
  void *unk511;
  void *unk512;
  void *unk513;
  void *unk514;
  void *unk515;
  void *unk516;
  void *unk517;
  void *unk518;
  void *unk519;
  void *unk520;
  void *unk521;
  void *unk522;
  void *unk523;
  void *unk524;
  void *unk525;
  void *unk526;
  void *unk527;
  void *unk528;
  void *unk529;
  void *unk530;
  void *unk531;
  void *unk532;
  void *unk533;
  void *unk534;
  void *unk535;
  void *unk536;
  void *unk537;
  void *unk538;
  void *unk539;
  void *unk540;
  void *unk541;
  void *unk542;
  void *unk543;
  void *unk544;
  void *unk545;
  void *unk546;
  void *unk547;
  void *unk548;
  void *unk549;
  void *unk550;
  void *unk551;
  void *unk552;
  void *unk553;
  void *unk554;
  void *unk555;
  void *unk556;
  void *unk557;
  void *unk558;
  void *unk559;
  void *unk560;
  void *unk561;
  void *unk562;
  void *unk563;
  void *unk564;
  void *unk565;
  void *unk566;
  void *unk567;
  void *unk568;
  void *unk569;
  void *unk570;
  void *unk571;
  void *unk572;
  void *unk573;
  void *unk574;
  void *unk575;
  void *unk576;
  void *unk577;
  void *unk578;
  void *unk579;
  void *unk580;
  void *unk581;
  void *unk582;
  void *unk583;
  void *unk584;
  void *unk585;
  void *unk586;
  void *unk587;
  void *unk588;
  void *unk589;
  void *unk590;
  void *unk591;
  void *unk592;
  void *unk593;
  void *unk594;
  void *unk595;
  void *unk596;
  void *unk597;
  void *unk598;
  void *unk599;
  void *unk600;
  void *unk601;
  void *unk602;
  void *unk603;
  void *unk604;
  void *unk605;
  void *unk606;
  void *unk607;
  void *unk608;
  void *unk609;
  void *unk610;
  void *unk611;
  void *unk612;
  void *unk613;
  void *unk614;
  void *unk615;
  void *unk616;
  void *unk617;
  void *unk618;
  void *unk619;
  void *unk620;
  void *unk621;
  void *unk622;
  void *unk623;
  void *unk624;
  void *unk625;
  void *unk626;
  void *unk627;
  void *unk628;
  void *unk629;
  void *unk630;
  void *unk631;
  void *unk632;
  void *unk633;
  void *unk634;
  void *unk635;
  void *unk636;
  void *unk637;
  void *unk638;
  void *unk639;
  void *unk640;
  void *unk641;
  void *unk642;
  void *unk643;
  void *unk644;
  void *unk645;
  void *unk646;
  void *unk647;
  void *unk648;
  void *unk649;
  void *unk650;
  void *unk651;
  void *unk652;
  void *unk653;
  void *unk654;
  void *unk655;
  void *unk656;
  void *unk657;
  void *unk658;
  void *unk659;
  void *unk660;
  void *unk661;
  void *unk662;
  void *unk663;
  void *unk664;
  void *unk665;
  void *unk666;
  void *unk667;
  void *unk668;
  void *unk669;
  void *unk670;
  void *unk671;
  void *unk672;
  void *unk673;
  void *unk674;
  void *unk675;
  void *unk676;
  void *unk677;
  void *unk678;
  void *unk679;
  void *unk680;
  void *unk681;
  void *unk682;
  void *unk683;
  void *unk684;
  void *unk685;
  void *unk686;
  void *unk687;
  void *unk688;
  void *unk689;
  void *unk690;
  void *unk691;
  void *unk692;
  void *unk693;
  void *unk694;
  void *unk695;
  void *unk696;
  void *unk697;
  void *unk698;
  void *unk699;
  void *unk700;
  void *unk701;
  void *unk702;
  void *unk703;
  void *unk704;
  void *unk705;
  void *unk706;
  void *unk707;
  void *unk708;
  void *unk709;
  void *unk710;
  void *unk711;
  void *unk712;
  void *unk713;
  void *unk714;
  void *unk715;
  void *unk716;
  void *unk717;
  void *unk718;
  void *unk719;
  void *unk720;
  void *unk721;
  void *unk722;
  void *unk723;
  void *unk724;
  void *unk725;
  void *unk726;
  void *unk727;
  void *unk728;
  void *unk729;
  void *unk730;
  void *unk731;
  void *unk732;
  void *unk733;
  void *unk734;
  void *unk735;
  void *unk736;
  void *unk737;
  void *unk738;
  void *unk739;
  void *unk740;
  void *unk741;
  void *unk742;
  void *unk743;
  void *unk744;
  void *unk745;
  void *unk746;
  void *unk747;
  void *unk748;
  void *unk749;
  void *unk750;
  void *unk751;
  void *unk752;
  void *unk753;
  void *unk754;
  void *unk755;
  void *unk756;
  void *unk757;
  void *unk758;
  void *unk759;
  void *unk760;
  void *unk761;
  void *unk762;
  void *unk763;
  void *unk764;
  void *unk765;
  void *unk766;
  void *unk767;
  void *unk768;
  void *unk769;
  void *unk770;
  void *unk771;
  void *unk772;
  void *unk773;
  void *unk774;
  void *unk775;
  void *unk776;
  void *unk777;
  void *unk778;
  void *unk779;
  void *unk780;
  void *unk781;
  void *unk782;
  void *unk783;
  void *unk784;
  void *unk785;
  void *unk786;
  void *unk787;
  void *unk788;
  void *unk789;
  void *unk790;
  void *unk791;
  void *unk792;
  void *unk793;
  void *unk794;
  void *unk795;
  void *unk796;
  void *unk797;
  void *unk798;
  void *unk799;
  void *unk800;
  void *unk801;
  void *unk802;
  void *unk803;
  void *unk804;
  void *unk805;
  void *unk806;
  void *unk807;
  void *unk808;
  void *unk809;
  void *unk810;
  void *unk811;
  void *unk812;
  void *unk813;
  void *unk814;
  void *unk815;
  void *unk816;
  void *unk817;
  void *unk818;
  void *unk819;
  void *unk820;
  void *unk821;
  void *unk822;
  void *unk823;
  void *unk824;
  void *unk825;
  void *unk826;
  void *unk827;
  void *unk828;
  void *unk829;
  void *unk830;
  void *unk831;
  void *unk832;
  void *unk833;
  void *unk834;
  void *unk835;
  void *unk836;
  void *unk837;
  void *unk838;
  void *unk839;
  void *unk840;
  void *unk841;
  void *unk842;
  void *unk843;
  void *unk844;
  void *unk845;
  void *unk846;
  void *unk847;
  void *unk848;
  void *unk849;
  void *unk850;
  void *unk851;
  void *unk852;
  void *unk853;
  void *unk854;
  void *unk855;
  void *unk856;
  void *unk857;
  void *unk858;
  void *unk859;
  void *unk860;
  void *unk861;
  void *unk862;
  void *unk863;
  void *unk864;
  void *unk865;
  void *unk866;
  void *unk867;
  void *unk868;
  void *unk869;
  void *unk870;
  void *unk871;
  void *unk872;
  void *unk873;
  void *unk874;
  void *unk875;
  void *unk876;
  void *unk877;
  void *unk878;
  void *unk879;
  void *unk880;
  void *unk881;
  void *unk882;
  void *unk883;
  void *unk884;
  void *unk885;
  void *unk886;
  void *unk887;
  void *unk888;
  void *unk889;
  void *unk890;
  void *unk891;
  void *unk892;
  void *unk893;
  void *unk894;
  void *unk895;
  void *unk896;
  void *unk897;
  void *unk898;
  void *unk899;
  void *unk900;
  void *unk901;
  void *unk902;
  void *unk903;
  void *unk904;
  void *unk905;
  void *unk906;
  void *unk907;
  void *unk908;
  void *unk909;
  void *unk910;
  void *unk911;
  void *unk912;
  void *unk913;
  void *unk914;
  void *unk915;
  void *unk916;
  void *unk917;
  void *unk918;
  void *unk919;
  void *unk920;
  void *unk921;
  void *unk922;
  void *unk923;
  void *unk924;
  void *unk925;
  void *unk926;
  void *unk927;
  void *unk928;
  void *unk929;
  void *unk930;
  void *unk931;
  void *unk932;
  void *unk933;
  void *unk934;
  void *unk935;
  void *unk936;
  void *unk937;
  void *unk938;
  void *unk939;
  void *unk940;
  void *unk941;
  void *unk942;
  void *unk943;
  void *unk944;
  void *unk945;
  void *unk946;
  void *unk947;
  void *unk948;
  void *unk949;
  void *unk950;
  void *unk951;
  void *unk952;
  void *unk953;
  void *unk954;
  void *unk955;
  void *unk956;
  void *unk957;
  void *unk958;
  void *unk959;
  void *unk960;
  void *unk961;
  void *unk962;
  void *unk963;
  void *unk964;
  void *unk965;
  void *unk966;
  void *unk967;
  void *unk968;
  void *unk969;
  void *unk970;
  void *unk971;
  void *unk972;
  void *unk973;
  void *unk974;
  void *unk975;
  void *unk976;
  void *unk977;
  void *unk978;
  void *unk979;
  void *unk980;
  void *unk981;
  void *unk982;
  void *unk983;
  void *unk984;
  void *unk985;
  void *unk986;
  void *unk987;
  void *unk988;
  void *unk989;
  void *unk990;
  void *unk991;
  void *unk992;
  void *unk993;
  void *unk994;
  void *unk995;
  void *unk996;
  void *unk997;
  void *unk998;
  void *unk999;
  void *unk1000;
  void *unk1001;
  void *unk1002;
  void *unk1003;
  void *unk1004;
  void *unk1005;
  void *unk1006;
  void *unk1007;
  void *unk1008;
  void *unk1009;
  void *unk1010;
  void *unk1011;
  void *unk1012;
  void *unk1013;
  void *unk1014;
  void *unk1015;
  void *unk1016;
  void *unk1017;
  void *unk1018;
  void *unk1019;
  void *unk1020;
  void *unk1021;
  void *unk1022;
  void *unk1023;
  void *unk1024;
  void *unk1025;
};

struct Component {
  ComponentVTable *vtable;                               // 0x00
  uint32_t _objid;                                       // 0x08
  uint8_t update_override_flags;                         // 0x0C
  uint8_t render_override_flags;                         // 0x0D
  bool entity_enabled;                                   // 0x0E
  bool deleted;                                          // 0x0F
  bool enabled;                                          // 0x10
  bool started;                                          // 0x11
  uint8_t _pad12[6];                                     // 0x12
  Entity *entity;                                        // 0x18
  Scene *scene;                                          // 0x20
  Director *director;                                    // 0x28
  double timescale;                                      // 0x30
};
static_assert(sizeof(Component) == 0x38, "Component size mismatch");

struct EntityVTable {
  void *(__cdecl *VDtor)(Entity *thiss, uint32_t flags);
};

struct Entity {
  EntityVTable *vtable;                                  // 0x00
  Scene *scene;                                          // 0x08
  double timescale;                                      // 0x10
  bool deleted;                                          // 0x18
  bool enabled;                                          // 0x19
  uint8_t _pad1A[6];                                     // 0x1A
  podvector<Component *> components;                     // 0x20
  podvector<Component *> hibernated_components;          // 0x30
};
static_assert(sizeof(Entity) == 0x40, "Entity size mismatch");

struct cached_pointer_vector_block {
  int32_t refcount;                                      // 0x00
  uint8_t _pad04[4];                                     // 0x04
  podvector<Component *> list;                           // 0x08
};
static_assert(sizeof(cached_pointer_vector_block) == 0x18, "cached_pointer_vector_block size mismatch");

template <typename T>
struct CachedPointerVector {
  cached_pointer_vector_block *internal_data;            // 0x00
};

struct CachedActiveComponentList {
  CachedPointerVector<Component *> ActiveComponents;     // 0x00
  bool changed;                                          // 0x08
  uint8_t _pad09[7];                                     // 0x09
};
static_assert(sizeof(CachedActiveComponentList) == 0x10, "CachedActiveComponentList size mismatch");

struct Scene {
  Director *director;                                    // 0x000
  podvector<Entity *> Entities;                          // 0x008
  podvector<Component *> *ComponentLists;                // 0x018
  CachedActiveComponentList *CachedActiveComponentLists; // 0x020
  flatset<Component *> CompList_earliest_update;         // 0x028
  flatset<Component *> CompList_early_update;            // 0x070
  flatset<Component *> CompList_update;                  // 0x0B8
  flatset<Component *> CompList_late_update;             // 0x100
  flatset<Component *> CompList_always_update;           // 0x148
  flatset<Component *> CompList_latest_update;           // 0x190
  flatset<Component *> CompList_earliest_unlocked_update;// 0x1D8
  flatset<Component *> CompList_early_unlocked_update;   // 0x220
  flatset<Component *> CompList_unlocked_update;         // 0x268
  flatset<Component *> CompList_late_unlocked_update;    // 0x2B0
  flatset<Component *> CompList_latest_unlocked_update;  // 0x2F8
  flatset<Component *> CompList_prerender;               // 0x340
  flatset<Component *> CompList_render_event;            // 0x388
  flatset<Component *> CompList_debug_render_event;      // 0x3D0
  flatset<Component *> CompList_postrender;              // 0x418
  double SortDepth;                                      // 0x460
  bool any_deleted_entities;                             // 0x468
  uint8_t _pad469[7];                                    // 0x469
  podvector<bool> any_deleted_components;                // 0x470
  int32_t deleted_components_estimate;                   // 0x480
  uint8_t _pad484[4];                                    // 0x484
  uint8_t start_component_queue[40];                     // 0x488
  bool doing_scene_destruction;                          // 0x4B0
  uint8_t _pad4B1[7];                                    // 0x4B1
  MsvcReleaseModeXString name;                           // 0x4B8
  bool paused;                                           // 0x4D8
  bool hidden;                                           // 0x4D9
  bool suspended;                                        // 0x4DA
  bool deleted;                                          // 0x4DB
  bool controls_prevented;                               // 0x4DC
  uint8_t _pad4DD[3];                                    // 0x4DD
  uint8_t taskpool[1304];                                // 0x4E0
  int64_t scene_id;                                      // 0x9F8
  int64_t update_count;                                  // 0xA00
};
static_assert(sizeof(Scene) == 2568, "Scene size mismatch");

struct Director {
  MsvcReleaseModeVector<Scene *> scenes;                 // 0x00
  void *shared;                                          // 0x18
  bool invalidate_bitmap_atlas_on_scene_change;          // 0x20
  uint8_t _pad21[7];                                     // 0x21
  double delta_time;                                     // 0x28
  double frame_percent;                                  // 0x30
};
static_assert(sizeof(Director) == 0x38, "Director size mismatch");

struct DamageNumber : Component {
  void *transform;                                       // 0x38
  void *renderer;                                        // 0x40
  Vec3D speed;                                           // 0x48
  iVec2D source_tile;                                    // 0x60
  double timer;                                          // 0x68
  Ref<Character> owner;                                  // 0x70
  Character *orig_owner;                                 // 0x80
  Vec3D last_valid_source_position;                      // 0x88
};
static_assert(sizeof(DamageNumber) == 160, "DamageNumber size mismatch");

struct Transform {
  uint8_t _pad0[56];                                     // 0x00
  uint8_t previous_state[72];                            // 0x38
  Vec3D position;                                        // 0x80
  Vec2D scale;                                           // 0x98
  double rotation;                                       // 0xA8
  double sortdepth;                                      // 0xB0
  Vec2D flip;                                            // 0xB8
  bool discontinuous;                                    // 0xC8
  uint8_t _padC9[7];                                     // 0xC9
};
static_assert(sizeof(Transform) == 208, "Transform size mismatch");

struct CatPlacementArea : Component {
  Transform *transform;                                  // 0x38
  MsvcReleaseModeXString roomname;                       // 0x40
  bool counts_for_zoomout;                               // 0x60
  bool focusable;                                        // 0x61
  uint8_t _pad62[6];                                     // 0x62
  podvector<HouseCat *> owned_cats;                      // 0x68
  int32_t autofeed_available;                            // 0x78
  uint8_t _pad7C[4];                                     // 0x7C
  uint8_t no_effects[24];                                // 0x80
  podvector<uint8_t> extra_constrain_planes;             // 0x98
  void *sfx_channel;                                     // 0xA8
  bool show_room_tooltip;                                // 0xB0
  uint8_t _padB1[7];                                     // 0xB1
  MsvcReleaseModeXString interstitial_background_frame;  // 0xB8
  int32_t deferred_poop;                                 // 0xD8
  uint8_t _padDC[4];                                     // 0xDC
};
static_assert(sizeof(CatPlacementArea) == 224, "CatPlacementArea size mismatch");

struct HouseCat : Component {
  void *physics;                                         // 0x38
  Transform *transform;                                  // 0x40
  void *renderer;                                        // 0x48
  void *nametag;                                         // 0x50
  void *icontag;                                         // 0x58
  void *moodbubble;                                      // 0x60
  void *animator;                                        // 0x68
  CatParts *catart;                                      // 0x70
  void *ragdoll;                                         // 0x78
  int64_t cat_id;                                        // 0x80
  bool exists;                                           // 0x88
  bool brain_has_control;                                // 0x89
  bool lock_walk;                                        // 0x8A
  bool pipe_immune;                                      // 0x8B
  bool pgrounded;                                        // 0x8C
  uint8_t _pad8D[3];                                     // 0x8D
  int32_t entered_idle_time;                             // 0x90
  int32_t brain_state;                                   // 0x94
  int32_t prev_brain_state;                              // 0x98
  uint8_t _pad9C[4];                                     // 0x9C
  uint8_t furniture_stats[24];                           // 0xA0
  double regain_control_timer;                           // 0xB8
  double brain_idle_timer;                               // 0xC0
  double idle_animation_timer;                           // 0xC8
  double walk_speed;                                     // 0xD0
  bool can_sit;                                          // 0xD8
  bool check_for_overlaps;                               // 0xD9
  bool attacking;                                        // 0xDA
  bool ragdolling;                                       // 0xDB
  bool departed;                                         // 0xDC
  bool panicy;                                           // 0xDD
  uint8_t _padDE[2];                                     // 0xDE
  double impact_mute_timer;                              // 0xE0
  CatPlacementArea *current_location;                    // 0xE8
  iVec2D desired_room_position;                          // 0xF0
  int32_t depth;                                         // 0xF8
  uint8_t _padFC[4];                                     // 0xFC
  void *clickbounds;                                     // 0x100
  void *catbounds;                                       // 0x108
  void *attackbounds;                                    // 0x110
};
static_assert(sizeof(HouseCat) == 280, "HouseCat size mismatch");

struct ButchBox : CatPlacementArea {
  void *renderer;                                        // 0xE0
  void *mask;                                            // 0xE8
  bool unlocked;                                         // 0xF0
  uint8_t _padF1[7];                                     // 0xF1
  podvector<HouseCat *> slots;                           // 0xF8
  podvector<Vec2D> slot_offsets;                         // 0x108
};
static_assert(sizeof(ButchBox) == 280, "ButchBox size mismatch");

// ============================================================================
// Save & Progression
// ============================================================================

struct SQLSaveFile {
  void *db;                                              // 0x00
  MsvcReleaseModeXString filename;                       // 0x08
  int32_t transaction_stack;                             // 0x28
  uint8_t _pad2C[4];                                     // 0x2C
};
static_assert(sizeof(SQLSaveFile) == 48, "SQLSaveFile size mismatch");

struct HouseInventory {
  uint8_t _pad0[0xB0];                                   // 0x00
  int32_t house_food;                                    // 0xB0
  int32_t house_gold;                                    // 0xB4
  int32_t blank_collars;                                 // 0xB8
  int32_t storage_upgrades;                              // 0xBC
};
static_assert(sizeof(HouseInventory) == 192, "HouseInventory size mismatch");

struct GlobalProgressionData {
  uint8_t _pad0[0x1C];                                   // 0x00
  int32_t absolute_day;                                  // 0x1C
  uint8_t _pad20[0x3F0];                                 // 0x20
  struct {
    struct {
      uint8_t _pad[8];
      int32_t current_difficulty;                        // 0x410
      uint8_t _pad2[12];
    } act1;                                              // 0x408
    struct {
      uint8_t _pad[8];
      int32_t current_difficulty;                        // 0x428
      uint8_t _pad2[12];
    } act2;                                              // 0x420
    struct {
      uint8_t _pad[8];
      int32_t current_difficulty;                        // 0x440
      uint8_t _pad2[12];
    } act3;                                              // 0x438
  } difficulty;                                          // 0x408
  uint8_t _pad458[896];                                  // 0x458
};
static_assert(sizeof(GlobalProgressionData) == 2008, "GlobalProgressionData size mismatch");

struct CatDatabase {
  uint8_t _pad0[384];                                    // 0x00
};
static_assert(sizeof(CatDatabase) == 384, "CatDatabase size mismatch");

struct MewSaveFile {
  uint8_t _pad0[0x470];                                  // 0x000
  SQLSaveFile db;                                        // 0x470
  uint8_t _pad4A0[0xA8];                                 // 0x4A0
};
static_assert(sizeof(MewSaveFile) == 1352, "MewSaveFile size mismatch");

struct MewDirector : Component {
  MewSaveFile current_save_file;                         // 0x038
  int64_t current_day;                                   // 0x580
  void *inventory;                                       // 0x588
  void *troll_engine;                                    // 0x590
  CatDatabase *cat_db;                                   // 0x598
  HouseInventory *furniture;                             // 0x5A0
  GlobalProgressionData *progression;                    // 0x5A8
  void *tutorial;                                        // 0x5B0
  podvector<int64_t> current_battle_cats;                // 0x5B8
  uint8_t _pad5C8[0x78];                                 // 0x5C8
  podvector<Character *> cat_familiars;                  // 0x640
  uint8_t _pad650[0x80];                                 // 0x650
  int32_t next_event_bonus;                              // 0x6D0
  int32_t extra_events;                                  // 0x6D4
  MapNodeType current_node_type;                         // 0x6D8
  int32_t current_act;                                   // 0x6DC
  int32_t current_chapter;                               // 0x6E0
  uint8_t _pad6E4[4];                                    // 0x6E4
  MsvcReleaseModeXString current_chapter_id;             // 0x6E8
  Area current_chapter_area;                             // 0x708
  bool house_boss;                                       // 0x70C
  uint8_t _pad70D[3];                                    // 0x70D
  SaveScumLocation savescum_flag_on_quit;                // 0x710
  bool savescum_took_action;                             // 0x714
  bool realsave;                                         // 0x715
  uint8_t _pad716[0x7A];                                 // 0x716
};
static_assert(sizeof(MewDirector) == 1936, "MewDirector size mismatch");

struct GameTLSLayout {
  char padding[0x178];                                   // 0x000
  uint32_t rngState[4];                                  // 0x178
  uint64_t rngState4;                                    // 0x188
  uint64_t rngState5;                                    // 0x190
  char padding2[16];                                     // 0x198
};

// ============================================================================
// Cat & Character Data
// ============================================================================

struct CatStats {
  int32_t hp;
  int32_t mana;
  int32_t speed;
  int32_t attack;
  int32_t defense;
  int32_t wisdom;
  int32_t luck;
};

struct CatData {
  int64_t random_seed;                                   // 0x000
  uint8_t earned_achievements[16];                       // 0x008
  MsvcReleaseModeWString name_;                          // 0x018
  MsvcReleaseModeXString tagged_icon;                    // 0x038
  Gender internal_gender;                                // 0x058
  Gender external_gender;                                // 0x05C
  uint8_t appearance[1680];                              // 0x060
  CatStats base_stats;                                   // 0x6F0
  CatStats bonus_stats;                                  // 0x70C
  CatStats injuries;                                     // 0x728
  uint8_t injury_counts[68];                             // 0x744
  int32_t last_injury;                                   // 0x788
  uint8_t _pad78C[28];                                   // 0x78C
  uint8_t temp_stats[40];                                // 0x7A8
  uint8_t abilities[480];                                // 0x7D0
  uint8_t equipment[480];                                // 0x9B0
  uint8_t personality[104];                              // 0xB90
  uint64_t flags;                                        // 0xBF8
  uint8_t areas_visited[8];                              // 0xC00
  uint8_t max_act_completed;                             // 0xC08
  uint8_t max_chapter_completed;                         // 0xC09
  uint8_t max_difficulty_completed;                      // 0xC0A
  uint8_t _padC0B[5];                                    // 0xC0B
  MsvcReleaseModeXString cat_class;                      // 0xC10
  int32_t level;                                         // 0xC30
  LifeStage lifestage;                                   // 0xC34
  int64_t birthday;                                      // 0xC38
  int64_t deathday;                                      // 0xC40
  int64_t cat_uid;                                       // 0xC48
  double inbred_percent;                                 // 0xC50
};
static_assert(sizeof(CatData) == 3160, "CatData size mismatch");

struct GonObject {
  uint8_t children_map[56];                              // 0x00
  uint8_t children_array[24];                            // 0x38
  int32_t int_data;                                      // 0x50
  uint8_t _pad54[4];                                     // 0x54
  double float_data;                                     // 0x58
  bool bool_data;                                        // 0x60
  uint8_t _pad61[7];                                     // 0x61
  MsvcReleaseModeXString string_data;                    // 0x68
  MsvcReleaseModeXString name;                           // 0x88
  int32_t type;                                          // 0xA8
  uint8_t _padAC[4];                                     // 0xAC
};
static_assert(sizeof(GonObject) == 176, "GonObject size mismatch");

struct TacticsTile : Component {
  void *obj;                                             // 0x38
  TacticsGrid *grid;                                     // 0x40
  void *renderer;                                        // 0x48
  Transform *transform;                                  // 0x50
  int32_t enter_cost;                                    // 0x58
  int32_t exit_cost;                                     // 0x5C
  ElementList elements;                                  // 0x60
  uint8_t _pad68[208];                                   // 0x68
};
static_assert(sizeof(TacticsTile) == 312, "TacticsTile size mismatch");

struct TacticsObject {
  uint8_t _pad0[56];                                     // 0x00
  TacticsGrid *grid;                                     // 0x38
  Transform *transform;                                  // 0x40
  iVec2D position;                                       // 0x48
  iVec2D old_position;                                   // 0x50
  Character *character;                                  // 0x58
  TacticsTile *tile;                                     // 0x60
  uint8_t _pad68[248];                                   // 0x68
};
static_assert(sizeof(TacticsObject) == 352, "TacticsObject size mismatch");

struct Passive {
  uint8_t _pad0[56];                                     // 0x00
  Character *character;                                  // 0x38
  int64_t equipment_id;                                  // 0x40
  void *cat_passive_def;                                 // 0x48
  Ability *associated_ability;                           // 0x50
  int32_t priority;                                      // 0x58
  int32_t stacks;                                        // 0x5C
  GonObject *data;                                       // 0x60
  uint8_t _pad68[96];                                    // 0x68
};
static_assert(sizeof(Passive) == 200, "Passive size mismatch");

// ============================================================================
// Combat & Turn Actions
// ============================================================================

struct TurnAction {
  ActionKind kind;                                       // 0x00
  uint8_t _pad04[4];                                     // 0x04
  Ability *ability;                                      // 0x08
  iVec2D tile;                                           // 0x10
  iVec2D orientation;                                    // 0x18
  Ref<Character> source;                                 // 0x20
  bool no_cost;                                          // 0x30
  bool prime_trigger;                                    // 0x31
  bool is_chain;                                         // 0x32
  bool even_if_dead;                                     // 0x33
  bool force_display_name;                               // 0x34
  bool auto_recompute_target;                            // 0x35
  bool respect_prime_when_nocost;                        // 0x36
  bool intentional;                                      // 0x37
  int32_t additional_data;                               // 0x38
  uint8_t _pad3C[4];                                     // 0x3C
  MsvcReleaseModeStdFunction CustomAction;               // 0x40
  Animate animate;                                       // 0x80
  uint8_t _pad84[4];                                     // 0x84
};
static_assert(sizeof(TurnAction) == 136, "TurnAction size mismatch");

struct Turn {
  TurnKind kind;                                         // 0x00
  uint8_t _pad04[4];                                     // 0x04
  Ref<Character> character;                              // 0x08
  int64_t uid;                                           // 0x18
};
static_assert(sizeof(Turn) == 32, "Turn size mismatch");

struct Ability {
  uint8_t _pad0[16];                                     // 0x00
  Character *character;                                  // 0x10
  void *vcharacter;                                      // 0x18
  TacticsGrid *grid;                                     // 0x20
  GonObject *data;                                       // 0x28
  int64_t equipment_id;                                  // 0x30
  int32_t equipment_durability;                          // 0x38
  int32_t equipment_aux;                                 // 0x3C
  uint8_t cost[144];                                     // 0x40
  uint8_t target_info[320];                              // 0xD0
  uint8_t damage_instance[336];                          // 0x210
  uint8_t damage_instance_splash[336];                   // 0x360
  int32_t bonus_damage_based_on_range;                   // 0x4B0
  bool no_cost_modification;                             // 0x4B4
  bool allow_offmap_casts;                               // 0x4B5
  uint8_t _pad4B6[2];                                    // 0x4B6
  uint8_t self_damage[336];                              // 0x4B8
  int32_t self_damage_mode;                              // 0x608
  uint8_t _pad60C[4];                                    // 0x60C
  TurnAction target;                                     // 0x610
  bool complete;                                         // 0x698
  bool past_castpoint;                                   // 0x699
  bool will_chain;                                       // 0x69A
  bool no_wait;                                          // 0x69B
  bool super_armor;                                      // 0x69C
  bool use_super_armor;                                  // 0x69D
  bool temp_knockback_immune;                            // 0x69E
  bool dont_visualize_ai;                                // 0x69F
  bool canceled;                                         // 0x6A0
  bool castpoint_canceled;                               // 0x6A1
  bool lock_ai_base_score;                               // 0x6A2
  bool speedpause;                                       // 0x6A3
  uint8_t _pad6A4[4];                                    // 0x6A4
  double dT;                                             // 0x6A8
  int32_t charge;                                        // 0x6B0
  int32_t prime;                                         // 0x6B4
  int32_t level;                                         // 0x6B8
  int32_t ncasts;                                        // 0x6BC
  int32_t ncasts_this_turn;                              // 0x6C0
  int32_t X;                                             // 0x6C4
  int32_t custom_X;                                      // 0x6C8
  int32_t stored_mana;                                   // 0x6CC
  bool lock_X;                                           // 0x6D0
  uint8_t _pad6D1[3];                                    // 0x6D1
  iVec2D last_casted_from_location;                      // 0x6D4
  iVec2D last_movement_direction;                        // 0x6DC
  uint8_t _pad6E4[4];                                    // 0x6E4
  int64_t cast_id;                                       // 0x6E8
  int64_t cached_stat_changed_id;                        // 0x6F0
  Ability *ai_ability;                                   // 0x6F8
  Ability *chain_ability;                                // 0x700
  Ability *parent_ability;                               // 0x708
  Ability *sub_ability;                                  // 0x710
  podvector<int32_t> customvalues;                       // 0x718
  uint8_t temporary_statuses[16];                        // 0x728
  podvector<const GonObject *> temporary_passives;       // 0x738
  MsvcReleaseModeVector<Ref<Passive>> current_temporary_passives; // 0x748
  uint8_t _pad760[480];                                  // 0x760
};
static_assert(sizeof(Ability) == 2368, "Ability size mismatch");

struct TurnControl : Component {
  int64_t choice_id;                                     // 0x38
  uint8_t queued_actions[40];                            // 0x40
  Character *current_turn;                               // 0x68
  Turn pending_next_turn;                                // 0x70
  TurnAction current_action;                             // 0x90
  uint8_t current_round[16];                             // 0x118
  void *current_turn_iterator;                           // 0x128
  uint8_t initiative_tiebreakers[16];                    // 0x130
  uint8_t extra_turn_tracker[16];                        // 0x140
  int64_t uid;                                           // 0x150
  int32_t round_count;                                   // 0x158
  int32_t runaway_grace_period;                          // 0x15C
  double minimum_delay;                                  // 0x160
  bool battle_started;                                   // 0x168
  bool first_round_popup;                                // 0x169
  uint8_t _pad16A[6];                                    // 0x16A
  double battle_start_delay;                             // 0x170
  double current_combat_speed;                           // 0x178
  bool request_speedpause;                               // 0x180
  uint8_t _pad181[7];                                    // 0x181
  uint8_t upkeep_queue[16];                              // 0x188
  uint8_t upkeep_queue_global_effects[16];               // 0x198
  bool did_upkeep;                                       // 0x1A8
  bool ambush;                                           // 0x1A9
  bool ambush_factions[14];                              // 0x1AA
};
static_assert(sizeof(TurnControl) == 440, "TurnControl size mismatch");

struct Level : Component {
  void *data;                                            // 0x38
  void *spawndb;                                         // 0x40
  GonObject *spawnlist;                                  // 0x48
  GonObject *tilelist;                                   // 0x50
  TacticsGrid *grid;                                     // 0x58
  uint8_t difficulty[64];                                // 0x60
  double bird_chance;                                    // 0xA0
  podvector<int32_t> champion_spawns;                    // 0xA8
  podvector<int32_t> elite_spawns;                       // 0xB8
  ElementList banned_random_weather_elements;            // 0xC8
  MsvcReleaseModeVector<Ref<Character>> party_cat_objects; // 0xD0
  ElementList hint_weather_elements;                     // 0xE8
  uint8_t tagged_locations[64];                          // 0xF0
  uint8_t elite_buff_limit_counter[64];                  // 0x130
  uint8_t earned_rewards[32];                            // 0x170
  int32_t max_wave;                                      // 0x190
  int32_t current_wave;                                  // 0x194
  int32_t luck_punishment;                               // 0x198
  int32_t runaway_count;                                 // 0x19C
  double end_timer;                                      // 0x1A0
  bool won;                                              // 0x1A8
  bool signal_win;                                       // 0x1A9
  bool lost;                                             // 0x1AA
  bool prevent_loss;                                     // 0x1AB
  bool permanent_prevent_loss;                           // 0x1AC
  bool boss_minions_autorun;                             // 0x1AD
  bool did_combatendstuff;                               // 0x1AE
  bool is_boss_level;                                    // 0x1AF
  MsvcReleaseModeXString levelfilename;                  // 0x1B0
  bool cached_omniscience;                               // 0x1D0
  uint8_t _pad1D1[7];                                    // 0x1D1
  int64_t last_cached_update_id;                         // 0x1D8
  float rolls[256];                                      // 0x1E0
};
static_assert(sizeof(Level) == 1504, "Level size mismatch");

struct Brain : Component {
  Character *character;                                  // 0x038
  GonObject *data;                                       // 0x040
  void *optimizer;                                       // 0x048
  uint8_t ai_metrics[464];                               // 0x050
  TurnAction choice;                                     // 0x220
  double timer;                                          // 0x2A8
  bool default_animate_choice;                           // 0x2B0
  bool auto_orient;                                      // 0x2B1
  bool desire_runaway;                                   // 0x2B2
  bool run_if_skipped_turn;                              // 0x2B3
  bool moved_this_turn;                                  // 0x2B4
  uint8_t _pad2B5[3];                                    // 0x2B5
  TurnKind current_turn_kind;                            // 0x2B8
  uint8_t _pad2BC[4];                                    // 0x2BC
};
static_assert(sizeof(Brain) == 704, "Brain size mismatch");

struct PlayerBrain : Brain {
  TurnAction user_choice;                                // 0x2C0
  TurnAction pending_choice;                             // 0x348
  bool requested_choice;                                 // 0x3D0
  bool actual_requested_choice;                          // 0x3D1
  bool p_requested_choice;                               // 0x3D2
  bool submitted_end_turn;                               // 0x3D3
  bool taking_turn;                                      // 0x3D4
  uint8_t _pad3D5[3];                                    // 0x3D5
  Ability *pending_ability;                              // 0x3D8
  iVec2D prev_hovered_square;                            // 0x3E0
  int64_t last_confirm;                                  // 0x3E8
  uint8_t prev_highlighted_set[16];                      // 0x3F0
  iVec2D doubleclick_prevtarget;                         // 0x400
  iVec2D doubleclick_prevorient;                         // 0x408
  double doubleclick_timer;                              // 0x410
  int32_t targeting_state;                               // 0x418
  iVec2D og_orientation;                                 // 0x41C
  uint8_t _pad424[4];                                    // 0x424
};
static_assert(sizeof(PlayerBrain) == 1064, "PlayerBrain size mismatch");

struct Character : Component {
  Transform *transform;                                  // 0x038
  void *renderer;                                        // 0x040
  void *shadow;                                          // 0x048
  void *mask;                                            // 0x050
  void *animations;                                      // 0x058
  TacticsObject *obj;                                    // 0x060
  Brain *brain;                                          // 0x068
  void *statusbar;                                       // 0x070
  void *combat_animations;                               // 0x078
  TacticsGrid *grid;                                     // 0x080
  CatData *pcat_data;                                    // 0x088
  CatParts *catart;                                      // 0x090
  void *flasher;                                         // 0x098
  void *water_shroud;                                    // 0x0A0
  Vec3D prev_position;                                   // 0x0A8
  bool owns_pcatdata;                                    // 0x0C0
  bool catdata_is_specific_enemy;                        // 0x0C1
  bool temp_prevent_corpse;                              // 0x0C2
  uint8_t _pad0C3[1];                                    // 0x0C3
  int32_t familiar_index;                                // 0x0C4
  Ability *current_ability;                              // 0x0C8
  Ability *move;                                         // 0x0D0
  Ability *attack;                                       // 0x0D8
  Ability *bonus_ability;                                // 0x0E0
  podvector<Ability *> abilities;                        // 0x0E8
  podvector<Ability *> temporary_abilities;              // 0x0F8
  podvector<Ability *> hibernated_abilities;             // 0x108
  Ability *primed_ability;                               // 0x118
  TurnAction primed_target;                              // 0x120
  TurnAction chained_action;                             // 0x1A8
  podvector<Ability *> prev_chained_abilities;           // 0x230
  GonObject *data;                                       // 0x240
  MsvcReleaseModeXString name;                           // 0x248
  MsvcReleaseModeXString tooltip;                        // 0x268
  GonObject *extra_keyword_tooltips;                     // 0x288
  MsvcReleaseModeWString display_name;                   // 0x290
  MsvcReleaseModeXString classname;                      // 0x2B0
  MsvcReleaseModeXString default_projectile;             // 0x2D0
  MsvcReleaseModeXString default_attack_animation;       // 0x2F0
  MsvcReleaseModeXString shroud_frame;                   // 0x310
  MsvcReleaseModeXString tactical_view_frame;            // 0x330
  int32_t faction;                                       // 0x350
  int32_t brain_faction;                                 // 0x354
  int32_t displayed_faction;                             // 0x358
  int32_t disguised_faction;                             // 0x35C
  bool inherit_spawner_faction;                          // 0x360
  bool inherit_spawner_elite_buffs;                      // 0x361
  bool ignore_heritable_elite_buff_tag;                  // 0x362
  bool is_rare_spawn;                                    // 0x363
  bool force_not_familiar;                               // 0x364
  bool kill_required;                                    // 0x365
  bool default_kill_required;                            // 0x366
  bool can_run;                                          // 0x367
  int32_t auto_run_priority;                             // 0x368
  int32_t move_block;                                    // 0x36C
  int32_t pierce_block;                                  // 0x370
  uint8_t _pad374[4];                                    // 0x374
  double ai_scale;                                       // 0x378
  double initial_ai_scale;                               // 0x380
  iVec2D orientation;                                    // 0x388
  iVec2D desire_orientation;                             // 0x390
  iVec2D initial_position;                               // 0x398
  int32_t art_flip;                                      // 0x3A0
  int32_t art_180_flip;                                  // 0x3A4
  bool no_horizontal_flip;                               // 0x3A8
  bool no_splatter;                                      // 0x3A9
  uint8_t _pad3AA[2];                                    // 0x3AA
  iVec2D lock_orientation;                               // 0x3AC
  bool always_face_forward;                              // 0x3B4
  uint8_t _pad3B5[3];                                    // 0x3B5
  int32_t portrait_frame;                                // 0x3B8
  int32_t portrait_generation;                           // 0x3BC
  double move_speed_multiplier;                          // 0x3C0
  double original_scale;                                 // 0x3C8
  double original_shadow_scale;                          // 0x3D0
  int32_t piece_alt_frame;                               // 0x3D8
  uint8_t _pad3DC[4];                                    // 0x3DC
  MsvcReleaseModeXString additional_substate;            // 0x3E0
  void *portrait;                                        // 0x400
  uint8_t equipment_bindings[24];                        // 0x408
  uint8_t cat_passive_bindings[24];                      // 0x420
  uint8_t elite_buffs[24];                               // 0x438
  uint8_t added_elite_buffs[24];                         // 0x450
  Ref<Character> spawned_by;                             // 0x468
  Ref<Character> mounted_on;                             // 0x478
  bool is_summon;                                        // 0x488
  bool is_player_cat;                                    // 0x489
  uint8_t _pad48A[2];                                    // 0x48A
  int32_t post_combat_idlestate;                         // 0x48C
  MsvcReleaseModeXString spawnin_override;               // 0x490
  int32_t health;                                        // 0x4B0
  int32_t shield;                                        // 0x4B4
  int32_t divine_shield;                                 // 0x4B8
  int32_t max_health;                                    // 0x4BC
  bool uncapped_hp;                                      // 0x4C0
  bool uncapped_mana;                                    // 0x4C1
  bool dead;                                             // 0x4C2
  bool count_as_dead;                                    // 0x4C3
  bool possibly_reviving;                                // 0x4C4
  bool passives_dead;                                    // 0x4C5
  bool started_dead;                                     // 0x4C6
  bool died_to_damage;                                   // 0x4C7
  bool popped;                                           // 0x4C8
  bool leave_no_corpse;                                  // 0x4C9
  bool played_dying_animation;                           // 0x4CA
  bool no_death_state;                                   // 0x4CB
  bool use_turn_animations;                              // 0x4CC
  bool four_way_animations;                              // 0x4CD
  bool shield_counts_as_health;                          // 0x4CE
  bool no_death_passives;                                // 0x4CF
  bool allow_offmap_corpse;                              // 0x4D0
  uint8_t _pad4D1[7];                                    // 0x4D1
  Ref<TacticsObject> last_damaged_by;                    // 0x4D8
  uint8_t _pad4E8[2033];                                 // 0x4E8
  bool inanimate;                                        // 0xCD9
  bool static_object;                                    // 0xCDA
  bool speculative_inanimate;                            // 0xCDB
  bool ignore_mouseover;                                 // 0xCDC
  bool allow_statuses_when_dead;                         // 0xCDD
  bool champion;                                         // 0xCDE
  bool elite;                                            // 0xCDF
  bool virtual_champion;                                 // 0xCE0
  bool can_be_overkilled;                                // 0xCE1
  bool avoid_ally_trample;                               // 0xCE2
  bool dont_remove_turns_on_death;                       // 0xCE3
  int32_t tile_desire_cost;                              // 0xCE4
  double champ_health_multiplier;                        // 0xCE8
  int32_t champ_health_minimum;                          // 0xCF0
  CharacterType character_type;                          // 0xCF4
  ElementList innate_elements;                           // 0xCF8
  ElementList immune_elements;                           // 0xD00
  ElementList free_pathfind_elements;                    // 0xD08
  int32_t move_points;                                   // 0xD10
  int32_t act_points;                                    // 0xD14
  int32_t mana;                                          // 0xD18
  int32_t max_mana;                                      // 0xD1C
  uint8_t grouped_cantrip_tracker[24];                   // 0xD20
  int32_t total_abilities_used;                          // 0xD38
  int32_t basic_moves_used;                              // 0xD3C
  int32_t basic_attacks_used;                            // 0xD40
  int32_t spells_used;                                   // 0xD44
  int32_t storm_count;                                   // 0xD48
  int32_t weapons_used;                                  // 0xD4C
  int32_t trinkets_used;                                 // 0xD50
  int32_t turn_count;                                    // 0xD54
  int32_t turns_taken_this_round;                        // 0xD58
  uint8_t _padD5C[4];                                    // 0xD5C
  uint8_t last_consumed_trinket[96];                     // 0xD60
  iVec2D last_targeted_tile;                             // 0xDC0
  int32_t last_spell_used;                               // 0xDC8
  uint8_t _padDCC[4];                                    // 0xDCC
  double uifloaters_up;                                  // 0xDD0
  Vec2D projectile_spawn_offset;                         // 0xDD8
  iVec2D last_end_move_position;                         // 0xDE8
  TurnKind current_turn_kind;                            // 0xDF0
  bool auto_position_on_start;                           // 0xDF4
  bool finished_spawning;                                // 0xDF5
  bool mutations_were_applied;                           // 0xDF6
  uint8_t _padDF7[1];                                    // 0xDF7
  uint8_t current_shroud[40];                            // 0xDF8
  int32_t deathpoof_size;                                // 0xE20
  bool pretend_to_trample;                               // 0xE24
  uint8_t _padE25[3];                                    // 0xE25
  uint8_t delay_deathtext[24];                           // 0xE28
  uint8_t WaitForHookReturn[16];                         // 0xE40
  bool touched_passives;                                 // 0xE50
  uint8_t _padE51[7];                                    // 0xE51
  int64_t last_cached_tiles;                             // 0xE58
  int64_t last_cached_auras;                             // 0xE60
  int64_t stat_change_counter;                           // 0xE68
  podvector<Passive *> cached_passives;                  // 0xE70
  podvector<Passive *> cached_auras;                     // 0xE80
  podvector<TacticsTile *> cached_standing_tiles;        // 0xE90
  podvector<Ability *> abilities_pending_removal;        // 0xEA0
  bool abilities_touched;                                // 0xEB0
  bool auras_effecting_abilities;                        // 0xEB1
  uint8_t _padEB2[2];                                    // 0xEB2
  int32_t passives_locked;                               // 0xEB4
  int32_t auras_locked;                                  // 0xEB8
  int32_t standing_tiles_locked;                         // 0xEBC
  int32_t abilities_locked;                              // 0xEC0
  bool recompute_item_effects_locked;                    // 0xEC4
  bool needs_recompute_item_effects;                     // 0xEC5
  bool recompute_item_bindings_locked;                   // 0xEC6
  bool needs_recompute_bindings;                         // 0xEC7
  bool defer_displacement_check;                         // 0xEC8
  bool view_bleeding_characters_as_enemies;              // 0xEC9
  bool view_bugs_as_enemies;                             // 0xECA
  bool aggro_target_is_enemy;                            // 0xECB
  bool small_enemies_ignore_you;                         // 0xECC
  bool spawner_is_ally;                                  // 0xECD
  bool disguised;                                        // 0xECE
  bool omniscient;                                       // 0xECF
  bool drank_water;                                      // 0xED0
  bool visually_spiky;                                   // 0xED1
  bool cant_respawn;                                     // 0xED2
  bool black_holed;                                      // 0xED3
  bool gibbed;                                           // 0xED4
  bool splatted;                                         // 0xED5
  bool temp_dont_correct_position_on_remove;             // 0xED6
  bool triggeredgib;                                     // 0xED7
};
static_assert(sizeof(Character) == 3800, "Character size mismatch");

// ============================================================================
// UI & Screens
// ============================================================================

struct Button : Component {
  Ref<void> renderer;                                    // 0x038
  void *button_clip;                                     // 0x048
  int32_t panel;                                         // 0x050
  int32_t radio_group;                                   // 0x054
  bool radio_selected;                                   // 0x058
  bool trigger_on_press;                                 // 0x059
  bool semi_disabled;                                    // 0x05A
  bool navigable_while_disabled;                         // 0x05B
  uint8_t _pad5C[4];                                     // 0x05C
  double mash_cooldown;                                  // 0x060
  double mash_timer;                                     // 0x068
  uint8_t cached_bounds[16];                             // 0x070
  void *main_camera;                                     // 0x080
  void *hotkey;                                          // 0x088
  void *manager;                                         // 0x090
  bool hotkey_pressed;                                   // 0x098
  uint8_t _pad99[7];                                     // 0x099
  Vec2D center_offset;                                   // 0x0A0
  int32_t hotkey_mode;                                   // 0x0B0
  uint8_t _padB4[4];                                     // 0x0B4
  MsvcReleaseModeStdFunction onclick;                    // 0x0B8
  MsvcReleaseModeStdFunction onpress;                    // 0x0F8
  MsvcReleaseModeStdFunction onprerender;                // 0x138
  MsvcReleaseModeStdFunction on_display_tooltip;         // 0x178
  MsvcReleaseModeWString label;                          // 0x1B8
  MsvcReleaseModeWString tooltip;                        // 0x1D8
  MsvcReleaseModeXString sound_event_prefix;             // 0x1F8
  void *userdata;                                        // 0x218
  podvector<void *> extra_userdata;                      // 0x220
  int32_t userdata_type;                                 // 0x230
  bool camera_specified;                                 // 0x234
  bool goto_and_play_mode;                               // 0x235
  bool use_hand_cursor;                                  // 0x236
  uint8_t _pad237[1];                                    // 0x237
  int32_t current_rendergroup;                           // 0x238
  bool autonav;                                          // 0x23C
  uint8_t _pad23D[3];                                    // 0x23D
  Button *navigation[6];                                 // 0x240
  Matrix3x2 orig_matrix;                                 // 0x270
  ColorTransform orig_color_matrix;                      // 0x288
  bool cached_orig_matrix;                               // 0x2A8
  uint8_t _pad2A9[7];                                    // 0x2A9
  uint8_t tweens[24];                                    // 0x2B0
  void *tween_curve_hover;                               // 0x2C8
  void *tween_curve_unhover;                             // 0x2D0
  double tween_time_hover;                               // 0x2D8
  double tween_time_unhover;                             // 0x2E0
  double hover_scale;                                    // 0x2E8
  int32_t state;                                         // 0x2F0
  int32_t prev_state;                                    // 0x2F4
  int32_t prev_played_state;                             // 0x2F8
  int32_t mode;                                          // 0x2FC
  MsvcReleaseModeXString stateframes[6];                 // 0x300
};
static_assert(sizeof(Button) == 960, "Button size mismatch");

struct CombatMenu : Component {
  uint8_t nulldat[88];                                   // 0x038 (offset 56)
  void *tooltip;                                         // 0x090 (offset 144)
  void *renderer;                                        // 0x098 (offset 152)
  void *background;                                      // 0x0A0 (offset 160)
  Ability *current_ability;                              // 0x0A8 (offset 168)
  Ability *currently_casting;                            // 0x0B0 (offset 176)
  MsvcReleaseModeXString prev_gamepad_focus;             // 0x0B8 (offset 184)
  uint8_t tweens[24];                                    // 0x0D8 (offset 216)
  MsvcReleaseModeVector<Button *> buttons;               // 0x0F0 (offset 240)
  uint8_t btnmap[16];                                    // 0x108 (offset 264)
  Ref<Character> current_character;                      // 0x118 (offset 280)
  uint8_t cached_udata[24];                              // 0x128 (offset 296)
  bool showing;                                          // 0x140 (offset 320)
  bool no_options_left;                                  // 0x141 (offset 321)
  bool examining;                                        // 0x142 (offset 322)
  bool end_turn_disabled;                                // 0x143 (offset 323)
  bool hint_last_action_endturn;                         // 0x144 (offset 324)
  uint8_t _pad145[3];                                    // 0x145
};
static_assert(sizeof(CombatMenu) == 328, "CombatMenu size mismatch");

struct PauseMenuScene : Component {
  uint8_t _pad0[0x68];                                   // 0x38
  podvector<Scene *> paused_scenes;                      // 0xA0
  void *pausemenu;                                       // 0xB0
  void *pausecontrols;                                   // 0xB8
  bool paused;                                           // 0xC0
  bool quit_confirmation_open;                           // 0xC1
  bool pause_requested;                                  // 0xC2
  uint8_t _padC3[5];                                     // 0xC3
};
static_assert(sizeof(PauseMenuScene) == 200, "PauseMenuScene size mismatch");

struct MapNode {
  podvector<MapNodeType> possible_types;                 // 0x000
  MsvcReleaseModeXString group;                          // 0x010
  MsvcReleaseModeXString name;                           // 0x030
  MsvcReleaseModeXString override_level;                 // 0x050
  MsvcReleaseModeXString boss_choice;                    // 0x070
  MsvcReleaseModeXString unlockcheck_on_complete;        // 0x090
  MsvcReleaseModeXString override_art;                   // 0x0B0
  MsvcReleaseModeXString override_music;                 // 0x0D0
  MsvcReleaseModeXString override_tileset;               // 0x0F0
  int32_t index;                                         // 0x110
  int32_t override_music_layer;                          // 0x114
  uint8_t seed[32];                                      // 0x118
  MapNodeType type;                                      // 0x138
  uint8_t _pad13C[4];                                    // 0x13C
  podvector<MapNode *> connections;                      // 0x140
  podvector<MapNode *> rev_connections;                  // 0x150
  void *graphics;                                        // 0x160
  bool cleared;                                          // 0x168
  bool locked;                                           // 0x169
  bool hidden;                                           // 0x16A
  bool phidden;                                          // 0x16B
  bool traversable;                                      // 0x16C
  bool was_generated;                                    // 0x16D
  bool is_final_boss;                                    // 0x16E
  uint8_t _pad16F[1];                                    // 0x16F
  MapScreen *parent;                                     // 0x170
  Button *button;                                        // 0x178
};
static_assert(sizeof(MapNode) == 384, "MapNode size mismatch");

struct MapMarker : Component {
  MapScreen *map;                                        // 0x38
  void *renderer;                                        // 0x40
  Transform *transform;                                  // 0x48
  MapNode *current_node;                                 // 0x50
  MapNode *next_node;                                    // 0x58
  MapNode *pathfind_to;                                  // 0x60
  double move_timer;                                     // 0x68
  double speed;                                          // 0x70
  bool prevent_moving;                                   // 0x78
  uint8_t _pad79[7];                                     // 0x79
  MsvcReleaseModeStdFunction OnEnterNode;                // 0x80
};
static_assert(sizeof(MapMarker) == 192, "MapMarker size mismatch");

struct MapScreen : Component {
  uint8_t generation_rng[32];                            // 0x038
  MsvcReleaseModeXString map_filename;                   // 0x058
  podvector<MapNode *> nodes;                            // 0x078
  void *map_bg;                                          // 0x088
  void *map_fg;                                          // 0x090
  void *map_floaters;                                    // 0x098
  MapMarker *cat_marker;                                 // 0x0A0
  MapMarker *nemesis;                                    // 0x0A8
  GonObject *orig_data;                                  // 0x0B0
  GonObject data;                                        // 0x0B8
  Vec2D queued_input;                                    // 0x168
  void *panel;                                           // 0x178
  void *camera;                                          // 0x180
  uint8_t cam_bounds[32];                                // 0x188
  uint8_t gen_bounds[32];                                // 0x1A8
  Vec3D target_cam_pos;                                  // 0x1C8
  int32_t cam_mode;                                      // 0x1E0
  bool new_item_process;                                 // 0x1E4
  bool allow_mousescrolling_x;                           // 0x1E5
  bool allow_mousescrolling_y;                           // 0x1E6
  bool early_exited;                                     // 0x1E7
  bool cam_squeeze_zoom;                                 // 0x1E8
  bool world_name_is_text;                               // 0x1E9
  bool qitems_need_update;                               // 0x1EA
  uint8_t _pad1EB[5];                                    // 0x1EB
  MsvcReleaseModeWString world_name_text;                // 0x1F0
  double cam_zoom_timer;                                 // 0x210
  int32_t nemesis_timer;                                 // 0x218
  int32_t nemesis_count;                                 // 0x21C
  int32_t nemesis_moves;                                 // 0x220
  int32_t prev_coins;                                    // 0x224
  int32_t prev_food;                                     // 0x228
  int32_t prev_boxes;                                    // 0x22C
  double lose_timer;                                     // 0x230
  uint8_t node_map[16];                                  // 0x238
  uint8_t node_groups[16];                               // 0x248
  uint8_t map_flags[16];                                 // 0x258
};
static_assert(sizeof(MapScreen) == 616, "MapScreen size mismatch");

struct MapNodeClosure {
  void *vtable;                                        // 0x00
  MapNode *node;                                       // 0x08
};
static_assert(sizeof(MapNodeClosure) == 16, "MapNodeClosure size mismatch");

struct CatSelector : Component {
  MsvcReleaseModeVector<Transform *> cats;               // 0x38
  podvector<double> cat_annoyance_counters;              // 0x50
  bool pet_cooldown;                                     // 0x60
  uint8_t _pad61[7];                                     // 0x61
  void *renderer;                                        // 0x68
  void *panel;                                           // 0x70
  int32_t current_cat_index;                             // 0x78
  uint8_t _pad7C[4];                                     // 0x7C
  double current_rotation;                               // 0x80
  int64_t current_cat;                                   // 0x88
  uint8_t classbutton_materials[16];                     // 0x90
  double petting_cursor_timer;                           // 0xA0
};
static_assert(sizeof(CatSelector) == 168, "CatSelector size mismatch");

struct LevelUpOption {
  int32_t kind;                                          // 0x00
  uint8_t _pad04[4];                                     // 0x04
  MsvcReleaseModeWString title;                          // 0x08
  MsvcReleaseModeWString label;                          // 0x28
  MsvcReleaseModeWString desc;                           // 0x48
  uint8_t item[96];                                      // 0x68
  MsvcReleaseModeXString data;                           // 0xC8
  int32_t int_data;                                      // 0xE8
  bool include_full_heal;                                // 0xEC
  uint8_t _padED[3];                                     // 0xED
};
static_assert(sizeof(LevelUpOption) == 240, "LevelUpOption size mismatch");

struct LevelUpScreen : Component {
  uint8_t el_TutorialNotify[104];                        // 0x038
  CatData *cat;                                          // 0x0A0
  CatParts *catart;                                      // 0x0A8
  void *panel;                                           // 0x0B0
  uint8_t rng[32];                                       // 0x0B8
  void *catrenderer;                                     // 0x0D8
  bool cat_is_multiclass;                                // 0x0E0
  uint8_t _pad0E1[3];                                    // 0x0E1
  int32_t rerolls;                                       // 0x0E4
  int32_t max_rerolls;                                   // 0x0E8
  uint8_t _pad0EC[4];                                    // 0x0EC
  double complicated_ability_ban_chance;                 // 0x0F0
  uint8_t active_pool_groups[16];                        // 0x0F8
  uint8_t upgrade_class_actives[24];                     // 0x108
  uint8_t upgrade_class_passives[24];                    // 0x120
  uint8_t class_active_pool[48];                         // 0x138
  uint8_t multiclass_active_pool[48];                    // 0x168
  uint8_t colorless_active_pool[48];                     // 0x198
  uint8_t class_passive_pool[48];                        // 0x1C8
  uint8_t multiclass_passive_pool[48];                   // 0x1F8
  uint8_t colorless_passive_pool[48];                    // 0x228
  uint8_t complicated_active_pool[48];                   // 0x258
  uint8_t complicated_passive_pool[48];                  // 0x288
  uint8_t jester_active_pool[48];                        // 0x2B8
  uint8_t jester_passive_pool[48];                       // 0x2E8
  bool chosen;                                           // 0x318
  uint8_t _pad319[3];                                    // 0x319
  int32_t stat_multiplier;                               // 0x31C
  MsvcReleaseModeStdFunction on_close;                   // 0x320
  MsvcReleaseModeVector<LevelUpOption> options;          // 0x360
};
static_assert(sizeof(LevelUpScreen) == 888, "LevelUpScreen size mismatch");

struct WorldEventOption {
  uint8_t data[240];
};
static_assert(sizeof(WorldEventOption) == 240, "WorldEventOption size mismatch");

struct IntroPane {
  MsvcReleaseModeXString title;
  MsvcReleaseModeXString prompt;
};
static_assert(sizeof(IntroPane) == 64, "IntroPane size mismatch");

struct CatChoicePane {
  int32_t cat_choice;
  int32_t history_size;
};
static_assert(sizeof(CatChoicePane) == 8, "CatChoicePane size mismatch");

struct ActionChoicePane {
  MsvcReleaseModeXString prompt;
  GonObject *setup_action;
  MsvcReleaseModeVector<WorldEventOption> options;
};
static_assert(sizeof(ActionChoicePane) == 64, "ActionChoicePane size mismatch");

struct WorldEvent : Component {
  void *renderer;                                        // 0x038
  void *panel;                                           // 0x040
  void *wheel;                                           // 0x048
  CatSelector *selector;                               // 0x050
  void *cat_animator;                                    // 0x058
  void *subject;                                         // 0x060
  GonObject *data;                                       // 0x068
  IntroPane intro_pane;                                  // 0x070
  CatChoicePane choice_pane;                             // 0x0B0
  ActionChoicePane action_pane;                          // 0x0B8
  int32_t wheel_result;                                  // 0x0F8
  uint8_t _pad0FC[4];                                    // 0x0FC
  WorldEventOption *choice;                              // 0x100
  MsvcReleaseModeWString result_text;                    // 0x108
  CatData *cat_choice;                                   // 0x128
  CatData original_catdata;                              // 0x130
  bool unlocked_alt_path;                                // 0xD88
  uint8_t _padD89[3];                                    // 0xD89
  int32_t roleplay_bonus;                                // 0xD8C
  int32_t difficulty;                                    // 0xD90
  uint8_t _padD94[4];                                    // 0xD94
  CatData debug_alloptions_cat;                          // 0xD98
  bool include_debug_cat;                                // 0x19F0
  uint8_t _pad19F1[3];                                   // 0x19F1
  int32_t force_result;                                  // 0x19F4
  bool show_all_options;                                 // 0x19F8
  bool options_screen;                                   // 0x19F9
  uint8_t _pad19FA[6];                                   // 0x19FA
  double spintimer;                                      // 0x1A00
  bool spinning;                                         // 0x1A08
  uint8_t _pad1A09[7];                                   // 0x1A09
  MsvcReleaseModeXString event_id;                       // 0x1A10
  MsvcReleaseModeXString outcome_sound;                  // 0x1A30
  int32_t max_options;                                   // 0x1A50
  uint8_t _pad1A54[4];                                   // 0x1A54
  uint8_t end_option_queue[24];                          // 0x1A58
  bool allow_skip_rewards;                               // 0x1A70
  uint8_t _pad1A71[3];                                   // 0x1A71
  int32_t bonus;                                         // 0x1A74
  MsvcReleaseModeXString reward_animation;               // 0x1A78
  bool manually_set_reward_animation;                    // 0x1A98
  bool manually_set_outcome_sound;                       // 0x1A99
  uint8_t _pad1A9A[6];                                   // 0x1A9A
  int64_t subject_initialized;                           // 0x1AA0
  double reward_animation_timer;                         // 0x1AA8
  double reward_animation_delay;                         // 0x1AB0
  double result_animation_timer;                         // 0x1AB8
  bool skip_tear;                                        // 0x1AC0
  bool mute_animations;                                  // 0x1AC1
  bool buffered_next;                                    // 0x1AC2
  bool needs_updated_appearance;                         // 0x1AC3
  bool early_exited;                                     // 0x1AC4
  uint8_t _pad1AC5[3];                                   // 0x1AC5
  uint8_t result_animation[136];                         // 0x1AC8
  uint8_t cutscene_result[192];                          // 0x1B50
  MsvcReleaseModeXString override_end_option_label;      // 0x1C10
};
static_assert(sizeof(WorldEvent) == 7216, "WorldEvent size mismatch");

struct WorldEventClickEvent {
  void *vtable;                                          // 0x00
  WorldEvent *worldEvent;                                // 0x08
  void *sender;                                          // 0x10
};
static_assert(sizeof(WorldEventClickEvent) == 24, "WorldEventClickEvent size mismatch");

struct WorldEventCatButton : Component {
  uint8_t _pad0[0x558];                                  // 0x038
  void *otherPtr;                                        // 0x590
  void *catManager;                                      // 0x598
};

struct AbilityChooser : Component {
  Scene *parent;                                         // 0x38
  void *panel;                                           // 0x40
  int32_t selected;                                      // 0x48
  uint8_t _pad4C[4];                                     // 0x4C
  MsvcReleaseModeStdFunction onchoice;                   // 0x50
  podvector<Scene *> paused_scenes;                      // 0x90
};
static_assert(sizeof(AbilityChooser) == 160, "AbilityChooser size mismatch");

struct ShopItem {
  int32_t type;                                          // 0x00
  int32_t index;                                         // 0x04
  int32_t cost;                                          // 0x08
  int32_t stock;                                         // 0x0C
  MsvcReleaseModeXString data_str;                       // 0x10
  int32_t data_int;                                      // 0x30
  uint8_t _pad34[4];                                     // 0x34
  uint8_t item[96];                                      // 0x38
  MsvcReleaseModeWString title;                          // 0x98
  bool mandatory;                                        // 0xB8
  bool allow_duplicates;                                 // 0xB9
  uint8_t _padBA[2];                                     // 0xBA
  int32_t aux;                                           // 0xBC
};
static_assert(sizeof(ShopItem) == 192, "ShopItem size mismatch");

struct Shop : Component {
  void *renderer;                                        // 0x38
  void *panel;                                           // 0x40
  void *inventory;                                       // 0x48
  GonObject *data;                                       // 0x50
  MewDirector *dir;                                      // 0x58
  MsvcReleaseModeVector<ShopItem> items;                 // 0x60
  bool cansteal;                                         // 0x78
  bool is_treasure;                                      // 0x79
  bool treasure_complete;                                // 0x7A
  bool house_shop;                                       // 0x7B
  bool allow_empty_items;                                // 0x7C
  bool clicked_chest;                                    // 0x7D
  bool tooltips_enabled;                                 // 0x7E
  uint8_t _pad7F[1];                                     // 0x7F
  double treasure_timer;                                 // 0x80
  int32_t stealcount;                                    // 0x88
  uint8_t _pad8C[4];                                     // 0x8C
  double advantage;                                      // 0x90
  int32_t pickn;                                         // 0x98
  uint8_t _pad9C[4];                                     // 0x9C
  void *wheel;                                           // 0xA0
  void *cat_animator;                                    // 0xA8
  CatData *thiefcat;                                     // 0xB0
  Button *previous_displayed_tooltip;                    // 0xB8
};
static_assert(sizeof(Shop) == 192, "Shop size mismatch");

struct Equipment {
  int64_t uid;                             // 0x00
  MsvcReleaseModeXString name;             // 0x08
  MsvcReleaseModeXString str_aux;          // 0x28 (40)
  int32_t durability;                      // 0x48 (72)
  int32_t aux;                             // 0x4C (76)
  uint32_t flags;                          // 0x50 (80)
  int32_t inventory_sortorder;             // 0x54 (84)
  int8_t quest_item_destination;           // 0x58 (88)
  int8_t _pad[3];                          // 0x59 (89)
  uint32_t condition;                      // 0x5C (92)
};
static_assert(sizeof(Equipment) == 96, "Equipment size mismatch");

struct InventoryItemBox : Component {
  InventoryScreen2 *parent;                              // 0x038
  void *renderer;                                        // 0x040
  Button *button;                                        // 0x048
  Transform *transform;                                  // 0x050
  int64_t item_id;                                       // 0x058
  int32_t slot;                                          // 0x060
  uint8_t _pad64[4];                                     // 0x064
  MsvcReleaseModeXString slotstr;                        // 0x068
  uint8_t sets[24];                                      // 0x088
  uint8_t set_bonuses[24];                               // 0x0A0
  Vec2D want_location;                                   // 0x0B8
  double want_scale;                                     // 0x0C8
  bool equipped;                                         // 0x0D0
  uint8_t _padD1[3];                                     // 0x0D1
  int32_t sort_index;                                    // 0x0D4
  uint8_t sort_info[48];                                 // 0x0D8
};
static_assert(sizeof(InventoryItemBox) == 264, "InventoryItemBox size mismatch");

struct InventoryScreen2 : Component {
  Scene *parent;                                         // 0x038
  void *panel;                                           // 0x040
  void *bg_panel;                                        // 0x048
  CatSelector *selector;                               // 0x050
  int32_t iloc;                                          // 0x058
  int32_t grid_width;                                    // 0x05C
  bool read_only;                                        // 0x060
  uint8_t _pad61[3];                                     // 0x061
  uint8_t _pad64[4];                                     // 0x064
  MsvcReleaseModeStdFunction on_close;                   // 0x068
  podvector<InventoryItemBox *> boxes;                   // 0x0A8
  podvector<void *> bg_boxes;                            // 0x0B8
  uint8_t item_equipment_status[64];                     // 0x0C8
  int64_t current_cat;                                   // 0x108
  MsvcReleaseModeXString cat_cutscene_on_close;          // 0x110
  int32_t close_cooldown;                                // 0x130
  int32_t current_sort;                                  // 0x134
};
static_assert(sizeof(InventoryScreen2) == 312, "InventoryScreen2 size mismatch");

struct ClassTagBox;

struct ClassChooser : Component {
  void *panel;                                           // 0x038
  void *bg_panel;                                        // 0x040
  CatSelector *selector;                                 // 0x048
  int32_t grid_width;                                    // 0x050
  uint8_t _pad54[4];                                     // 0x054
  MsvcReleaseModeStdFunction on_close;                   // 0x058
  podvector<ClassTagBox *> boxes;                        // 0x098
  podvector<void *> bg_boxes;                            // 0x0A8
  MsvcReleaseModeVector<CatData> cat_defaults;           // 0x0B8
  int64_t current_cat;                                   // 0x0D0
};
static_assert(sizeof(ClassChooser) == 216, "ClassChooser size mismatch");

struct ClassTagBox : Component {
  ClassChooser *parent;                                  // 0x038
  void *renderer;                                        // 0x040
  Button *button;                                        // 0x048
  Transform *transform;                                  // 0x050
  MsvcReleaseModeXString cat_class;                      // 0x058
  Vec2D want_location;                                   // 0x078
  double want_scale;                                     // 0x088
  int64_t equipped_cat_id;                               // 0x090
  bool equipped;                                         // 0x098
  uint8_t _pad99[7];                                     // 0x099
};
static_assert(sizeof(ClassTagBox) == 160, "ClassTagBox size mismatch");

