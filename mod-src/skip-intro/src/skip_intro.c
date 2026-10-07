#include "modding.h"
#include "recomputils.h"
#include "ultra64.h"
#include "save.h"
#include "sram.h"
#include "scene.h"

// A new file opens with the intro cutscene queued (cutscene index 0xFFF1 at Link's house).
// Clearing it starts the game in Link's house straight away, the way Nintendo's debug save does.
RECOMP_HOOK_RETURN("Sram_OpenSave") void skip_intro_on_open_save(void) {
    if (gSaveContext.save.cutsceneIndex == CS_INDEX_1 && gSaveContext.save.entranceIndex == ENTR_LINKS_HOUSE_0) {
        gSaveContext.save.cutsceneIndex = CS_INDEX_NONE;
        recomp_printf("Skip Intro: starting in Link's house\n");
    }
}
