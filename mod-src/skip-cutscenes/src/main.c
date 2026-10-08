#include "modding.h"
#include "ultra64.h"
#include "array_count.h"
#include "controller.h"
#include "cutscene.h"
#include "play_state.h"
#include "save.h"
#include "scene.h"
#include "transition.h"

// START skips a cutscene. A script with a destination command has it fire now, so the destination's own code
// changes the scene and gives whatever it gives (e.g. Requiem and Nocturne). A script without one (area intros,
// Epona's jumps) is stopped, as its own CS_MISC_STOP_CUTSCENE would at its last frame; only the area's title card
// is lost. Either way the rest of the script is jumped over, so only cutscenes that hand out nothing on the way
// may be skipped: those started from the entrance table or the conditional triggers (which set their flag, and
// any item, before playing) and on the list below. Left out on purpose: the Ocarina of Time (it lands in the
// moat mid-cutscene), the Light Arrows (Zelda gives them mid-cutscene), the Sun's Song grave, the barriers, boss
// intros and the tower collapse.

static const s16 sSkippableEntrances[] = {
    // First visits.
    ENTR_HYRULE_FIELD_3, ENTR_DEATH_MOUNTAIN_TRAIL_0, ENTR_KAKARIKO_VILLAGE_0, ENTR_ZORAS_DOMAIN_0,
    ENTR_HYRULE_CASTLE_0, ENTR_GORON_CITY_0, ENTR_TEMPLE_OF_TIME_0, ENTR_DEKU_TREE_0, ENTR_LAKE_HYLIA_0,
    ENTR_GERUDO_VALLEY_0, ENTR_GERUDOS_FORTRESS_0, ENTR_LON_LON_RANCH_0, ENTR_JABU_JABU_0, ENTR_GRAVEYARD_0,
    ENTR_ZORAS_FOUNTAIN_2, ENTR_DESERT_COLOSSUS_0, ENTR_DEATH_MOUNTAIN_CRATER_0, ENTR_DEATH_MOUNTAIN_CRATER_1,
    // Epona jumping the fences, the first capture at the fortress.
    ENTR_HYRULE_FIELD_11, ENTR_HYRULE_FIELD_12, ENTR_HYRULE_FIELD_13, ENTR_HYRULE_FIELD_15, ENTR_GERUDOS_FORTRESS_17,
    // Conditional: Sheik at the Colossus and in burning Kakariko (the destination gives the song), Saria's
    // ocarina (given before).
    ENTR_LOST_WOODS_0,
};

static PlayState* sPlay;
static s32 sSkippable;
static u8 sPrevTrigger;
static u16 sPrevCutsceneIndex;

static s32 entrance_skippable(void) {
    for (u32 i = 0; i < ARRAY_COUNT(sSkippableEntrances); i++) {
        if (gSaveContext.save.entranceIndex == sSkippableEntrances[i]) {
            return true;
        }
    }
    return false;
}

RECOMP_HOOK("Play_Init") void skip_cutscenes_on_play_init(PlayState* play) {
    sPlay = play;
    sSkippable = false;
}

RECOMP_HOOK("Cutscene_HandleEntranceTriggers") void skip_cutscenes_before_entrance_triggers(PlayState* play) {
    sPrevTrigger = gSaveContext.cutsceneTrigger;
}

RECOMP_HOOK_RETURN("Cutscene_HandleEntranceTriggers") void skip_cutscenes_after_entrance_triggers(void) {
    if ((sPrevTrigger != 2) && (gSaveContext.cutsceneTrigger == 2) && entrance_skippable()) {
        sSkippable = true;
    }
}

RECOMP_HOOK("Cutscene_HandleConditionalTriggers") void skip_cutscenes_before_conditional(PlayState* play) {
    sPrevCutsceneIndex = gSaveContext.save.cutsceneIndex;
}

RECOMP_HOOK_RETURN("Cutscene_HandleConditionalTriggers") void skip_cutscenes_after_conditional(void) {
    if ((sPrevCutsceneIndex < CS_INDEX_0) && (gSaveContext.save.cutsceneIndex == CS_INDEX_0) && entrance_skippable()) {
        sSkippable = true;
    }
}

// Each frame of a running script: a START press requests the skip, which the script's destination command (called
// from in here every frame) takes if it has one; otherwise the script is stopped after this frame's commands.
static CutsceneContext* sSkipCsCtx;

RECOMP_HOOK("CutsceneHandler_RunScript") void skip_cutscenes_on_run_script(PlayState* play, CutsceneContext* csCtx) {
    sSkipCsCtx = NULL;
    if (sSkippable && (play == sPlay) && (gSaveContext.gameMode == GAMEMODE_NORMAL) &&
        (csCtx->state == CS_STATE_RUN) && (csCtx->curFrame > 20) && (play->transitionTrigger == TRANS_TRIGGER_OFF) &&
        CHECK_BTN_ALL(play->state.input[0].press.button, BTN_START)) {
        sSkippable = false;
        sSkipCsCtx = csCtx;
    }
}

RECOMP_HOOK("CutsceneCmd_Destination")
void skip_cutscenes_on_destination(PlayState* play, CutsceneContext* csCtx, CsCmdDestination* cmd) {
    if ((sSkipCsCtx == csCtx) && (csCtx->curFrame < cmd->startFrame)) {
        sSkipCsCtx = NULL;
        csCtx->curFrame = cmd->startFrame;
    }
}

RECOMP_HOOK_RETURN("CutsceneHandler_RunScript") void skip_cutscenes_after_run_script(void) {
    if ((sSkipCsCtx != NULL) && (sSkipCsCtx->state == CS_STATE_RUN)) {
        sSkipCsCtx->state = CS_STATE_STOP;
    }
    sSkipCsCtx = NULL;
}
