#include "global.h"
#include "modding.h"

static GraphicsContext *sGfxCtx;
static GameState *sGameState;

void pollNetwork_on_Graph_Update(void);
void processClientEvents_on_Graph_Update(void);
void updateLoggerLevel_on_Graph_Update(void);

RECOMP_HOOK("Graph_Update") void on_Graph_Update(GraphicsContext *gfxCtx, GameState *gameState) {
    sGfxCtx = gfxCtx;
    sGameState = gameState;

    pollNetwork_on_Graph_Update();
    processClientEvents_on_Graph_Update();
    updateLoggerLevel_on_Graph_Update();
}

// RECOMP_HOOK_RETURN("Graph_Update") void on_return_Graph_Update(void) {
// }
