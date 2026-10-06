#pragma once

namespace GameSymbols {
    // Binary Version: 1.1.21239 (Image Base: 0x140000000)

    // --- Application & Misc ---
    constexpr uintptr_t ApplicationBase_consume_time    = 0x009BAD00;
    constexpr uintptr_t TrollEngine_TriggerSaveScum     = 0x008E1610;
    constexpr uintptr_t PauseMenuScene_pause            = 0x002896D0;
    constexpr uintptr_t SaveSelection_ContinueFile      = 0x001BD8B0;
    constexpr uintptr_t MewDirector_GoOnAnAdventure     = 0x003B01C0;
    constexpr uintptr_t Director_DestroyScene           = 0x009D4E00;
    constexpr uintptr_t MewDirector_instance            = 0x013DAC30;
    constexpr uintptr_t Camera_main                     = 0x00978330;
    constexpr uintptr_t Scene_current                   = 0x013C18F0;

    // --- Combat ---
    constexpr uintptr_t Character_BeginTurn             = 0x0010A9B0;
    constexpr uintptr_t Level_update                    = 0x003611E0;
    constexpr uintptr_t Ability_trigger                 = 0x000320A0;
    constexpr uintptr_t TurnControl_QueueAction         = 0x008E3870;
    constexpr uintptr_t TurnControl_NextTurn            = 0x008E3C60;
    constexpr uintptr_t Character_Face                  = 0x0010CE50;
    constexpr uintptr_t Ability_ComputeX                = 0x00044450;
    constexpr uintptr_t PlayerBrain_OnRequestAction     = 0x007789C0;
    constexpr uintptr_t MountBrain_OnRequestAction      = 0x0077C950;
    constexpr uintptr_t Character_CompleteAbilityNow    = 0x00108310;
    constexpr uintptr_t CombatMenu_show                 = 0x002AEB60;
    constexpr uintptr_t CombatMenu_hide                 = 0x002B32C0;
    constexpr uintptr_t Passive_DisplayText             = 0x00768F30;
    constexpr uintptr_t Passive_TickSound               = 0x0076AA80;

    // --- Adventure Box ---
    constexpr uintptr_t ButchBox_init                   = 0x000AD740;
    constexpr uintptr_t ButchBox_depart                 = 0x000ADBF0;
    constexpr uintptr_t ButchBox_late_update            = 0x000AE9D0;
    constexpr uintptr_t ButchBox_try_place_cat          = 0x000AED00;
    constexpr uintptr_t ButchBox_try_remove_cat         = 0x000AF960;
    constexpr uintptr_t ButchBox_depart_transition      = 0x000AE360;
    constexpr uintptr_t CatDatabase_get_cat             = 0x000D7220;
    constexpr uintptr_t free_base                       = 0x00D6D140;
    constexpr uintptr_t acrt_heap                       = 0x013BBC00;

    // --- Class Chooser ---
    constexpr uintptr_t CatSelector_init                = 0x000DE040;
    constexpr uintptr_t CatSelector_RefreshAll          = 0x000DF2E0;
    constexpr uintptr_t ClassTagBox_click               = 0x001405A0;
    constexpr uintptr_t ClassChooser_init_embark        = 0x00141E40;
    constexpr uintptr_t CatData_set_class_preview       = 0x000BDC70;
    constexpr uintptr_t ClassChooser_refresh_item_locations = 0x0013EF10;

    // --- Storage ---
    constexpr uintptr_t InventoryItemBox_click          = 0x0034EB20;
    constexpr uintptr_t InventoryItemBox_click_equip    = 0x0034F0A0;
    constexpr uintptr_t InventoryScreen2_close          = 0x0034D870;
    constexpr uintptr_t InventoryScreen2_refresh_item_locations = 0x0034BE50;
    constexpr uintptr_t Director_AddScene               = 0x009D4950;
    constexpr uintptr_t Scene_AddComponent              = 0x0096B340;

    // --- Map ---
    constexpr uintptr_t MapScreen_EnterNode             = 0x00391C20;
    constexpr uintptr_t MapNode_click_action            = 0x00228700;
    constexpr uintptr_t MapScreen_open_inventory        = 0x0039A340;

    // --- Level Up ---
    constexpr uintptr_t LevelUpScreen_init              = 0x0037A290;
    constexpr uintptr_t LevelUpScreen_select_option     = 0x003835D0;
    constexpr uintptr_t LevelUpScreen_Reroll            = 0x00383FE0;
    constexpr uintptr_t AbilityChooser_init             = 0x00052B50;
    constexpr uintptr_t AbilityChooser_close            = 0x00053D70;

    // --- World Event ---
    constexpr uintptr_t WorldEvent_init                 = 0x00913A90;
    constexpr uintptr_t WorldEvent_setupActionChoice_action = 0x0093C1C0;
    constexpr uintptr_t WorldEvent_setupCatChoice_action = 0x0093C220;
    constexpr uintptr_t WorldEvent_setupEndButton_action1 = 0x0093CB10;
    constexpr uintptr_t WorldEvent_setupEndButton_action2 = 0x0093CC40;
    constexpr uintptr_t WorldEvent_push_end_option      = 0x0091E290;

    // --- Popup ---
    constexpr uintptr_t YesNoPrompt_init                = 0x0077D240;
    constexpr uintptr_t YesNoPrompt_Open                = 0x0077DD30;
    constexpr uintptr_t YesNoPrompt_OpenOk              = 0x0077DE30;

    // --- Shop ---
    constexpr uintptr_t Shop_random_cat                 = 0x000AB770;
    constexpr uintptr_t Shop_init                       = 0x00794CF0;
    constexpr uintptr_t Shop_buy_item_action            = 0x0079A3F0;
    constexpr uintptr_t Shop_exit_action                = 0x0079BFC0;
    constexpr uintptr_t Shop_chest_click_action         = 0x0079C0E0;
    constexpr uintptr_t Shop_levelup_action             = 0x0079C8B0;
    constexpr uintptr_t Shop_update_transition_call     = 0x007985C3;
    constexpr uintptr_t Shop_ItemOutTransition          = 0x0079C410;

    // --- Act Selection ---
    constexpr uintptr_t ActSelectionScreen_init         = 0x00067CD0;
    constexpr uintptr_t ActSelectionScreen_SelectAct    = 0x00069A70;

    // --- Save ---
    constexpr uintptr_t CatDatabase_generate_and_add_cat = 0x000D7160;
    constexpr uintptr_t GetCollars                      = 0x003AF990;
    constexpr uintptr_t MewSaveFile_open_or_create      = 0x00229CD0;
    constexpr uintptr_t MewSaveFile_Load                = 0x00230060;
    constexpr uintptr_t MewDirector_MewDirector         = 0x001BE7F0;
    constexpr uintptr_t SQLSaveFile_SQL                 = 0x00A0CEB0;
    constexpr uintptr_t SQLSaveFile_open                = 0x00A0D3C0;
    constexpr uintptr_t SQLSaveFile_Retrieve            = 0x00A0ED10;
    constexpr uintptr_t sqlite3Close                    = 0x00B45280;
    constexpr uintptr_t string_Tidy_deallocate          = 0x00052780;
    constexpr uintptr_t std_Allocate_16                 = 0x00052A40;
    constexpr uintptr_t sqlite3LockAndPrepare           = 0x00B0AAC0;
    constexpr uintptr_t sqlite3_step                    = 0x00AC0A50;
    constexpr uintptr_t sqlite3_column_text             = 0x00AC19B0;
    constexpr uintptr_t sqlite3_column_bytes            = 0x00AC1510;
    constexpr uintptr_t sqlite3_finalize                = 0x00ABF980;
    constexpr uintptr_t base_save_path                  = 0x013C58F0;
}
