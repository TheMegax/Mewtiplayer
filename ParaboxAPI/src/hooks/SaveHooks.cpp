#include "hooks/SaveHooks.h"
#include "hooks/HookMacros.h"
#include "GameUtils.h"
#include "MewSQL.h"
#include "Scanner.h"
#include "ParaboxAPI.h"

HOOK_DEFINE(CreateStrayCat, void*, void*)
HOOK_DEFINE(GetCollarVector, int64_t*, int64_t*, int64_t, int64_t, int64_t)

typedef void* (__fastcall *GameAllocate_t)(size_t size);
static GameAllocate_t g_GameAllocate = nullptr;

namespace ParaboxAPI {
    PARABOX_API void* GameAllocate(size_t size) {
        if (g_GameAllocate) return g_GameAllocate(size);
        return nullptr;
    }
}

void* __fastcall Hook_CreateStrayCat(void* catsManager) {
    void* cat = nullptr;
    if (g_origCreateStrayCat) {
        cat = g_origCreateStrayCat(catsManager);
    }
    
    ParaboxAPI::CreateStrayCatEvent ev = {};
    ev.catsManager = catsManager;
    ev.returnValue = cat;
    ParaboxAPI::OnCreateStrayCat.Publish(ev);
    
    return ev.returnValue;
}

int64_t* __fastcall Hook_GetCollarVector(int64_t* outVector, int64_t collarId, int64_t param_3, int64_t param_4) {
    ParaboxAPI::GetCollarVectorEvent ev = {};
    ev.outVector = outVector;
    ev.collarId = collarId;
    ev.param_3 = param_3;
    ev.param_4 = param_4;
    ParaboxAPI::OnGetCollarVector.Publish(ev);
    
    if (ev.cancelled)
        return ev.returnValue;

    if (g_origGetCollarVector) {
        return g_origGetCollarVector(outVector, collarId, param_3, param_4);
    }
    return outVector;
}

void SaveHooks_Init(MewjectorAPI *mj, uintptr_t gameBase) {
  GameUtils::InitializeSave_t initializeSave = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::MewSaveFile_open_or_create, initializeSave);
  if (initializeSave) {
    GameUtils::SetInitializeSavePtr(initializeSave);
  }

  auto ctorPtr = reinterpret_cast<GameUtils::MewDirector_ctor_t>(gameBase + GameSymbols::MewDirector_MewDirector);
  GameUtils::SetMewDirectorCtorPtr(ctorPtr);

  MewSQL::ExecSQL_t execSql = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::SQLSaveFile_SQL, execSql);
  if (execSql) {
    MewSQL::SetExecSQLPtr(execSql);
    GameUtils::SetExecSQLPtr((GameUtils::ExecSQL_t)execSql);
  }

  MewSQL::SQLSaveFile_open_t sqlOpen = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::SQLSaveFile_open, sqlOpen);
  if (sqlOpen) MewSQL::SetOpenPtr(sqlOpen);

  MewSQL::Retrieve_t sqlRetrieve = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::SQLSaveFile_Retrieve, sqlRetrieve);
  if (sqlRetrieve) MewSQL::SetRetrievePtr(sqlRetrieve);

  MewSQL::CloseConnection_t sqlClose = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::sqlite3Close, sqlClose);
  if (sqlClose) MewSQL::SetCloseConnectionPtr(sqlClose);

  GameUtils::DestructString_t destructStr = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::string_Tidy_deallocate, destructStr);
  if (destructStr) {
    MewSQL::SetDestructStringPtr(destructStr);
    GameUtils::SetDestructStringPtr(destructStr);
  }

  MewSQL::sqlite3Prepare_t sqlite3Prep = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::sqlite3LockAndPrepare, sqlite3Prep);
  if (sqlite3Prep) MewSQL::SetSqlite3PreparePtr(sqlite3Prep);

  MewSQL::sqlite3_step_t sqlite3Step = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::sqlite3_step, sqlite3Step);
  if (sqlite3Step) MewSQL::SetSqlite3StepPtr(sqlite3Step);

  MewSQL::sqlite3_column_text_t sqlite3ColText = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::sqlite3_column_text, sqlite3ColText);
  if (sqlite3ColText) MewSQL::SetSqlite3ColumnTextPtr(sqlite3ColText);

  MewSQL::sqlite3_column_bytes_t sqlite3ColBytes = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::sqlite3_column_bytes, sqlite3ColBytes);
  if (sqlite3ColBytes) MewSQL::SetSqlite3ColumnBytesPtr(sqlite3ColBytes);

  MewSQL::sqlite3_finalize_t sqlite3Finalize = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::sqlite3_finalize, sqlite3Finalize);
  if (sqlite3Finalize) MewSQL::SetSqlite3FinalizePtr(sqlite3Finalize);

  MsvcReleaseModeXString *baseSavePath = nullptr;
  RESOLVE_DATA(gameBase, GameSymbols::base_save_path, baseSavePath);
  if (baseSavePath) MewSQL::SetBaseSavePathPtr(baseSavePath);

  GameUtils::MewSaveFile_Load_t mewSaveFileLoad = nullptr;
  RESOLVE_FUNC(gameBase, GameSymbols::MewSaveFile_Load, mewSaveFileLoad);
  if (mewSaveFileLoad) GameUtils::SetMewSaveFileLoadPtr(mewSaveFileLoad);

  HOOK_INSTALL(mj, gameBase, CreateStrayCat, GameSymbols::CatDatabase_generate_and_add_cat, 15);

  RESOLVE_FUNC(gameBase, GameSymbols::std_Allocate_16, g_GameAllocate);

  HOOK_INSTALL(mj, gameBase, GetCollarVector, GameSymbols::GetCollars, 16);
}
