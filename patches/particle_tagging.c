#include "patches.h"
#include "array_count.h"
#include "transform_ids.h"
#include "effect.h"
#include "gfx.h"
#include "play_state.h"

// Tags particles (EffectSs) for frame interpolation, following Zelda64Recomp's particle tagging. Each
// particle is identified by its slot in the particle table. A slot that was reset since it last drew
// holds a new particle, which must not be interpolated from the old one's position.

extern EffectSsInfo sEffectSsInfo;

static u8 sParticleReset[PARTICLE_TRANSFORM_ID_COUNT];

// @recomp Patched to note that the slot was reset. Original: EffectSs_Reset in src/code/z_effect_soft_sprite.c.
RECOMP_PATCH void EffectSs_Reset(EffectSs* effectSs) {
    u32 i;
    u32 index = effectSs - sEffectSsInfo.table;

    effectSs->type = EFFECT_SS_TYPE_MAX;
    effectSs->accel.x = effectSs->accel.y = effectSs->accel.z = 0;
    effectSs->velocity.x = effectSs->velocity.y = effectSs->velocity.z = 0;
    effectSs->vec.x = effectSs->vec.y = effectSs->vec.z = 0;
    effectSs->pos.x = effectSs->pos.y = effectSs->pos.z = 0;
    effectSs->life = -1;
    effectSs->flags = 0;
    effectSs->priority = 128;
    effectSs->draw = NULL;
    effectSs->update = NULL;
    effectSs->gfx = NULL;
    effectSs->actor = NULL;

    for (i = 0; i < ARRAY_COUNT(effectSs->regs); i++) {
        effectSs->regs[i] = 0;
    }

    // @recomp Mark the slot so its next particle starts without interpolation.
    if (index < PARTICLE_TRANSFORM_ID_COUNT) {
        sParticleReset[index] = true;
    }
}

// @recomp Patched to tag the particle's matrices with its slot.
// Original: EffectSs_Draw in src/code/z_effect_soft_sprite.c.
RECOMP_PATCH void EffectSs_Draw(PlayState* play, s32 index) {
    EffectSs* effectSs = &sEffectSsInfo.table[index];
    u32 id = PARTICLE_TRANSFORM_ID_START + index;

    if (effectSs->draw == NULL) {
        return;
    }
    if ((u32)index >= PARTICLE_TRANSFORM_ID_COUNT) {
        effectSs->draw(play, index, effectSs);
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, "../z_effect_soft_sprite.c", 0);

    if (sParticleReset[index]) {
        gEXMatrixGroupDecomposedSkipAll(POLY_OPA_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        gEXMatrixGroupDecomposedSkipAll(POLY_XLU_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    } else {
        gEXMatrixGroupDecomposedObject(POLY_OPA_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
        gEXMatrixGroupDecomposedObject(POLY_XLU_DISP++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
    }
    sParticleReset[index] = false;

    effectSs->draw(play, index, effectSs);

    gEXPopMatrixGroup(POLY_OPA_DISP++, G_MTX_MODELVIEW);
    gEXPopMatrixGroup(POLY_XLU_DISP++, G_MTX_MODELVIEW);

    CLOSE_DISPS(play->state.gfxCtx, "../z_effect_soft_sprite.c", 0);
}
