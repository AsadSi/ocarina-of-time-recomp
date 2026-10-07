#include "patches.h"
#include "misc_funcs.h"
#include "gfx.h"
#include "pause.h"
#include "play_state.h"
#include "player.h"
#include "save.h"
#include "versions.h"

// Autosave: with the setting on, every two minutes of safe gameplay the game writes the same save the pause
// menu does, to the file being played. As with a save from the pause menu, loading it starts Link at his
// house, the Temple of Time or the current dungeon's entrance. Zelda64Recomp's icon shows each autosave.

#define AUTOSAVE_INTERVAL_MS (2 * 60 * 1000)
// Frames the game must have been safe to save in a row, so a save never lands on the edge of an event.
#define AUTOSAVE_READY_FRAMES 20

#define AUTOSAVE_ICON_FADE_FRAMES 5
#define AUTOSAVE_ICON_SHOW_FRAMES 30
#define AUTOSAVE_ICON_TOTAL_FRAMES (AUTOSAVE_ICON_FADE_FRAMES + AUTOSAVE_ICON_SHOW_FRAMES + AUTOSAVE_ICON_FADE_FRAMES)
#define AUTOSAVE_ICON_WIDTH 24
#define AUTOSAVE_ICON_HEIGHT 16
#define AUTOSAVE_ICON_DRAW_WIDTH (AUTOSAVE_ICON_WIDTH * 3 / 4)
#define AUTOSAVE_ICON_DRAW_HEIGHT (AUTOSAVE_ICON_HEIGHT * 3 / 4)

INCBIN(sAutosaveIconTex, "autosave.rgba32.bin");

// Microseconds since start, from the host; unsigned differences stay right across its wraparound.
static u32 sLastAutosaveTime = 0;
static u32 sPrevFrameTime = 0;
static int sTimerStarted = false;
static s32 sReadyFrames = 0;
static s32 sIconTimer = 0;

void Play_SaveSceneFlags(PlayState* this);
void Sram_WriteSave(SramContext* sramCtx);

// Whether the player could pause right now (the conditions in KaleidoSetup_Update), with no text on screen,
// the full HUD up, no timer running and Link alive.
static int can_autosave(PlayState* play) {
    Player* player = GET_PLAYER(play);

    return !IS_PAUSED(&play->pauseCtx) &&
           (play->gameOverCtx.state == GAMEOVER_INACTIVE) && (play->transitionTrigger == TRANS_TRIGGER_OFF) &&
           (play->transitionMode == TRANS_MODE_OFF) && (gSaveContext.save.cutsceneIndex < CS_INDEX_0) &&
           (gSaveContext.nextCutsceneIndex < CS_INDEX_0) && !Play_InCsMode(play) &&
           (play->shootingGalleryStatus <= 1) && (gSaveContext.magicState != MAGIC_STATE_STEP_CAPACITY) &&
           (gSaveContext.magicState != MAGIC_STATE_FILL) &&
           ((play->sceneId != SCENE_BOMBCHU_BOWLING_ALLEY) || !Flags_GetSwitch(play, 0x38)) &&
           (play->msgCtx.msgMode == MSGMODE_NONE) && (gSaveContext.hudVisibilityMode == HUD_VISIBILITY_ALL) &&
           (gSaveContext.timerState == TIMER_STATE_OFF) && (gSaveContext.subTimerState == SUBTIMER_STATE_OFF) &&
           (gSaveContext.save.info.playerData.health > 0) && !(player->stateFlags1 & PLAYER_STATE1_DEAD);
}

// Called once per frame after the play state updates.
void autosave_post_play_update(PlayState* play) {
    u32 now = recomp_time_us();

    u32 prevFrameTime = sPrevFrameTime;

    sPrevFrameTime = now;

    // While autosave is off, no save file is being played (the title screen's demo, map select), or play just
    // resumed from another game state (file select), keep the timer fresh so the next autosave is a full
    // interval of play away.
    if (!recomp_get_autosave_enabled() || (gSaveContext.fileNum < 0) || (gSaveContext.fileNum > 2) ||
        !sTimerStarted || ((now - prevFrameTime) > 1000000U)) {
        sLastAutosaveTime = now;
        sTimerStarted = true;
        sReadyFrames = 0;
        return;
    }

    sReadyFrames = can_autosave(play) ? (sReadyFrames + 1) : 0;
    if ((sReadyFrames >= AUTOSAVE_READY_FRAMES) &&
        ((now - sLastAutosaveTime) > 1000U * AUTOSAVE_INTERVAL_MS)) {
        // What the pause menu's save does (KaleidoScope_Update in ovl_kaleido_scope).
        Play_SaveSceneFlags(play);
        gSaveContext.save.info.playerData.savedSceneId = play->sceneId;
        Sram_WriteSave(&play->sramCtx);

        sLastAutosaveTime = now;
        sIconTimer = AUTOSAVE_ICON_TOTAL_FRAMES;
    }
}

// Draws the autosave icon in the bottom right corner while it fades in, shows and fades out.
void autosave_draw_icon(PlayState* play) {
    s32 alpha;
    s32 x = -AUTOSAVE_ICON_DRAW_WIDTH - 4;
    s32 y = SCREEN_HEIGHT - AUTOSAVE_ICON_DRAW_HEIGHT - 4;
    u16 dsdx = (1024 * AUTOSAVE_ICON_WIDTH) / AUTOSAVE_ICON_DRAW_WIDTH;
    u16 dtdy = (1024 * AUTOSAVE_ICON_HEIGHT) / AUTOSAVE_ICON_DRAW_HEIGHT;

    if (sIconTimer <= 0) {
        return;
    }
    if (sIconTimer > AUTOSAVE_ICON_SHOW_FRAMES + AUTOSAVE_ICON_FADE_FRAMES) {
        alpha = 255 * (AUTOSAVE_ICON_TOTAL_FRAMES - sIconTimer) / AUTOSAVE_ICON_FADE_FRAMES;
    } else if (sIconTimer > AUTOSAVE_ICON_FADE_FRAMES) {
        alpha = 255;
    } else {
        alpha = 255 * sIconTimer / AUTOSAVE_ICON_FADE_FRAMES;
    }
    sIconTimer--;

    OPEN_DISPS(play->state.gfxCtx, "autosave.c", 0);

    gDPPipeSync(OVERLAY_DISP++);
    gDPSetOtherMode(OVERLAY_DISP++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gEXForceUpscale2D(OVERLAY_DISP++, 1);
    gDPLoadTextureBlock(OVERLAY_DISP++, sAutosaveIconTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, AUTOSAVE_ICON_WIDTH,
                        AUTOSAVE_ICON_HEIGHT, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                        G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    // Drop shadow, then the icon, anchored to the right edge of the screen.
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, (alpha > 100) ? 100 : alpha);
    gEXTextureRectangle(OVERLAY_DISP++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, (x + 2) * 4, (y + 2) * 4,
                        (x + AUTOSAVE_ICON_DRAW_WIDTH + 2) * 4, (y + AUTOSAVE_ICON_DRAW_HEIGHT + 2) * 4,
                        G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, alpha);
    gEXTextureRectangle(OVERLAY_DISP++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, x * 4, y * 4,
                        (x + AUTOSAVE_ICON_DRAW_WIDTH) * 4, (y + AUTOSAVE_ICON_DRAW_HEIGHT) * 4, G_TX_RENDERTILE, 0,
                        0, dsdx, dtdy);
    gEXForceUpscale2D(OVERLAY_DISP++, 0);

    CLOSE_DISPS(play->state.gfxCtx, "autosave.c", 0);
}
