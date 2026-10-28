#include "global.h"
#include "modding.h"

PlayState *sPlay;

void checkForSceneUpdate_on_Play_Main(PlayState *play);

RECOMP_HOOK("Play_Main") void on_Play_Main(PlayState *play) {
    sPlay = play;

    checkForSceneUpdate_on_Play_Main(sPlay);
}

// RECOMP_HOOK_RETURN("Play_Main") void on_return_Play_Main(void) {
// }
