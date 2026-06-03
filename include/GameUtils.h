#pragma once
#include "MewgenicsTypes.h"
#include <cstdint>

#include <vector>
#include <string>

namespace GameUtils {

// MewDirector singleton
void SetMewDirectorSingletonPtr(MewDirector **ptr);
MewDirector *GetMewDirectorSingleton();

typedef void (__fastcall *ContinueFile_t)(void* saveSelection, int saveSlotIndex, bool bSkipIntro);
void SetContinueFilePtr(ContinueFile_t ptr);

// ExecSQL
typedef void(__fastcall *ExecSQL_t)(void *sqlSaveFile, void *queryStr, void *stdFuncCallback);
void SetExecSQLPtr(ExecSQL_t ptr);
ExecSQL_t GetExecSQLPtr();
void ExecuteSQL(const char* query);

std::string SanitizeSQLString(const std::string& input);
void SetSaveProperty(const std::string& key, int value);

extern bool g_injectCustomSaveData;
extern int g_customTeamSize;
extern int g_customDifficulty;
extern int g_customCollarIndex;
extern bool g_startCustomRunPending;

void LoadSaveFile(const char *saveName);

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
std::vector<Component *> GetEntityComponents(Entity *entity);
Component *FindComponentByTypeName(const Scene *scene, const char *typeName);

// Component name lookup
bool SafeGetComponentName(const Component *p_component,
                          MsvcReleaseModeXString *out_name);

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

// Resolves a GridNode pointer for specific coordinates by getting the
// TacticsTile component.
void *ResolveGridTile(int x, int y);

std::vector<UIAbilitySlot *> GetUIAbilitySlots(CombatUISlotManager *em);
} // namespace GameUtils
