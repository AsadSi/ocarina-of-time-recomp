#include "modding.h"
#include "ultra64.h"
#include "save.h"

// Audit aid: map select's debug save has finished the Forest, Fire and Water Temples but not seen burning Kakariko,
// so entering Kakariko as adult plays Sheik's Nocturne cutscene (a conditional trigger with a destination command).
RECOMP_HOOK_RETURN("Sram_InitDebugSave") void test_kakariko_on_init_debug_save(void) {
    SET_EVENTCHKINF(EVENTCHKINF_48);
    SET_EVENTCHKINF(EVENTCHKINF_49);
    SET_EVENTCHKINF(EVENTCHKINF_4A);
    CLEAR_EVENTCHKINF(EVENTCHKINF_AA);
    gSaveContext.save.info.inventory.questItems &= ~(1 << QUEST_SONG_NOCTURNE);
}
