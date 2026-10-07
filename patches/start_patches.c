#include "patches.h"
#include "misc_funcs.h"
#include "game.h"
#include "setup_state.h"
#include "console_logo_state.h"
#include "map_select_state.h"
#include "save.h"

void SaveContext_Init(void);
void ConsoleLogo_Init_NORELOCATE(GameState* thisx);
void MapSelect_Init_NORELOCATE(GameState* thisx);

// @recomp Patched to boot into the developer map select instead of the intro when the launcher asks
// for it. Original: Setup_InitImpl in src/code/title_setup.c.
RECOMP_PATCH void Setup_InitImpl(SetupState* this) {
    SaveContext_Init();
    this->state.running = false;

    if (recomp_get_start_map_select()) {
        // No file loaded, so map select creates its debug save instead of using an empty one.
        gSaveContext.fileNum = 0xFF;
        SET_NEXT_GAMESTATE(&this->state, MapSelect_Init_NORELOCATE, MapSelectState);
    } else {
        SET_NEXT_GAMESTATE(&this->state, ConsoleLogo_Init_NORELOCATE, ConsoleLogoState);
    }
}
