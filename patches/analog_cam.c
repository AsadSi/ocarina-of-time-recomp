#include "camera_patches.h"
#include "input.h"
#include "ocarina.h"
#include "debug.h"
#include "attributes.h"
#include "stack_pad.h"
#include "rand.h"
#include "debug_display.h"
#include "regs.h"

// Analog camera, ported from Zelda64Recomp: with the setting on, the right stick turns the camera in the
// free camera modes (on foot, jumping, swimming, riding). The camera stays where the stick left it until
// Z targeting resets it behind Link, or until a camera mode without analog control takes over.

extern s8 sOcarinaInstrumentId;

static int sAnalogCamActive = false;
static int sCanUseAnalogCam = false;
static VecGeo sAnalogCamPos = { 66.0f, 0, 0 };

// Per game frame. Zelda64Recomp's values are for Majora's Mask's 20 frames per second; these give the
// same turning speed at PAL OoT's 50/3.
#define ANALOG_CAM_YAW_SPEED 1800.0f
#define ANALOG_CAM_PITCH_SPEED 600.0f
#define ANALOG_CAM_THRESHOLD 0.1f
#define ANALOG_CAM_PITCH_MAX 0x36B0
#define ANALOG_CAM_PITCH_MIN -0x16D0

// Called by the patched camera functions with the at-to-eye direction they are about to use. Overrides it
// with the analog camera's while that is active.
int analog_cam_update(Camera* camera, VecGeo* eyeAtGeo) {
    static int sPrevTargeting = false;
    Player* player = GET_PLAYER(camera->play);
    int targeting;
    f32 inputX;
    f32 inputY;

    if (!recomp_get_analog_cam_enabled()) {
        return false;
    }
    sCanUseAnalogCam = true;

    // Starting to Z target puts the camera back behind Link.
    targeting = (player->stateFlags1 & PLAYER_STATE1_PARALLEL) || (player->focusActor != NULL);
    if (targeting && !sPrevTargeting) {
        sAnalogCamActive = false;
    }
    sPrevTargeting = targeting;

    recomp_get_camera_inputs(&inputX, &inputY);
    if ((fabsf(inputX) >= ANALOG_CAM_THRESHOLD) || (fabsf(inputY) >= ANALOG_CAM_THRESHOLD)) {
        sAnalogCamActive = true;
    }
    if (!sAnalogCamActive) {
        return false;
    }

    {
        s32 invertX;
        s32 invertY;

        recomp_get_analog_inverted_axes(&invertX, &invertY);
        if (invertX) {
            inputX = -inputX;
        }
        if (invertY) {
            inputY = -inputY;
        }
    }

    sAnalogCamPos.yaw += (s16)(-inputX * ANALOG_CAM_YAW_SPEED);
    sAnalogCamPos.pitch += (s16)(inputY * ANALOG_CAM_PITCH_SPEED);
    sAnalogCamPos.pitch = CLAMP(sAnalogCamPos.pitch, ANALOG_CAM_PITCH_MIN, ANALOG_CAM_PITCH_MAX);

    eyeAtGeo->pitch = sAnalogCamPos.pitch;
    eyeAtGeo->yaw = sAnalogCamPos.yaw;
    return true;
}

// Called once per frame after the play state updates.
void analog_cam_post_play_update(PlayState* play) {
    Camera* camera = GET_ACTIVE_CAM(play);

    // The right stick turns the camera instead of pressing C buttons, except while the ocarina is out.
    recomp_set_right_analog_suppressed(recomp_get_analog_cam_enabled() && (sOcarinaInstrumentId == OCARINA_INSTRUMENT_OFF));

    if ((play->pauseCtx.state == PAUSE_STATE_OFF) && (R_PAUSE_BG_PRERENDER_STATE == PAUSE_BG_PRERENDER_OFF)) {
        // A frame whose camera mode has no analog control ends it; until the stick is used again, the
        // analog camera follows the real one.
        if (!sCanUseAnalogCam) {
            sAnalogCamActive = false;
        }
        if (!sAnalogCamActive) {
            VecGeo geo = OLib_Vec3fDiffToVecGeo(&camera->at, &camera->eye);

            sAnalogCamPos.yaw = geo.yaw;
            sAnalogCamPos.pitch = geo.pitch;
        }
        sCanUseAnalogCam = false;
    }

    // Link's stick input is relative to this direction.
    if (sAnalogCamActive) {
        camera->inputDir.x = sAnalogCamPos.pitch;
        camera->inputDir.y = sAnalogCamPos.yaw + 0x8000;
    }
}

// @recomp Patched for the analog camera. Original: Camera_Normal1 in src/code/z_camera.c.
RECOMP_PATCH s32 Camera_Normal1(Camera* camera) {
    Vec3f* eye = &camera->eye;
    Vec3f* at = &camera->at;
    Vec3f* eyeNext = &camera->eyeNext;
    f32 spA0;
    f32 sp9C;
    f32 sp98;
    f32 sp94;
    Vec3f sp88;
    s16 wiggleAdj;
    s16 t;
    VecGeo eyeAdjustment;
    VecGeo atEyeGeo;
    VecGeo atEyeNextGeo;
    UNUSED PosRot* playerPosRot = &camera->playerPosRot;
    Normal1ReadOnlyData* roData = &camera->paramData.norm1.roData;
    Normal1ReadWriteData* rwData = &camera->paramData.norm1.rwData;
    f32 playerHeight;
    f32 rate = 0.1f;

    playerHeight = Player_GetHeight(camera->player);
    if (RELOAD_PARAMS(camera) || CAM_DEBUG_RELOAD_PARAMS) {
        CameraModeValue* values = sCameraSettings[camera->setting].cameraModes[camera->mode].values;
        f32 yNormal = (1.0f + CAM_YOFFSET_NORM - CAM_YOFFSET_NORM * (68.0f / playerHeight));

        sp94 = yNormal * CAM_DATA_SCALED(playerHeight);

        roData->yOffset = GET_NEXT_RO_DATA(values) * sp94;
        roData->distMin = GET_NEXT_RO_DATA(values) * sp94;
        roData->distMax = GET_NEXT_RO_DATA(values) * sp94;
        roData->pitchTarget = CAM_DEG_TO_BINANG(GET_NEXT_RO_DATA(values));
        roData->unk_0C = GET_NEXT_RO_DATA(values);
        roData->unk_10 = GET_NEXT_RO_DATA(values);
        roData->unk_14 = GET_NEXT_SCALED_RO_DATA(values);
        roData->fovTarget = GET_NEXT_RO_DATA(values);
        roData->atLERPScaleMax = GET_NEXT_SCALED_RO_DATA(values);
        roData->interfaceField = GET_NEXT_RO_DATA(values);
    }

    CAM_DEBUG_RELOAD_PREG(camera);

    sCameraInterfaceField = roData->interfaceField;

    atEyeGeo = OLib_Vec3fDiffToVecGeo(at, eye);
    atEyeNextGeo = OLib_Vec3fDiffToVecGeo(at, eyeNext);

    switch (camera->animState) {
        case 20:
            camera->yawUpdateRateInv = CAM_GLOBAL_27;
            camera->pitchUpdateRateInv = CAM_GLOBAL_27;
            FALLTHROUGH;
        case 0:
        case 10:
        case 25:
            rwData->swing.atEyePoly = NULL;
            rwData->slopePitchAdj = 0;
            rwData->unk_28 = 0xA;
            rwData->swing.unk_16 = rwData->swing.unk_14 = rwData->swing.unk_18 = 0;
            rwData->swing.swingUpdateRate = roData->unk_0C;
            rwData->yOffset = camera->playerPosRot.pos.y;
            rwData->unk_20 = camera->xzSpeed;
            rwData->swing.swingUpdateRateTimer = 0;
            rwData->swingYawTarget = atEyeGeo.yaw;
            sUpdateCameraDirection = 0;
            rwData->startSwingTimer = CAM_GLOBAL_50 + CAM_GLOBAL_51;
            break;
        default:
            break;
    }

    camera->animState = 1;
    sUpdateCameraDirection = 1;

    if (rwData->unk_28 != 0) {
        rwData->unk_28--;
    }

    if (camera->xzSpeed > 0.001f) {
        rwData->startSwingTimer = CAM_GLOBAL_50 + CAM_GLOBAL_51;
    } else if (rwData->startSwingTimer > 0) {
        if (rwData->startSwingTimer > CAM_GLOBAL_50) {
            rwData->swingYawTarget = atEyeGeo.yaw + ((s16)((s16)(camera->playerPosRot.rot.y - 0x7FFF) - atEyeGeo.yaw) /
                                                     rwData->startSwingTimer);
        }
        rwData->startSwingTimer--;
    }

    spA0 = camera->speedRatio * CAM_UPDATE_RATE_STEP_SCALE_XZ;
    sp9C = camera->speedRatio * CAM_UPDATE_RATE_STEP_SCALE_Y;
    sp98 = rwData->swing.unk_18 != 0 ? CAM_UPDATE_RATE_STEP_SCALE_XZ : spA0;

    sp94 = (camera->xzSpeed - rwData->unk_20) * (0.333333f);
    if (sp94 > 1.0f) {
        sp94 = 1.0f;
    }
    if (sp94 > -1.0f) {
        sp94 = -1.0f;
    }

    rwData->unk_20 = camera->xzSpeed;

    if (rwData->swing.swingUpdateRateTimer != 0) {
        camera->yawUpdateRateInv =
            Camera_LERPCeilF(rwData->swing.swingUpdateRate + (f32)(rwData->swing.swingUpdateRateTimer * 2),
                             camera->yawUpdateRateInv, sp98, rate);
        camera->pitchUpdateRateInv =
            Camera_LERPCeilF((f32)CAM_PITCH_UPDATE_RATE_INV + (f32)(rwData->swing.swingUpdateRateTimer * 2),
                             camera->pitchUpdateRateInv, sp9C, rate);
        rwData->swing.swingUpdateRateTimer--;
    } else {
        camera->yawUpdateRateInv =
            Camera_LERPCeilF(rwData->swing.swingUpdateRate - (rwData->swing.swingUpdateRate * CAM_GLOBAL_49 * sp94),
                             camera->yawUpdateRateInv, sp98, rate);
        camera->pitchUpdateRateInv =
            Camera_LERPCeilF(CAM_PITCH_UPDATE_RATE_INV, camera->pitchUpdateRateInv, sp9C, rate);
    }

    camera->pitchUpdateRateInv = Camera_LERPCeilF(CAM_PITCH_UPDATE_RATE_INV, camera->pitchUpdateRateInv, sp9C, rate);
    camera->xzOffsetUpdateRate = Camera_LERPCeilF(CAM_XZ_OFFSET_UPDATE_RATE, camera->xzOffsetUpdateRate, spA0, rate);
    camera->yOffsetUpdateRate = Camera_LERPCeilF(CAM_Y_OFFSET_UPDATE_RATE, camera->yOffsetUpdateRate, sp9C, rate);
    camera->fovUpdateRate =
        Camera_LERPCeilF(CAM_FOV_UPDATE_RATE, camera->yOffsetUpdateRate, camera->speedRatio * 0.05f, rate);

    if (roData->interfaceField & NORMAL1_FLAG_0) {
        t = Camera_GetPitchAdjFromFloorHeightDiffs(camera, atEyeGeo.yaw - 0x7FFF, false);
        sp9C = ((1.0f / roData->unk_10) * 0.5f) * (1.0f - camera->speedRatio);
        rwData->slopePitchAdj =
            Camera_LERPCeilS(t, rwData->slopePitchAdj, ((1.0f / roData->unk_10) * 0.5f) + sp9C, 0xF);
    } else {
        rwData->slopePitchAdj = 0;
        if (camera->playerGroundY == camera->playerPosRot.pos.y) {
            rwData->yOffset = camera->playerPosRot.pos.y;
        }
    }

    spA0 = ((rwData->swing.unk_18 != 0) && (roData->yOffset > -40.0f))
               ? (sp9C = Math_SinS(rwData->swing.unk_14), ((-40.0f * sp9C) + (roData->yOffset * (1.0f - sp9C))))
               : roData->yOffset;

    if (roData->interfaceField & NORMAL1_FLAG_7) {
        func_800458D4(camera, &atEyeNextGeo, spA0, &rwData->yOffset, roData->interfaceField & NORMAL1_FLAG_0);
    } else if (roData->interfaceField & NORMAL1_FLAG_5) {
        func_80045B08(camera, &atEyeNextGeo, spA0, rwData->slopePitchAdj);
    } else {
        Camera_CalcAtDefault(camera, &atEyeNextGeo, spA0, roData->interfaceField & NORMAL1_FLAG_0);
    }

    eyeAdjustment = OLib_Vec3fDiffToVecGeo(at, eyeNext);

    camera->dist = eyeAdjustment.r =
        Camera_ClampDist(camera, eyeAdjustment.r, roData->distMin, roData->distMax, rwData->unk_28);

    if (rwData->startSwingTimer <= 0) {
        eyeAdjustment.pitch = atEyeNextGeo.pitch;
        eyeAdjustment.yaw =
            Camera_LERPCeilS(rwData->swingYawTarget, atEyeNextGeo.yaw, 1.0f / camera->yawUpdateRateInv, 0xA);
    } else if (rwData->swing.unk_18 != 0) {
        eyeAdjustment.yaw =
            Camera_LERPCeilS(rwData->swing.unk_16, atEyeNextGeo.yaw, 1.0f / camera->yawUpdateRateInv, 0xA);
        eyeAdjustment.pitch =
            Camera_LERPCeilS(rwData->swing.unk_14, atEyeNextGeo.pitch, 1.0f / camera->yawUpdateRateInv, 0xA);
    } else {
        // rotate yaw to follow player.
        eyeAdjustment.yaw =
            Camera_CalcDefaultYaw(camera, atEyeNextGeo.yaw, camera->playerPosRot.rot.y, roData->unk_14, sp94);
        eyeAdjustment.pitch =
            Camera_CalcDefaultPitch(camera, atEyeNextGeo.pitch, roData->pitchTarget, rwData->slopePitchAdj);
    }

    // set eyeAdjustment pitch from 79.65 degrees to -85 degrees
    if (eyeAdjustment.pitch > 0x38A4) {
        eyeAdjustment.pitch = 0x38A4;
    }
    if (eyeAdjustment.pitch < -0x3C8C) {
        eyeAdjustment.pitch = -0x3C8C;
    }

    // @recomp Let the right stick steer the camera.
    analog_cam_update(camera, &eyeAdjustment);

    *eyeNext = Camera_AddVecGeoToVec3f(at, &eyeAdjustment);
    if ((camera->status == CAM_STAT_ACTIVE) && !(roData->interfaceField & NORMAL1_FLAG_4)) {
        rwData->swingYawTarget = camera->playerPosRot.rot.y - 0x7FFF;
        if (rwData->startSwingTimer > 0) {
            func_80046E20(camera, &eyeAdjustment, roData->distMin, roData->unk_0C, &sp98, &rwData->swing);
        } else {
            sp88 = *eyeNext;
            rwData->swing.swingUpdateRate = camera->yawUpdateRateInv = roData->unk_0C * 2.0f;
            if (Camera_BGCheck(camera, at, &sp88)) {
                rwData->swingYawTarget = atEyeNextGeo.yaw;
                rwData->startSwingTimer = -1;
            } else {
                *eye = *eyeNext;
            }
            rwData->swing.unk_18 = 0;
        }

        if (rwData->swing.unk_18 != 0) {
            camera->inputDir.y =
                Camera_LERPCeilS(camera->inputDir.y + (s16)((s16)(rwData->swing.unk_16 - 0x7FFF) - camera->inputDir.y),
                                 camera->inputDir.y, 1.0f - (0.99f * sp98), 0xA);
        }

        if (roData->interfaceField & NORMAL1_FLAG_2) {
            camera->inputDir.x = -atEyeGeo.pitch;
            camera->inputDir.y = atEyeGeo.yaw - 0x7FFF;
            camera->inputDir.z = 0;
        } else {
            eyeAdjustment = OLib_Vec3fDiffToVecGeo(eye, at);
            camera->inputDir.x = eyeAdjustment.pitch;
            camera->inputDir.y = eyeAdjustment.yaw;
            camera->inputDir.z = 0;
        }

        // crit wiggle
        if (gSaveContext.save.info.playerData.health <= 16 && ((camera->play->state.frames % 256) == 0)) {
            wiggleAdj = Rand_ZeroOne() * 10000.0f;
            camera->inputDir.y = wiggleAdj + camera->inputDir.y;
        }
    } else {
        rwData->swing.swingUpdateRate = roData->unk_0C;
        rwData->swing.unk_18 = 0;
        sUpdateCameraDirection = 0;
        *eye = *eyeNext;
    }

    spA0 = (gSaveContext.save.info.playerData.health <= 16 ? 0.8f : 1.0f);
    camera->fov = Camera_LERPCeilF(roData->fovTarget * spA0, camera->fov, camera->fovUpdateRate, 1.0f);
    camera->roll = Camera_LERPCeilS(0, camera->roll, 0.5f, 0xA);
    camera->atLERPStepScale = Camera_ClampLERPScale(camera, roData->atLERPScaleMax);
    return 1;
}

// @recomp Patched for the analog camera (jumping and falling). Original: Camera_Jump1 in src/code/z_camera.c.
RECOMP_PATCH s32 Camera_Jump1(Camera* camera) {
    Vec3f* eye = &camera->eye;
    Vec3f* at = &camera->at;
    Vec3f* eyeNext = &camera->eyeNext;
    STACK_PAD(s32);
    f32 spA4;
    Vec3f newEye;
    VecGeo eyeAtOffset;
    VecGeo eyeNextAtOffset;
    VecGeo eyeDiffGeo;
    VecGeo eyeDiffTarget;
    PosRot* playerPosRot = &camera->playerPosRot;
    UNUSED PosRot playerhead;
    STACK_PAD(s16);
    Jump1ReadOnlyData* roData = &camera->paramData.jump1.roData;
    Jump1ReadWriteData* rwData = &camera->paramData.jump1.rwData;
    STACK_PAD(s32);
    f32 playerHeight;

    playerHeight = Player_GetHeight(camera->player);
    if (RELOAD_PARAMS(camera) || CAM_DEBUG_RELOAD_PARAMS) {
        CameraModeValue* values = sCameraSettings[camera->setting].cameraModes[camera->mode].values;
        f32 yNormal = 1.0f + CAM_YOFFSET_NORM - (CAM_YOFFSET_NORM * (68.0f / playerHeight));

        roData->atYOffset = CAM_DATA_SCALED(GET_NEXT_RO_DATA(values)) * playerHeight * yNormal;
        roData->distMin = CAM_DATA_SCALED(GET_NEXT_RO_DATA(values)) * playerHeight * yNormal;
        roData->distMax = CAM_DATA_SCALED(GET_NEXT_RO_DATA(values)) * playerHeight * yNormal;
        roData->yawUpateRateTarget = GET_NEXT_RO_DATA(values);
        roData->maxYawUpdate = CAM_DATA_SCALED(GET_NEXT_RO_DATA(values));
        roData->unk_14 = GET_NEXT_RO_DATA(values);
        roData->atLERPScaleMax = CAM_DATA_SCALED(GET_NEXT_RO_DATA(values));
        roData->interfaceField = GET_NEXT_RO_DATA(values);
    }

    CAM_DEBUG_RELOAD_PREG(camera);

    playerhead = Actor_GetFocus(&camera->player->actor);

    eyeAtOffset = OLib_Vec3fDiffToVecGeo(at, eye);
    eyeNextAtOffset = OLib_Vec3fDiffToVecGeo(at, eyeNext);

    sCameraInterfaceField = roData->interfaceField;

    if (RELOAD_PARAMS(camera)) {
        rwData->swing.unk_16 = rwData->swing.unk_18 = 0;
        rwData->swing.atEyePoly = NULL;
        rwData->unk_24 = 0;
        rwData->unk_26 = 200;
        rwData->swing.swingUpdateRateTimer = 0;
        rwData->swing.swingUpdateRate = roData->yawUpateRateTarget;
        rwData->unk_1C = playerPosRot->pos.y - camera->playerPosDelta.y;
        rwData->unk_20 = eyeAtOffset.r;
        camera->playerToAtOffset.y -= camera->playerPosDelta.y;
        camera->xzOffsetUpdateRate = (1.0f / 10000.0f);
        camera->animState++;
    }

    if (rwData->swing.swingUpdateRateTimer != 0) {
        camera->yawUpdateRateInv = Camera_LERPCeilF(roData->yawUpateRateTarget + rwData->swing.swingUpdateRateTimer,
                                                    camera->yawUpdateRateInv, CAM_UPDATE_RATE_STEP_SCALE_Y, 0.1f);
        camera->pitchUpdateRateInv =
            Camera_LERPCeilF((f32)CAM_PITCH_UPDATE_RATE_INV + rwData->swing.swingUpdateRateTimer,
                             camera->pitchUpdateRateInv, CAM_UPDATE_RATE_STEP_SCALE_Y, 0.1f);
        rwData->swing.swingUpdateRateTimer--;
    } else {
        camera->yawUpdateRateInv =
            Camera_LERPCeilF(roData->yawUpateRateTarget, camera->yawUpdateRateInv, CAM_UPDATE_RATE_STEP_SCALE_Y, 0.1f);
        camera->pitchUpdateRateInv = Camera_LERPCeilF((f32)CAM_PITCH_UPDATE_RATE_INV, camera->pitchUpdateRateInv,
                                                      CAM_UPDATE_RATE_STEP_SCALE_Y, 0.1f);
    }

    camera->xzOffsetUpdateRate =
        Camera_LERPCeilF(CAM_XZ_OFFSET_UPDATE_RATE, camera->xzOffsetUpdateRate, CAM_UPDATE_RATE_STEP_SCALE_XZ, 0.1f);
    camera->yOffsetUpdateRate =
        Camera_LERPCeilF(CAM_Y_OFFSET_UPDATE_RATE, camera->yOffsetUpdateRate, CAM_UPDATE_RATE_STEP_SCALE_Y, 0.1f);
    camera->fovUpdateRate = Camera_LERPCeilF(CAM_FOV_UPDATE_RATE, camera->yOffsetUpdateRate, 0.05f, 0.1f);

    func_800458D4(camera, &eyeNextAtOffset, roData->atYOffset, &rwData->unk_1C, false);

    eyeDiffGeo = eyeAtOffset;

    eyeDiffTarget = OLib_Vec3fDiffToVecGeo(at, eye);

    eyeDiffGeo.r = Camera_LERPCeilF(eyeDiffTarget.r, eyeAtOffset.r, CAM_GLOBAL_29, 1.0f);
    eyeDiffGeo.pitch = Camera_LERPCeilS(eyeDiffTarget.pitch, eyeAtOffset.pitch, CAM_GLOBAL_29, 0xA);

    if (rwData->swing.unk_18) {
        eyeDiffGeo.yaw =
            Camera_LERPCeilS(rwData->swing.unk_16, eyeNextAtOffset.yaw, 1.0f / camera->yawUpdateRateInv, 0xA);
        eyeDiffGeo.pitch =
            Camera_LERPCeilS(rwData->swing.unk_14, eyeNextAtOffset.pitch, 1.0f / camera->yawUpdateRateInv, 0xA);
    } else {
        eyeDiffGeo.yaw =
            Camera_CalcDefaultYaw(camera, eyeNextAtOffset.yaw, camera->playerPosRot.rot.y, roData->maxYawUpdate, 0.0f);
    }

    // Clamp the eye->at distance to roData->distMin < eyeDiffGeo.r < roData->distMax
    if (eyeDiffGeo.r < roData->distMin) {
        eyeDiffGeo.r = roData->distMin;
    } else if (eyeDiffGeo.r > roData->distMax) {
        eyeDiffGeo.r = roData->distMax;
    }

    // Clamp the phi rotation at CAM_MAX_PITCH AND CAM_MIN_PITCH_2
    if (eyeDiffGeo.pitch > CAM_MAX_PITCH) {
        eyeDiffGeo.pitch = CAM_MAX_PITCH;
    } else if (eyeDiffGeo.pitch < CAM_MIN_PITCH_2) {
        eyeDiffGeo.pitch = CAM_MIN_PITCH_2;
    }

    // @recomp Let the right stick steer the camera.
    analog_cam_update(camera, &eyeDiffGeo);

    newEye = Camera_AddVecGeoToVec3f(at, &eyeDiffGeo);
    eyeNext->x = newEye.x;
    eyeNext->z = newEye.z;
    eyeNext->y += (newEye.y - eyeNext->y) * CAM_JUMP1_EYE_Y_STEP_SCALE;
    if ((camera->status == CAM_STAT_ACTIVE) && !(roData->interfaceField & JUMP1_FLAG_4)) {
        func_80046E20(camera, &eyeDiffGeo, roData->distMin, roData->yawUpateRateTarget, &spA4, &rwData->swing);
        if (roData->interfaceField & JUMP1_FLAG_2) {
            camera->inputDir.x = -eyeAtOffset.pitch;
            camera->inputDir.y = eyeAtOffset.yaw - 0x7FFF;
            camera->inputDir.z = 0;
        } else {
            eyeDiffGeo = OLib_Vec3fDiffToVecGeo(eye, at);
            camera->inputDir.x = eyeDiffGeo.pitch;
            camera->inputDir.y = eyeDiffGeo.yaw;
            camera->inputDir.z = 0;
        }
        if (rwData->swing.unk_18) {
            camera->inputDir.y =
                Camera_LERPCeilS(camera->inputDir.y + (s16)((s16)(rwData->swing.unk_16 - 0x7FFF) - camera->inputDir.y),
                                 camera->inputDir.y, 1.0f - (0.99f * spA4), 0xA);
        }
    } else {
        rwData->swing.swingUpdateRate = roData->yawUpateRateTarget;
        rwData->swing.unk_18 = 0;
        sUpdateCameraDirection = 0;
        *eye = *eyeNext;
    }

    camera->dist = OLib_Vec3fDist(at, eye);
    camera->roll = Camera_LERPCeilS(0, camera->roll, 0.5f, 0xA);
    camera->atLERPStepScale = Camera_ClampLERPScale(camera, roData->atLERPScaleMax);
    return true;
}

// @recomp Patched for the analog camera (swimming). Original: Camera_Jump3 in src/code/z_camera.c.
RECOMP_PATCH s32 Camera_Jump3(Camera* camera) {
    Vec3f* eye = &camera->eye;
    Vec3f* at = &camera->at;
    Vec3f* eyeNext = &camera->eyeNext;
    UNUSED_NDEBUG s32 prevMode;
    f32 spC4;
    f32 spC0;
    f32 spBC;
    UNUSED Vec3f spB0;
    VecGeo eyeDiffGeo;
    PosRot* playerPosRot = &camera->playerPosRot;
    Jump3ReadOnlyData* roData = &camera->paramData.jump3.roData;
    VecGeo eyeAtOffset;
    VecGeo eyeNextAtOffset;
    STACK_PADS(s32, 2);
    CameraModeValue* values;
    f32 t2;
    f32 phi_f0;
    f32 phi_f2;
    f32 playerHeight;
    PosRot playerhead;
    f32 yNormal;
    STACK_PAD(s32);
    s32 modeSwitch;
    STACK_PAD(s32);
    Jump3ReadWriteData* rwData = &camera->paramData.jump3.rwData;

    playerHeight = Player_GetHeight(camera->player);
    playerhead = Actor_GetFocus(&camera->player->actor);

    modeSwitch = false;
    if (((camera->waterYPos - eye->y) < CAM_GLOBAL_44 || (camera->animState == 0))) {
        if (rwData->mode != CAM_MODE_NORMAL) {
            rwData->mode = CAM_MODE_NORMAL;
            modeSwitch = true;
        }
    } else if (((camera->waterYPos - eye->y) > CAM_GLOBAL_45) && (rwData->mode != CAM_MODE_AIM_BOOMERANG)) {
        rwData->mode = CAM_MODE_AIM_BOOMERANG;
        modeSwitch = true;
    }

    eyeAtOffset = OLib_Vec3fDiffToVecGeo(at, eye);
    eyeNextAtOffset = OLib_Vec3fDiffToVecGeo(at, eyeNext);

    if (RELOAD_PARAMS(camera) || modeSwitch || CAM_DEBUG_RELOAD_PARAMS) {
        values = sCameraSettings[camera->setting].cameraModes[rwData->mode].values;
        yNormal = 1.0f + CAM_YOFFSET_NORM - (CAM_YOFFSET_NORM * (68.0f / playerHeight));
        t2 = CAM_DATA_SCALED(playerHeight) * yNormal;
        roData->yOffset = GET_NEXT_RO_DATA(values) * t2;
        roData->distMin = GET_NEXT_RO_DATA(values) * t2;
        roData->distMax = GET_NEXT_RO_DATA(values) * t2;
        roData->pitchTarget = CAM_DEG_TO_BINANG(GET_NEXT_RO_DATA(values));
        roData->swingUpdateRate = GET_NEXT_RO_DATA(values);
        roData->unk_10 = GET_NEXT_RO_DATA(values);
        roData->unk_14 = GET_NEXT_SCALED_RO_DATA(values);
        roData->fovTarget = GET_NEXT_RO_DATA(values);
        roData->unk_1C = GET_NEXT_SCALED_RO_DATA(values);
        roData->interfaceField = GET_NEXT_RO_DATA(values);
    }

#if DEBUG_FEATURES
    if (R_RELOAD_CAM_PARAMS) {
        prevMode = camera->mode;
        camera->mode = rwData->mode;
        Camera_CopyPREGToModeValues(camera);
        camera->mode = prevMode;
    }
#endif

    sCameraInterfaceField = roData->interfaceField;

    switch (camera->animState) {
        case 0:
        case 10:
        case 20:
        case 25:
            rwData->swing.atEyePoly = NULL;
            rwData->unk_1C = camera->playerGroundY;
            rwData->swing.unk_16 = rwData->swing.unk_14 = rwData->swing.unk_18 = 0;
            rwData->animTimer = 10;
            rwData->swing.swingUpdateRate = roData->swingUpdateRate;
            camera->animState++;
            rwData->swing.swingUpdateRateTimer = 0;
            break;
        default:
            if (rwData->animTimer != 0) {
                rwData->animTimer--;
            }
            break;
    }

    spB0 = *eye;

    spC4 = CAM_UPDATE_RATE_STEP_SCALE_XZ * camera->speedRatio;
    spC0 = camera->speedRatio * CAM_UPDATE_RATE_STEP_SCALE_Y;
    spBC = rwData->swing.unk_18 != 0 ? CAM_UPDATE_RATE_STEP_SCALE_XZ : spC4;

    if (rwData->swing.swingUpdateRateTimer != 0) {
        camera->yawUpdateRateInv =
            Camera_LERPCeilF(rwData->swing.swingUpdateRate + (rwData->swing.swingUpdateRateTimer * 2),
                             camera->yawUpdateRateInv, spC4, 0.1f);
        camera->pitchUpdateRateInv =
            Camera_LERPCeilF((rwData->swing.swingUpdateRateTimer * 2) + 40.0f, camera->pitchUpdateRateInv, spC0, 0.1f);
        rwData->swing.swingUpdateRateTimer--;
    } else {
        camera->yawUpdateRateInv =
            Camera_LERPCeilF(rwData->swing.swingUpdateRate, camera->yawUpdateRateInv, spBC, 0.1f);
        camera->pitchUpdateRateInv = Camera_LERPCeilF(40.0f, camera->pitchUpdateRateInv, spC0, 0.1f);
    }

    camera->xzOffsetUpdateRate = Camera_LERPCeilF(CAM_XZ_OFFSET_UPDATE_RATE, camera->xzOffsetUpdateRate, spC4, 0.1f);
    camera->yOffsetUpdateRate = Camera_LERPCeilF(CAM_Y_OFFSET_UPDATE_RATE, camera->yOffsetUpdateRate, spC0, 0.1f);
    camera->fovUpdateRate =
        Camera_LERPCeilF(CAM_FOV_UPDATE_RATE, camera->yOffsetUpdateRate, camera->speedRatio * 0.05f, 0.1f);

    Camera_CalcAtDefault(camera, &eyeNextAtOffset, roData->yOffset, roData->interfaceField);
    eyeDiffGeo = OLib_Vec3fDiffToVecGeo(at, eyeNext);

    camera->dist = eyeDiffGeo.r =
        Camera_ClampDist(camera, eyeDiffGeo.r, roData->distMin, roData->distMax, rwData->animTimer);

    if (camera->playerGroundY <= playerPosRot->pos.y) {
        phi_f0 = playerPosRot->pos.y - camera->playerGroundY;
    } else {
        phi_f0 = -(playerPosRot->pos.y - camera->playerGroundY);
    }

    if (!(phi_f0 < 10.0f)) {
        if (camera->waterYPos <= playerhead.pos.y) {
            phi_f2 = playerhead.pos.y - camera->waterYPos;
        } else {
            phi_f2 = -(playerhead.pos.y - camera->waterYPos);
        }
        if (!(phi_f2 < 50.0f)) {
            camera->pitchUpdateRateInv = 100.0f;
        }
    }
    if (rwData->swing.unk_18 != 0) {
        eyeDiffGeo.yaw =
            Camera_LERPCeilS(rwData->swing.unk_16, eyeNextAtOffset.yaw, 1.0f / camera->yawUpdateRateInv, 0xA);
        eyeDiffGeo.pitch =
            Camera_LERPCeilS(rwData->swing.unk_14, eyeNextAtOffset.pitch, 1.0f / camera->yawUpdateRateInv, 0xA);
    } else {
        eyeDiffGeo.yaw = Camera_CalcDefaultYaw(camera, eyeNextAtOffset.yaw, playerPosRot->rot.y, roData->unk_14, 0.0f);
        eyeDiffGeo.pitch = Camera_CalcDefaultPitch(camera, eyeNextAtOffset.pitch, roData->pitchTarget, 0);
    }

    if (eyeDiffGeo.pitch > CAM_MAX_PITCH) {
        eyeDiffGeo.pitch = CAM_MAX_PITCH;
    }

    if (eyeDiffGeo.pitch < CAM_MIN_PITCH_1) {
        eyeDiffGeo.pitch = CAM_MIN_PITCH_1;
    }

    // @recomp Let the right stick steer the camera.
    analog_cam_update(camera, &eyeDiffGeo);

    *eyeNext = Camera_AddVecGeoToVec3f(at, &eyeDiffGeo);
    if ((camera->status == CAM_STAT_ACTIVE) && !(roData->interfaceField & JUMP3_FLAG_4)) {
        func_80046E20(camera, &eyeDiffGeo, roData->distMin, roData->swingUpdateRate, &spBC, &rwData->swing);
        if (roData->interfaceField & JUMP3_FLAG_2) {
            camera->inputDir.x = -eyeAtOffset.pitch;
            camera->inputDir.y = eyeAtOffset.yaw - 0x7FFF;
            camera->inputDir.z = 0;
        } else {
            eyeDiffGeo = OLib_Vec3fDiffToVecGeo(eye, at);
            camera->inputDir.x = eyeDiffGeo.pitch;
            camera->inputDir.y = eyeDiffGeo.yaw;
            camera->inputDir.z = 0;
        }

        if (rwData->swing.unk_18 != 0) {
            camera->inputDir.y =
                Camera_LERPCeilS(camera->inputDir.y + (s16)((s16)(rwData->swing.unk_16 - 0x7FFF) - camera->inputDir.y),
                                 camera->inputDir.y, 1.0f - (0.99f * spBC), 0xA);
        }
    } else {
        rwData->swing.swingUpdateRate = roData->swingUpdateRate;
        rwData->swing.unk_18 = 0;
        sUpdateCameraDirection = 0;
        *eye = *eyeNext;
    }
    camera->fov = Camera_LERPCeilF(roData->fovTarget, camera->fov, camera->fovUpdateRate, 1.0f);
    camera->roll = Camera_LERPCeilS(0, camera->roll, 0.5f, 0xA);
    camera->atLERPStepScale = Camera_ClampLERPScale(camera, roData->unk_1C);
    return true;
}

// @recomp Patched for the analog camera (riding Epona). Original: Camera_Normal3 in src/code/z_camera.c.
RECOMP_PATCH s32 Camera_Normal3(Camera* camera) {
    Vec3f* eye = &camera->eye;
    Vec3f* at = &camera->at;
    Vec3f* eyeNext = &camera->eyeNext;
    f32 sp98;
    f32 sp94;
    f32 sp90;
    f32 sp8C;
    VecGeo sp84;
    VecGeo sp7C;
    VecGeo sp74;
    PosRot* playerPosRot = &camera->playerPosRot;
    f32 temp_f0;
    STACK_PAD(s32);
    s16 phi_a0;
    s16 t2;
    Normal3ReadOnlyData* roData = &camera->paramData.norm3.roData;
    Normal3ReadWriteData* rwData = &camera->paramData.norm3.rwData;
    f32 playerHeight;

    playerHeight = Player_GetHeight(camera->player);
    if (RELOAD_PARAMS(camera) || CAM_DEBUG_RELOAD_PARAMS) {
        CameraModeValue* values = sCameraSettings[camera->setting].cameraModes[camera->mode].values;

        roData->yOffset = GET_NEXT_RO_DATA(values) * CAM_DATA_SCALED(playerHeight);
        roData->distMin = GET_NEXT_RO_DATA(values) * CAM_DATA_SCALED(playerHeight);
        roData->distMax = GET_NEXT_RO_DATA(values) * CAM_DATA_SCALED(playerHeight);
        roData->pitchTarget = CAM_DEG_TO_BINANG(GET_NEXT_RO_DATA(values));
        roData->yawUpdateSpeed = GET_NEXT_RO_DATA(values);
        roData->unk_10 = GET_NEXT_RO_DATA(values);
        roData->fovTarget = GET_NEXT_RO_DATA(values);
        roData->maxAtLERPScale = GET_NEXT_SCALED_RO_DATA(values);
        roData->interfaceField = GET_NEXT_RO_DATA(values);
    }

    CAM_DEBUG_RELOAD_PREG(camera);

    sp7C = OLib_Vec3fDiffToVecGeo(at, eye);
    sp74 = OLib_Vec3fDiffToVecGeo(at, eyeNext);

    sUpdateCameraDirection = true;
    sCameraInterfaceField = roData->interfaceField;
    switch (camera->animState) {
        case 0:
        case 10:
        case 20:
        case 25:
            rwData->swing.atEyePoly = NULL;
            rwData->curPitch = 0;
            rwData->unk_1C = 0.0f;
            rwData->unk_20 = camera->playerGroundY;
            rwData->swing.unk_16 = rwData->swing.unk_14 = rwData->swing.unk_18 = 0;
            rwData->swing.swingUpdateRate = roData->yawUpdateSpeed;
            rwData->yawUpdAmt = (s16)((s16)(playerPosRot->rot.y - 0x7FFF) - sp7C.yaw) * (1.0f / CAM_DEFAULT_ANIM_TIME);
            rwData->distTimer = 10;
            rwData->yawTimer = CAM_DEFAULT_ANIM_TIME;
            camera->animState = 1;
            rwData->swing.swingUpdateRateTimer = 0;
    }

    if (rwData->distTimer != 0) {
        rwData->distTimer--;
    }

    sp98 = CAM_UPDATE_RATE_STEP_SCALE_XZ * camera->speedRatio;
    sp94 = CAM_UPDATE_RATE_STEP_SCALE_Y * camera->speedRatio;

    if (rwData->swing.swingUpdateRateTimer != 0) {
        camera->yawUpdateRateInv = Camera_LERPCeilF(roData->yawUpdateSpeed + (rwData->swing.swingUpdateRateTimer * 2),
                                                    camera->yawUpdateRateInv, sp98, 0.1f);
        camera->pitchUpdateRateInv =
            Camera_LERPCeilF((f32)CAM_PITCH_UPDATE_RATE_INV + (rwData->swing.swingUpdateRateTimer * 2),
                             camera->pitchUpdateRateInv, sp94, 0.1f);
        rwData->swing.swingUpdateRateTimer--;
    } else {
        camera->yawUpdateRateInv = Camera_LERPCeilF(roData->yawUpdateSpeed, camera->yawUpdateRateInv, sp98, 0.1f);
        camera->pitchUpdateRateInv =
            Camera_LERPCeilF(CAM_PITCH_UPDATE_RATE_INV, camera->pitchUpdateRateInv, sp94, 0.1f);
    }

    camera->xzOffsetUpdateRate = Camera_LERPCeilF(CAM_XZ_OFFSET_UPDATE_RATE, camera->xzOffsetUpdateRate, sp98, 0.1f);
    camera->yOffsetUpdateRate = Camera_LERPCeilF(CAM_Y_OFFSET_UPDATE_RATE, camera->yOffsetUpdateRate, sp94, 0.1f);
    camera->fovUpdateRate = Camera_LERPCeilF(CAM_FOV_UPDATE_RATE, camera->fovUpdateRate, sp94, 0.1f);

    t2 = Camera_GetPitchAdjFromFloorHeightDiffs(camera, sp7C.yaw - 0x7FFF, true);
    sp94 = ((1.0f / roData->unk_10) * 0.5f);
    temp_f0 = (((1.0f / roData->unk_10) * 0.5f) * (1.0f - camera->speedRatio));
    rwData->curPitch = Camera_LERPCeilS(t2, rwData->curPitch, sp94 + temp_f0, 0xF);

    Camera_CalcAtForHorse(camera, &sp74, roData->yOffset, &rwData->unk_20, true);
    sp90 = (roData->distMax + roData->distMin) * 0.5f;
    sp84 = OLib_Vec3fDiffToVecGeo(at, eyeNext);
    camera->dist = sp84.r = Camera_ClampDist(camera, sp84.r, roData->distMin, roData->distMax, rwData->distTimer);
    if (camera->xzSpeed > 0.001f) {
        sp84.r += (sp90 - sp84.r) * 0.002f;
    }
    phi_a0 = roData->pitchTarget - rwData->curPitch;
    sp84.pitch = Camera_LERPCeilS(phi_a0, sp74.pitch, 1.0f / camera->pitchUpdateRateInv, 0xA);

    if (sp84.pitch > CAM_MAX_PITCH) {
        sp84.pitch = CAM_MAX_PITCH;
    }
    if (sp84.pitch < CAM_MIN_PITCH_1) {
        sp84.pitch = CAM_MIN_PITCH_1;
    }

    phi_a0 = playerPosRot->rot.y - (s16)(sp74.yaw - 0x7FFF);
    if (ABS(phi_a0) > 0x2AF8) {
        if (phi_a0 > 0) {
            phi_a0 = 0x2AF8;
        } else {
            phi_a0 = -0x2AF8;
        }
    }

    sp90 = 1.0f;
    sp98 = 0.5;
    sp94 = camera->speedRatio;
    sp90 -= sp98;
    sp98 = sp98 + (sp94 * sp90);
    sp98 = (sp98 * phi_a0) / camera->yawUpdateRateInv;

    if (1) {}
    sp84.yaw = fabsf(sp98) > (150.0f * (1.0f - camera->speedRatio)) ? (s16)(sp74.yaw + sp98) : sp74.yaw;

    if (rwData->yawTimer > 0) {
        sp84.yaw += rwData->yawUpdAmt;
        rwData->yawTimer--;
    }

    // @recomp Let the right stick steer the camera.
    analog_cam_update(camera, &sp84);

    *eyeNext = Camera_AddVecGeoToVec3f(at, &sp84);

    if (camera->status == CAM_STAT_ACTIVE) {
        func_80046E20(camera, &sp84, roData->distMin, roData->yawUpdateSpeed, &sp8C, &rwData->swing);
    } else {
        *eye = *eyeNext;
    }

    camera->fov = Camera_LERPCeilF(roData->fovTarget, camera->fov, camera->fovUpdateRate, 1.0f);
    camera->roll = Camera_LERPCeilS(0, camera->roll, 0.5f, 0xA);
    camera->atLERPStepScale = Camera_ClampLERPScale(camera, roData->maxAtLERPScale);
    return 1;
}

void Play_Update(PlayState* this);
void Play_Draw(PlayState* this);
extern Input* D_8012D1F8;
#define PLAY_LOG(line) (void)0

// @recomp Patched to follow up on the camera after each update. Original: Play_Main in src/code/z_play.c.
RECOMP_PATCH void Play_Main(GameState* thisx) {
    PlayState* this = (PlayState*)thisx;

    D_8012D1F8 = &this->state.input[0];

    DebugDisplay_Init();

    PLAY_LOG(4556);

    if (DEBUG_FEATURES && (R_HREG_MODE == HREG_MODE_PLAY) && (R_PLAY_INIT != HREG_MODE_PLAY)) {
        R_PLAY_RUN_UPDATE = true;
        R_PLAY_RUN_DRAW = true;
        R_PLAY_DRAW_SKYBOX = true;
        R_PLAY_DRAW_ROOM_FLAGS = (ROOM_DRAW_OPA | ROOM_DRAW_XLU);
        R_PLAY_DRAW_ACTORS = true;
        R_PLAY_DRAW_LENS_FLARES = true;
        R_PLAY_DRAW_SCREEN_FILLS = true;
        R_PLAY_DRAW_SANDSTORM = true;
        R_PLAY_DRAW_OVERLAY_ELEMENTS = true;
        R_PLAY_DRAW_ENV_FLAGS = (PLAY_ENV_DRAW_SKYBOX_FILTERS | PLAY_ENV_DRAW_SUN_AND_MOON | PLAY_ENV_DRAW_LIGHTNING |
                                 PLAY_ENV_DRAW_LIGHTS);
        HREG(91) = 1; // reg is not used in this mode
        R_PLAY_DRAW_COVER_ELEMENTS = true;
        R_PLAY_DRAW_DEBUG_OBJECTS = true;
        R_PLAY_INIT = HREG_MODE_PLAY;
    }

    if (!DEBUG_FEATURES || (R_HREG_MODE != HREG_MODE_PLAY) || R_PLAY_RUN_UPDATE) {
        Play_Update(this);
    }

    // @recomp Follow up on this frame's camera update.
    analog_cam_post_play_update(this);

    PLAY_LOG(4583);

    Play_Draw(this);

    PLAY_LOG(4587);
}
