#include "patches.h"
#include "transform_ids.h"
#include "actor.h"
#include "animation.h"
#include "effect.h"
#include "fault.h"
#include "gfx.h"
#include "light.h"
#include "play_state.h"
#include "printf.h"
#include "segmented_address.h"
#include "skin.h"
#include "skin_matrix.h"
#include "stack_pad.h"
#include "sys_matrix.h"
#include "translation.h"

// Tags actors and their skeletons for frame interpolation, following Zelda64Recomp's actor tagging.
//
// RT64 matches a transform between frames by ID. An actor's address can't be the ID: the game reuses
// addresses for new actors, which would then fly in from the old one's position. Each actor instead
// gets an ID from a spawn counter the first time it draws, released when the actor is destroyed.

#define ACTOR_REGISTRY_SIZE 512

static Actor* sRegistryActors[ACTOR_REGISTRY_SIZE];
static u32 sRegistryIds[ACTOR_REGISTRY_SIZE];
static u32 sNextActorSpawn = 0;

// The actor's base transform ID, assigning one if `assign` is set. TRANSFORM_ID_NONE if there is none.
static u32 actor_transform_id(void* actor, int assign) {
    s32 free_slot = -1;

    if (actor == NULL) {
        return TRANSFORM_ID_NONE;
    }
    for (s32 i = 0; i < ACTOR_REGISTRY_SIZE; i++) {
        if (sRegistryActors[i] == actor) {
            return sRegistryIds[i];
        }
        if (free_slot < 0 && sRegistryActors[i] == NULL) {
            free_slot = i;
        }
    }
    if (!assign || free_slot < 0) {
        return TRANSFORM_ID_NONE;
    }
    sRegistryActors[free_slot] = actor;
    sRegistryIds[free_slot] = ACTOR_TRANSFORM_ID_START + (sNextActorSpawn++ % 0x7F000U) * ACTOR_TRANSFORM_ID_COUNT;
    return sRegistryIds[free_slot];
}

static void actor_registry_remove(Actor* actor) {
    for (s32 i = 0; i < ACTOR_REGISTRY_SIZE; i++) {
        if (sRegistryActors[i] == actor) {
            sRegistryActors[i] = NULL;
            return;
        }
    }
}

static Gfx* tag_push(Gfx* dlist, u32 id) {
    gEXMatrixGroupDecomposedObject(dlist++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
    return dlist;
}

static Gfx* tag_pop(Gfx* dlist) {
    gEXPopMatrixGroup(dlist++, G_MTX_MODELVIEW);
    return dlist;
}

// @recomp Patched to release the actor's transform ID. Original: Actor_Destroy in src/code/z_actor.c.
RECOMP_PATCH void Actor_Destroy(Actor* actor, PlayState* play) {
    actor_registry_remove(actor);

    if (actor->destroy != NULL) {
        actor->destroy(actor, play);
        actor->destroy = NULL;
    }
}

static s32 count_matrix_loads(Gfx* start, Gfx* end) {
    s32 count = 0;
    for (Gfx* cur = start; cur != end; cur++) {
        if ((cur->words.w0 >> 24) == G_MTX) {
            count++;
        }
    }
    return count;
}

void Actor_FaultPrint(Actor* actor, char* command);

// @recomp Patched to give the actor a transform ID before it draws, and to tag its drawing as a whole
// when it loaded a single matrix per list. Skeletons are tagged limb by limb in the patches below.
// Original: Actor_Draw in src/code/z_actor.c.
RECOMP_PATCH void Actor_Draw(PlayState* play, Actor* actor) {
    FaultClient faultClient;
    Lights* lights;
    Gfx* opaSlot;
    Gfx* xluSlot;
    u32 actorId;

    Fault_AddClient(&faultClient, Actor_FaultPrint, actor, "Actor_draw");

    OPEN_DISPS(play->state.gfxCtx, "../z_actor.c", 6035);

    lights = LightContext_NewLights(&play->lightCtx, play->state.gfxCtx);

    Lights_BindAll(lights, play->lightCtx.listHead,
                   (actor->flags & ACTOR_FLAG_IGNORE_POINT_LIGHTS) ? NULL : &actor->world.pos);
    Lights_Draw(lights, play->state.gfxCtx);

    if (actor->flags & ACTOR_FLAG_IGNORE_QUAKE) {
        Matrix_SetTranslateRotateYXZ(actor->world.pos.x + play->mainCamera.quakeOffset.x,
                                     actor->world.pos.y +
                                         ((actor->shape.yOffset * actor->scale.y) + play->mainCamera.quakeOffset.y),
                                     actor->world.pos.z + play->mainCamera.quakeOffset.z, &actor->shape.rot);
    } else {
        Matrix_SetTranslateRotateYXZ(actor->world.pos.x, actor->world.pos.y + (actor->shape.yOffset * actor->scale.y),
                                     actor->world.pos.z, &actor->shape.rot);
    }

    Matrix_Scale(actor->scale.x, actor->scale.y, actor->scale.z, MTXMODE_APPLY);
    Actor_SetObjectDependency(play, actor);

    gSPSegment(POLY_OPA_DISP++, 0x06, play->objectCtx.slots[actor->objectSlot].segment);
    gSPSegment(POLY_XLU_DISP++, 0x06, play->objectCtx.slots[actor->objectSlot].segment);

    if (actor->colorFilterTimer != 0) {
        Color_RGBA8 color = { 0, 0, 0, 255 };

        if (actor->colorFilterParams & COLORFILTER_COLORFLAG_GRAY) {
            color.r = color.g = color.b = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        } else if (actor->colorFilterParams & COLORFILTER_COLORFLAG_RED) {
            color.r = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        } else {
            color.b = COLORFILTER_GET_COLORINTENSITY(actor->colorFilterParams) | 7;
        }

        if (actor->colorFilterParams & COLORFILTER_BUFFLAG_XLU) {
            func_80026860(play, &color, actor->colorFilterTimer, COLORFILTER_GET_DURATION(actor->colorFilterParams));
        } else {
            func_80026400(play, &color, actor->colorFilterTimer, COLORFILTER_GET_DURATION(actor->colorFilterParams));
        }
    }

    // @recomp Give the actor its ID, and reserve one group command's worth of no-ops in each list to fill
    // in once it's known what the actor drew.
    actorId = actor_transform_id(actor, true);
    opaSlot = POLY_OPA_DISP;
    xluSlot = POLY_XLU_DISP;
    gDPNoOp(POLY_OPA_DISP++);
    gDPNoOp(POLY_OPA_DISP++);
    gDPNoOp(POLY_XLU_DISP++);
    gDPNoOp(POLY_XLU_DISP++);

    actor->draw(actor, play);

    // @recomp Tag the actor as a whole if it loaded exactly one matrix in a list. More than one means a
    // skeleton (already tagged per limb) or something the order of loads can't identify reliably.
    if (actorId != TRANSFORM_ID_NONE) {
        if (count_matrix_loads(opaSlot, POLY_OPA_DISP) == 1) {
            gEXMatrixGroupDecomposedObject(opaSlot, actorId + ACTOR_TRANSFORM_WHOLE_OPA_OFFSET, G_EX_PUSH,
                                           G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
            POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        }
        if (count_matrix_loads(xluSlot, POLY_XLU_DISP) == 1) {
            gEXMatrixGroupDecomposedObject(xluSlot, actorId + ACTOR_TRANSFORM_WHOLE_XLU_OFFSET, G_EX_PUSH,
                                           G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
            POLY_XLU_DISP = tag_pop(POLY_XLU_DISP);
        }
    }

    if (actor->colorFilterTimer != 0) {
        if (actor->colorFilterParams & COLORFILTER_BUFFLAG_XLU) {
            func_80026A6C(play);
        } else {
            func_80026608(play);
        }
    }

    // @recomp Tag the shadow, which draws after the actor's own tags have closed.
    if (actor->shape.shadowDraw != NULL) {
        if (actorId != TRANSFORM_ID_NONE) {
            POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_SHADOW_OFFSET);
            POLY_XLU_DISP = tag_push(POLY_XLU_DISP, actorId + ACTOR_TRANSFORM_SHADOW_OFFSET);
        }
        actor->shape.shadowDraw(actor, lights, play);
        if (actorId != TRANSFORM_ID_NONE) {
            POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
            POLY_XLU_DISP = tag_pop(POLY_XLU_DISP);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx, "../z_actor.c", 6119);

    Fault_RemoveClient(&faultClient);
}

// The skeleton patches below are the originals from src/code/z_skelanime.c with each limb's matrix
// load wrapped in a group (opened before the override so a replaced limb is tagged too), and the
// post-limb callback, which draws things attached to the limb, wrapped in a group of its own.
// `arg` is the actor for almost every caller; anything that isn't a registered actor goes untagged.

// @recomp Patched to tag each limb.
RECOMP_PATCH void SkelAnime_DrawLimbOpa(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                        OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg) {
    StandardLimb* limb;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1076);
    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];
    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;
    dList = limb->dList;

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + limbIndex);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_skelanime.c", 1103);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + limbIndex);
    }

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &dList, &rot, arg);
    }

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawLimbOpa(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawLimbOpa(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg);
    }
    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1121);
}

// @recomp Patched to tag the root limb.
RECOMP_PATCH void SkelAnime_DrawOpa(PlayState* play, void** skeleton, Vec3s* jointTable, OverrideLimbDrawOpa overrideLimbDraw,
                                    PostLimbDrawOpa postLimbDraw, void* arg) {
    StandardLimb* rootLimb;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId;

    if (skeleton == NULL) {
        return;
    }
    actorId = actor_transform_id(arg, false);

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1148);

    Matrix_Push();
    rootLimb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];
    dList = rootLimb->dList;

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + 1);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_skelanime.c", 1176);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + 1);
    }

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &dList, &rot, arg);
    }

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawLimbOpa(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg);
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1190);
}

// @recomp Patched to tag each limb.
RECOMP_PATCH void SkelAnime_DrawFlexLimbOpa(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                            OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg,
                                            Mtx** limbMatrices) {
    StandardLimb* limb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1214);

    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    newDList = limbDList = limb->dList;

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + limbIndex);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(*limbMatrices, "../z_skelanime.c", 1242);
            gSPMatrix(POLY_OPA_DISP++, *limbMatrices, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            (*limbMatrices)++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(*limbMatrices, "../z_skelanime.c", 1249);
            (*limbMatrices)++;
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + limbIndex);
    }

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &limbDList, &rot, arg);
    }

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbOpa(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                  limbMatrices);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawFlexLimbOpa(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                  limbMatrices);
    }
    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1265);
}

// @recomp Patched to tag the root limb.
RECOMP_PATCH void SkelAnime_DrawFlexOpa(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount,
                                        OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg) {
    StandardLimb* rootLimb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    Mtx* mtx = GRAPH_ALLOC(play->state.gfxCtx, dListCount * sizeof(Mtx));
    u32 actorId;

    if (skeleton == NULL) {
        return;
    }
    actorId = actor_transform_id(arg, false);

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1294);

    gSPSegment(POLY_OPA_DISP++, 0xD, mtx);

    Matrix_Push();

    rootLimb = SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];

    newDList = limbDList = rootLimb->dList;

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + 1);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(mtx, "../z_skelanime.c", 1327);
            gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            mtx++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(mtx, "../z_skelanime.c", 1334);
            mtx++;
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + 1);
    }

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &limbDList, &rot, arg);
    }

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbOpa(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                  &mtx);
    }

    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1347);
}

// @recomp Patched to tag each limb. Link draws through this level of detail variant.
RECOMP_PATCH void SkelAnime_DrawFlexLimbLod(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                                            OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg,
                                            s32 lod, Mtx** mtx) {
    LodLimb* limb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    Matrix_Push();

    limb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];
    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;
    newDList = limbDList = limb->dLists[lod];

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 946);

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + limbIndex);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(*mtx, "../z_skelanime.c", 945);
            gSPMatrix(POLY_OPA_DISP++, *mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            (*mtx)++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(*mtx, "../z_skelanime.c", 954);
            (*mtx)++;
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + limbIndex);
    }

    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &limbDList, &rot, arg);
    }

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 949);

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbLod(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod,
                                  mtx);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawFlexLimbLod(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod,
                                  mtx);
    }
}

// @recomp Patched to tag the root limb.
RECOMP_PATCH void SkelAnime_DrawFlexLod(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount,
                                        OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg, s32 lod) {
    LodLimb* rootLimb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    Mtx* mtx = GRAPH_ALLOC(play->state.gfxCtx, dListCount * sizeof(Mtx));
    u32 actorId;

    if (skeleton == NULL) {
        return;
    }
    actorId = actor_transform_id(arg, false);

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1000);

    gSPSegment(POLY_OPA_DISP++, 0xD, mtx);

    Matrix_Push();

    rootLimb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);
    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];
    newDList = limbDList = rootLimb->dLists[lod];

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + 1);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &newDList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(mtx, "../z_skelanime.c", 1033);
            gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, newDList);
            mtx++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(mtx, "../z_skelanime.c", 1040);
            mtx++;
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + 1);
    }

    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &limbDList, &rot, arg);
    }

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawFlexLimbLod(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod,
                                  &mtx);
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 1053);
}

// The remaining skeleton variants, tagged the same way: the non-flexible level of detail skeleton, and the
// skeletons drawn into a display list the caller passes in (Navi and most translucent skeletons).

// @recomp Patched to tag each limb.
RECOMP_PATCH void SkelAnime_DrawLimbLod(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                           OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* arg, s32 lod) {
    LodLimb* limb;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 773);

    Matrix_Push();
    limb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    dList = limb->dLists[lod];

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + limbIndex);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_skelanime.c", 805);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + limbIndex);
    }
    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &dList, &rot, arg);
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    if (limb->child != LIMB_DONE) {
        SkelAnime_DrawLimbLod(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        SkelAnime_DrawLimbLod(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod);
    }

    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 821);
}

// @recomp Patched to tag the root limb.
RECOMP_PATCH void SkelAnime_DrawLod(PlayState* play, void** skeleton, Vec3s* jointTable, OverrideLimbDrawOpa overrideLimbDraw,
                       PostLimbDrawOpa postLimbDraw, void* arg, s32 lod) {
    LodLimb* rootLimb;
    STACK_PAD(s32);
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    if (skeleton == NULL) {
        PRINTF_COLOR_RED();
        PRINTF(T("Si2_Lod_draw():skelがNULLです。\n", "Si2_Lod_draw(): skel is NULL.\n"));
        PRINTF_RST();
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, "../z_skelanime.c", 849);

    Matrix_Push();

    rootLimb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);
    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];
    dList = rootLimb->dLists[lod];

    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + 1);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &dList, &pos, &rot, arg)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_skelanime.c", 881);
            gSPDisplayList(POLY_OPA_DISP++, dList);
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        POLY_OPA_DISP = tag_push(POLY_OPA_DISP, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + 1);
    }
    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &dList, &rot, arg);
    }
    if (actorId != TRANSFORM_ID_NONE) {
        POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
    }

    if (rootLimb->child != LIMB_DONE) {
        SkelAnime_DrawLimbLod(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, lod);
    }

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx, "../z_skelanime.c", 894);
}

// @recomp Patched to tag each limb.
RECOMP_PATCH Gfx* SkelAnime_DrawLimb(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                        OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw, void* arg, Gfx* gfx) {
    StandardLimb* limb;
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;

    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    dList = limb->dList;

    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + limbIndex);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &dList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(gfx++, play->state.gfxCtx, "../z_skelanime.c", 1489);
            gSPDisplayList(gfx++, dList);
        }
    }

    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + limbIndex);
    }
    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &dList, &rot, arg, &gfx);
    }
    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
    }

    if (limb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawLimb(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, gfx);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        gfx = SkelAnime_DrawLimb(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, gfx);
    }

    return gfx;
}

// @recomp Patched to tag the root limb.
RECOMP_PATCH Gfx* SkelAnime_Draw(PlayState* play, void** skeleton, Vec3s* jointTable, OverrideLimbDraw overrideLimbDraw,
                    PostLimbDraw postLimbDraw, void* arg, Gfx* gfx) {
    StandardLimb* rootLimb;
    STACK_PAD(s32);
    Gfx* dList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    if (skeleton == NULL) {
        PRINTF_COLOR_RED();
        PRINTF(T("Si2_draw2():skelがNULLです。NULLを返します。\n", "Si2_draw2(): skel is NULL. Returns NULL.\n"));
        PRINTF_RST();
        return NULL;
    }

    Matrix_Push();

    rootLimb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];

    dList = rootLimb->dList;

    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + 1);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &dList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (dList != NULL) {
            MATRIX_FINALIZE_AND_LOAD(gfx++, play->state.gfxCtx, "../z_skelanime.c", 1558);
            gSPDisplayList(gfx++, dList);
        }
    }

    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + 1);
    }
    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &dList, &rot, arg, &gfx);
    }
    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
    }

    if (rootLimb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawLimb(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, gfx);
    }

    Matrix_Pop();

    return gfx;
}

// @recomp Patched to tag each limb.
RECOMP_PATCH Gfx* SkelAnime_DrawFlexLimb(PlayState* play, s32 limbIndex, void** skeleton, Vec3s* jointTable,
                            OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw, void* arg, Mtx** mtx,
                            Gfx* gfx) {
    StandardLimb* limb;
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);

    Matrix_Push();

    limb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    limbIndex++;
    rot = jointTable[limbIndex];

    pos.x = limb->jointPos.x;
    pos.y = limb->jointPos.y;
    pos.z = limb->jointPos.z;

    newDList = limbDList = limb->dList;
    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + limbIndex);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, limbIndex, &newDList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(*mtx, "../z_skelanime.c", 1623);
            gSPMatrix(gfx++, *mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(gfx++, newDList);
            (*mtx)++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(*mtx, "../z_skelanime.c", 1630);
            (*mtx)++;
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + limbIndex);
    }
    if (postLimbDraw != NULL) {
        postLimbDraw(play, limbIndex, &limbDList, &rot, arg, &gfx);
    }
    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
    }
    if (limb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawFlexLimb(play, limb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg, mtx,
                                     gfx);
    }

    Matrix_Pop();

    if (limb->sibling != LIMB_DONE) {
        gfx = SkelAnime_DrawFlexLimb(play, limb->sibling, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                     mtx, gfx);
    }

    return gfx;
}

// @recomp Patched to tag the root limb.
RECOMP_PATCH Gfx* SkelAnime_DrawFlex(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount,
                        OverrideLimbDraw overrideLimbDraw, PostLimbDraw postLimbDraw, void* arg, Gfx* gfx) {
    StandardLimb* rootLimb;
    STACK_PAD(s32);
    Gfx* newDList;
    Gfx* limbDList;
    Vec3f pos;
    Vec3s rot;
    u32 actorId = actor_transform_id(arg, false);
    Mtx* mtx = GRAPH_ALLOC(play->state.gfxCtx, dListCount * sizeof(*mtx));

    if (skeleton == NULL) {
        PRINTF_COLOR_RED();
        PRINTF(T("Si2_draw2_SV():skelがNULLです。NULLを返します。\n", "Si2_draw2_SV(): skel is NULL. Returns NULL.\n"));
        PRINTF_RST();
        return NULL;
    }

    gSPSegment(gfx++, 0xD, mtx);
    Matrix_Push();
    rootLimb = (StandardLimb*)SEGMENTED_TO_VIRTUAL(skeleton[0]);

    pos.x = jointTable[0].x;
    pos.y = jointTable[0].y;
    pos.z = jointTable[0].z;

    rot = jointTable[1];

    newDList = limbDList = rootLimb->dList;

    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + 1);
    }
    if ((overrideLimbDraw == NULL) || !overrideLimbDraw(play, 1, &newDList, &pos, &rot, arg, &gfx)) {
        Matrix_TranslateRotateZYX(&pos, &rot);
        if (newDList != NULL) {
            MATRIX_TO_MTX(mtx, "../z_skelanime.c", 1710);
            gSPMatrix(gfx++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(gfx++, newDList);
            mtx++;
        } else if (limbDList != NULL) {
            MATRIX_TO_MTX(mtx, "../z_skelanime.c", 1717);
            mtx++;
        }
    }
    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
        gfx = tag_push(gfx, actorId + ACTOR_TRANSFORM_POST_LIMB_OFFSET + 1);
    }
    if (postLimbDraw != NULL) {
        postLimbDraw(play, 1, &limbDList, &rot, arg, &gfx);
    }
    if (actorId != TRANSFORM_ID_NONE) {
        gfx = tag_pop(gfx);
    }
    if (rootLimb->child != LIMB_DONE) {
        gfx = SkelAnime_DrawFlexLimb(play, rootLimb->child, skeleton, jointTable, overrideLimbDraw, postLimbDraw, arg,
                                     &mtx, gfx);
    }

    Matrix_Pop();

    return gfx;
}

extern MtxF gSkinLimbMatrices[60];

// @recomp Patched to tag each limb of a skinned skeleton (Epona and the other horses). A skinned limb's
// vertices are rewritten every frame, so its group interpolates vertices too, as in Zelda64Recomp. Each limb
// gets its own matrix (the current one multiplied by the identity) for the tag to attach to.
// Original: Skin_DrawImpl in src/code/z_skin.c.
RECOMP_PATCH void Skin_DrawImpl(Actor* actor, PlayState* play, Skin* skin, SkinPostDraw postDraw,
                                SkinOverrideLimbDraw overrideLimbDraw, s32 setTranslation, s32 arg6, s32 drawFlags) {
    s32 i;
    s32 segmentType;
    SkinLimb** skeleton;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Mtx* mtx;
    u32 actorId = actor_transform_id(actor, false);

    OPEN_DISPS(gfxCtx, "../z_skin.c", 471);

    if (!(drawFlags & SKIN_DRAW_FLAG_CUSTOM_TRANSFORMS)) {
        Skin_ApplyAnimTransformations(skin, gSkinLimbMatrices, actor, setTranslation);
    }

    skeleton = SEGMENTED_TO_VIRTUAL(skin->skeletonHeader->segment);

    if (!(drawFlags & SKIN_DRAW_FLAG_CUSTOM_MATRIX)) {
        gSPMatrix(POLY_OPA_DISP++, &gIdentityMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        mtx = SkinMatrix_MtxFToNewMtx(gfxCtx, &skin->mtx);

        if (mtx == NULL) {
            goto close_disps;
        }

        gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    }

    for (i = 0; i < skin->skeletonHeader->limbCount; i++) {
        s32 shouldDraw = true;

        if (overrideLimbDraw != NULL) {
            shouldDraw = overrideLimbDraw(actor, play, i, skin);
        }

        segmentType = ((SkinLimb*)SEGMENTED_TO_VIRTUAL(skeleton[i]))->segmentType;

        // @recomp Give the limb a matrix of its own and tag it.
        if (actorId != TRANSFORM_ID_NONE) {
            gSPMatrix(POLY_OPA_DISP++, &gIdentityMtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
            gEXMatrixGroupDecomposedVerts(POLY_OPA_DISP++, actorId + ACTOR_TRANSFORM_LIMB_OFFSET + i, G_EX_PUSH,
                                          G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
        }

        if (segmentType == SKIN_LIMB_TYPE_ANIMATED && shouldDraw == true) {
            Skin_DrawAnimatedLimb(gfxCtx, skin, i, arg6, drawFlags);
        } else if (segmentType == SKIN_LIMB_TYPE_NORMAL && shouldDraw == true) {
            Skin_DrawLimb(gfxCtx, skin, i, NULL, drawFlags);
        }

        // @recomp Pop the limb's matrix and tag.
        if (actorId != TRANSFORM_ID_NONE) {
            gSPPopMatrix(POLY_OPA_DISP++, G_MTX_MODELVIEW);
            POLY_OPA_DISP = tag_pop(POLY_OPA_DISP);
        }
    }

    if (postDraw != NULL) {
        postDraw(actor, play, skin);
    }

close_disps:
    CLOSE_DISPS(gfxCtx, "../z_skin.c", 534);
}
