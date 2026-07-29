#include "events/SaveSubscribers.h"
#include "ParaboxAPI.h"
#include "NetworkManager.h"
#include "Overlay.h"
#include "GameUtils.h"
#include "MewSQL.h"
#include "MewgenicsTypes.h"

// SaveHooks state
std::map<int64_t, uint64_t> g_catIdToOwnerSteamID;

bool g_isHandlingNetworkMapInventoryOpen = false;

// ---------------------------------------------------------------------------
// SaveHooks Subscribers
// ---------------------------------------------------------------------------
void RegisterSaveSubscribers() {
    ParaboxAPI::OnCreateStrayCat.Subscribe([](ParaboxAPI::CreateStrayCatEvent& ev) {
        void* cat = ev.returnValue;
        if (GameUtils::g_isLoadingCustomCats && cat) {
            const int index = GameUtils::g_currentCustomCatIndex;
            Overlay::Log("[SAVE] Hooked CreateStrayCat, loading custom cat at index %d from %s...", index, "mewtiplayer.sav");

            if (glaiel::SQLSaveFile* dbFile = MewSQL::OpenSaveDatabase("mewtiplayer.sav")) {
                if (GameUtils::MewSaveFile_Load_t mewSaveFileLoad = GameUtils::GetMewSaveFileLoadPtr()) {
                    MewSaveFile dummySave = {};
                    dummySave.sqlFile = *dbFile;

                    const auto catIdPtr = &((PersistentCharacter*)cat)->catID;
                    const int64_t originalID = *catIdPtr;

                    mewSaveFileLoad(&dummySave, index, cat);
                    *catIdPtr = originalID;

                    // sql_key is now populated from the deserialized blob.
                    const int64_t sqlKey = ((PersistentCharacter*)cat)->sql_key;
                    const int64_t catID  = ((PersistentCharacter*)cat)->catID;

                    const int slot = GameUtils::g_currentCustomCatIndex; // 1, 2, ...
                    const auto [ownerSteamID, catAge] = MewSQL::ReadCatOwnershipEntry(dbFile, slot);

                    if (ownerSteamID != 0) {
                        g_catIdToOwnerSteamID[catID] = ownerSteamID;
                        NetworkManager::Get().GetOwnershipMap()[sqlKey] = ownerSteamID;
                        Overlay::Log("[SAVE] CreateStrayCat: OK slot=%d sql_key=%lld -> owner=%llu age=%d",
                                     slot, sqlKey, ownerSteamID, catAge);
                    } else {
                        Overlay::Log("[SAVE] [WARN] CreateStrayCat: No owner for slot %d (sql_key=%lld)", slot, sqlKey);
                    }

                    std::string narrowNameStr = ((PersistentCharacter*)cat)->name.to_utf8();
                    NetworkManager::Get().RegisterCat(sqlKey, narrowNameStr.c_str(), ((PersistentCharacter*)cat)->className.begin());

                    if (catAge > 0) {
                        const MewDirector* dir = GameUtils::GetMewDirectorSingleton();
                        const int32_t currentDayVal = dir ? dir->currentDay : 1;
                        ((PersistentCharacter*)cat)->birthDay = currentDayVal - catAge;
                        Overlay::Log("[SAVE] Restored age %d for sql_key=%lld", catAge, sqlKey);
                    }
                }
                MewSQL::CloseSaveDatabase(dbFile);
            }
            GameUtils::g_currentCustomCatIndex++;
        }
    });

    ParaboxAPI::OnGetCollarVector.Subscribe([](ParaboxAPI::GetCollarVectorEvent& ev) {
        if (!GameUtils::g_useCustomCollarClasses || GameUtils::g_customCollarClasses.empty()) {
            return;
        }

        Overlay::Log("[SAVE] Populating custom collar vector with %zu classes...", GameUtils::g_customCollarClasses.size());

        const size_t count = GameUtils::g_customCollarClasses.size();
        const size_t bytesToAllocate = count * sizeof(MsvcReleaseModeXString);

        ev.outVector[0] = 0;
        ev.outVector[1] = 0;
        ev.outVector[2] = 0;

        if (auto* array = (MsvcReleaseModeXString*)ParaboxAPI::GameAllocate(bytesToAllocate)) {
            memset(array, 0, bytesToAllocate);
            for (size_t i = 0; i < count; ++i) {
                GameUtils::InitXString(array[i], GameUtils::g_customCollarClasses[i].c_str());
            }
            ev.outVector[0] = (int64_t)array;
            ev.outVector[1] = (int64_t)(array + count);
            ev.outVector[2] = (int64_t)(array + count);
            ev.returnValue = ev.outVector;
            ev.Cancel(); // Prevent calling original
        }
    });
}
