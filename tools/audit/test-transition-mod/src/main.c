#include "modding.h"
#include "ultra64.h"
#include "save.h"
#include "transition_circle.h"
#include "transition.h"

// Audit aid: every scene load opens with the circle (child) or the wipe (adult), so routes can see both
// in widescreen without hunting for an exit that uses them.
RECOMP_HOOK("Play_Init") void test_transition_on_play_init(void* play) {
    gSaveContext.nextTransitionType = (gSaveContext.save.linkAge == LINK_AGE_CHILD)
                                          ? TRANS_TYPE_CIRCLE(TCA_NORMAL, TCC_BLACK, TCS_SLOW)
                                          : TRANS_TYPE_WIPE;
}
