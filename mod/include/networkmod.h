#ifndef NETWORKMOD_H
#define NETWORKMOD_H

#include "attributes.h"
#include "PR/ultratypes.h"
#include "modding.h"
#include "recompinterop.h"
#include "netmessages_api.h"

typedef u32 bool32;

#define ALL_CLIENTS ((NetMessagesConnectionId)0)
#define SERVER_CONNECTION_ID ((NetMessagesConnectionId)0)

RECOMP_IMPORT(".", bool32 NetMessagesExt_init(void));
RECOMP_IMPORT(".", bool32 NetMessagesExt_connect(const char *ip, const char *roomName, const char *nickname, const char *gameId));
RECOMP_IMPORT(".", void NetMessagesExt_disconnect(void));
RECOMP_IMPORT(".", void NetMessagesExt_doPolling(void));
RECOMP_IMPORT(".", bool32 NetMessagesExt_isClientEventPending(void));
RECOMP_IMPORT(".", void NetMessagesExt_requestLeader(const char *messageGroup));
RECOMP_IMPORT(".", void NetMessagesExt_releaseLeader(const char *messageGroup));
RECOMP_IMPORT(".", void NetMessagesExt_subscribeToMessageGroup(const char *messageGroup));
RECOMP_IMPORT(".", void NetMessagesExt_unsubscribeFromMessageGroup(const char *messageGroup));
RECOMP_IMPORT(".", void NetMessagesExt_sendSceneIdUpdate(s32 sceneId));
RECOMP_IMPORT(".", bool32 NetMessagesExt_getClientNickname(NetMessagesConnectionId id, char *dest, size_t destSize));

#pragma pack(push, 1)
// fields are 4 bytes in size each
// When adding/removing/reordering fields NetMessagesExt_retrieveNextClientEvent in recompinterop.cpp MUST be updated
typedef struct NetMessagesExt_SendMessageArg {
    NetMessagesConnectionId target;
    u32 flags;
    size_t payloadBufferSize;
} NetMessagesExt_SendMessageArg;

typedef struct NetMessagesExt_RetrieveClientEventArg {
    struct {
        RecompClientEvent clientEvent;
        NetMessagesConnectionId senderId;
        size_t payloadSize;
    } extlibOutputs;
    struct {
        size_t dataBufSize;
        size_t messageGroupBufSize;
    } extlibInputs;
} NetMessagesExt_RetrieveClientEventArg;
#pragma pack(pop)

// 1st arg needs to be array since we can only pass 3 args to extlib
RECOMP_IMPORT(".", bool32 NetMessagesExt_retrieveNextClientEvent(NetMessagesExt_RetrieveClientEventArg *evtIdAndconnIdAndSize, void *buf, char *messageGroupOut));
RECOMP_IMPORT(".", bool32 NetMessagesExt_sendMessage(NetMessagesExt_SendMessageArg *messageInfo, const void *buf, const char *messageGroup));

#endif
