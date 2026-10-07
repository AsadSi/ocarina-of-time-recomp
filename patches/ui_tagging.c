#include "patches.h"
#include "transform_ids.h"
#include "gfx.h"
#include "buffers.h"
#include "letterbox.h"
#include "main.h"
#include "message.h"
#include "message_data_static.h"
#include "play_state.h"
#include "regs.h"
#include "sys_matrix.h"
#include "pause.h"
#include "view.h"
#include "assets/textures/parameter_static/parameter_static.h"

// Frame interpolation for 2D elements that move or resize: the text box and the letterbox. The game
// draws them as rectangles, which RT64 can't interpolate, so as in Zelda64Recomp they are drawn as two
// triangles under a tagged orthographic projection and a tagged matrix that places and sizes them.

static void set_translate_scale(Mtx* mtx, f32 scaleX, f32 scaleY, f32 x, f32 y) {
    MtxF mf;

    mf.xx = scaleX; mf.yx = 0.0f;   mf.zx = 0.0f; mf.wx = 0.0f;
    mf.xy = 0.0f;   mf.yy = scaleY; mf.zy = 0.0f; mf.wy = 0.0f;
    mf.xz = 0.0f;   mf.yz = 0.0f;   mf.zz = 1.0f; mf.wz = 0.0f;
    mf.xw = x;      mf.yw = y;      mf.zw = 0.0f; mf.ww = 1.0f;
    guMtxF2L(mf.mf, mtx);
}

// Saves the render state, then tags and loads a screen-space orthographic projection over a full-screen
// viewport. `left`..`bottom` are the projection's bounds in screen pixels.
static Gfx* begin_ortho_2d(Gfx* gfx, GraphicsContext* gfxCtx, u32 projId, u32 modelId, f32 left, f32 right,
                           f32 bottom, f32 top) {
    Mtx* proj = GRAPH_ALLOC(gfxCtx, sizeof(Mtx));
    Vp* vp = GRAPH_ALLOC(gfxCtx, sizeof(Vp));

    gEXPushProjectionMatrix(gfx++);
    gEXPushGeometryMode(gfx++);
    gEXPushOtherMode(gfx++);
    gEXPushCombineMode(gfx++);
    gEXPushViewport(gfx++);
    gEXMatrixGroupSimpleTC(gfx++, projId, G_EX_PUSH, G_MTX_PROJECTION, G_EX_COMPONENT_INTERPOLATE,
                           G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                           G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_INTERPOLATE,
                           G_EX_COMPONENT_INTERPOLATE);
    gEXMatrixGroupSimpleTC(gfx++, modelId, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_COMPONENT_INTERPOLATE,
                           G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                           G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_INTERPOLATE,
                           G_EX_COMPONENT_INTERPOLATE);

    vp->vp.vscale[0] = vp->vp.vtrans[0] = (gScreenWidth / 2) << 2;
    vp->vp.vscale[1] = vp->vp.vtrans[1] = (gScreenHeight / 2) << 2;
    vp->vp.vscale[2] = G_MAXZ;
    vp->vp.vtrans[2] = 0;
    vp->vp.vscale[3] = vp->vp.vtrans[3] = 0;
    gSPViewport(gfx++, vp);

    guOrtho(proj, left, right, bottom, top, -1.0f, 1.0f, 1.0f);
    gSPMatrix(gfx++, proj, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPLoadGeometryMode(gfx++, 0);
    return gfx;
}

static Gfx* end_ortho_2d(Gfx* gfx) {
    gEXPopProjectionMatrix(gfx++);
    gEXPopGeometryMode(gfx++);
    gEXPopOtherMode(gfx++);
    gEXPopCombineMode(gfx++);
    gEXPopViewport(gfx++);
    gEXPopMatrixGroup(gfx++, G_MTX_MODELVIEW);
    gEXPopMatrixGroup(gfx++, G_MTX_PROJECTION);
    return gfx;
}

#define TEXTBOX_TEX_WIDTH 256
#define TEXTBOX_TEX_HEIGHT 64

// @recomp Patched to draw the box as a tagged, scaled quad instead of a texture rectangle, so it grows
// smoothly when it opens. The rectangle always shows the box's whole 256x64 texture (128 wide, mirrored),
// so the quad is that size, centred on the rectangle and scaled to it.
// Original: Message_DrawTextBox in src/code/z_message.c.
RECOMP_PATCH void Message_DrawTextBox(PlayState* play, Gfx** p) {
    static Vtx sTextboxVtx[4] = {
        VTX(-TEXTBOX_TEX_WIDTH / 2, -TEXTBOX_TEX_HEIGHT / 2, 0, 0, 0, 0, 0, 0, 255),
        VTX(TEXTBOX_TEX_WIDTH / 2, -TEXTBOX_TEX_HEIGHT / 2, 0, TEXTBOX_TEX_WIDTH << 5, 0, 0, 0, 0, 255),
        VTX(-TEXTBOX_TEX_WIDTH / 2, TEXTBOX_TEX_HEIGHT / 2, 0, 0, TEXTBOX_TEX_HEIGHT << 5, 0, 0, 0, 255),
        VTX(TEXTBOX_TEX_WIDTH / 2, TEXTBOX_TEX_HEIGHT / 2, 0, TEXTBOX_TEX_WIDTH << 5, TEXTBOX_TEX_HEIGHT << 5, 0, 0, 0,
            255),
    };
    MessageContext* msgCtx = &play->msgCtx;
    Gfx* gfx = *p;
    Mtx* model;

    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, msgCtx->textboxColorRed, msgCtx->textboxColorGreen, msgCtx->textboxColorBlue,
                    msgCtx->textboxColorAlphaCurrent);

    if (!msgCtx->textBoxType /* TEXTBOX_TYPE_BLACK */ || msgCtx->textBoxType == TEXTBOX_TYPE_BLUE) {
        gDPLoadTextureBlock_4b(gfx++, msgCtx->textboxSegment, G_IM_FMT_I, 128, 64, 0, G_TX_MIRROR, G_TX_NOMIRROR, 7, 0,
                               G_TX_NOLOD, G_TX_NOLOD);
    } else {
        if (msgCtx->textBoxType == TEXTBOX_TYPE_OCARINA) {
            gDPSetEnvColor(gfx++, 0, 0, 0, 255);
        } else {
            gDPSetEnvColor(gfx++, 50, 20, 0, 255);
        }

        gDPLoadTextureBlock_4b(gfx++, msgCtx->textboxSegment, G_IM_FMT_IA, 128, 64, 0, G_TX_MIRROR, G_TX_MIRROR, 7, 0,
                               G_TX_NOLOD, G_TX_NOLOD);
    }

    // @recomp Draw the quad in screen pixels, sized and placed by a tagged matrix.
    gfx = begin_ortho_2d(gfx, play->state.gfxCtx, TEXTBOX_PROJECTION_TRANSFORM_ID, TEXTBOX_TRANSFORM_ID, 0.0f,
                         gScreenWidth, gScreenHeight, 0.0f);
    model = GRAPH_ALLOC(play->state.gfxCtx, sizeof(Mtx));
    set_translate_scale(model, R_TEXTBOX_WIDTH / (f32)TEXTBOX_TEX_WIDTH, R_TEXTBOX_HEIGHT / (f32)TEXTBOX_TEX_HEIGHT,
                        R_TEXTBOX_X + R_TEXTBOX_WIDTH / 2.0f, R_TEXTBOX_Y + R_TEXTBOX_HEIGHT / 2.0f);
    gSPMatrix(gfx++, model, G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPVertex(gfx++, sTextboxVtx, 4, 0);
    // The texture doesn't suit bilinear filtering across a scaled quad; triangles need perspective correction.
    gDPSetTextureFilter(gfx++, G_TF_POINT);
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    gSP2Triangles(gfx++, 0, 1, 3, 0, 0, 3, 2, 0);
    gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    gfx = end_ortho_2d(gfx);

    // Draw treble clef
    if (msgCtx->textBoxType == TEXTBOX_TYPE_OCARINA) {
        gDPPipeSync(gfx++);
        gDPSetCombineLERP(gfx++, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, 1, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE,
                          0);
        gDPSetPrimColor(gfx++, 0, 0, 255, 100, 0, 255);
        gDPLoadTextureBlock_4b(gfx++, gOcarinaTrebleClefTex, G_IM_FMT_I, 16, 32, 0, G_TX_MIRROR, G_TX_MIRROR,
                               G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPTextureRectangle(gfx++, R_TEXTBOX_CLEF_XPOS << 2, R_TEXTBOX_CLEF_YPOS << 2, (R_TEXTBOX_CLEF_XPOS + 16) << 2,
                            (R_TEXTBOX_CLEF_YPOS + 32) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    }

    *p = gfx;
}

// @recomp Patched to leave the letterbox out of the scissor: the scene draws to the full view and
// Gfx_SetupFrame covers it with interpolated bars instead. Original: View_ApplyLetterbox in src/code/z_view.c.
RECOMP_PATCH void View_ApplyLetterbox(View* view) {
    GraphicsContext* gfxCtx = view->gfxCtx;

    OPEN_DISPS(gfxCtx, "../z_view.c", 459);

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetScissor(POLY_OPA_DISP++, G_SC_NON_INTERLACE, view->viewport.leftX, view->viewport.topY,
                  view->viewport.rightX, view->viewport.bottomY);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetScissor(POLY_XLU_DISP++, G_SC_NON_INTERLACE, view->viewport.leftX, view->viewport.topY,
                  view->viewport.rightX, view->viewport.bottomY);

    CLOSE_DISPS(gfxCtx, "../z_view.c", 472);
}

extern Gfx sFillSetupDL[];

// @recomp Patched to clear the whole screen and draw the letterbox as two tagged quads that slide in and
// out smoothly. They are drawn every frame, even at size 0 (off screen), so their first frame on screen
// already has a previous frame to interpolate from. The quads reach far past the sides to cover any
// widescreen width. Original: Gfx_SetupFrame in src/code/z_rcp.c, minus its debug-only register overrides.
RECOMP_PATCH void Gfx_SetupFrame(GraphicsContext* gfxCtx, u8 r, u8 g, u8 b) {
    static Vtx sLetterboxVtx[4] = {
        VTX(-32000, -SCREEN_HEIGHT / 2, 0, 0, 0, 0, 0, 0, 255),
        VTX(32000, -SCREEN_HEIGHT / 2, 0, 0, 0, 0, 0, 0, 255),
        VTX(-32000, 0, 0, 0, 0, 0, 0, 0, 255),
        VTX(32000, 0, 0, 0, 0, 0, 0, 0, 255),
    };

    OPEN_DISPS(gfxCtx, "../z_rcp.c", 2386);

    // Set up the RDP render state for rectangles in FILL mode
    gSPDisplayList(POLY_OPA_DISP++, sFillSetupDL);
    gSPDisplayList(POLY_XLU_DISP++, sFillSetupDL);
    gSPDisplayList(OVERLAY_DISP++, sFillSetupDL);

    // Set the scissor region to the full screen
    gDPSetScissor(POLY_OPA_DISP++, G_SC_NON_INTERLACE, 0, 0, gScreenWidth, gScreenHeight);
    gDPSetScissor(POLY_XLU_DISP++, G_SC_NON_INTERLACE, 0, 0, gScreenWidth, gScreenHeight);
    gDPSetScissor(OVERLAY_DISP++, G_SC_NON_INTERLACE, 0, 0, gScreenWidth, gScreenHeight);

    // Set up the framebuffer, primitives will be drawn here
    gDPSetColorImage(POLY_OPA_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, gScreenWidth, gfxCtx->curFrameBuffer);
    gDPSetColorImage(POLY_OPA_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, gScreenWidth, gfxCtx->curFrameBuffer);
    gDPSetColorImage(POLY_XLU_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, gScreenWidth, gfxCtx->curFrameBuffer);
    gDPSetColorImage(OVERLAY_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, gScreenWidth, gfxCtx->curFrameBuffer);

    // Set up the z-buffer
    gDPSetDepthImage(POLY_OPA_DISP++, gZBuffer);
    gDPSetDepthImage(POLY_XLU_DISP++, gZBuffer);
    gDPSetDepthImage(OVERLAY_DISP++, gZBuffer);

    if ((R_PAUSE_BG_PRERENDER_STATE <= PAUSE_BG_PRERENDER_SETUP) && (gTransitionTileState <= TRANS_TILE_SETUP)) {
        s32 letterboxSize = Letterbox_GetSize();
        Gfx* gfx;
        Mtx* top;
        Mtx* bottom;

        // Set the whole z buffer to maximum depth
        gDPSetColorImage(POLY_OPA_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, gScreenWidth, gZBuffer);
        gDPSetCycleType(POLY_OPA_DISP++, G_CYC_FILL);
        gDPSetRenderMode(POLY_OPA_DISP++, G_RM_NOOP, G_RM_NOOP2);
        gDPSetFillColor(POLY_OPA_DISP++, (GPACK_ZDZ(G_MAXFBZ, 0) << 16) | GPACK_ZDZ(G_MAXFBZ, 0));
        gDPFillRectangle(POLY_OPA_DISP++, 0, 0, gScreenWidth - 1, gScreenHeight - 1);
        gDPPipeSync(POLY_OPA_DISP++);

        // Fill the whole screen with the base color
        gDPSetColorImage(POLY_OPA_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, gScreenWidth, gfxCtx->curFrameBuffer);
        gDPSetCycleType(POLY_OPA_DISP++, G_CYC_FILL);
        gDPSetRenderMode(POLY_OPA_DISP++, G_RM_NOOP, G_RM_NOOP2);
        gDPSetFillColor(POLY_OPA_DISP++, (GPACK_RGBA5551(r, g, b, 1) << 16) | GPACK_RGBA5551(r, g, b, 1));
        gDPFillRectangle(POLY_OPA_DISP++, 0, 0, gScreenWidth - 1, gScreenHeight - 1);
        gDPPipeSync(POLY_OPA_DISP++);

        // @recomp Draw the letterbox (in the screen base color) as quads whose inner edge sits letterboxSize
        // pixels in from the top and bottom, in a projection centred on the screen.
        if (letterboxSize < 0) {
            letterboxSize = 0;
        }
        if (letterboxSize > SCREEN_HEIGHT / 2) {
            letterboxSize = SCREEN_HEIGHT / 2;
        }
        gfx = OVERLAY_DISP;
        gDPPipeSync(gfx++);
        gfx = begin_ortho_2d(gfx, gfxCtx, LETTERBOX_PROJECTION_TRANSFORM_ID, LETTERBOX_TRANSFORM_ID,
                             -SCREEN_WIDTH / 2, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, -SCREEN_HEIGHT / 2);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gDPSetRenderMode(gfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
        gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetPrimColor(gfx++, 0, 0, r, g, b, 255);
        top = GRAPH_ALLOC(gfxCtx, sizeof(Mtx));
        bottom = GRAPH_ALLOC(gfxCtx, sizeof(Mtx));
        set_translate_scale(top, 1.0f, 1.0f, 0.0f, -SCREEN_HEIGHT / 2 + letterboxSize);
        set_translate_scale(bottom, 1.0f, -1.0f, 0.0f, SCREEN_HEIGHT / 2 - letterboxSize);
        gSPMatrix(gfx++, top, G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(gfx++, sLetterboxVtx, 4, 0);
        gSP2Triangles(gfx++, 0, 1, 3, 0, 0, 3, 2, 0);
        gSPMatrix(gfx++, bottom, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(gfx++, sLetterboxVtx, 4, 0);
        gSP2Triangles(gfx++, 0, 3, 1, 0, 0, 2, 3, 0);
        gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
        gfx = end_ortho_2d(gfx);
        gDPPipeSync(gfx++);
        OVERLAY_DISP = gfx;
    }

    CLOSE_DISPS(gfxCtx, "../z_rcp.c", 2497);
}
