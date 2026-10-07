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

#define gEXMatrixGroupDecomposedNormal(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit)

#define gEXMatrixGroupDecomposedSkipAll(cmd, id, push, proj, edit) \
    gEXMatrixGroupDecomposed(cmd, id, push, proj, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, edit)

// camera_tagging.c
int camera_was_skipped(void);
void clear_camera_skipped(void);
struct GameState;
void camera_post_game_update(struct GameState* gameState);

#endif
