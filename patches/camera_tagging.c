#include "patches.h"
#include "transform_ids.h"
#include "view.h"
#include "gfx.h"
#include "z_lib.h"
#include "play_state.h"
#include "pause.h"
#include "file_select_state.h"

// Ported from Zelda64Recomp's camera_transform_tagging.c (Majora's Mask) to OoT.

static int camera_interpolation_forced = false;
static int camera_skip_interpolation_forced = false;
static int camera_ignore_tracking = false;
static int in_kaleido = false;
static int prev_in_kaleido = false;
static int camera_skipped = false;

int camera_was_skipped(void) {
    return camera_skipped;
}

void clear_camera_skipped(void) {
    camera_skipped = false;
}

void Play_Destroy(GameState* thisx);

// Called once per frame after the game state updates, to track whether the pause menu is open.
void camera_post_game_update(GameState* gameState) {
    prev_in_kaleido = in_kaleido;
    if (gameState->destroy == Play_Destroy) {
        PlayState* play = (PlayState*)gameState;
        in_kaleido = (play->pauseCtx.state != 0) || (play->pauseCtx.debugState != 0);
    } else {
        in_kaleido = false;
    }
}

// Predicts the eye and focus positions from last frame's motion. A large miss means the camera cut
// rather than moved, so that frame must not be interpolated. The thresholds are MM's, picked by testing.
static int should_interpolate_perspective(Vec3f* eye, Vec3f* at) {
    static Vec3f prev_eye = { 0, 0, 0 };
    static Vec3f prev_at = { 0, 0, 0 };
    static Vec3f eye_velocity = { 0, 0, 0 };
    static Vec3f at_velocity = { 0, 0, 0 };
    Vec3f predicted_eye;
    Vec3f predicted_at;

    Math_Vec3f_Sum(&prev_eye, &eye_velocity, &predicted_eye);
    Math_Vec3f_Sum(&prev_at, &at_velocity, &predicted_at);
    Math_Vec3f_Diff(eye, &prev_eye, &eye_velocity);
    Math_Vec3f_Diff(at, &prev_at, &at_velocity);

    f32 eye_dist = Math_Vec3f_DistXYZ(&predicted_eye, eye);
    f32 at_dist = Math_Vec3f_DistXYZ(&predicted_at, at);
    f32 velocity_diff = Math_Vec3f_DistXYZ(&eye_velocity, &at_velocity);

    prev_eye = *eye;
    prev_at = *at;

    if (velocity_diff <= 3.0f && eye_dist <= 100.0f && at_dist <= 100.0f) {
        return true;
    }
    if (at_dist <= 10.0f && eye_dist <= 300.0f) {
        return true;
    }
    if (eye_dist <= 10.0f && at_dist <= 300.0f) {
        return true;
    }
    if (velocity_diff > 50.0f || at_dist > 50.0f || eye_dist > 300.0f) {
        eye_velocity.x = eye_velocity.y = eye_velocity.z = 0.0f;
        at_velocity.x = at_velocity.y = at_velocity.z = 0.0f;
        return false;
    }
    return true;
}

s32 View_ApplyPerspective(View* view);
s32 View_ApplyOrtho(View* view);

// @recomp Patched to tag the camera's projection so RT64 can interpolate it, skipping interpolation on
// camera cuts. Original: View_Apply in src/code/z_view.c.
RECOMP_PATCH s32 View_Apply(View* view, s32 mask) {
    s32 result;
    int interpolate_camera = false;

    mask = (view->flags & mask) | (mask >> 4);

    if (mask & VIEW_PROJECTION_ORTHO) {
        result = View_ApplyOrtho(view);
    } else {
        result = View_ApplyPerspective(view);
        if (!camera_ignore_tracking) {
            interpolate_camera = should_interpolate_perspective(&view->eye, &view->at);
        }
    }
    camera_ignore_tracking = false;

    // Snap rather than interpolate on the frame the pause menu closes.
    if (prev_in_kaleido && !in_kaleido) {
        camera_skip_interpolation_forced = true;
    }

    if (camera_skip_interpolation_forced) {
        interpolate_camera = false;
    } else if (camera_interpolation_forced) {
        interpolate_camera = true;
    }

    GraphicsContext* gfxCtx = view->gfxCtx;
    OPEN_DISPS(gfxCtx, "../z_view.c", 0);
    if (interpolate_camera) {
        // Simple interpolation suits cameras, which orbit around a focus.
        gEXMatrixGroupSimpleTC(POLY_OPA_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
            G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE);
        gEXMatrixGroupSimpleTC(POLY_XLU_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
            G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE);
    } else {
        gEXMatrixGroupSimpleTC(POLY_OPA_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
            G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
        gEXMatrixGroupSimpleTC(POLY_XLU_DISP++, CAMERA_TRANSFORM_ID, G_EX_NOPUSH, G_MTX_PROJECTION,
            G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
    }
    CLOSE_DISPS(gfxCtx, "../z_view.c", 0);

    camera_skipped = !interpolate_camera;
    camera_interpolation_forced = false;
    camera_skip_interpolation_forced = false;

    return result;
}

// @recomp Patched to always interpolate the file select camera, which orbits smoothly.
// Original: FileSelect_SetView in src/overlays/gamestates/ovl_file_choose/z_file_choose.c.
RECOMP_PATCH void FileSelect_SetView(FileSelectState* this, f32 eyeX, f32 eyeY, f32 eyeZ) {
    Vec3f eye;
    Vec3f lookAt;
    Vec3f up;

    eye.x = eyeX;
    eye.y = eyeY;
    eye.z = eyeZ;

    lookAt.x = lookAt.y = lookAt.z = 0.0f;

    up.x = up.z = 0.0f;
    up.y = 1.0f;

    camera_interpolation_forced = true;
    camera_ignore_tracking = true;

    View_LookAt(&this->view, &eye, &lookAt, &up);
    View_Apply(&this->view, VIEW_ALL | VIEW_FORCE_VIEWING | VIEW_FORCE_VIEWPORT | VIEW_FORCE_PROJECTION_PERSPECTIVE);
}

// @recomp Patched to always interpolate the pause menu's camera as it turns between pages.
// Original: KaleidoScope_SetView in src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope.c.
RECOMP_PATCH void KaleidoScope_SetView(PauseContext* pauseCtx, f32 x, f32 y, f32 z) {
    Vec3f eye;
    Vec3f lookAt;
    Vec3f up;

    eye.x = x;
    eye.y = y;
    eye.z = z;
    lookAt.x = lookAt.y = lookAt.z = 0.0f;
    up.x = up.z = 0.0f;
    up.y = 1.0f;

    camera_interpolation_forced = true;
    camera_ignore_tracking = true;

    View_LookAt(&pauseCtx->view, &eye, &lookAt, &up);
    View_Apply(&pauseCtx->view,
               VIEW_ALL | VIEW_FORCE_VIEWING | VIEW_FORCE_VIEWPORT | VIEW_FORCE_PROJECTION_PERSPECTIVE);
}
