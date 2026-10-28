#include "global.h"
#include "modding.h"
#include "recompinterop.h"
#include "networkmod.h"
#include "logger.h"
#include "netmessages_api.h"
#include "yazmtcorelib_api.h"
#include "networkconnection.h"
#include "netmessages.h"

RECOMP_DECLARE_EVENT(onConnectToServer(NetMessagesConnectionId ownId));
RECOMP_DECLARE_EVENT(onDisconnectFromServer(NetMessagesConnectionId ownId));
RECOMP_DECLARE_EVENT(onRemoteClientJoined(NetMessagesConnectionId id));
RECOMP_DECLARE_EVENT(onRemoteClientRemoved(NetMessagesConnectionId id));
RECOMP_DECLARE_EVENT(onRemoteClientSceneReceive(NetMessagesConnectionId connId, s16 scene));

#define PRINT_UNK_MSG_GROUP(groupStr) Logger_printWarning("Received message with unknown message group '%s'", groupStr)

static void handleDisconnectCommon(void) {
    NetworkConnection_reset();
    NetMessages_onDisconnect();
}

void processClientEvents_on_Graph_Update() {
    while (NetMessagesExt_isClientEventPending()) {
        static char messageGroup[MAX_RECOMP_NETWORK_MESSAGE_GROUP_NAME_BUFFER_SIZE];
        _Alignas(u64) static char tmp[MAX_RECOMP_NETWORK_DATA_SIZE];

        NetMessagesExt_RetrieveClientEventArg clEvt = {
            .extlibInputs = {
                .dataBufSize = sizeof(tmp),
                .messageGroupBufSize = sizeof(messageGroup),
            }};

        messageGroup[0] = '\0';

        if (NetMessagesExt_retrieveNextClientEvent(&clEvt, tmp, messageGroup)) {
            RecompClientEvent evt = clEvt.extlibOutputs.clientEvent;
            NetMessagesConnectionId id = clEvt.extlibOutputs.senderId;
            size_t dataSize = clEvt.extlibOutputs.payloadSize;

            // Logger_printInfo(
            //     "retrieveNextClientEvent Outputs:\n"
            //     "evt = %u\n"
            //     "id = %u\n"
            //     "dataSize = %u\n",
            //     evt, id, dataSize);

            // TODO: Add an initial subscription group to avoid missing some reliable events

            if (!NetworkConnection_isConnected()) {
                if (evt == RE_CL_EVT_OWN_ID_RECEIVED) {
                    gSelfConnectionId = id;
                    NetworkConnection_markVerified();
                    NetMessages_onConnect();
                    onConnectToServer(id);
                } else if (evt == RE_CL_EVT_DISCONNECTED) {
                    handleDisconnectCommon();
                } else {
                    Logger_printWarning("Discarding message from group '%s' (Received out of order?)", messageGroup);
                }
            } else {
                switch (evt) {
                    case RE_CL_EVT_REMOTE_PLAYER_ADDED: {
                        onRemoteClientJoined(id);
                    } break;

                    case RE_CL_EVT_REMOTE_PLAYER_REMOVED: {
                        onRemoteClientRemoved(id);
                    } break;

                    case RE_CL_EVT_DISCONNECTED: {
                        NetMessagesConnectionId id = gSelfConnectionId;
                        handleDisconnectCommon();
                        onDisconnectFromServer(id);
                    } break;

                    case RE_CL_EVT_LEADER_AVAILABLE: {
                        NetworkMessageInfo *info = NetMessages_getMessageInfoFromString(messageGroup);

                        if (info) {
                            if (info->isActive) {
                                info->leader = EMPTY_MESSAGE_LEADER;
                                info->callback(info->handle, SERVER_CONNECTION_ID, NET_MSG_CB_LEADER_AVAILABLE, NULL);
                            }
                        } else {
                            PRINT_UNK_MSG_GROUP(messageGroup);
                        }
                    } break;

                    case RE_CL_EVT_LEADER_ID_RECEIVED: {
                        NetworkMessageInfo *info = NetMessages_getMessageInfoFromString(messageGroup);

                        if (info) {
                            if (info->isActive && info->leader != id) {
                                info->leader = id;
                                info->callback(info->handle, info->leader, NET_MSG_CB_LEADER_ID_RECEIVED, NULL);
                            }
                        } else {
                            PRINT_UNK_MSG_GROUP(messageGroup);
                        }
                    } break;

                    case RE_CL_EVT_RECOMP_BLOB: {
                        NetworkMessageInfo *info = NetMessages_getMessageInfoFromString(messageGroup);

                        if (info) {
                            if (info->isActive) {
                                if (info->callbackDataSize == dataSize) {
                                    info->callback(info->handle, id, NET_MSG_CB_DATA_MSG, tmp);
                                } else {
                                    Logger_printWarning("Received message for group '%s' with size %u but expected size was %u! Discarding...", messageGroup, info->callbackDataSize, dataSize);
                                }
                            }
                        } else {
                            PRINT_UNK_MSG_GROUP(messageGroup);
                        }
                    } break;

                    case RE_CL_EVT_SCENE_UPDATE: {
                        s16 scene = *((s16 *)tmp);
                        onRemoteClientSceneReceive(id, scene);
                        Logger_printInfo("Client %u moved to scene %hd", id, scene);
                    } break;

                    default: {
                        Logger_printWarning("Received unhandled event %d", evt);
                    } break;
                }
            }
        }
    }
}
