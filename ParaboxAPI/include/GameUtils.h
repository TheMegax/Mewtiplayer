#pragma once
#include "ParaboxAPI.h"
#include "MewgenicsTypes.h"
#include <cstdint>
#include "ParaboxArray.h"

extern const ParaboxAPI::String CUSTOM_SAVE_NAME;

namespace GameUtils {

// MewDirector singleton
PARABOX_API void SetMewDirectorSingletonPtr(MewDirector **ptr);
PARABOX_API MewDirector *GetMewDirectorSingleton();

typedef void* (__fastcall *MewDirector_ctor_t)(void* thisPtr);
typedef void (__fastcall *InitializeSave_t)(void* gameStateMap, void* saveNameStr);

PARABOX_API void SetMewDirectorCtorPtr(MewDirector_ctor_t ptr);
PARABOX_API void SetInitializeSavePtr(InitializeSave_t ptr);
PARABOX_API MewDirector_ctor_t GetMewDirectorCtorPtr();
PARABOX_API InitializeSave_t GetInitializeSavePtr();

typedef void (__fastcall *ContinueFile_t)(void* saveSelection, int saveSlotIndex, bool bSkipIntro);
PARABOX_API void SetContinueFilePtr(ContinueFile_t ptr);

// ExecSQL
typedef void(__fastcall *ExecSQL_t)(void *sqlSaveFile, void *queryStr, void *stdFuncCallback);
PARABOX_API void SetExecSQLPtr(ExecSQL_t ptr);
PARABOX_API ExecSQL_t GetExecSQLPtr();
PARABOX_API void ExecuteSQL(const char* query);

PARABOX_API ParaboxAPI::String SanitizeSQLString(const char* input);
PARABOX_API void SetSaveProperty(const char* key, int value);

extern PARABOX_API int g_customTeamSize;
extern PARABOX_API int g_customDifficulty;
extern PARABOX_API int g_departureMode; // 0 = Shared Progress Only (Intersection), 1 = All Progress Combined (Union)
extern PARABOX_API bool g_useCustomCollarClasses;
extern PARABOX_API ParaboxAPI::Array<ParaboxAPI::String> g_customCollarClasses;
PARABOX_API void SetCustomCollarClasses(const char* input);
extern PARABOX_API bool g_startCustomRunPending;
extern PARABOX_API void* g_oldDirector;
extern PARABOX_API bool g_isLoadingCustomCats;
extern PARABOX_API int g_currentCustomCatIndex;

// Blob parsers and mergers
struct UnlocksData {
    uint32_t version = 3;
    ParaboxAPI::Array<ParaboxAPI::String> categories[7];
};
PARABOX_API bool ParseUnlocksBlob(const ParaboxAPI::Array<uint8_t>& blob, UnlocksData& outData);
PARABOX_API ParaboxAPI::Array<uint8_t> SerializeUnlocksBlob(const UnlocksData& data);
PARABOX_API void MergeUnlocksBlobs(const glaiel::SQLSaveFile* db, const ParaboxAPI::Array<ParaboxAPI::Array<uint8_t>>& clientBlobs);
PARABOX_API void MergeMapFlags(glaiel::SQLSaveFile* db, const ParaboxAPI::Array<ParaboxAPI::Array<ParaboxAPI::String>>& clientFlagsList);
PARABOX_API void MergeInventoryBlobs(const glaiel::SQLSaveFile* db, const ParaboxAPI::Array<ParaboxAPI::Array<uint8_t>>& clientBlobs);

typedef void (__fastcall *MewSaveFile_Load_t)(void* thisPtr, int64_t sql_id, void* catPtr);
PARABOX_API void SetMewSaveFileLoadPtr(MewSaveFile_Load_t ptr);
PARABOX_API MewSaveFile_Load_t GetMewSaveFileLoadPtr();

PARABOX_API void LoadSaveFile(const char *saveName);
PARABOX_API void CreateSaveFile(const char *saveName);
PARABOX_API void CreateMewtiplayerSave(const char *saveName);

typedef void (__fastcall *StartRun_t)(void* mewDirector, MsvcReleaseModeXString* mapNameStr, uint32_t collarId, uint32_t teamSize, char startFlag);
PARABOX_API void SetStartRunPtr(StartRun_t ptr);
PARABOX_API void SetActiveScenePtr(void** ptr);
PARABOX_API void StartCustomRun(int teamSize, int difficulty, int collarIndex);

// Scenes
PARABOX_API ParaboxAPI::Array<Scene *> GetCurrentScenes();
PARABOX_API Scene *GetSceneByName(const char *name);
typedef void (__fastcall *Director_DestroyScene_t)(Director* director, MsvcReleaseModeXString* sceneNameStr);
PARABOX_API void SetDestroyScenePtr(Director_DestroyScene_t ptr);
PARABOX_API void DestroyScene(const char *sceneName);
PARABOX_API void SetTurnControlPtr(TurnControl **ptr);
PARABOX_API TurnControl *GetTurnControl();
PARABOX_API ParaboxAPI::Array<Character *> GetAllEntities();
PARABOX_API ParaboxAPI::Array<Character *> GetFighters();
PARABOX_API ParaboxAPI::String GetAbilityName(Ability *ability);
PARABOX_API Ability *FindCharacterAbility(const Character *actor, const char *targetName);
PARABOX_API Component *FindCharacterPassive(const Character *actor, const char *targetName);

// Components
PARABOX_API ParaboxAPI::Array<Component *> GetSceneComponents(const Scene *scene);
PARABOX_API ParaboxAPI::Array<Component *> GetEntityComponents(const Entity *entity);
PARABOX_API Component *FindComponentByTypeName(const Scene *scene, const char *typeName);
PARABOX_API bool IsComponentValid(const void *component);

// Component name lookup
PARABOX_API bool SafeGetComponentName(const Component *p_component,
                                      MsvcReleaseModeXString *out_name);

// MSVC XString Helpers
typedef void (__fastcall *DestructString_t)(MsvcReleaseModeXString* str);
PARABOX_API void SetDestructStringPtr(DestructString_t ptr);
PARABOX_API void InitXString(MsvcReleaseModeXString& xstr, const char* str);
PARABOX_API void FreeXString(MsvcReleaseModeXString& xstr);
PARABOX_API void InitWString(MsvcReleaseModeWString& wstr, const wchar_t* str);
PARABOX_API void FreeWString(MsvcReleaseModeWString& wstr);

// Returns the base address of the game's TLS block.
// Assumes slot 0 for the main executable.
PARABOX_API void *GetThreadLocalStoragePointer();

// Sets the 32-byte Xoshiro256 state at the specific TLS offset.
PARABOX_API void SetRNGState(const void *seed32);

// Reads the current 32-byte Xoshiro256 state from the TLS.
PARABOX_API void GetRNGState(void *outSeed32);

// Simple CRC32 implementation for data verification and signatures.
PARABOX_API uint32_t CalculateCRC32(const void *data, size_t size);

PARABOX_API ParaboxAPI::Array<UIAbilitySlot *> GetUIAbilitySlots(const CombatUISlotManager *em);

} // namespace GameUtils
