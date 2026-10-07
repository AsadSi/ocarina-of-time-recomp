#include "modding.h"
#include "ultra64.h"
#include "save.h"

// Audit aid: map select's debug save also owns Epona, so routes can call and ride her as adult Link.
RECOMP_HOOK_RETURN("Sram_InitDebugSave") void test_epona_on_init_debug_save(void) {
    SET_EVENTCHKINF(EVENTCHKINF_EPONA_OBTAINED);
}
