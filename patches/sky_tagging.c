#include "patches.h"
#include "transform_ids.h"
#include "skybox.h"
#include "gfx.h"
#include "gfx_setupdl.h"
#include "sys_matrix.h"
#include "environment.h"
#include "cutscene.h"
#include "play_state.h"
#include "gfx_setupdl.h"
#include "save.h"
#include "z_lib.h"
#include "assets/objects/gameplay_keep/moon.h"
#include "assets/objects/gameplay_keep/gameplay_keep_0x4D160.h"
#include "assets/objects/gameplay_field_keep/gameplay_field_keep.h"

extern Mtx* sSkyboxDrawMatrix;

// @recomp Patched to tag the skybox's matrix for interpolation, skipping it when the camera cut.
// Original: Skybox_Draw in src/code/z_vr_box_draw.c.
RECOMP_PATCH void Skybox_Draw(SkyboxContext* skyboxCtx, GraphicsContext* gfxCtx, s16 skyboxId, s16 blend, f32 x, f32 y, f32 z) {
    OPEN_DISPS(gfxCtx, "../z_vr_box_draw.c", 52);

    Gfx_SetupDL_40Opa(gfxCtx);

    gSPSegment(POLY_OPA_DISP++, 0x7, skyboxCtx->staticSegments[0]);
    gSPSegment(POLY_OPA_DISP++, 0x8, skyboxCtx->staticSegments[1]);
    gSPSegment(POLY_OPA_DISP++, 0x9, skyboxCtx->palettes);

    gDPSetPrimColor(POLY_OPA_DISP++, 0x00, 0x00, 0, 0, 0, blend);
    gSPTexture(POLY_OPA_DISP++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);

    sSkyboxDrawMatrix = GRAPH_ALLOC(gfxCtx, sizeof(Mtx));
    Matrix_Translate(x, y, z, MTXMODE_NEW);
    Matrix_Scale(1.0f, 1.0f, 1.0f, MTXMODE_APPLY);
    Matrix_RotateX(skyboxCtx->rot.x, MTXMODE_APPLY);
    Matrix_RotateY(skyboxCtx->rot.y, MTXMODE_APPLY);
    Matrix_RotateZ(skyboxCtx->rot.z, MTXMODE_APPLY);
    MATRIX_TO_MTX(sSkyboxDrawMatrix, "../z_vr_box_draw.c", 76);
    gSPMatrix(POLY_OPA_DISP++, sSkyboxDrawMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // @recomp Tag the skybox's matrix.
    if (camera_was_skipped()) {
        gEXMatrixGroupDecomposedSkipAll(POLY_OPA_DISP++, SKYBOX_TRANSFORM_ID_START, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    } else {
        gEXMatrixGroupDecomposedNormal(POLY_OPA_DISP++, SKYBOX_TRANSFORM_ID_START, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    }

    gDPSetColorDither(POLY_OPA_DISP++, G_CD_MAGICSQ);
    gDPSetTextureFilter(POLY_OPA_DISP++, G_TF_BILERP);
    gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[0]);
    gDPSetTextureLUT(POLY_OPA_DISP++, G_TT_RGBA16);
    gDPSetTextureConvert(POLY_OPA_DISP++, G_TC_FILT);

    if (skyboxCtx->drawType != SKYBOX_DRAW_128) {
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[0]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[1]);

        gDPPipeSync(POLY_OPA_DISP++);
        gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[1]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[2]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[3]);

        if (skyboxId != SKYBOX_BAZAAR) {
            if (skyboxId < SKYBOX_KOKIRI_SHOP || skyboxId > SKYBOX_BOMBCHU_SHOP) {
                gDPPipeSync(POLY_OPA_DISP++);
                gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[2]);
                gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[4]);
                gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[5]);

                gDPPipeSync(POLY_OPA_DISP++);

                if (skyboxCtx->drawType != SKYBOX_DRAW_256_3FACE) {
                    gDPLoadTLUT_pal256(POLY_OPA_DISP++, skyboxCtx->palettes[3]);
                    gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[6]);
                    gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[7]);
                }
            }
        }
    } else {
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[0]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[2]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[4]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[6]);
        gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[8]);
        if (skyboxId == SKYBOX_CUTSCENE_MAP) {
            gSPDisplayList(POLY_OPA_DISP++, skyboxCtx->dListBuf[10]);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);

    // @recomp Pop the skybox's matrix tag.
    gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);

    CLOSE_DISPS(gfxCtx, "../z_vr_box_draw.c", 125);
}

// Interpolated or snapped with the camera: the sun, the moon and the sun's lens flare are placed relative
// to the camera, so after a cut they must not sweep across the sky from the old view.
static Gfx* sky_tag_push(Gfx* gfx, u32 id) {
    if (camera_was_skipped()) {
        gEXMatrixGroupDecomposedSkipAll(gfx++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    } else {
        gEXMatrixGroupDecomposedNormal(gfx++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    }
    return gfx;
}

// @recomp Patched to tag the sun and the moon. Original: Environment_DrawSunAndMoon in src/code/z_kankyo.c.
RECOMP_PATCH void Environment_DrawSunAndMoon(PlayState* play) {
    f32 alpha;
    f32 color;
    f32 y;
    f32 scale;
    f32 temp;

    OPEN_DISPS(play->state.gfxCtx, "../z_kankyo.c", 2266);

    if (play->csCtx.state != CS_STATE_IDLE) {
        Math_SmoothStepToF(&play->envCtx.sunPos.x,
                           -(Math_SinS(((void)0, gSaveContext.save.dayTime) - CLOCK_TIME(12, 0)) * 120.0f) * 25.0f,
                           1.0f, 0.8f, 0.8f);
        Math_SmoothStepToF(&play->envCtx.sunPos.y,
                           (Math_CosS(((void)0, gSaveContext.save.dayTime) - CLOCK_TIME(12, 0)) * 120.0f) * 25.0f, 1.0f,
                           0.8f, 0.8f);
        //! @bug This should be z.
        Math_SmoothStepToF(&play->envCtx.sunPos.y,
                           (Math_CosS(((void)0, gSaveContext.save.dayTime) - CLOCK_TIME(12, 0)) * 20.0f) * 25.0f, 1.0f,
                           0.8f, 0.8f);
    } else {
        play->envCtx.sunPos.x = -(Math_SinS(((void)0, gSaveContext.save.dayTime) - CLOCK_TIME(12, 0)) * 120.0f) * 25.0f;
        play->envCtx.sunPos.y = +(Math_CosS(((void)0, gSaveContext.save.dayTime) - CLOCK_TIME(12, 0)) * 120.0f) * 25.0f;
        play->envCtx.sunPos.z = +(Math_CosS(((void)0, gSaveContext.save.dayTime) - CLOCK_TIME(12, 0)) * 20.0f) * 25.0f;
    }

    if (gSaveContext.save.entranceIndex != ENTR_HYRULE_FIELD_0 || ((void)0, gSaveContext.sceneLayer) != 5) {
        Matrix_Translate(play->view.eye.x + play->envCtx.sunPos.x, play->view.eye.y + play->envCtx.sunPos.y,
                         play->view.eye.z + play->envCtx.sunPos.z, MTXMODE_NEW);

        y = play->envCtx.sunPos.y / 25.0f;
        temp = y / 80.0f;

        alpha = temp * 255.0f;
        if (alpha < 0.0f) {
            alpha = 0.0f;
        }
        if (alpha > 255.0f) {
            alpha = 255.0f;
        }

        alpha = 255.0f - alpha;

        color = temp;
        if (color < 0.0f) {
            color = 0.0f;
        }

        if (color > 1.0f) {
            color = 1.0f;
        }

        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, (u8)(color * 75.0f) + 180, (u8)(color * 155.0f) + 100, 255);
        gDPSetEnvColor(POLY_OPA_DISP++, 255, (u8)(color * 255.0f), (u8)(color * 255.0f), alpha);

        scale = (color * 2.0f) + 10.0f;
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        // @recomp Tag the sun.
        POLY_OPA_DISP = sky_tag_push(POLY_OPA_DISP, SUN_TRANSFORM_ID);
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_kankyo.c", 2364);
        Gfx_SetupDL_54Opa(play->state.gfxCtx);
        gSPDisplayList(POLY_OPA_DISP++, gSunDL);
        gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);

        Matrix_Translate(play->view.eye.x - play->envCtx.sunPos.x, play->view.eye.y - play->envCtx.sunPos.y,
                         play->view.eye.z - play->envCtx.sunPos.z, MTXMODE_NEW);

        color = -y / 120.0f;
        color = CLAMP_MIN(color, 0.0f);

        scale = -15.0f * color + 25.0f;
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);

        temp = -y / 80.0f;
        temp = CLAMP_MAX(temp, 1.0f);

        alpha = temp * 255.0f;

        if (alpha > 0.0f) {
            // @recomp Tag the moon.
            POLY_OPA_DISP = sky_tag_push(POLY_OPA_DISP, MOON_TRANSFORM_ID);
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_kankyo.c", 2406);
            Gfx_SetupDL_51Opa(play->state.gfxCtx);
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 240, 255, 180, alpha);
            gDPSetEnvColor(POLY_OPA_DISP++, 80, 70, 20, alpha);
            gSPDisplayList(POLY_OPA_DISP++, gMoonDL);
            gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx, "../z_kankyo.c", 2429);
}

// @recomp Patched to tag the sun's lens flare, all of its elements under one ID in drawing order.
// Original: Environment_DrawSunLensFlare in src/code/z_kankyo.c.
RECOMP_PATCH void Environment_DrawSunLensFlare(PlayState* play, EnvironmentContext* envCtx, View* view,
                                               GraphicsContext* gfxCtx, Vec3f pos, s32 arg5) {
    if ((play->envCtx.precipitation[PRECIP_RAIN_CUR] == 0) && (play->envCtx.skyboxConfig == 0)) {
        OPEN_DISPS(play->state.gfxCtx, "../z_kankyo.c", 0);
        POLY_XLU_DISP = sky_tag_push(POLY_XLU_DISP, SUN_LENS_FLARE_TRANSFORM_ID);
        CLOSE_DISPS(play->state.gfxCtx, "../z_kankyo.c", 0);

        Environment_DrawLensFlare(play, &play->envCtx, &play->view, play->state.gfxCtx, pos, 2000, 370,
                                  Math_CosS(((void)0, gSaveContext.save.dayTime) - CLOCK_TIME(12, 0)) * 120.0f, 400,
                                  true);

        OPEN_DISPS(play->state.gfxCtx, "../z_kankyo.c", 0);
        gEXPopMatrixGroup(POLY_XLU_DISP++, G_MTX_MODELVIEW);
        CLOSE_DISPS(play->state.gfxCtx, "../z_kankyo.c", 0);
    }
}
