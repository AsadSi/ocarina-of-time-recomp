#include "modding.h"
#include "ultra64.h"
#include "message.h"
#include "message_data_static.h"
#include "play_state.h"

// Prints each page of text at once, as pressing B does, but still waits for A between pages (B in the original
// game also skips the pages after, via sTextboxSkipped, which this leaves alone). Pages the game marks as
// unskippable, and credits-style text without a box, keep their original speed.
RECOMP_HOOK("Message_Update") void faster_text_on_message_update(PlayState* play) {
    MessageContext* msgCtx = &play->msgCtx;

    if ((msgCtx->msgMode == MSGMODE_TEXT_DISPLAYING) && (msgCtx->textBoxType != TEXTBOX_TYPE_NONE_BOTTOM) &&
        !msgCtx->textUnskippable && (msgCtx->textDrawPos < msgCtx->decodedTextLen)) {
        msgCtx->textDrawPos = msgCtx->decodedTextLen;
    }
}
