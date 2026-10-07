#include "patches.h"
#include "rt64_extended_gbi.h"
#include "libc64/malloc.h"
#include "buffers.h"
#include "gfx.h"
#include "fault.h"
#include "line_numbers.h"
#include "printf.h"
#include "regs.h"
#include "stack_pad.h"
#include "terminal.h"
#include "translation.h"
#include "audio.h"
#include "play_state.h"
#include "speed_meter.h"

// From src/code/graph.c.
#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

void Graph_InitTHGA(GraphicsContext* gfxCtx);
void Graph_TaskSet00(GraphicsContext* gfxCtx);
extern OSTime sGraphPrevUpdateEndTime;

// @recomp Patched to turn on RT64's extended display list commands each frame and tell it how often the
// game produces a frame, which is what it interpolates between at higher display rates. The PAL game
// runs its logic every R_UPDATE_RATE VIs at 50 Hz. Otherwise the original Graph_Update from
// src/code/graph.c, with the debug-only blocks compiled out as in the retail ROM.
RECOMP_PATCH void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState) {
    u32 problem;
    s32 updateRate;

    gameState->inPreNMIState = false;
    Graph_InitTHGA(gfxCtx);

    // @recomp The work list is empty after Graph_InitTHGA, so these are the first commands RT64 sees.
    updateRate = R_UPDATE_RATE;
    if (updateRate < 1) {
        updateRate = 3;
    }
    OPEN_DISPS(gfxCtx, "../graph.c", 966);
    gEXEnable(WORK_DISP++);
    gEXSetRefreshRate(WORK_DISP++, 50 / updateRate);
    CLOSE_DISPS(gfxCtx, "../graph.c", 975);

    GameState_ReqPadData(gameState);
    GameState_Update(gameState);

    // @recomp Track the pause menu for camera interpolation, and reset the camera skip state.
    camera_post_game_update(gameState);
    clear_camera_skipped();

    OPEN_DISPS(gfxCtx, "../graph.c", 999);

    gSPBranchList(WORK_DISP++, gfxCtx->polyOpaBuffer);
    gSPBranchList(POLY_OPA_DISP++, gfxCtx->polyXluBuffer);
    gSPBranchList(POLY_XLU_DISP++, gfxCtx->overlayBuffer);
    gDPPipeSync(OVERLAY_DISP++);
    gDPFullSync(OVERLAY_DISP++);
    gSPEndDisplayList(OVERLAY_DISP++);

    CLOSE_DISPS(gfxCtx, "../graph.c", 1028);

    problem = false;

    {
        GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];

        if (pool->headMagic != GFXPOOL_HEAD_MAGIC) {
            Fault_AddHungupAndCrash("../graph.c", LN4(937, 940, 951, 1067, 1070));
        }

        if (pool->tailMagic != GFXPOOL_TAIL_MAGIC) {
            problem = true;
            Fault_AddHungupAndCrash("../graph.c", LN4(943, 946, 957, 1073, 1076));
        }
    }

    if (THGA_IsCrash(&gfxCtx->polyOpa)) {
        problem = true;
    }
    if (THGA_IsCrash(&gfxCtx->polyXlu)) {
        problem = true;
    }
    if (THGA_IsCrash(&gfxCtx->overlay)) {
        problem = true;
    }

    if (!problem) {
        Graph_TaskSet00(gfxCtx);
        gfxCtx->gfxPoolIdx++;
        gfxCtx->fbIdx++;
    }

    Audio_Update();

    {
        OSTime timeNow = osGetTime();

        gRSPGfxTimeTotal = gRSPGfxTimeAcc;
        gRSPAudioTimeTotal = gRSPAudioTimeAcc;
        gRDPTimeTotal = gRDPTimeAcc;
        gRSPGfxTimeAcc = 0;
        gRSPAudioTimeAcc = 0;
        gRDPTimeAcc = 0;

        if (sGraphPrevUpdateEndTime != 0) {
            gGraphUpdatePeriod = timeNow - sGraphPrevUpdateEndTime;
        }
        sGraphPrevUpdateEndTime = timeNow;
    }
}
