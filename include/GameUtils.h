#pragma once
#include "MewgenicsTypes.h"
#include <cstdint>

#include <vector>
#include <string>

extern const std::string CUSTOM_SAVE_NAME;

namespace GameUtils {

// MewDirector singleton
void SetMewDirectorSingletonPtr(MewDirector **ptr);
MewDirector *GetMewDirectorSingleton();

typedef void* (__fastcall *MewDirector_ctor_t)(void* thisPtr);
typedef void (__fastcall *InitializeSave_t)(void* gameStateMap, void* saveNameStr);

void SetMewDirectorCtorPtr(MewDirector_ctor_t ptr);
void SetInitializeSavePtr(InitializeSave_t ptr);
MewDirector_ctor_t GetMewDirectorCtorPtr();
InitializeSave_t GetInitializeSavePtr();

typedef void (__fastcall *ContinueFile_t)(void* saveSelection, int saveSlotIndex, bool bSkipIntro);
void SetContinueFilePtr(ContinueFile_t ptr);

// ExecSQL
typedef void(__fastcall *ExecSQL_t)(void *sqlSaveFile, void *queryStr, void *stdFuncCallback);
void SetExecSQLPtr(ExecSQL_t ptr);
ExecSQL_t GetExecSQLPtr();
void ExecuteSQL(const char* query);

std::string SanitizeSQLString(const std::string& input);
void SetSaveProperty(const std::string& key, int value);

extern int g_customTeamSize;
extern int g_customDifficulty;
extern int g_departureMode; // 0 = Shared Progress Only (Intersection), 1 = All Progress Combined (Union)
extern bool g_useCustomCollarClasses;
extern std::vector<std::string> g_customCollarClasses;
void SetCustomCollarClasses(const std::string& input);
extern bool g_startCustomRunPending;
extern void* g_oldDirector;
extern bool g_isLoadingCustomCats;
extern int g_currentCustomCatIndex;

// Blob parsers and mergers
struct UnlocksData {
    uint32_t version = 3;
    std::vector<std::string> categories[7];
};
bool ParseUnlocksBlob(const std::vector<uint8_t>& blob, UnlocksData& outData);
std::vector<uint8_t> SerializeUnlocksBlob(const UnlocksData& data);
void MergeUnlocksBlobs(const glaiel::SQLSaveFile* db, const std::vector<std::vector<uint8_t>>& clientBlobs);
void MergeMapFlags(glaiel::SQLSaveFile* db, const std::vector<std::vector<std::string>>& clientFlagsList);
void MergeInventoryBlobs(const glaiel::SQLSaveFile* db, const std::vector<std::vector<uint8_t>>& clientBlobs);

typedef void (__fastcall *MewSaveFile_Load_t)(void* thisPtr, int64_t sql_id, void* catPtr);
void SetMewSaveFileLoadPtr(MewSaveFile_Load_t ptr);
MewSaveFile_Load_t GetMewSaveFileLoadPtr();

void LoadSaveFile(const char *saveName);
void CreateSaveFile(const char *saveName);
void CreateMewtiplayerSave(const char *saveName);

typedef void (__fastcall *StartRun_t)(void* mewDirector, MsvcReleaseModeXString* mapNameStr, uint32_t collarId, uint32_t teamSize, char startFlag);
void SetStartRunPtr(StartRun_t ptr);
void SetActiveScenePtr(void** ptr);
void StartCustomRun(int teamSize, int difficulty, int collarIndex);

// Scenes
std::vector<Scene *> GetCurrentScenes();
Scene *GetSceneByName(const char *name);
void SetTurnControlPtr(TurnControl **ptr);
TurnControl *GetTurnControl();
std::vector<Character *> GetAllEntities();
std::vector<Character *> GetFighters();
std::string GetAbilityName(Ability *ability);
Ability *FindCharacterAbility(const Character *actor, const std::string &targetName);

// Components
std::vector<Component *> GetSceneComponents(const Scene *scene);
std::vector<Component *> GetEntityComponents(const Entity *entity);
Component *FindComponentByTypeName(const Scene *scene, const char *typeName);
bool IsComponentValid(void *component);

// Component name lookup
bool SafeGetComponentName(const Component *p_component,
                          MsvcReleaseModeXString *out_name);

// MSVC XString Helpers
typedef void (__fastcall *DestructString_t)(MsvcReleaseModeXString* str);
void SetDestructStringPtr(DestructString_t ptr);
void InitXString(MsvcReleaseModeXString& xstr, const std::string& str);
void FreeXString(MsvcReleaseModeXString& xstr);

// Button interaction state at offset +0x2F0
enum ButtonState : int32_t {
  ButtonState_Idle = 0,
  ButtonState_Hovered = 1,
  ButtonState_Pressed = 2,
  ButtonState_Unknown = 3, // Unused?
  ButtonState_Disabled = 4,
  ButtonState_Invalid = -1,
};

// Find a single Button component by its role name
Component *FindButton(const Scene *scene, const char *roleName);

// Find ALL Button components sharing a role name (e.g. "Combat_SpellButton")
std::vector<Component *> FindAllButtons(const Scene *scene, const char *roleName);

// Read the button interaction state (+0x2F0). Returns ButtonState_Invalid on
// failure.
ButtonState GetButtonState(Component *button);

// Read the button's role name string at +0x1F8
bool GetButtonRoleName(Component *button, char *outBuf, size_t bufSize);

struct ButtonChange {
  int index;
  ButtonState oldState;
  ButtonState newState;
};

// Tracks state changes across a group of buttons sharing a role name.
// Call Init() once when combat starts, Poll() each frame.
struct ButtonGroupTracker {
  const char *roleName = nullptr;
  std::vector<Component *> buttons;
  std::vector<ButtonState> lastStates;

  void Init(const Scene *scene, const char *role);
  // Returns details for buttons whose state changed this frame.
  std::vector<ButtonChange> Poll();
  void Reset();
};

// Returns the base address of the game's TLS block.
// Assumes slot 0 for the main executable.
void *GetThreadLocalStoragePointer();

// Sets the 32-byte Xoshiro256 state at the specific TLS offset.
void SetRNGState(const void *seed32);

// Reads the current 32-byte Xoshiro256 state from the TLS.
void GetRNGState(void *outSeed32);

// Simple CRC32 implementation for data verification and signatures.
uint32_t CalculateCRC32(const void *data, size_t size);

std::vector<UIAbilitySlot *> GetUIAbilitySlots(const CombatUISlotManager *em);
} // namespace GameUtils
