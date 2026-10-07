#ifndef __PATCHES_H__
#define __PATCHES_H__

#define RECOMP_EXPORT __attribute__((section(".recomp_export")))
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH __attribute__((section(".recomp_force_patch")))
#define RECOMP_DECLARE_EVENT(func) \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"") \
    __attribute__((noinline, weak, used, section(".recomp_event"))) void func {} \
    _Pragma("GCC diagnostic pop")

// The runtime implements these natively; patches call them through the addresses in syms.ld.
#define osWritebackDCache osWritebackDCache_recomp
#define osInvalICache osInvalICache_recomp
#define bzero bzero_recomp

#include "ultra64.h"
#include "segment_symbols.h"
#include "libu64/overlay.h"
#include "rt64_extended_gbi.h"

// RT64 added texcoord and lookat components to matrix groups; older versions take neither.
#ifdef G_EX_MATRIX_FLOAT_V1
#define gEXMatrixGroupSimpleTC(cmd, id, push, proj, pos, rot, persp, vert, tile, order, edit, tc, lookat) \
    gEXMatrixGroupSimple(cmd, id, push, proj, pos, rot, persp, vert, tile, order, edit, tc, lookat)
#define gEXMatrixGroupDecomposedTC(cmd, id, push, proj, pos, rot, scale, skew, persp, vert, tile, order, edit, tc, lookat) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, pos, rot, scale, skew, persp, vert, tile, order, edit, tc, lookat)
#else
#define gEXMatrixGroupSimpleTC(cmd, id, push, proj, pos, rot, persp, vert, tile, order, edit, tc, lookat) \
    gEXMatrixGroupSimple(cmd, id, push, proj, pos, rot, persp, vert, tile, order, edit)
#define gEXMatrixGroupDecomposedTC(cmd, id, push, proj, pos, rot, scale, skew, persp, vert, tile, order, edit, tc, lookat) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, pos, rot, scale, skew, persp, vert, tile, order, edit)
#endif

#define gEXMatrixGroupDecomposedNormal(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposedTC(cmd, id, push, proj, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE)

#define gEXMatrixGroupDecomposedVerts(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposedTC(cmd, id, push, proj, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE)

#define gEXMatrixGroupDecomposedSkipRot(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposedTC(cmd, id, push, proj, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE)

// A tag for something in the world: interpolated normally, but without rotation on a frame where the camera cut,
// since billboards turn with the camera and would otherwise swing round from the old view.
#define gEXMatrixGroupDecomposedObject(cmd, id, push, proj, edit) \
    do { \
        if (camera_was_skipped()) { \
            gEXMatrixGroupDecomposedSkipRot(cmd, id, push, proj, edit); \
        } else { \
            gEXMatrixGroupDecomposedNormal(cmd, id, push, proj, edit); \
        } \
    } while (0)

#define gEXMatrixGroupDecomposedSkipAll(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposedTC(cmd, id, push, proj, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP)

// Embeds a binary file (path relative to the patches directory) as a read-only array.
#define INCBIN(identifier, filename)          \
    asm(".pushsection .rodata\n"              \
        "\t.local " #identifier "\n"          \
        "\t.type " #identifier ", @object\n"  \
        "\t.balign 8\n"                       \
        #identifier ":\n"                     \
        "\t.incbin \"" filename "\"\n\n"      \
        "\t.balign 8\n"                       \
        "\t.popsection\n");                   \
    extern u8 identifier[]

// camera_tagging.c
int camera_was_skipped(void);
void clear_camera_skipped(void);
struct GameState;
void camera_post_game_update(struct GameState* gameState);

// autosave.c
struct PlayState;
void autosave_post_play_update(struct PlayState* play);
void autosave_draw_icon(struct PlayState* play);

// dpad.c
void dpad_post_play_update(struct PlayState* play);

#endif
