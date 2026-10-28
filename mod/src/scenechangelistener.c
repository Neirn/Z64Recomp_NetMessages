#include "global.h"
#include "networkmod.h"
#include "networkconnection.h"
#include "netmessages.h"
#include "netmessages_api.h"

RECOMP_DECLARE_EVENT(onLocalClientSceneSend(NetMessagesConnectionId id, s16 scene));

static s16 sPrevSceneId;

void checkForSceneUpdate_on_Play_Main(PlayState *play) {
    if (NetworkConnection_isConnected()) {
        if (play->sceneId != sPrevSceneId) {
            sPrevSceneId = play->sceneId;
            NetMessagesExt_sendSceneIdUpdate(sPrevSceneId);
            onLocalClientSceneSend(gSelfConnectionId, play->sceneId);
        }
    }
}

NETMESSAGES_ON_SERVER_CONNECT void resetPrevSceneIdOnConnect(void) {
    sPrevSceneId = -1;
}
