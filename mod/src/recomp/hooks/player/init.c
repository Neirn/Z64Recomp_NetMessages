#include "global.h"
#include "modding.h"

static Player *sPlayer;
static PlayState *sPlay;

void tryConnectToServer_on_Player_Init(void);

RECOMP_HOOK("Player_Init") void on_Player_Init(Player *this, PlayState *play) {
    sPlayer = this;
    sPlay = play;

    tryConnectToServer_on_Player_Init();
}

// RECOMP_HOOK_RETURN("Player_Init") void on_return_Player_Init(void) {
// }
