#include "patches.h"
#include "transform_ids.h"
#include "skybox.h"
#include "gfx.h"
#include "gfx_setupdl.h"
#include "sys_matrix.h"

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
