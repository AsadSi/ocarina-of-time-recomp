#include "patches.h"
#include "input.h"
#include "controller.h"
#include "play_state.h"
#include "player.h"
#include "z_lib.h"

// First-person aiming, ported from Zelda64Recomp: the right stick aims too when the analog camera is on, the
// Aiming Camera Mode setting inverts the axes, and gyro and mouse turn the aim while an item is out. As in
// Zelda64Recomp, looking around with C-Up takes the sticks only.

// Controller 1's stick, plus the right stick when the analog camera is on. Clamped like the game's own
// stick values so using both sticks doesn't turn twice as fast.
static void aim_stick_inputs(Input* input, s32* x, s32* y) {
    s32 rightX = 0;
    s32 rightY = 0;

    if (recomp_get_analog_cam_enabled()) {
        f32 analogX;
        f32 analogY;

        recomp_get_camera_inputs(&analogX, &analogY);
        // Scaled like the left stick (127 at full tilt), and the host's Y axis points down.
        rightX = (s32)(analogX * 127.0f);
        rightY = (s32)(analogY * -127.0f);
    }
    *x = CLAMP(input->rel.stick_x + rightX, -61, 61);
    *y = CLAMP(input->rel.stick_y + rightY, -61, 61);
}

// This frame's change in aim from gyro and mouse, as pitch (x) and yaw (y) angles. The totals are kept so
// that rounding never loses motion.
static void gyro_mouse_aim(s32* x, s32* y) {
    static f32 sTotalGyroX, sTotalGyroY, sTotalMouseX, sTotalMouseY;
    static s32 sAppliedX, sAppliedY;
    f32 deltaX;
    f32 deltaY;
    s32 targetX;
    s32 targetY;

    recomp_get_gyro_deltas(&deltaX, &deltaY);
    sTotalGyroX += deltaX;
    sTotalGyroY += deltaY;
    recomp_get_mouse_deltas(&deltaX, &deltaY);
    sTotalMouseX += deltaX;
    sTotalMouseY += deltaY;

    // Gyro tilt and mouse up/down pitch the aim; gyro rotation and mouse left/right turn it.
    targetX = (s32)(sTotalGyroX * -3.0f + sTotalMouseY * 20.0f);
    targetY = (s32)(sTotalGyroY * 3.0f + sTotalMouseX * -20.0f);
    *x = targetX - sAppliedX;
    *y = targetY - sAppliedY;
    sAppliedX = targetX;
    sAppliedY = targetY;
}

int func_808334B4(Player* this);
s32 func_80836AB8(Player* this, s32 arg1);

// @recomp Patched for right stick, gyro and mouse aiming and the aiming inversion setting.
// Original: func_8084ABD8 in src/overlays/actors/ovl_player_actor/z_player.c.
RECOMP_PATCH s16 func_8084ABD8(PlayState* play, Player* this, s32 arg2, s16 arg3) {
    s32 temp1;
    s16 temp2;
    s16 temp3;
    // @recomp sControlInput isn't a known symbol; while aiming, Player_Update points it at controller 1.
    Input* sControlInput = &play->state.input[0];
    s32 invertX;
    s32 invertY;
    s32 stickX;
    s32 stickY;

    // @recomp Add the right stick when the analog camera is on, and apply the aiming inversion setting (the
    // game's own controls are inverted on Y).
    aim_stick_inputs(sControlInput, &stickX, &stickY);
    recomp_get_inverted_axes(&invertX, &invertY);
    if (!invertY) {
        stickY = -stickY;
    }
    if (invertX) {
        stickX = -stickX;
    }

    if (!func_8002DD78(this) && !func_808334B4(this) && !arg2) {
        temp2 = stickY * 240.0f;
        Math_SmoothStepToS(&this->actor.focus.rot.x, temp2, 14, 4000, 30);

        temp2 = stickX * -16.0f;
        temp2 = CLAMP(temp2, -3000, 3000);
        this->actor.focus.rot.y += temp2;
    } else {
        temp1 = (this->stateFlags1 & PLAYER_STATE1_23) ? 3500 : 14000;
        s32 gyroMouseX;
        s32 gyroMouseY;

        // @recomp Gyro and mouse turn the aim directly.
        gyro_mouse_aim(&gyroMouseX, &gyroMouseY);

        temp3 = ((stickY >= 0) ? 1 : -1) * (s32)((1.0f - Math_CosS(stickY * 200)) * 1500.0f);
        this->actor.focus.rot.x += temp3 + gyroMouseX;
        this->actor.focus.rot.x = CLAMP(this->actor.focus.rot.x, -temp1, temp1);

        temp1 = 19114;
        temp2 = this->actor.focus.rot.y - this->actor.shape.rot.y;
        temp3 = ((stickX >= 0) ? 1 : -1) * (s32)((1.0f - Math_CosS(stickX * 200)) * -1500.0f);
        temp2 += temp3 + gyroMouseY;
        this->actor.focus.rot.y = CLAMP(temp2, -temp1, temp1) + this->actor.shape.rot.y;
    }

    this->unk_6AE_rotFlags |= UNK6AE_ROT_FOCUS_Y;
    return func_80836AB8(this, (play->shootingGalleryStatus != 0) || func_8002DD78(this) || func_808334B4(this)) - arg3;
}
