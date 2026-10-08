#include "modding.h"
#include "ultra64.h"
#include "message.h"
#include "ocarina.h"
#include "play_state.h"

// After a song is played correctly, the game replays it on the staff before its effect. Stopping the playback
// and clearing the staff state ends that replay this frame; Message_Update then moves on exactly as when the
// replay finishes (the song, the ocarina mode and its action were all set before the replay began). Teachers'
// demonstrations (MSGMODE_SONG_DEMONSTRATION) are left alone: they are how the player learns the notes.
RECOMP_HOOK("Message_Update") void fast_songs_on_message_update(PlayState* play) {
    if (play->msgCtx.msgMode == MSGMODE_DISPLAY_SONG_PLAYED) {
        AudioOcarina_SetPlaybackSong(0, 0);
        AudioOcarina_GetPlaybackStaff()->state = 0;
    }
}
