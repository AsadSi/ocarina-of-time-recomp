#include "patches.h"
#include "alignment.h"
#include "animation.h"
#include "dma.h"
#include "object.h"
#include "pause.h"
#include "play_state.h"
#include "player.h"
#include "save.h"
#include "sched.h"
#include "segmented_address.h"
#include "assets/objects/gameplay_keep/player_anim_headers.h"

// Opening and closing the pause menu (and the game over screen, which reuses its drawing) DMAs over
// object memory without waiting for the RCP: the menu copies gameplay_keep and Link's object over
// the live gameplay_keep, and closing it reloads every object. The previous frame's display list
// still points into that memory. On console the RSP has always read it by then; RT64 runs on a host
// thread and may still be walking it, follows an overwritten G_DL into zeros, and runs off the end
// of RDRAM. Both patches flush the scheduler first, as Play_Draw already does before it reads the
// pause backdrop back. Otherwise they are the originals.

// @recomp Patched to wait for the RCP before overwriting gameplay_keep.
// Original: Player_InitPauseDrawData in src/code/z_player_lib.c.
RECOMP_PATCH u32 Player_InitPauseDrawData(PlayState* play, u8* segment, SkelAnime* skelAnime) {
    s16 linkObjectId = gLinkObjectIds[(void)0, gSaveContext.save.linkAge];
    u32 size;
    void* ptr;

    // @recomp Let the renderer finish the frame that still draws from gameplay_keep.
    Sched_FlushTaskQueue();

    size = gObjectTable[OBJECT_GAMEPLAY_KEEP].vromEnd - gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart;
    ptr = PAUSE_PLAYER_SEGMENT_GAMEPLAY_KEEP_START(segment);
    DmaMgr_RequestSync(ptr, gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart, size);

    size = gObjectTable[linkObjectId].vromEnd - gObjectTable[linkObjectId].vromStart;
    ptr = PAUSE_PLAYER_SEGMENT_LINK_OBJECT(segment);
    DmaMgr_RequestSync(ptr, gObjectTable[linkObjectId].vromStart, size);

    // Joint tables are placed after the link object
    ptr = (void*)ALIGN16((uintptr_t)ptr + size);

    gSegments[4] = OS_K0_TO_PHYSICAL(PAUSE_PLAYER_SEGMENT_GAMEPLAY_KEEP_START(segment));
    gSegments[6] = OS_K0_TO_PHYSICAL(PAUSE_PLAYER_SEGMENT_LINK_OBJECT(segment));

    SkelAnime_InitLink(play, skelAnime, gPlayerSkelHeaders[(void)0, gSaveContext.save.linkAge],
                       &gPlayerAnim_link_normal_wait, 9, ptr, ptr, PLAYER_LIMB_MAX);

    return PAUSE_PLAYER_SEGMENT_TOTAL_SIZE(size);
}

// @recomp Patched to wait for the RCP before reloading every object over the pause menu's copy.
// Original: Object_ReloadAll in src/code/z_scene.c.
RECOMP_PATCH void Object_ReloadAll(ObjectContext* objectCtx) {
    s32 i;
    s32 id;
    u32 size;

    // @recomp Let the renderer finish the menu's last frame, which draws from this memory.
    Sched_FlushTaskQueue();

    for (i = 0; i < objectCtx->numEntries; i++) {
        id = objectCtx->slots[i].id;
        size = gObjectTable[id].vromEnd - gObjectTable[id].vromStart;
        DmaMgr_RequestSync(objectCtx->slots[i].segment, gObjectTable[id].vromStart, size);
    }
}
