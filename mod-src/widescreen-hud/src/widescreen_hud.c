#include "modding.h"
#include "ultra64.h"
#include "gfx.h"
#include "play_state.h"
#include "rt64_extended_gbi.h"

// Anchors HUD elements to the screen edges, following Zelda64Recomp's Interface_Draw patch. Interface_Draw
// draws the HUD as a sequence of helper calls, each called only from there, so hooks around them set
// and reset RT64's rect alignment without replacing any of the game's code. Coordinates stay those of
// the 4:3 screen, measured from the chosen edge; `MARGIN` pulls elements that many pixels further out.

#define MARGIN 8

// Re-applies the HUD's orthographic view, so a new viewport alignment takes effect. Unnamed in the decomp.
void func_8008A994(InterfaceContext* interfaceCtx);

static PlayState* sPlay;

static void set_align(u32 origin, s32 x_offset, s32 y_offset) {
    GraphicsContext* gfxCtx = sPlay->state.gfxCtx;
    OPEN_DISPS(gfxCtx, "widescreen_hud.c", 0);
    gEXSetRectAlign(OVERLAY_DISP++, origin, origin, x_offset * 4, y_offset * 4, x_offset * 4, y_offset * 4);
    gEXSetViewportAlign(OVERLAY_DISP++, origin, x_offset * 4, y_offset * 4);
    CLOSE_DISPS(gfxCtx, "widescreen_hud.c", 0);
    func_8008A994(&sPlay->interfaceCtx);
}

static void anchor_left(s32 y_offset) {
    set_align(G_EX_ORIGIN_LEFT, -MARGIN, y_offset);
}

static void anchor_right(s32 y_offset) {
    set_align(G_EX_ORIGIN_RIGHT, -(SCREEN_WIDTH - MARGIN), y_offset);
}

static void anchor_none(void) {
    set_align(G_EX_ORIGIN_NONE, 0, 0);
}

// Widen the HUD's scissor to the whole screen, or elements moved past the 4:3 area are clipped.
RECOMP_HOOK("Interface_Draw") void on_interface_draw(PlayState* play) {
    sPlay = play;
    OPEN_DISPS(play->state.gfxCtx, "widescreen_hud.c", 0);
    gEXSetScissorAlign(OVERLAY_DISP++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, -MARGIN, -SCREEN_WIDTH, MARGIN, 0, 0,
                       SCREEN_WIDTH, SCREEN_HEIGHT);
    CLOSE_DISPS(play->state.gfxCtx, "widescreen_hud.c", 0);
}

// Hearts, top left.
RECOMP_HOOK("Health_DrawMeter") void on_health_draw(PlayState* play) {
    anchor_left(-MARGIN);
}

// Rupees and small keys are drawn inline right after the hearts, bottom left.
RECOMP_HOOK_RETURN("Health_DrawMeter") void after_health_draw(void) {
    anchor_left(MARGIN);
}

// Magic meter, top left under the hearts.
RECOMP_HOOK("Magic_DrawMeter") void on_magic_draw(PlayState* play) {
    anchor_left(-MARGIN);
}

// Minimap, bottom right.
RECOMP_HOOK("Minimap_Draw") void on_minimap_draw(PlayState* play) {
    anchor_right(MARGIN);
}

// The Z-target reticle drawn next points at the world, so it must not be anchored.
RECOMP_HOOK_RETURN("Minimap_Draw") void after_minimap_draw(void) {
    anchor_none();
}

// B and C buttons, their items and ammo counts, then the A button, top right.
RECOMP_HOOK("Interface_DrawItemButtons") void on_item_buttons_draw(PlayState* play) {
    anchor_right(-MARGIN);
}

// The A button's label is the last right-hand element; exactly one call draws it each frame.
RECOMP_HOOK("Interface_DrawActionLabel") void on_action_label_draw(GraphicsContext* gfxCtx, void* texture) {
    anchor_right(-MARGIN);
}

RECOMP_HOOK_RETURN("Interface_DrawActionLabel") void after_action_label_draw(void) {
    anchor_none();
}

// Never leave an alignment active past the HUD, whichever of the above ran.
RECOMP_HOOK_RETURN("Interface_Draw") void after_interface_draw(void) {
    anchor_none();
    OPEN_DISPS(sPlay->state.gfxCtx, "widescreen_hud.c", 0);
    gEXSetScissorAlign(OVERLAY_DISP++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    CLOSE_DISPS(sPlay->state.gfxCtx, "widescreen_hud.c", 0);
}
