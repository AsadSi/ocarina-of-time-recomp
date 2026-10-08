#include "patches.h"
#include "array_count.h"
#include "regs.h"
#include "controller.h"
#include "gfx.h"
#include "interface.h"
#include "inventory.h"
#include "item.h"
#include "game_over.h"
#include "message.h"
#include "pause.h"
#include "play_state.h"
#include "player.h"
#include "save.h"
#include "segment_symbols.h"
#include "sfx.h"
#include "sys_matrix.h"
#include "z_lib.h"
#include "assets/textures/parameter_static/parameter_static.h"

// D-pad items: four more item buttons, like Zelda64Recomp's D-pad mod. Pressing a direction on the pause
// screen's item page assigns the item under the cursor to it (or clears it, if that item is already there);
// pressing it in play uses the item, under the
// same restrictions the game puts on C buttons. Each age has its own four, stored with the save file in
// SaveInfo bytes the game never uses.
//
// The player's item button loop can't be extended (its state is in overlay statics with no symbols), but it
// already asks Player_GetItemOnButton for button 4 when no B or C button is pressed, and again for what is
// held. That query answers for the D-pad, and `heldItemButton` 4 then means the last D-pad direction used.

#define DPAD_DIRECTIONS 4
// Inventory slot + 1 per direction for each age; 0 is empty, as on a new save. Boots and tunics are
// DPAD_EQUIP | (equipment type << 2) | value, assigned from the equipment page.
#define DPAD_SLOTS(age) ((u8*)&gSaveContext.save.info.unk_E8C[(age) * DPAD_DIRECTIONS])
#define DPAD_BUTTON_INDEX 4
#define DPAD_EQUIP 0x80
#define DPAD_EQUIP_VALUE(type, value) (DPAD_EQUIP | ((type) << 2) | (value))

static const u16 sDpadButtons[DPAD_DIRECTIONS] = { BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT };
static s32 sLastDpadDirection = 0;

// gSlotAgeReqs from the pause menu overlay: which age can use each inventory slot.
#define AGE_REQ_NONE 9
static const u8 sSlotAgeReqs[] = {
    LINK_AGE_CHILD, AGE_REQ_NONE,   AGE_REQ_NONE, LINK_AGE_ADULT, LINK_AGE_ADULT, AGE_REQ_NONE,
    LINK_AGE_CHILD, AGE_REQ_NONE,   AGE_REQ_NONE, LINK_AGE_ADULT, LINK_AGE_ADULT, AGE_REQ_NONE,
    LINK_AGE_CHILD, AGE_REQ_NONE,   LINK_AGE_CHILD, LINK_AGE_ADULT, LINK_AGE_ADULT, AGE_REQ_NONE,
    AGE_REQ_NONE,   AGE_REQ_NONE,   AGE_REQ_NONE, AGE_REQ_NONE,   LINK_AGE_ADULT, LINK_AGE_CHILD,
};

static int slot_usable_at_age(s32 slot) {
    return (slot >= 0) && (slot < (s32)ARRAY_COUNT(sSlotAgeReqs)) &&
           ((sSlotAgeReqs[slot] == AGE_REQ_NONE) || (sSlotAgeReqs[slot] == gSaveContext.save.linkAge));
}

static int dpad_is_equipment(u8 item) {
    return (item >= ITEM_TUNIC_KOKIRI) && (item <= ITEM_BOOTS_HOVER);
}

// The item assigned to a direction, as a C button would hold it. Boots and tunics come back as their item
// (ITEM_TUNIC_GORON etc.), for the icon; only adult Link wears the ones that can be assigned.
static u8 dpad_item(s32 dir) {
    u8 value = DPAD_SLOTS(gSaveContext.save.linkAge)[dir];
    s32 slot = value - 1;
    u8 item;

    if (value & DPAD_EQUIP) {
        s32 type = (value >> 2) & 3;
        s32 equipValue = value & 3;

        if ((gSaveContext.save.linkAge != LINK_AGE_ADULT) || !CHECK_OWNED_EQUIP(type, equipValue - 1)) {
            return ITEM_NONE;
        }
        return ((type == EQUIP_TYPE_TUNIC) ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI) + equipValue - 1;
    }
    if (!slot_usable_at_age(slot)) {
        return ITEM_NONE;
    }
    item = gSaveContext.save.info.inventory.items[slot];
    // Magic arrows go on a button as the bow with that arrow, as the pause menu does.
    if (item == ITEM_ARROW_FIRE) {
        return ITEM_BOW_FIRE;
    }
    if (item == ITEM_ARROW_ICE) {
        return ITEM_BOW_ICE;
    }
    if (item == ITEM_ARROW_LIGHT) {
        return ITEM_BOW_LIGHT;
    }
    return item;
}

// Whether an item could be used from a C button right now: the rules func_80083108 in z_parameter.c uses to
// disable C buttons, applied to one item.
static int dpad_item_enabled(PlayState* play, u8 item) {
    Player* player = GET_PLAYER(play);
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    s32 hazard = Player_GetEnvironmentalHazard(play);
    int isBottle = (item >= ITEM_BOTTLE_EMPTY) && (item <= ITEM_BOTTLE_POE);
    int isTradeItem = (item >= ITEM_WEIRD_EGG) && (item <= ITEM_CLAIM_CHECK);
    int isOcarina = (item == ITEM_OCARINA_FAIRY) || (item == ITEM_OCARINA_OF_TIME);
    int isHookshot = (item == ITEM_HOOKSHOT) || (item == ITEM_LONGSHOT);

    if (item >= ITEM_NONE_FE) {
        return false;
    }
    if (dpad_is_equipment(item)) {
        return !Player_InCsMode(play);
    }
    if ((player->stateFlags1 & PLAYER_STATE1_23) || (play->shootingGalleryStatus > 1) ||
        ((play->sceneId == SCENE_BOMBCHU_BOWLING_ALLEY) && Flags_GetSwitch(play, 0x38)) ||
        (play->sceneId == SCENE_CHAMBER_OF_THE_SAGES) || (play->sceneId == SCENE_FISHING_POND)) {
        return false;
    }
    if ((hazard >= PLAYER_ENV_HAZARD_UNDERWATER_FLOOR) && (hazard <= PLAYER_ENV_HAZARD_UNDERWATER_FREE)) {
        return (hazard == PLAYER_ENV_HAZARD_UNDERWATER_FLOOR) && isHookshot;
    }
    if ((player->stateFlags1 & PLAYER_STATE1_21) || (player->stateFlags2 & PLAYER_STATE2_CRAWLING)) {
        return false;
    }
    if (GET_EVENTINF_INGO_RACE_STATE() == INGO_RACE_STATE_HORSE_RENTAL_PERIOD) {
        return isOcarina;
    }
    if (isBottle) {
        return !interfaceCtx->restrictions.bottles;
    }
    if (isTradeItem) {
        return !interfaceCtx->restrictions.tradeItems;
    }
    if (isOcarina) {
        return !interfaceCtx->restrictions.ocarina;
    }
    if (interfaceCtx->restrictions.all) {
        return (play->sceneId == SCENE_TREASURE_BOX_SHOP) && (item == ITEM_LENS_OF_TRUTH);
    }
    if (isHookshot) {
        return !interfaceCtx->restrictions.hookshot;
    }
    if (item == ITEM_FARORES_WIND) {
        return !interfaceCtx->restrictions.farores;
    }
    if ((item == ITEM_DINS_FIRE) || (item == ITEM_NAYRUS_LOVE)) {
        return !interfaceCtx->restrictions.dinsNayrus;
    }
    return true;
}

static u8 dpad_usable_item(PlayState* play, s32 dir) {
    u8 item = dpad_item(dir);

    return dpad_item_enabled(play, item) ? item : ITEM_NONE;
}

// Player_ProcessItemButtons asks for button 4 first for a newly pressed button, then (if nothing was pressed)
// for a held one.
static u8 dpad_item_on_button(PlayState* play) {
    static u32 sQueryFrame = 0;
    static s32 sQueries = 0;
    Player* player = GET_PLAYER(play);
    Input* input = &play->state.input[0];
    u16 buttons;

    // Player_Update gives the player no input in these states.
    if ((play->bombchuBowlingStatus != 0) || (player->stateFlags1 & (PLAYER_STATE1_5 | PLAYER_STATE1_29))) {
        return ITEM_NONE;
    }
    if (sQueryFrame != play->state.frames) {
        sQueryFrame = play->state.frames;
        sQueries = 0;
    }
    buttons = (sQueries++ == 0) ? input->press.button : input->cur.button;

    for (s32 dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        if (CHECK_BTN_ALL(buttons, sDpadButtons[dir])) {
            u8 item = dpad_usable_item(play, dir);

            if ((item != ITEM_NONE) && !dpad_is_equipment(item)) {
                sLastDpadDirection = dir;
                return item;
            }
        }
    }
    return ITEM_NONE;
}

// @recomp Patched to answer for the D-pad when asked for button 4. Original: Player_GetItemOnButton in
// src/overlays/actors/ovl_player_actor/z_player.c.
RECOMP_PATCH s32 Player_GetItemOnButton(PlayState* play, s32 index) {
    if (index >= DPAD_BUTTON_INDEX) {
        return dpad_item_on_button(play);
    } else if (play->bombchuBowlingStatus != 0) {
        return (play->bombchuBowlingStatus > 0) ? ITEM_BOMBCHU : ITEM_NONE;
    } else if (index == 0) {
        return B_BTN_ITEM;
    } else if (index == 1) {
        return C_BTN_ITEM(0);
    } else if (index == 2) {
        return C_BTN_ITEM(1);
    } else {
        return C_BTN_ITEM(2);
    }
}

s32 Player_ItemToItemAction(s32 item);

// Whether any D-pad item is the given item action.
static int dpad_has_item_action(s32 itemAction) {
    for (s32 dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        u8 item = dpad_item(dir);

        if ((item < ITEM_NONE_FE) && !dpad_is_equipment(item) && (Player_ItemToItemAction(item) == itemAction)) {
            return true;
        }
    }
    return false;
}

// @recomp Patched so the held item isn't put away while it is on the D-pad. Only Player_ProcessItemButtons
// calls this. Original: Player_ItemIsInUse in src/overlays/actors/ovl_player_actor/z_player.c.
RECOMP_PATCH s32 Player_ItemIsInUse(Player* this, s32 item) {
    if (((item < ITEM_NONE_FE) && (Player_ItemToItemAction(item) == this->itemAction)) ||
        dpad_has_item_action(this->itemAction)) {
        return true;
    } else {
        return false;
    }
}

// @recomp Patched so a mask isn't taken off while it is on the D-pad. Only Player_ProcessItemButtons calls
// this. Original: Player_ItemIsItemAction in src/overlays/actors/ovl_player_actor/z_player.c.
RECOMP_PATCH s32 Player_ItemIsItemAction(s32 item1, s32 itemAction) {
    if (((item1 < ITEM_NONE_FE) && (Player_ItemToItemAction(item1) == itemAction)) ||
        dpad_has_item_action(itemAction)) {
        return true;
    } else {
        return false;
    }
}

// @recomp Patched to update a bottle used from the D-pad (button 4), which would otherwise be written past
// the C button arrays. Original: Inventory_UpdateBottleItem in src/code/z_parameter.c.
RECOMP_PATCH void Inventory_UpdateBottleItem(PlayState* play, u8 item, u8 button) {
    if (button >= DPAD_BUTTON_INDEX) {
        s32 slot = DPAD_SLOTS(gSaveContext.save.linkAge)[sLastDpadDirection] - 1;

        if ((gSaveContext.save.info.inventory.items[slot] == ITEM_BOTTLE_MILK_FULL) && (item == ITEM_BOTTLE_EMPTY)) {
            item = ITEM_BOTTLE_MILK_HALF;
        }
        gSaveContext.save.info.inventory.items[slot] = item;

        // A C button holding the same bottle shows its new contents too.
        for (s32 i = 1; i < 4; i++) {
            if (gSaveContext.save.info.equips.cButtonSlots[i - 1] == slot) {
                gSaveContext.save.info.equips.buttonItems[i] = item;
                Interface_LoadItemIcon1(play, i);
            }
        }
        play->pauseCtx.cursorItem[PAUSE_ITEM] = item;
        return;
    }

    // Special case to only empty half of a Lon Lon Milk Bottle
    if ((gSaveContext.save.info.inventory.items[gSaveContext.save.info.equips.cButtonSlots[button - 1]] ==
         ITEM_BOTTLE_MILK_FULL) &&
        (item == ITEM_BOTTLE_EMPTY)) {
        item = ITEM_BOTTLE_MILK_HALF;
    }

    gSaveContext.save.info.inventory.items[gSaveContext.save.info.equips.cButtonSlots[button - 1]] = item;
    gSaveContext.save.info.equips.buttonItems[button] = item;

    Interface_LoadItemIcon1(play, button);

    play->pauseCtx.cursorItem[PAUSE_ITEM] = item;
    gSaveContext.buttonStatus[button] = BTN_ENABLED;
}

// Puts `value` on a direction: pressing the direction it is already on clears it; otherwise it moves there from any
// other direction, as an item would between C buttons.
static void dpad_assign(s32 dir, u8 value) {
    u8* slots = DPAD_SLOTS(gSaveContext.save.linkAge);
    u8 newValue = (slots[dir] == value) ? 0 : value;

    for (s32 other = 0; other < DPAD_DIRECTIONS; other++) {
        if (slots[other] == value) {
            slots[other] = 0;
        }
    }
    slots[dir] = newValue;
    SFX_PLAY_CENTERED(newValue != 0 ? NA_SE_SY_DECIDE : NA_SE_SY_CURSOR);
}

// In play, a direction holding boots or a tunic puts them on, or back to Kokiri ones if they are already worn.
static void dpad_use_equipment(PlayState* play) {
    Input* input = &play->state.input[0];

    for (s32 dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        u8 item = dpad_item(dir);

        if (CHECK_BTN_ALL(input->press.button, sDpadButtons[dir]) && dpad_is_equipment(item) &&
            dpad_item_enabled(play, item)) {
            s32 type = (item <= ITEM_TUNIC_ZORA) ? EQUIP_TYPE_TUNIC : EQUIP_TYPE_BOOTS;
            s32 value = item - ((type == EQUIP_TYPE_TUNIC) ? ITEM_TUNIC_KOKIRI : ITEM_BOOTS_KOKIRI) + 1;

            if (CUR_EQUIP_VALUE(type) == value) {
                value = (type == EQUIP_TYPE_TUNIC) ? EQUIP_VALUE_TUNIC_KOKIRI : EQUIP_VALUE_BOOTS_KOKIRI;
            }
            Inventory_ChangeEquipment(type, value);
            Player_SetEquipmentData(play, GET_PLAYER(play));
            SFX_PLAY_CENTERED(NA_SE_SY_DECIDE);
            return;
        }
    }
}

// Called once per frame after the play state updates. In play, uses D-pad boots and tunics. On the pause screen, a
// D-pad press assigns what is under the cursor: an item on the item page, with the checks the page makes for C
// buttons, or (adult only) the Goron or Zora Tunic or the Iron or Hover Boots on the equipment page.
void dpad_post_play_update(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = &play->state.input[0];
    s32 dir;

    if ((pauseCtx->state == PAUSE_STATE_OFF) && (pauseCtx->debugState == 0)) {
        dpad_use_equipment(play);
        return;
    }
    if ((pauseCtx->debugState != 0) || (pauseCtx->state != PAUSE_STATE_MAIN) ||
        (pauseCtx->mainState != PAUSE_MAIN_STATE_IDLE) || (pauseCtx->cursorSpecialPos != 0)) {
        return;
    }
    for (dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        if (CHECK_BTN_ALL(input->press.button, sDpadButtons[dir])) {
            break;
        }
    }
    if (dir == DPAD_DIRECTIONS) {
        return;
    }

    if ((pauseCtx->pageIndex == PAUSE_ITEM) && (pauseCtx->cursorItem[PAUSE_ITEM] != PAUSE_ITEM_NONE)) {
        s32 slot = pauseCtx->cursorSlot[PAUSE_ITEM];

        if (slot_usable_at_age(slot) && (pauseCtx->cursorItem[PAUSE_ITEM] != ITEM_SOLD_OUT)) {
            dpad_assign(dir, slot + 1);
        } else {
            SFX_PLAY_CENTERED(NA_SE_SY_ERROR);
        }
    } else if ((pauseCtx->pageIndex == PAUSE_EQUIP) && (pauseCtx->cursorItem[PAUSE_EQUIP] != PAUSE_ITEM_NONE)) {
        s32 type = pauseCtx->cursorY[PAUSE_EQUIP];
        s32 value = pauseCtx->cursorX[PAUSE_EQUIP];

        if (((type == EQUIP_TYPE_TUNIC) || (type == EQUIP_TYPE_BOOTS)) && (value >= 2) &&
            (gSaveContext.save.linkAge == LINK_AGE_ADULT)) {
            dpad_assign(dir, DPAD_EQUIP_VALUE(type, value));
        } else {
            SFX_PLAY_CENTERED(NA_SE_SY_ERROR);
        }
    }
}

// HUD: the four items in a cross below the C buttons, in the 4:3 coordinates the C buttons use.
#define DPAD_CENTER_X 263
#define DPAD_CENTER_Y 84
#define DPAD_ICON_SIZE 16
#define DPAD_ICON_SPACING 15

static u64 sDpadIcons[DPAD_DIRECTIONS][ITEM_ICON_SIZE / sizeof(u64)];
static u8 sDpadIconItems[DPAD_DIRECTIONS] = { ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE };
static const s8 sDpadOffsets[DPAD_DIRECTIONS][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };

s32 DmaMgr_RequestSync(void* ram, uintptr_t vrom, size_t size);
Gfx* Gfx_TextureIA8(Gfx* displayListHead, void* texture, s16 textureWidth, s16 textureHeight, s16 rectLeft,
                    s16 rectTop, s16 rectWidth, s16 rectHeight, u16 dsdx, u16 dtdy);

// The ammo count at the bottom of an icon, as Interface_DrawAmmoCount in z_parameter.c draws a C button's.
static Gfx* dpad_draw_ammo(Gfx* gfx, u8 item, s32 x, s32 y, s16 alpha) {
    s32 ammo;
    s32 full;

    if ((item >= ITEM_BOW_FIRE) && (item <= ITEM_BOW_LIGHT)) {
        item = ITEM_BOW;
    }
    switch (item) {
        case ITEM_DEKU_STICK:
            full = CUR_CAPACITY(UPG_DEKU_STICKS);
            break;
        case ITEM_DEKU_NUT:
            full = CUR_CAPACITY(UPG_DEKU_NUTS);
            break;
        case ITEM_BOMB:
            full = CUR_CAPACITY(UPG_BOMB_BAG);
            break;
        case ITEM_BOW:
            full = CUR_CAPACITY(UPG_QUIVER);
            break;
        case ITEM_SLINGSHOT:
            full = CUR_CAPACITY(UPG_BULLET_BAG);
            break;
        case ITEM_BOMBCHU:
            full = 50;
            break;
        case ITEM_MAGIC_BEAN:
            full = 15;
            break;
        default:
            return gfx;
    }
    ammo = AMMO(item);

    gDPPipeSync(gfx++);
    if (ammo == 0) {
        gDPSetPrimColor(gfx++, 0, 0, 100, 100, 100, alpha);
    } else if (ammo == full) {
        gDPSetPrimColor(gfx++, 0, 0, 120, 255, 0, alpha);
    } else {
        gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, alpha);
    }
    if (ammo >= 10) {
        gfx = Gfx_TextureIA8(gfx, (u8*)gAmmoDigit0Tex + 8 * 8 * (ammo / 10), 8, 8, x, y, 8, 8, 1 << 10, 1 << 10);
    }
    return Gfx_TextureIA8(gfx, (u8*)gAmmoDigit0Tex + 8 * 8 * (ammo % 10), 8, 8, x + 6, y, 8, 8, 1 << 10, 1 << 10);
}

static void dpad_draw(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    s16 alpha = MAX(MAX(interfaceCtx->cLeftAlpha, interfaceCtx->cDownAlpha), interfaceCtx->cRightAlpha);
    s32 anyItem = false;

    for (s32 dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        anyItem |= dpad_item(dir) < ITEM_NONE_FE;
    }
    if ((alpha == 0) || !anyItem) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx, "dpad.c", 0);

    gEXPushOtherMode(OVERLAY_DISP++);
    gEXPushCombineMode(OVERLAY_DISP++);
    gEXPushPrimColor(OVERLAY_DISP++);
    gEXPushScissor(OVERLAY_DISP++);
    gEXPushEnvColor(OVERLAY_DISP++);
    gDPPipeSync(OVERLAY_DISP++);
    // The A button's view limits the scissor to the button; open it to the screen (which the HUD's scissor
    // alignment, if the Widescreen HUD set one, widens to the window).
    gDPSetScissor(OVERLAY_DISP++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetOtherMode(OVERLAY_DISP++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);

    // The cross in the middle.
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 160, 160, 160, alpha * 3 / 5);
    gDPFillRectangle(OVERLAY_DISP++, DPAD_CENTER_X - 1, DPAD_CENTER_Y - 5, DPAD_CENTER_X + 2, DPAD_CENTER_Y + 6);
    gDPFillRectangle(OVERLAY_DISP++, DPAD_CENTER_X - 5, DPAD_CENTER_Y - 1, DPAD_CENTER_X + 6, DPAD_CENTER_Y + 2);

    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    for (s32 dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        u8 item = dpad_item(dir);
        s32 x;
        s32 y;

        if (item >= ITEM_NONE_FE) {
            continue;
        }
        if (sDpadIconItems[dir] != item) {
            DmaMgr_RequestSync(sDpadIcons[dir], GET_ITEM_ICON_VROM(item), ITEM_ICON_SIZE);
            sDpadIconItems[dir] = item;
        }
        x = DPAD_CENTER_X + sDpadOffsets[dir][0] * DPAD_ICON_SPACING - DPAD_ICON_SIZE / 2;
        y = DPAD_CENTER_Y + sDpadOffsets[dir][1] * DPAD_ICON_SPACING - DPAD_ICON_SIZE / 2;

        gDPPipeSync(OVERLAY_DISP++);
        // Dimmed like a disabled C button.
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, dpad_item_enabled(play, item) ? alpha : MIN(alpha, 70));
        gDPLoadTextureBlock(OVERLAY_DISP++, sDpadIcons[dir], G_IM_FMT_RGBA, G_IM_SIZ_32b, ITEM_ICON_WIDTH,
                            ITEM_ICON_HEIGHT, 0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK,
                            G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + DPAD_ICON_SIZE) << 2, (y + DPAD_ICON_SIZE) << 2,
                            G_TX_RENDERTILE, 0, 0, (ITEM_ICON_WIDTH << 10) / DPAD_ICON_SIZE,
                            (ITEM_ICON_HEIGHT << 10) / DPAD_ICON_SIZE);
    }

    gDPPipeSync(OVERLAY_DISP++);
    gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    for (s32 dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        u8 item = dpad_item(dir);

        if (item < ITEM_NONE_FE) {
            s32 x = DPAD_CENTER_X + sDpadOffsets[dir][0] * DPAD_ICON_SPACING - DPAD_ICON_SIZE / 2;
            s32 y = DPAD_CENTER_Y + sDpadOffsets[dir][1] * DPAD_ICON_SPACING - DPAD_ICON_SIZE / 2;

            OVERLAY_DISP = dpad_draw_ammo(OVERLAY_DISP, item, x + 1, y + DPAD_ICON_SIZE - 7,
                                          dpad_item_enabled(play, item) ? alpha : MIN(alpha, 70));
        }
    }

    gDPPipeSync(OVERLAY_DISP++);
    gEXPopEnvColor(OVERLAY_DISP++);
    gEXPopScissor(OVERLAY_DISP++);
    gEXPopPrimColor(OVERLAY_DISP++);
    gEXPopCombineMode(OVERLAY_DISP++);
    gEXPopOtherMode(OVERLAY_DISP++);

    CLOSE_DISPS(play->state.gfxCtx, "dpad.c", 0);
}

// @recomp Patched to draw the D-pad items after the A button. Interface_Draw calls this while the B and C
// buttons' alignment is in effect, so with the Widescreen HUD the D-pad moves with them, and its graphics
// state is restored afterwards for the A button label. Original: Interface_DrawActionButton in
// src/code/z_parameter.c.
RECOMP_PATCH void Interface_DrawActionButton(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    OPEN_DISPS(play->state.gfxCtx, "../z_parameter.c", 3172);

    Matrix_Translate(0.0f, 0.0f, XREG(18) / 10.0f, MTXMODE_NEW);
    Matrix_Scale(1.0f, 1.0f, 1.0f, MTXMODE_APPLY);
    Matrix_RotateX(interfaceCtx->unk_1F4 / 10000.0f, MTXMODE_APPLY);

    MATRIX_FINALIZE_AND_LOAD(OVERLAY_DISP++, play->state.gfxCtx, "../z_parameter.c", 3177);
    gSPVertex(OVERLAY_DISP++, &interfaceCtx->actionVtx[0], 4, 0);

    gDPLoadTextureBlock(OVERLAY_DISP++, gButtonBackgroundTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);

    gSP1Quadrangle(OVERLAY_DISP++, 0, 2, 3, 1, 0);

    CLOSE_DISPS(play->state.gfxCtx, "../z_parameter.c", 3187);

    // @recomp
    dpad_draw(play);
}

// Magic_Update's file statics (custom_syms.toml); Magic_DrawMeter draws the meter's border with them.
extern s16 sMagicBorderR;
extern s16 sMagicBorderG;
extern s16 sMagicBorderB;

static int dpad_has_item(u8 item) {
    for (s32 dir = 0; dir < DPAD_DIRECTIONS; dir++) {
        if (dpad_item(dir) == item) {
            return true;
        }
    }
    return false;
}

// @recomp Patched so the Lens of Truth stays on while it is on the D-pad; the game turns it off as soon as no C
// button holds it. Original: Magic_Update in src/code/z_parameter.c.
RECOMP_PATCH void Magic_Update(PlayState* play) {
    static s16 sMagicBorderColors[][3] = {
        { 255, 255, 255 },
        { 150, 150, 150 },
        { 255, 255, 150 }, // unused
        { 255, 255, 50 },  // unused
    };
    static s16 sMagicBorderIndices[] = { 0, 1, 1, 0 };
    static s16 sMagicBorderRatio = 2;
    static s16 sMagicBorderStep = 1;
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    MessageContext* msgCtx = &play->msgCtx;
    s16 borderChangeR;
    s16 borderChangeG;
    s16 borderChangeB;
    s16 temp; // target for magicCapacity, or magicBorderIndex

    switch (gSaveContext.magicState) {
        case MAGIC_STATE_STEP_CAPACITY:
            // Step magicCapacity to the capacity determined by magicLevel
            // This changes the width of the magic meter drawn
            temp = gSaveContext.save.info.playerData.magicLevel * MAGIC_NORMAL_METER;
            if (temp != gSaveContext.magicCapacity) {
                if (gSaveContext.magicCapacity < temp) {
                    gSaveContext.magicCapacity += 8;
                    if (gSaveContext.magicCapacity > temp) {
                        gSaveContext.magicCapacity = temp;
                    }
                } else {
                    gSaveContext.magicCapacity -= 8;
                    if (gSaveContext.magicCapacity <= temp) {
                        gSaveContext.magicCapacity = temp;
                    }
                }
            } else {
                // Once the capacity has reached its target,
                // follow up by filling magic to magicFillTarget
                gSaveContext.magicState = MAGIC_STATE_FILL;
            }
            break;

        case MAGIC_STATE_FILL:
            // Add magic until magicFillTarget is reached
            gSaveContext.save.info.playerData.magic += 4;

            if (gSaveContext.gameMode == GAMEMODE_NORMAL && !IS_CUTSCENE_LAYER) {
                SFX_PLAY_CENTERED(NA_SE_SY_GAUGE_UP - SFX_FLAG);
            }

            if (gSaveContext.save.info.playerData.magic >= gSaveContext.magicFillTarget) {
                gSaveContext.save.info.playerData.magic = gSaveContext.magicFillTarget;
                gSaveContext.magicState = gSaveContext.prevMagicState;
                gSaveContext.prevMagicState = MAGIC_STATE_IDLE;
            }
            break;

        case MAGIC_STATE_CONSUME_SETUP:
            // Sets the speed at which magic border flashes
            sMagicBorderRatio = 2;
            gSaveContext.magicState = MAGIC_STATE_CONSUME;
            break;

        case MAGIC_STATE_CONSUME:
            // Consume magic until target is reached or no more magic is available
            gSaveContext.save.info.playerData.magic -= 2;
            if (gSaveContext.save.info.playerData.magic <= 0) {
                gSaveContext.save.info.playerData.magic = 0;
                gSaveContext.magicState = MAGIC_STATE_METER_FLASH_1;
                sMagicBorderR = sMagicBorderG = sMagicBorderB = 255;
            } else if (gSaveContext.save.info.playerData.magic == gSaveContext.magicTarget) {
                gSaveContext.magicState = MAGIC_STATE_METER_FLASH_1;
                sMagicBorderR = sMagicBorderG = sMagicBorderB = 255;
            }
            FALLTHROUGH; // Flash border while magic is being consumed
        case MAGIC_STATE_METER_FLASH_1:
        case MAGIC_STATE_METER_FLASH_2:
        case MAGIC_STATE_METER_FLASH_3:
            temp = sMagicBorderIndices[sMagicBorderStep];
            borderChangeR = ABS(sMagicBorderR - sMagicBorderColors[temp][0]) / sMagicBorderRatio;
            borderChangeG = ABS(sMagicBorderG - sMagicBorderColors[temp][1]) / sMagicBorderRatio;
            borderChangeB = ABS(sMagicBorderB - sMagicBorderColors[temp][2]) / sMagicBorderRatio;

            if (sMagicBorderR >= sMagicBorderColors[temp][0]) {
                sMagicBorderR -= borderChangeR;
            } else {
                sMagicBorderR += borderChangeR;
            }

            if (sMagicBorderG >= sMagicBorderColors[temp][1]) {
                sMagicBorderG -= borderChangeG;
            } else {
                sMagicBorderG += borderChangeG;
            }

            if (sMagicBorderB >= sMagicBorderColors[temp][2]) {
                sMagicBorderB -= borderChangeB;
            } else {
                sMagicBorderB += borderChangeB;
            }

            sMagicBorderRatio--;
            if (sMagicBorderRatio == 0) {
                sMagicBorderR = sMagicBorderColors[temp][0];
                sMagicBorderG = sMagicBorderColors[temp][1];
                sMagicBorderB = sMagicBorderColors[temp][2];
                sMagicBorderRatio = YREG(40 + sMagicBorderStep);
                sMagicBorderStep++;
                if (sMagicBorderStep >= 4) {
                    sMagicBorderStep = 0;
                }
            }
            break;

        case MAGIC_STATE_RESET:
            sMagicBorderR = sMagicBorderG = sMagicBorderB = 255;
            gSaveContext.magicState = MAGIC_STATE_IDLE;
            break;

        case MAGIC_STATE_CONSUME_LENS:
            // Slowly consume magic while lens is on
            if (!IS_PAUSED(&play->pauseCtx) && (msgCtx->msgMode == MSGMODE_NONE) &&
                (play->gameOverCtx.state == GAMEOVER_INACTIVE) && (play->transitionTrigger == TRANS_TRIGGER_OFF) &&
                (play->transitionMode == TRANS_MODE_OFF) && !Play_InCsMode(play)) {
                if ((gSaveContext.save.info.playerData.magic == 0) ||

                    ((Player_GetEnvironmentalHazard(play) >= PLAYER_ENV_HAZARD_UNDERWATER_FLOOR) &&
                     (Player_GetEnvironmentalHazard(play) <= PLAYER_ENV_HAZARD_UNDERWATER_FREE)) ||
                    ((gSaveContext.save.info.equips.buttonItems[1] != ITEM_LENS_OF_TRUTH) &&
                     (gSaveContext.save.info.equips.buttonItems[2] != ITEM_LENS_OF_TRUTH) &&
                     (gSaveContext.save.info.equips.buttonItems[3] != ITEM_LENS_OF_TRUTH) &&
                     // @recomp
                     !dpad_has_item(ITEM_LENS_OF_TRUTH)) ||
                    !play->actorCtx.lensActive) {
                    // Force lens off and set magic meter state to idle
                    play->actorCtx.lensActive = false;
                    SFX_PLAY_CENTERED(NA_SE_SY_GLASSMODE_OFF);
                    gSaveContext.magicState = MAGIC_STATE_IDLE;
                    sMagicBorderR = sMagicBorderG = sMagicBorderB = 255;
                    break;
                }

                interfaceCtx->lensMagicConsumptionTimer--;
                if (interfaceCtx->lensMagicConsumptionTimer == 0) {
                    gSaveContext.save.info.playerData.magic--;
                    interfaceCtx->lensMagicConsumptionTimer = 80;
                }
            }

            temp = sMagicBorderIndices[sMagicBorderStep];
            borderChangeR = ABS(sMagicBorderR - sMagicBorderColors[temp][0]) / sMagicBorderRatio;
            borderChangeG = ABS(sMagicBorderG - sMagicBorderColors[temp][1]) / sMagicBorderRatio;
            borderChangeB = ABS(sMagicBorderB - sMagicBorderColors[temp][2]) / sMagicBorderRatio;

            if (sMagicBorderR >= sMagicBorderColors[temp][0]) {
                sMagicBorderR -= borderChangeR;
            } else {
                sMagicBorderR += borderChangeR;
            }

            if (sMagicBorderG >= sMagicBorderColors[temp][1]) {
                sMagicBorderG -= borderChangeG;
            } else {
                sMagicBorderG += borderChangeG;
            }

            if (sMagicBorderB >= sMagicBorderColors[temp][2]) {
                sMagicBorderB -= borderChangeB;
            } else {
                sMagicBorderB += borderChangeB;
            }

            sMagicBorderRatio--;
            if (sMagicBorderRatio == 0) {
                sMagicBorderR = sMagicBorderColors[temp][0];
                sMagicBorderG = sMagicBorderColors[temp][1];
                sMagicBorderB = sMagicBorderColors[temp][2];
                sMagicBorderRatio = YREG(40 + sMagicBorderStep);
                sMagicBorderStep++;
                if (sMagicBorderStep >= 4) {
                    sMagicBorderStep = 0;
                }
            }
            break;

        case MAGIC_STATE_ADD:
            // Add magic until target is reached
            gSaveContext.save.info.playerData.magic += 4;
            SFX_PLAY_CENTERED(NA_SE_SY_GAUGE_UP - SFX_FLAG);
            if (gSaveContext.save.info.playerData.magic >= gSaveContext.magicTarget) {
                gSaveContext.save.info.playerData.magic = gSaveContext.magicTarget;
                gSaveContext.magicState = gSaveContext.prevMagicState;
                gSaveContext.prevMagicState = MAGIC_STATE_IDLE;
            }
            break;

        default:
            gSaveContext.magicState = MAGIC_STATE_IDLE;
            break;
    }
}
