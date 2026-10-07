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
#include "thga.h"
#include "sys_cfb.h"

// From src/code/graph.c.
#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

// Four times the game's own display list buffers, to make room for RT64's interpolation tags.
// They live in the patch memory above the console's 8 MB, which gEXSetRDRAMExtended lets RT64 read.
typedef struct {
    Gfx polyOpaBuffer[0x17E0 * 4];
    Gfx polyXluBuffer[0x800 * 4];
    Gfx overlayBuffer[0x400 * 4];
    Gfx workBuffer[0x80 * 4];
} BiggerGfxPool;

static BiggerGfxPool sBiggerGfxPools[2];

// @recomp Patched to use the bigger display list buffers. Original: Graph_InitTHGA in src/code/graph.c.
RECOMP_PATCH void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];
    BiggerGfxPool* bigger = &sBiggerGfxPools[gfxCtx->gfxPoolIdx & 1];

    pool->headMagic = GFXPOOL_HEAD_MAGIC;
    pool->tailMagic = GFXPOOL_TAIL_MAGIC;
    THGA_Init(&gfxCtx->polyOpa, bigger->polyOpaBuffer, sizeof(bigger->polyOpaBuffer));
    THGA_Init(&gfxCtx->polyXlu, bigger->polyXluBuffer, sizeof(bigger->polyXluBuffer));
    THGA_Init(&gfxCtx->overlay, bigger->overlayBuffer, sizeof(bigger->overlayBuffer));
    THGA_Init(&gfxCtx->work, bigger->workBuffer, sizeof(bigger->workBuffer));

    gfxCtx->polyOpaBuffer = bigger->polyOpaBuffer;
    gfxCtx->polyXluBuffer = bigger->polyXluBuffer;
    gfxCtx->overlayBuffer = bigger->overlayBuffer;
    gfxCtx->workBuffer = bigger->workBuffer;

    gfxCtx->curFrameBuffer = SysCfb_GetFbPtr(gfxCtx->fbIdx % 2);
    gfxCtx->unk_014 = 0;
}
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
    gEXSetRDRAMExtended(WORK_DISP++, 1);
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
