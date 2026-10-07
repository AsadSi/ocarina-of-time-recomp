#include "patches.h"
#include "libc64/math64.h"
#include "transform_ids.h"
#include "effect.h"
#include "gfx.h"
#include "gfx_setupdl.h"
#include "printf.h"
#include "stack_pad.h"
#include "sys_matrix.h"
#include "translation.h"
#include "z_lib.h"

// Frame interpolation for the effects in src/code/z_effect.c: sparks, sword trails (EffectBlure) and
// shield particles. Sparks and shield particles are tagged by slot. Sword trails are vertex lists in world
// space under an identity matrix, which RT64 can only interpolate per vertex: following Zelda64Recomp, each
// vertex records where it was, and the newest section of a trail grows out of the previous one instead of
// popping in.

#define VTX_EX_T(x, y, z, s, t, cr, cg, cb, a, px, py, pz) \
    { { x, y, z }, 0, { s, t }, { cr, cg, cb, a }, { px, py, pz } }

extern EffectContext sEffectContext;
extern EffectInfo sEffectInfoTable[];

static void tag_effect(GraphicsContext* gfxCtx, u32 id, int interpolate) {
    OPEN_DISPS(gfxCtx, "../z_effect.c", 0);
    if (interpolate) {
        gEXMatrixGroupDecomposedNormal(POLY_OPA_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
        gEXMatrixGroupDecomposedNormal(POLY_XLU_DISP++, id + EFFECT_TRANSFORM_ID_COUNT, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
    } else {
        gEXMatrixGroupDecomposedSkipAll(POLY_OPA_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        gEXMatrixGroupDecomposedSkipAll(POLY_XLU_DISP++, id + EFFECT_TRANSFORM_ID_COUNT, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    }
    CLOSE_DISPS(gfxCtx, "../z_effect.c", 0);
}

static void pop_effect_tag(GraphicsContext* gfxCtx) {
    OPEN_DISPS(gfxCtx, "../z_effect.c", 0);
    gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);
    gEXPopMatrixGroup(POLY_XLU_DISP++, G_MTX_MODELVIEW);
    CLOSE_DISPS(gfxCtx, "../z_effect.c", 0);
}

// @recomp Patched to tag each effect by its slot. The sword trail's matrix is the identity, so its group
// skips matrix interpolation and leaves the motion to the vertices.
// Original: Effect_DrawAll in src/code/z_effect.c.
RECOMP_PATCH void Effect_DrawAll(GraphicsContext* gfxCtx) {
    s32 i;

    for (i = 0; i < SPARK_COUNT; i++) {
        if (!sEffectContext.sparks[i].status.active) {
            continue;
        }
        tag_effect(gfxCtx, EFFECT_SPARK_TRANSFORM_ID_START + i, true);
        sEffectInfoTable[EFFECT_SPARK].draw(&sEffectContext.sparks[i].effect, gfxCtx);
        pop_effect_tag(gfxCtx);
    }

    for (i = 0; i < BLURE_COUNT; i++) {
        if (!sEffectContext.blures[i].status.active) {
            continue;
        }
        tag_effect(gfxCtx, EFFECT_BLURE_TRANSFORM_ID_START + i, false);
        sEffectInfoTable[EFFECT_BLURE1].draw(&sEffectContext.blures[i].effect, gfxCtx);
        pop_effect_tag(gfxCtx);
    }

    for (i = 0; i < SHIELD_PARTICLE_COUNT; i++) {
        if (!sEffectContext.shieldParticles[i].status.active) {
            continue;
        }
        tag_effect(gfxCtx, EFFECT_SHIELD_PARTICLE_TRANSFORM_ID_START + i, true);
        sEffectInfoTable[EFFECT_SHIELD_PARTICLE].draw(&sEffectContext.shieldParticles[i].effect, gfxCtx);
        pop_effect_tag(gfxCtx);
    }
}

void EffectBlure_GetComputedValues(EffectBlure* this, s32 index, f32 ratio, Vec3s* vec1, Vec3s* vec2,
                                   Color_RGBA8* color1, Color_RGBA8* color2);
void EffectBlure_DrawSimple(EffectBlure* this2, GraphicsContext* gfxCtx);
void EffectBlure_DrawSmooth(EffectBlure* this2, GraphicsContext* gfxCtx);
s16 EffectSs_LerpS16(s16 a, s16 b, f32 weight);
u8 EffectSs_LerpU8(u8 a, u8 b, f32 weight);

// @recomp Patched to give the vertices previous positions.
// Original: EffectBlure_DrawElemNoInterpolation in src/code/z_eff_blure.c.
RECOMP_PATCH void EffectBlure_DrawElemNoInterpolation(EffectBlure* this, EffectBlureElement* elem, s32 index,
                                         GraphicsContext* gfxCtx) {
    // @recomp Vertices carry their previous position for RT64 to interpolate from.
    static VertexEXColor baseVtx = VTX_EX_T(0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0);
    VertexEX* vtx;
    Vec3s sp8C;
    Vec3s sp84;
    f32 ratio;
    Color_RGBA8 sp7C;
    Color_RGBA8 sp78;
    Vec3f sp6C;
    Vec3f sp60;
    Vec3f sp54;

    OPEN_DISPS(gfxCtx, "../z_eff_blure.c", 838);

    Math_Vec3s_ToVec3f(&sp6C, &this->elements[0].p2);

    vtx = GRAPH_ALLOC(gfxCtx, 4 * sizeof(VertexEX));
    if (vtx == NULL) {
        PRINTF(T("z_eff_blure.c::SQ_NoInterpolate_disp() 頂点確保できず。\n",
                 "z_eff_blure.c::SQ_NoInterpolate_disp() Vertices cannot be secured.\n"));
        goto close_disps;
    }

    vtx[0].v = baseVtx;
    vtx[1].v = baseVtx;
    vtx[2].v = baseVtx;
    vtx[3].v = baseVtx;

    ratio = (f32)elem->timer / (f32)this->elemDuration;
    EffectBlure_GetComputedValues(this, index, ratio, &sp8C, &sp84, &sp7C, &sp78);

    sp60.x = sp84.x;
    sp60.y = sp84.y;
    sp60.z = sp84.z;
    Math_Vec3f_Diff(&sp60, &sp6C, &sp54);
    Math_Vec3f_Scale(&sp54, 10.0f);
    vtx[0].v.ob[0] = sp54.x;
    vtx[0].v.ob[1] = sp54.y;
    vtx[0].v.ob[2] = sp54.z;
    vtx[0].v.cn[0] = sp78.r;
    vtx[0].v.cn[1] = sp78.g;
    vtx[0].v.cn[2] = sp78.b;
    vtx[0].v.cn[3] = sp78.a;

    sp60.x = sp8C.x;
    sp60.y = sp8C.y;
    sp60.z = sp8C.z;
    Math_Vec3f_Diff(&sp60, &sp6C, &sp54);
    Math_Vec3f_Scale(&sp54, 10.0f);
    vtx[1].v.ob[0] = sp54.x;
    vtx[1].v.ob[1] = sp54.y;
    vtx[1].v.ob[2] = sp54.z;
    vtx[1].v.cn[0] = sp7C.r;
    vtx[1].v.cn[1] = sp7C.g;
    vtx[1].v.cn[2] = sp7C.b;
    vtx[1].v.cn[3] = sp7C.a;

    ratio = (f32)(elem + 1)->timer / (f32)this->elemDuration;
    EffectBlure_GetComputedValues(this, index + 1, ratio, &sp8C, &sp84, &sp7C, &sp78);

    sp60.x = sp8C.x;
    sp60.y = sp8C.y;
    sp60.z = sp8C.z;
    Math_Vec3f_Diff(&sp60, &sp6C, &sp54);
    Math_Vec3f_Scale(&sp54, 10.0f);
    vtx[2].v.ob[0] = sp54.x;
    vtx[2].v.ob[1] = sp54.y;
    vtx[2].v.ob[2] = sp54.z;
    vtx[2].v.cn[0] = sp7C.r;
    vtx[2].v.cn[1] = sp7C.g;
    vtx[2].v.cn[2] = sp7C.b;
    vtx[2].v.cn[3] = sp7C.a;

    sp60.x = sp84.x;
    sp60.y = sp84.y;
    sp60.z = sp84.z;
    Math_Vec3f_Diff(&sp60, &sp6C, &sp54);
    Math_Vec3f_Scale(&sp54, 10.0f);
    vtx[3].v.ob[0] = sp54.x;
    vtx[3].v.ob[1] = sp54.y;
    vtx[3].v.ob[2] = sp54.z;
    vtx[3].v.cn[0] = sp78.r;
    vtx[3].v.cn[1] = sp78.g;
    vtx[3].v.cn[2] = sp78.b;
    vtx[3].v.cn[3] = sp78.a;

    // @recomp The trailing edge stays put. When this is the newest section (its far edge was added this
    // frame), the far edge grows out of the near one; otherwise it stays put too.
    for (s32 v = 0; v < 4; v++) {
        vtx[v].v.obp[0] = vtx[v].v.ob[0];
        vtx[v].v.obp[1] = vtx[v].v.ob[1];
        vtx[v].v.obp[2] = vtx[v].v.ob[2];
    }
    if (elem->timer == 2) {
        vtx[2].v.obp[0] = vtx[1].v.ob[0];
        vtx[2].v.obp[1] = vtx[1].v.ob[1];
        vtx[2].v.obp[2] = vtx[1].v.ob[2];
        vtx[3].v.obp[0] = vtx[0].v.ob[0];
        vtx[3].v.obp[1] = vtx[0].v.ob[1];
        vtx[3].v.obp[2] = vtx[0].v.ob[2];
    }

    gEXVertex(POLY_XLU_DISP++, vtx, 4, 0);
    gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);

close_disps:
    CLOSE_DISPS(gfxCtx, "../z_eff_blure.c", 932);
}

// @recomp Patched to give the vertices previous positions.
// Original: EffectBlure_DrawElemHermiteInterpolation in src/code/z_eff_blure.c.
RECOMP_PATCH void EffectBlure_DrawElemHermiteInterpolation(EffectBlure* this, EffectBlureElement* elem, s32 index,
                                              GraphicsContext* gfxCtx) {
    // @recomp Vertices carry their previous position for RT64 to interpolate from.
    static VertexEXColor baseVtx = VTX_EX_T(0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0);
    VertexEX* vtx;
    Vec3s sp1EC;
    Vec3s sp1E4;
    f32 ratio;
    Color_RGBA8 sp1DC;
    Color_RGBA8 sp1D8;
    Vec3f sp1CC;
    Vec3f sp1C0;
    Vec3f sp1B4;
    Vec3f sp1A8;
    Color_RGBA8 sp1A4;
    Color_RGBA8 sp1A0;
    Color_RGBA8 sp19C;
    Color_RGBA8 sp198;
    Vec3f sp18C;
    Vec3f sp180;
    Vec3f sp174;
    Vec3f sp168;
    s32 i;
    Vec3f sp158;
    Vec3f sp14C;
    Color_RGBA8 sp148;
    Color_RGBA8 sp144;
    Vec3f sp138;

    OPEN_DISPS(gfxCtx, "../z_eff_blure.c", 971);

    Math_Vec3s_ToVec3f(&sp138, &this->elements[0].p2);

    ratio = (f32)elem->timer / (f32)this->elemDuration;
    EffectBlure_GetComputedValues(this, index, ratio, &sp1EC, &sp1E4, &sp1A4, &sp1A0);
    Math_Vec3s_ToVec3f(&sp1CC, &sp1EC);
    Math_Vec3s_ToVec3f(&sp1C0, &sp1E4);

    ratio = (f32)(elem + 1)->timer / (f32)this->elemDuration;
    EffectBlure_GetComputedValues(this, index + 1, ratio, &sp1EC, &sp1E4, &sp19C, &sp198);
    Math_Vec3s_ToVec3f(&sp18C, &sp1EC);
    Math_Vec3s_ToVec3f(&sp180, &sp1E4);

    if ((elem->flags & 3) == 2) {
        Math_Vec3f_Diff(&sp18C, &sp1CC, &sp1B4);
        Math_Vec3f_Diff(&sp180, &sp1C0, &sp1A8);
    } else {
        Vec3f sp118;
        Vec3f sp10C;

        ASSERT(index - 1 >= 0, "index - 1 >= 0", "../z_eff_blure.c", 1005);

        ratio = (f32)(elem - 1)->timer / (f32)this->elemDuration;
        EffectBlure_GetComputedValues(this, index - 1, ratio, &sp1EC, &sp1E4, &sp1DC, &sp1D8);
        Math_Vec3s_ToVec3f(&sp118, &sp1EC);
        Math_Vec3s_ToVec3f(&sp10C, &sp1E4);
        Math_Vec3f_Diff(&sp18C, &sp118, &sp1B4);
        Math_Vec3f_Diff(&sp180, &sp10C, &sp1A8);
    }

    Math_Vec3f_Scale(&sp1B4, 0.5f);
    Math_Vec3f_Scale(&sp1A8, 0.5f);

    if (((elem + 1)->flags & 3) == 2) {
        Math_Vec3f_Diff(&sp18C, &sp1CC, &sp174);
        Math_Vec3f_Diff(&sp180, &sp1C0, &sp168);
    } else {
        Vec3f sp100;
        Vec3f spF4;

        ASSERT(index + 2 < this->numElements, "index + 2 < this2->now_edge_num", "../z_eff_blure.c", 1032);

        ratio = (f32)(elem + 2)->timer / (f32)this->elemDuration;
        EffectBlure_GetComputedValues(this, index + 2, ratio, &sp1EC, &sp1E4, &sp1DC, &sp1D8);
        Math_Vec3s_ToVec3f(&sp100, &sp1EC);
        Math_Vec3s_ToVec3f(&spF4, &sp1E4);
        Math_Vec3f_Diff(&sp100, &sp1CC, &sp174);
        Math_Vec3f_Diff(&spF4, &sp1C0, &sp168);
    }

    Math_Vec3f_Scale(&sp174, 0.5f);
    Math_Vec3f_Scale(&sp168, 0.5f);

    vtx = GRAPH_ALLOC(gfxCtx, 16 * sizeof(VertexEX));
    if (vtx == NULL) {
        PRINTF(T("z_eff_blure.c::SQ_HermiteInterpolate_disp() 頂点確保できず。\n",
                 "z_eff_blure.c::SQ_HermiteInterpolate_disp() Vertices cannot be secured.\n"));
    } else {
        Math_Vec3f_Diff(&sp1CC, &sp138, &sp158);
        Math_Vec3f_Scale(&sp158, 10.0f);
        Math_Vec3f_Diff(&sp1C0, &sp138, &sp14C);
        Math_Vec3f_Scale(&sp14C, 10.0f);

        Color_RGBA8_Copy(&sp148, &sp1A4);
        Color_RGBA8_Copy(&sp144, &sp1A0);

        vtx[0].v = baseVtx;
        vtx[1].v = baseVtx;

        vtx[0].v.ob[0] = Math_FNearbyIntF(sp158.x);
        vtx[0].v.ob[1] = Math_FNearbyIntF(sp158.y);
        vtx[0].v.ob[2] = Math_FNearbyIntF(sp158.z);
        vtx[0].v.cn[0] = sp148.r;
        vtx[0].v.cn[1] = sp148.g;
        vtx[0].v.cn[2] = sp148.b;
        vtx[0].v.cn[3] = sp148.a;
        vtx[1].v.ob[0] = Math_FNearbyIntF(sp14C.x);
        vtx[1].v.ob[1] = Math_FNearbyIntF(sp14C.y);
        vtx[1].v.ob[2] = Math_FNearbyIntF(sp14C.z);
        vtx[1].v.cn[0] = sp144.r;
        vtx[1].v.cn[1] = sp144.g;
        vtx[1].v.cn[2] = sp144.b;
        vtx[1].v.cn[3] = sp144.a;

        // @recomp The trailing edge stays put.
        vtx[0].v.obp[0] = vtx[0].v.ob[0];
        vtx[0].v.obp[1] = vtx[0].v.ob[1];
        vtx[0].v.obp[2] = vtx[0].v.ob[2];
        vtx[1].v.obp[0] = vtx[1].v.ob[0];
        vtx[1].v.obp[1] = vtx[1].v.ob[1];
        vtx[1].v.obp[2] = vtx[1].v.ob[2];

        for (i = 1; i < 8; i++) {
            s32 j1 = 2 * i;
            s32 j2 = 2 * i + 1;
            Vec3f spE0;
            f32 temp_f28 = i / 7.0f;                               // t
            f32 temp_f0 = temp_f28 * temp_f28;                     // t^2
            f32 temp_f2 = temp_f0 * temp_f28;                      // t^3
            f32 temp_f20 = temp_f2 - temp_f0;                      // t^3 - t^2
            f32 temp_f22 = temp_f2 - 2.0f * temp_f0 + temp_f28;    // t^3 - 2t^2 + t
            f32 temp_f24 = 2.0f * temp_f2 - temp_f0 * 3.0f + 1.0f; // 2t^3 - 3t^2 + 1
            f32 temp_f26 = temp_f0 * 3.0f - 2.0f * temp_f2;        // 3t^2 - 2t^3
            STACK_PADS(s32, 2);

            // p = (2t^3 - 3t^2 + 1)p0 + (3t^2 - 2t^3)p1 + (t^3 - 2t^2 + t)m0 + (t^3 - t^2)m1
            spE0.x = (temp_f24 * sp1CC.x) + (temp_f26 * sp18C.x) + (temp_f22 * sp1B4.x) + (temp_f20 * sp174.x);
            spE0.y = (temp_f24 * sp1CC.y) + (temp_f26 * sp18C.y) + (temp_f22 * sp1B4.y) + (temp_f20 * sp174.y);
            spE0.z = (temp_f24 * sp1CC.z) + (temp_f26 * sp18C.z) + (temp_f22 * sp1B4.z) + (temp_f20 * sp174.z);
            Math_Vec3f_Diff(&spE0, &sp138, &sp158);
            Math_Vec3f_Scale(&sp158, 10.0f);

            spE0.x = (temp_f24 * sp1C0.x) + (temp_f26 * sp180.x) + (temp_f22 * sp1A8.x) + (temp_f20 * sp168.x);
            spE0.y = (temp_f24 * sp1C0.y) + (temp_f26 * sp180.y) + (temp_f22 * sp1A8.y) + (temp_f20 * sp168.y);
            spE0.z = (temp_f24 * sp1C0.z) + (temp_f26 * sp180.z) + (temp_f22 * sp1A8.z) + (temp_f20 * sp168.z);
            Math_Vec3f_Diff(&spE0, &sp138, &sp14C);
            Math_Vec3f_Scale(&sp14C, 10.0f);

            vtx[j1].v = baseVtx;
            vtx[j2].v = baseVtx;

            vtx[j1].v.ob[0] = Math_FNearbyIntF(sp158.x);
            vtx[j1].v.ob[1] = Math_FNearbyIntF(sp158.y);
            vtx[j1].v.ob[2] = Math_FNearbyIntF(sp158.z);
            vtx[j1].v.cn[0] = EffectSs_LerpU8(sp1A4.r, sp19C.r, temp_f28);
            vtx[j1].v.cn[1] = EffectSs_LerpU8(sp1A4.g, sp19C.g, temp_f28);
            vtx[j1].v.cn[2] = EffectSs_LerpU8(sp1A4.b, sp19C.b, temp_f28);
            vtx[j1].v.cn[3] = EffectSs_LerpU8(sp1A4.a, sp19C.a, temp_f28);

            vtx[j2].v.ob[0] = Math_FNearbyIntF(sp14C.x);
            vtx[j2].v.ob[1] = Math_FNearbyIntF(sp14C.y);
            vtx[j2].v.ob[2] = Math_FNearbyIntF(sp14C.z);
            vtx[j2].v.cn[0] = EffectSs_LerpU8(sp1A0.r, sp198.r, temp_f28);
            vtx[j2].v.cn[1] = EffectSs_LerpU8(sp1A0.g, sp198.g, temp_f28);
            vtx[j2].v.cn[2] = EffectSs_LerpU8(sp1A0.b, sp198.b, temp_f28);
            vtx[j2].v.cn[3] = EffectSs_LerpU8(sp1A0.a, sp198.a, temp_f28);

            // @recomp When this is the newest section, the curve grows out of the trailing edge; otherwise
            // it stays put.
            if (elem->timer == 2) {
                vtx[j1].v.obp[0] = vtx[0].v.ob[0];
                vtx[j1].v.obp[1] = vtx[0].v.ob[1];
                vtx[j1].v.obp[2] = vtx[0].v.ob[2];
                vtx[j2].v.obp[0] = vtx[1].v.ob[0];
                vtx[j2].v.obp[1] = vtx[1].v.ob[1];
                vtx[j2].v.obp[2] = vtx[1].v.ob[2];
            } else {
                vtx[j1].v.obp[0] = vtx[j1].v.ob[0];
                vtx[j1].v.obp[1] = vtx[j1].v.ob[1];
                vtx[j1].v.obp[2] = vtx[j1].v.ob[2];
                vtx[j2].v.obp[0] = vtx[j2].v.ob[0];
                vtx[j2].v.obp[1] = vtx[j2].v.ob[1];
                vtx[j2].v.obp[2] = vtx[j2].v.ob[2];
            }
        }

        gEXVertex(POLY_XLU_DISP++, vtx, 16, 0);
        gSP2Triangles(POLY_XLU_DISP++, 0, 1, 3, 0, 0, 3, 2, 0);
        gSP2Triangles(POLY_XLU_DISP++, 2, 3, 5, 0, 2, 5, 4, 0);
        gSP2Triangles(POLY_XLU_DISP++, 4, 5, 7, 0, 4, 7, 6, 0);
        gSP2Triangles(POLY_XLU_DISP++, 6, 7, 9, 0, 6, 9, 8, 0);
        gSP2Triangles(POLY_XLU_DISP++, 8, 9, 11, 0, 8, 11, 10, 0);
        gSP2Triangles(POLY_XLU_DISP++, 10, 11, 13, 0, 10, 13, 12, 0);
        gSP2Triangles(POLY_XLU_DISP++, 12, 13, 15, 0, 12, 15, 14, 0);
    }

    CLOSE_DISPS(gfxCtx, "../z_eff_blure.c", 1184);
}

// @recomp Patched to give the vertices of the plain trail previous positions.
// Original: EffectBlure_Draw in src/code/z_eff_blure.c.
RECOMP_PATCH void EffectBlure_Draw(void* thisx, GraphicsContext* gfxCtx) {
    EffectBlure* this = (EffectBlure*)thisx;
    // @recomp Vertices carry their previous position for RT64 to interpolate from.
    VertexEX* vtx;
    EffectBlureElement* elem;
    s32 i;
    s32 j;
    s32 flag;

    OPEN_DISPS(gfxCtx, "../z_eff_blure.c", 1596);

    gSPMatrix(POLY_XLU_DISP++, &gIdentityMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    if (this->numElements != 0) {
        if (this->flags == 0) {
            Gfx_SetupDL_38Xlu(gfxCtx);
            gDPPipeSync(POLY_XLU_DISP++);

            vtx = GRAPH_ALLOC(gfxCtx, 32 * sizeof(VertexEX));
            if (vtx == NULL) {
                PRINTF(T("ブラ─表示:頂点テーブル確保できず\n", "Blur - display: vertex table could not be secured\n"));
            } else {
                j = 0;
                for (i = 0; i < this->numElements; i++) {
                    elem = &this->elements[i];

                    if (elem->state == 1) {
                        f32 ratio = (f32)elem->timer / (f32)this->elemDuration;

                        switch (this->calcMode) {
                            case 1:
                                vtx[j].v.ob[0] = EffectSs_LerpS16(elem->p1.x, elem->p2.x, ratio);
                                vtx[j].v.ob[1] = EffectSs_LerpS16(elem->p1.y, elem->p2.y, ratio);
                                vtx[j].v.ob[2] = EffectSs_LerpS16(elem->p1.z, elem->p2.z, ratio);
                                vtx[j + 1].v.ob[0] = elem->p2.x;
                                vtx[j + 1].v.ob[1] = elem->p2.y;
                                vtx[j + 1].v.ob[2] = elem->p2.z;
                                break;
                            case 2:
                                vtx[j].v.ob[0] = elem->p1.x;
                                vtx[j].v.ob[1] = elem->p1.y;
                                vtx[j].v.ob[2] = elem->p1.z;
                                vtx[j + 1].v.ob[0] = EffectSs_LerpS16(elem->p2.x, elem->p1.x, ratio);
                                vtx[j + 1].v.ob[1] = EffectSs_LerpS16(elem->p2.y, elem->p1.y, ratio);
                                vtx[j + 1].v.ob[2] = EffectSs_LerpS16(elem->p2.z, elem->p1.z, ratio);
                                break;
                            case 3:
                                ratio *= 0.5f;
                                vtx[j].v.ob[0] = EffectSs_LerpS16(elem->p1.x, elem->p2.x, ratio);
                                vtx[j].v.ob[1] = EffectSs_LerpS16(elem->p1.y, elem->p2.y, ratio);
                                vtx[j].v.ob[2] = EffectSs_LerpS16(elem->p1.z, elem->p2.z, ratio);
                                vtx[j + 1].v.ob[0] = EffectSs_LerpS16(elem->p2.x, elem->p1.x, ratio);
                                vtx[j + 1].v.ob[1] = EffectSs_LerpS16(elem->p2.y, elem->p1.y, ratio);
                                vtx[j + 1].v.ob[2] = EffectSs_LerpS16(elem->p2.z, elem->p1.z, ratio);
                                ratio *= 2.0f;
                                break;
                            case 0:
                            default:
                                vtx[j].v.ob[0] = elem->p1.x;
                                vtx[j].v.ob[1] = elem->p1.y;
                                vtx[j].v.ob[2] = elem->p1.z;
                                vtx[j + 1].v.ob[0] = elem->p2.x;
                                vtx[j + 1].v.ob[1] = elem->p2.y;
                                vtx[j + 1].v.ob[2] = elem->p2.z;
                                break;
                        }

                        // @recomp A vertex added this frame grows out of the previous edge's vertex; others
                        // stay put.
                        if ((elem->timer == 1) && (j >= 2)) {
                            vtx[j].v.obp[0] = vtx[j - 2].v.ob[0];
                            vtx[j].v.obp[1] = vtx[j - 2].v.ob[1];
                            vtx[j].v.obp[2] = vtx[j - 2].v.ob[2];
                        } else {
                            vtx[j].v.obp[0] = vtx[j].v.ob[0];
                            vtx[j].v.obp[1] = vtx[j].v.ob[1];
                            vtx[j].v.obp[2] = vtx[j].v.ob[2];
                        }
                        vtx[j].v.flag = 0;
                        vtx[j].v.tc[0] = 0;
                        vtx[j].v.tc[1] = 0;
                        vtx[j].v.cn[0] = EffectSs_LerpU8(this->p1StartColor[0], this->p1EndColor[0], ratio);
                        vtx[j].v.cn[1] = EffectSs_LerpU8(this->p1StartColor[1], this->p1EndColor[1], ratio);
                        vtx[j].v.cn[2] = EffectSs_LerpU8(this->p1StartColor[2], this->p1EndColor[2], ratio);
                        vtx[j].v.cn[3] = EffectSs_LerpU8(this->p1StartColor[3], this->p1EndColor[3], ratio);
                        j++;

                        // @recomp A vertex added this frame grows out of the previous edge's vertex; others
                        // stay put.
                        if ((elem->timer == 1) && (j >= 2)) {
                            vtx[j].v.obp[0] = vtx[j - 2].v.ob[0];
                            vtx[j].v.obp[1] = vtx[j - 2].v.ob[1];
                            vtx[j].v.obp[2] = vtx[j - 2].v.ob[2];
                        } else {
                            vtx[j].v.obp[0] = vtx[j].v.ob[0];
                            vtx[j].v.obp[1] = vtx[j].v.ob[1];
                            vtx[j].v.obp[2] = vtx[j].v.ob[2];
                        }
                        vtx[j].v.flag = 0;
                        vtx[j].v.tc[0] = 0;
                        vtx[j].v.tc[1] = 0;
                        vtx[j].v.cn[0] = EffectSs_LerpU8(this->p2StartColor[0], this->p2EndColor[0], ratio);
                        vtx[j].v.cn[1] = EffectSs_LerpU8(this->p2StartColor[1], this->p2EndColor[1], ratio);
                        vtx[j].v.cn[2] = EffectSs_LerpU8(this->p2StartColor[2], this->p2EndColor[2], ratio);
                        vtx[j].v.cn[3] = EffectSs_LerpU8(this->p2StartColor[3], this->p2EndColor[3], ratio);
                        j++;
                    }
                }

                flag = 0;
                j = 0;

                gEXVertex(POLY_XLU_DISP++, vtx, 32, 0);

                for (i = 0; i < this->numElements; i++) {
                    elem = &this->elements[i];

                    if (elem->state == 0) {
                        flag = 0;
                    } else {
                        if (flag == 0) {
                            flag = 1;
                        } else {
                            gSP1Quadrangle(POLY_XLU_DISP++, j - 2, j - 1, j + 1, j, 0);

                            if (1) {} // Necessary to match

                            if (this->unkFlag == 1) {
                                flag = 0;
                            }
                        }
                        j += 2;
                    }
                }
            }
        } else if (this->drawMode < 2) {
            EffectBlure_DrawSimple(this, gfxCtx);
        } else {
            EffectBlure_DrawSmooth(this, gfxCtx);
        }
    }

    CLOSE_DISPS(gfxCtx, "../z_eff_blure.c", 1823);
}
