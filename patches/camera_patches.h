#ifndef __CAMERA_PATCHES_H__
#define __CAMERA_PATCHES_H__

// Declarations from src/code/z_camera.c and z_camera_data.inc.c that patched camera functions need.

#include "patches.h"
#include "camera.h"
#include "olib.h"
#include "play_state.h"
#include "player.h"
#include "regs.h"
#include "save.h"
#include "z_lib.h"

typedef struct CameraModeValue {
    s16 val;
    s16 dataType;
} CameraModeValue;

typedef struct CameraMode {
    s16 funcIdx;
    s16 valueCnt;
    CameraModeValue* values;
} CameraMode;

typedef struct CameraSetting {
    union {
        u32 unk_00;
        struct {
            u32 unk_bit0 : 1;
            u32 unk_bit1 : 1;
            u32 validModes : 30;
        };
    };
    CameraMode* cameraModes;
} CameraSetting;


extern CameraSetting sCameraSettings[];
extern s32 sCameraInterfaceField;
extern s32 sUpdateCameraDirection;

f32 Camera_LERPCeilF(f32 target, f32 cur, f32 stepScale, f32 minDiff);
s16 Camera_LERPCeilS(s16 target, s16 cur, f32 stepScale, s16 minDiff);
Vec3f Camera_AddVecGeoToVec3f(Vec3f* a, VecGeo* geo);
s32 Camera_BGCheck(Camera* camera, Vec3f* from, Vec3f* to);
s16 Camera_GetPitchAdjFromFloorHeightDiffs(Camera* camera, s16 viewYaw, s16 initAndReturnZero);
f32 Camera_ClampLERPScale(Camera* camera, f32 maxLERPScale);
s32 Camera_CalcAtDefault(Camera* camera, VecGeo* eyeAtDir, f32 yOffset, s16 calcSlopeYAdj);
s32 func_800458D4(Camera* camera, VecGeo* eyeAtDir, f32 yOffset, f32* arg3, s16 calcSlopeYAdj);
s32 func_80045B08(Camera* camera, VecGeo* eyeAtDir, f32 yOffset, s16 arg3);
f32 Camera_ClampDist(Camera* camera, f32 dist, f32 minDist, f32 maxDist, s16 timer);
s16 Camera_CalcDefaultPitch(Camera* camera, s16 arg1, s16 arg2, s16 arg3);
s16 Camera_CalcDefaultYaw(Camera* camera, s16 cur, s16 target, f32 arg3, f32 accel);
void func_80046E20(Camera* camera, VecGeo* eyeAdjustment, f32 minDist, f32 arg3, f32* arg4, SwingAnimation* anim);
s32 Camera_CalcAtForHorse(Camera* camera, VecGeo* eyeAtDir, f32 yOffset, f32* yPosOffset, s16 calcSlope);


#if DEBUG_FEATURES
#define CAM_DEBUG_RELOAD_PREG(camera)        \
    if (R_RELOAD_CAM_PARAMS) {               \
        Camera_CopyPREGToModeValues(camera); \
    }                                        \
    (void)0
#else
#define CAM_DEBUG_RELOAD_PREG(camera) (void)0
#endif

// Camera will reload its paramData. Usually that means setting the read-only data from what is stored in
// CameraModeValue arrays. Although sometimes some read-write data is reset as well
#define RELOAD_PARAMS(camera) (camera->animState == 0 || camera->animState == 10 || camera->animState == 20)

#if DEBUG_FEATURES
#define CAM_DEBUG_RELOAD_PARAMS R_RELOAD_CAM_PARAMS
#else
#define CAM_DEBUG_RELOAD_PARAMS true
#endif

/**
 * Camera data is stored in both read-only data and OREG as s16, and then converted to the appropriate type during
 * runtime. If a small f32 is being stored as an s16, it is common to store that value 100 times larger than the
 * original value. This is then scaled back down during runtime with the CAM_DATA_SCALED macro.
 */
#define CAM_DATA_SCALED(x) ((x)*0.01f)

// Load the next value from camera read-only data stored in CameraModeValue
#define GET_NEXT_RO_DATA(values) ((values++)->val)
// Load the next value and scale down from camera read-only data stored in CameraModeValue
#define GET_NEXT_SCALED_RO_DATA(values) CAM_DATA_SCALED(GET_NEXT_RO_DATA(values))

#if DEBUG_FEATURES

#define CAM_GLOBAL_0 OREG(0)
#define CAM_GLOBAL_1 OREG(1)
#define CAM_XZ_OFFSET_UPDATE_RATE CAM_DATA_SCALED(R_CAM_XZ_OFFSET_UPDATE_RATE)
#define CAM_Y_OFFSET_UPDATE_RATE CAM_DATA_SCALED(R_CAM_Y_OFFSET_UPDATE_RATE)
#define CAM_FOV_UPDATE_RATE CAM_DATA_SCALED(R_CAM_FOV_UPDATE_RATE)
#define CAM_MAX_PITCH R_CAM_MAX_PITCH
#define CAM_R_UPDATE_RATE_INV R_CAM_R_UPDATE_RATE_INV
#define CAM_PITCH_UPDATE_RATE_INV R_CAM_PITCH_UPDATE_RATE_INV
#define CAM_GLOBAL_8 CAM_DATA_SCALED(OREG(8))
#define CAM_SLOPE_Y_ADJ_AMOUNT R_CAM_SLOPE_Y_ADJ_AMOUNT
#define CAM_GLOBAL_10 CAM_DATA_SCALED(OREG(10))
#define CAM_GLOBAL_11 CAM_DATA_SCALED(OREG(11))
#define CAM_GLOBAL_12 CAM_DATA_SCALED(OREG(12))
#define CAM_GLOBAL_13 OREG(13)
#define CAM_GLOBAL_14 OREG(14)
#define CAM_GLOBAL_15 OREG(15)
#define CAM_GLOBAL_16 OREG(16)
#define CAM_PITCH_FLOOR_CHECK_NEAR_DIST_FAC CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_NEAR_DIST_FAC)
#define CAM_PITCH_FLOOR_CHECK_FAR_DIST_FAC CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_FAR_DIST_FAC)
#define CAM_PITCH_FLOOR_CHECK_OFFSET_Y_FAC CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_OFFSET_Y_FAC)
#define CAM_PITCH_FLOOR_CHECK_NEAR_WEIGHT CAM_DATA_SCALED(R_CAM_PITCH_FLOOR_CHECK_NEAR_WEIGHT)
#define CAM_GLOBAL_21 OREG(21)
#define CAM_GLOBAL_22 CAM_DATA_SCALED(OREG(22))
#define CAM_DEFAULT_ANIM_TIME R_CAM_DEFAULT_ANIM_TIME
#define CAM_GLOBAL_24 OREG(24)
#define CAM_UPDATE_RATE_STEP_SCALE_XZ CAM_DATA_SCALED(R_CAM_UPDATE_RATE_STEP_SCALE_XZ)
#define CAM_UPDATE_RATE_STEP_SCALE_Y CAM_DATA_SCALED(R_CAM_UPDATE_RATE_STEP_SCALE_Y)
#define CAM_GLOBAL_27 OREG(27)
#define CAM_GLOBAL_28 CAM_DATA_SCALED(OREG(28))
#define CAM_GLOBAL_29 CAM_DATA_SCALED(OREG(29))
#define CAM_GLOBAL_30 CAM_DATA_SCALED(OREG(30))
#define CAM_JUMP1_EYE_Y_STEP_SCALE CAM_DATA_SCALED(R_CAM_JUMP1_EYE_Y_STEP_SCALE)
#define CAM_GLOBAL_32 OREG(32)
#define CAM_GLOBAL_33 OREG(33)
#define CAM_MIN_PITCH_1 R_CAM_MIN_PITCH_1
#define CAM_MIN_PITCH_2 R_CAM_MIN_PITCH_2
#define CAM_BATTLE1_ROLL_TARGET_BASE R_CAM_BATTLE1_ROLL_TARGET_BASE
#define CAM_BATTLE1_ROLL_STEP_SCALE CAM_DATA_SCALED(R_CAM_BATTLE1_ROLL_STEP_SCALE)
#define CAM_GLOBAL_38 CAM_DATA_SCALED(OREG(38))
#define CAM_GLOBAL_39 CAM_DATA_SCALED(OREG(39))
#define CAM_BATTLE1_XYZ_OFFSET_UPDATE_RATE_TARGET CAM_DATA_SCALED(R_CAM_BATTLE1_XYZ_OFFSET_UPDATE_RATE_TARGET)
#define CAM_AT_LERP_STEP_SCALE_MIN CAM_DATA_SCALED(R_CAM_AT_LERP_STEP_SCALE_MIN)
#define CAM_AT_LERP_STEP_SCALE_FAC CAM_DATA_SCALED(R_CAM_AT_LERP_STEP_SCALE_FAC)
#define CAM_GLOBAL_43 CAM_DATA_SCALED(OREG(43))
#define CAM_GLOBAL_44 OREG(44)
#define CAM_GLOBAL_45 OREG(45)
#define CAM_YOFFSET_NORM CAM_DATA_SCALED(R_CAM_YOFFSET_NORM)
#define CAM_GLOBAL_47 OREG(47)
#define CAM_GLOBAL_48 CAM_DATA_SCALED(OREG(48))
#define CAM_GLOBAL_49 CAM_DATA_SCALED(OREG(49))
#define CAM_GLOBAL_50 OREG(50)
#define CAM_GLOBAL_51 OREG(51)
#define CAM_GLOBAL_52 OREG(52)

#else

#define CAM_GLOBAL_0 0
#define CAM_GLOBAL_1 1
#define CAM_XZ_OFFSET_UPDATE_RATE .05f
#define CAM_Y_OFFSET_UPDATE_RATE .05f
#define CAM_FOV_UPDATE_RATE .05f
#define CAM_MAX_PITCH 14500
#define CAM_R_UPDATE_RATE_INV 20
#define CAM_PITCH_UPDATE_RATE_INV 16
#define CAM_GLOBAL_8 1.5f
#define CAM_SLOPE_Y_ADJ_AMOUNT 25
#define CAM_GLOBAL_10 1.5f
#define CAM_GLOBAL_11 0.06f
#define CAM_GLOBAL_12 .10f
#define CAM_GLOBAL_13 10
#define CAM_GLOBAL_14 0
#define CAM_GLOBAL_15 0
#define CAM_GLOBAL_16 1
#define CAM_PITCH_FLOOR_CHECK_NEAR_DIST_FAC 1.0f
#define CAM_PITCH_FLOOR_CHECK_FAR_DIST_FAC 2.5f
#define CAM_PITCH_FLOOR_CHECK_OFFSET_Y_FAC 1.2f
#define CAM_PITCH_FLOOR_CHECK_NEAR_WEIGHT 0.8f
#define CAM_GLOBAL_21 30
#define CAM_GLOBAL_22 1.2f
#define CAM_DEFAULT_ANIM_TIME 4
#define CAM_GLOBAL_24 1
#define CAM_UPDATE_RATE_STEP_SCALE_XZ .50f
#define CAM_UPDATE_RATE_STEP_SCALE_Y .20f
#define CAM_GLOBAL_27 1800
#define CAM_GLOBAL_28 0.5f
#define CAM_GLOBAL_29 0.5f
#define CAM_GLOBAL_30 0.5f
#define CAM_JUMP1_EYE_Y_STEP_SCALE 0.2f
#define CAM_GLOBAL_32 20
#define CAM_GLOBAL_33 -10
#define CAM_MIN_PITCH_1 -5460
#define CAM_MIN_PITCH_2 -9100
#define CAM_BATTLE1_ROLL_TARGET_BASE -6
#define CAM_BATTLE1_ROLL_STEP_SCALE 0.08f
#define CAM_GLOBAL_38 0.15f
#define CAM_GLOBAL_39 0.75f
#define CAM_BATTLE1_XYZ_OFFSET_UPDATE_RATE_TARGET 0.6f
#define CAM_AT_LERP_STEP_SCALE_MIN 0.12f
#define CAM_AT_LERP_STEP_SCALE_FAC 1.1f
#define CAM_GLOBAL_43 0.4f
#define CAM_GLOBAL_44 50
#define CAM_GLOBAL_45 250
#define CAM_YOFFSET_NORM -0.1f
#define CAM_GLOBAL_47 30
#define CAM_GLOBAL_48 0.3f
#define CAM_GLOBAL_49 0.7f
#define CAM_GLOBAL_50 20
#define CAM_GLOBAL_51 20
#define CAM_GLOBAL_52 20

#endif

// analog_cam.c
int analog_cam_update(Camera* camera, VecGeo* eyeAtGeo);
void analog_cam_post_play_update(PlayState* play);

#endif
