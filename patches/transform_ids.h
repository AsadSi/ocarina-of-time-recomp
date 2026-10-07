#ifndef __TRANSFORM_IDS_H__
#define __TRANSFORM_IDS_H__

// IDs RT64 uses to match a transform in one frame with the same transform in the next, so it can
// interpolate between them.
#define TRANSFORM_ID_NONE 0x0U
#define CAMERA_TRANSFORM_ID 0x10U

#define SKYBOX_TRANSFORM_ID_START 0x100U

// One per particle (EffectSs) slot.
#define PARTICLE_TRANSFORM_ID_START 0x200U
#define PARTICLE_TRANSFORM_ID_COUNT 0x100U

// Effects (z_effect.c), one per slot, with a second range for their translucent list.
#define EFFECT_TRANSFORM_ID_COUNT 0x40U
#define EFFECT_SPARK_TRANSFORM_ID_START 0x400U
#define EFFECT_BLURE_TRANSFORM_ID_START (EFFECT_SPARK_TRANSFORM_ID_START + 2 * EFFECT_TRANSFORM_ID_COUNT)
#define EFFECT_SHIELD_PARTICLE_TRANSFORM_ID_START (EFFECT_BLURE_TRANSFORM_ID_START + 2 * EFFECT_TRANSFORM_ID_COUNT)

// Each actor gets ACTOR_TRANSFORM_ID_COUNT IDs, starting from a spawn counter that never repeats.
#define ACTOR_TRANSFORM_ID_START 0x1000000U
#define ACTOR_TRANSFORM_ID_COUNT 0x200U
#define ACTOR_TRANSFORM_LIMB_OFFSET 0x000U
#define ACTOR_TRANSFORM_POST_LIMB_OFFSET 0x100U
#define ACTOR_TRANSFORM_WHOLE_OPA_OFFSET 0x1FEU
#define ACTOR_TRANSFORM_WHOLE_XLU_OFFSET 0x1FFU

#endif
