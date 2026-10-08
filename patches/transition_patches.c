#include "patches.h"
#include "graphics.h"
#include "gfx.h"
#include "transition_circle.h"

extern Gfx sTransCircleDL[];

// @recomp Patched to scale the circle by the aspect ratio, so it still covers the sides of a widescreen
// picture (Zelda64Recomp does the same to MM's TransitionWipe3_Draw, which this is).
// Original: TransitionCircle_Draw in src/code/z_fbdemo_circle.c.
RECOMP_PATCH void TransitionCircle_Draw(void* thisx, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    Mtx* modelView;
    TransitionCircle* this = (TransitionCircle*)thisx;
    Gfx* texScroll;
    f32 scale = 14.8f;

    // @recomp
    float original_aspect_ratio = ((float)SCREEN_WIDTH) / ((float)SCREEN_HEIGHT);
    scale *= recomp_get_target_aspect_ratio(original_aspect_ratio) / original_aspect_ratio;

    modelView = this->modelView[this->frame];

    this->frame ^= 1;
    gDPPipeSync(gfx++);
    texScroll = Gfx_BranchTexScroll(&gfx, this->texX, this->texY, 16, 64);
    gSPSegment(gfx++, 9, texScroll);
    gSPSegment(gfx++, 8, this->texture);
    gDPSetColor(gfx++, G_SETPRIMCOLOR, this->color.rgba);
    gDPSetColor(gfx++, G_SETENVCOLOR, this->color.rgba);
    gSPMatrix(gfx++, &this->projection, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPPerspNormalize(gfx++, this->normal);
    gSPMatrix(gfx++, &this->lookAt, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);

    guScale(&modelView[0], scale, scale, 1.0f);
    gSPMatrix(gfx++, &modelView[0], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    gSPDisplayList(gfx++, sTransCircleDL);
    gDPPipeSync(gfx++);
    *gfxP = gfx;
}
