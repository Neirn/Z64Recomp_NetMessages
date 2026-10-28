#include "global.h"
#include "recompdata.h"
#include "netmessages_api.h"
#include "yazmtcorelib_api.h"
#include "recompinterop.h"
#include "logger.h"
#include "networkconnection.h"
#include "netmessages_api.h"
#include "netmessages.h"
#include "networkmod.h"

RECOMP_DECLARE_EVENT(onRegisterMessageGroups(void));

NetMessagesConnectionId gSelfConnectionId;

static MemorySlotmapHandle sNetMessageInfos;
static YAZMTCore_DynamicU32Array *sNetMessageInfoPtrList;
static YAZMTCore_StringU32Dictionary *sMessageStrIdToPtr;

#define PRINT_INVALID_HANDLE_MSG(h) Logger_printError("Received invalid handle %u", h)
#define PRINT_NOT_CONNECTED_MSG() Logger_printWarning("Not connected to server!")
#define PRINT_UNSUBSCRIBED_MESSAGE_GROUP_ACTION_FAIL(groupName) Logger_printError("Can't perform this action on unsubscribed message group '%s'! Ignoring!", groupName)

RECOMP_CALLBACK("*", recomp_on_init) void doNetMessageInits(void) {
    sNetMessageInfos = recomputil_create_memory_slotmap(sizeof(NetworkMessageInfo));
    sNetMessageInfoPtrList = YAZMTCore_DynamicU32Array_new();
    sMessageStrIdToPtr = YAZMTCore_StringU32Dictionary_new();
    onRegisterMessageGroups();
}

static NetworkMessageInfo *NetMessages_getMessageInfoFromHandle(NetMessagesGroupHandle h) {
    void *out = NULL;

    if (h) {
        recomputil_memory_slotmap_get(sNetMessageInfos, h, &out);
    }

    return out;
}

NetworkMessageInfo *NetMessages_getMessageInfoFromString(const char *s) {
    uintptr_t out = (uintptr_t)NULL;

    if (s) {
        YAZMTCore_StringU32Dictionary_get(sMessageStrIdToPtr, s, &out);
    }

    return (NetworkMessageInfo *)out;
}

bool isGroupNameTooLong(const char *s) {
    size_t len = 0;

    while (len < (MAX_RECOMP_NETWORK_MESSAGE_GROUP_NAME_BUFFER_SIZE) && s[len] != '\0') {
        len++;
    }

    return len >= MAX_RECOMP_NETWORK_MESSAGE_GROUP_NAME_BUFFER_SIZE;
}

static void NetMessages_updateSubscribedStatus(NetworkMessageInfo *info) {
    if (NetworkConnection_isConnected()) {
        if (info->isActive) {
            NetMessagesExt_subscribeToMessageGroup(info->strId);
            // Logger_printInfo("Subscribing to message group %s", info->strId);
        } else {
            info->leader = EMPTY_MESSAGE_LEADER;
            NetMessagesExt_unsubscribeFromMessageGroup(info->strId);
            // Logger_printInfo("Unsubscribing from message group %s", info->strId);
        }
    }
}

static void emptyCallback(NetMessagesGroupHandle h, NetMessagesConnectionId connId, NetMessagesCallbackEvent type, const void *callbackData) {}

RECOMP_EXPORT NetMessagesGroupHandle NetMessages_registerMessageGroup(const char *messageId, NetMessagesFlag flags, NetMessages_CallbackFunc callback, size_t callbackDataSize) {
    NetMessagesGroupHandle h = 0;

    if (messageId != NULL && !isGroupNameTooLong(messageId)) {
        Logger_printInfo("Registering message group %s...", messageId);
        if (callbackDataSize <= MAX_RECOMP_NETWORK_DATA_SIZE) {
            if (!YAZMTCore_StringU32Dictionary_contains(sMessageStrIdToPtr, messageId)) {
                h = recomputil_memory_slotmap_create(sNetMessageInfos);

                NetworkMessageInfo *info = NetMessages_getMessageInfoFromHandle(h);

                if (info) {
                    info->flags = flags;
                    info->handle = h;
                    info->isActive = true;
                    info->strId = YAZMTCore_Utils_StrDup(messageId);
                    info->callback = callback == NULL ? emptyCallback : callback;
                    info->callbackDataSize = callbackDataSize;
                    info->leader = EMPTY_MESSAGE_LEADER;

                    YAZMTCore_DynamicU32Array_push(sNetMessageInfoPtrList, (uintptr_t)info);
                    YAZMTCore_StringU32Dictionary_set(sMessageStrIdToPtr, info->strId, (uintptr_t)info);

                    NetMessages_updateSubscribedStatus(info);

                    Logger_printInfo("Registered message group %s", messageId);
                }
            } else {
                Logger_printError("NetMessage with id '%s' already exists!", messageId);
            }
        } else {
            Logger_printError("callbackDataSize too large! (Max: %u)", callbackDataSize);
        }
    } else {
        Logger_printError("Passed in invalid id arg (was NULL or too long)!");
    }

    return h;
}

RECOMP_EXPORT bool NetMessages_sendDataMessage(NetMessagesGroupHandle h, NetMessagesConnectionId receiverId, const void *data) {
    if (NetworkConnection_isConnected()) {
        NetworkMessageInfo *info = NetMessages_getMessageInfoFromHandle(h);

        if (info) {
            if (info->isActive) {
                NetMessagesExt_SendMessageArg sendArg = {
                    .flags = info->flags,
                    .payloadBufferSize = info->callbackDataSize,
                    .target = receiverId,
                };

                return NetMessagesExt_sendMessage(&sendArg, data, info->strId);
            } else {
                PRINT_UNSUBSCRIBED_MESSAGE_GROUP_ACTION_FAIL(info->strId);
            }
        } else {
            PRINT_INVALID_HANDLE_MSG(h);
        }
    } else {
        PRINT_NOT_CONNECTED_MSG();
    }

    return false;
}

RECOMP_EXPORT bool NetMessages_setSubscribed(NetMessagesGroupHandle h, bool isSubscribed) {
    NetworkMessageInfo *info = NetMessages_getMessageInfoFromHandle(h);

    if (info) {
        if (info->isActive != isSubscribed) {
            info->isActive = isSubscribed;
            NetMessages_updateSubscribedStatus(info);
        }
    } else {
        PRINT_INVALID_HANDLE_MSG(h);
    }

    return false;
}

RECOMP_EXPORT bool NetMessages_getOwnConnectionId(NetMessagesConnectionId *out) {
    bool ret = false;

    if (NetworkConnection_isConnected()) {
        if (gSelfConnectionId) {
            *out = gSelfConnectionId;
            return true;
        }
    } else {
        PRINT_NOT_CONNECTED_MSG();
    }

    return false;
}

RECOMP_EXPORT bool NetMessages_getLeaderConnectionId(NetMessagesGroupHandle h, NetMessagesConnectionId *out) {
    bool ret = false;

    if (NetworkConnection_isConnected()) {
        NetworkMessageInfo *info = NetMessages_getMessageInfoFromHandle(h);
        if (info) {
            if (info->isActive) {
                if (info->leader != EMPTY_MESSAGE_LEADER) {
                    *out = info->leader;
                    ret = true;
                }
            } else {
                PRINT_UNSUBSCRIBED_MESSAGE_GROUP_ACTION_FAIL(info->strId);
            }
        } else {
            PRINT_INVALID_HANDLE_MSG(h);
        }
    } else {
        PRINT_NOT_CONNECTED_MSG();
    }

    return ret;
}

RECOMP_EXPORT bool NetMessages_requestBecomeLeader(NetMessagesGroupHandle h) {
    bool ret = false;

    if (NetworkConnection_isConnected()) {
        NetworkMessageInfo *info = NetMessages_getMessageInfoFromHandle(h);
        if (info) {
            if (info->isActive) {
                if (info->leader != gSelfConnectionId) {
                    NetMessagesExt_requestLeader(info->strId);
                } else {
                    Logger_printWarning("This client is already the leader of '%s'! Not sending request!", info->strId);
                }
            } else {
                PRINT_UNSUBSCRIBED_MESSAGE_GROUP_ACTION_FAIL(info->strId);
            }
        } else {
            PRINT_INVALID_HANDLE_MSG(h);
        }
    } else {
        PRINT_NOT_CONNECTED_MSG();
    }

    return ret;
}

RECOMP_EXPORT bool NetMessages_releaseLeader(NetMessagesGroupHandle h) {
    bool ret = false;

    if (NetworkConnection_isConnected()) {
        NetworkMessageInfo *info = NetMessages_getMessageInfoFromHandle(h);
        if (info) {
            if (info->isActive) {
                if (info->leader == gSelfConnectionId) {
                    NetMessagesExt_releaseLeader(info->strId);
                } else {
                    Logger_printWarning("This client isn't the leader of '%s'! Not sending release request!", info->strId);
                }
            } else {
                PRINT_UNSUBSCRIBED_MESSAGE_GROUP_ACTION_FAIL(info->strId);
            }
        } else {
            PRINT_INVALID_HANDLE_MSG(h);
        }
    } else {
        PRINT_NOT_CONNECTED_MSG();
    }

    return ret;
}

RECOMP_EXPORT bool NetMessages_isConnectedToServer(void) {
    return NetworkConnection_isConnected();
}

void NetMessages_onConnect(void) {
    size_t infosLen = YAZMTCore_DynamicU32Array_size(sNetMessageInfoPtrList);
    NetworkMessageInfo **infosData = (NetworkMessageInfo **)(YAZMTCore_DynamicU32Array_data(sNetMessageInfoPtrList));
    for (size_t i = 0; i < infosLen; i++) {
        NetMessages_updateSubscribedStatus(infosData[i]);
    }
}

void NetMessages_onDisconnect(void) {
    size_t infosLen = YAZMTCore_DynamicU32Array_size(sNetMessageInfoPtrList);
    NetworkMessageInfo **infosData = (NetworkMessageInfo **)(YAZMTCore_DynamicU32Array_data(sNetMessageInfoPtrList));
    for (size_t i = 0; i < infosLen; i++) {
        (infosData[i])->leader = EMPTY_MESSAGE_LEADER;
    }
}

RECOMP_EXPORT char *NetMessages_getClientNickname(NetMessagesConnectionId connId) {
    char tmp[MAX_RECOMP_NETWORK_NICKNAME_BUFFER_SIZE];
    if (NetMessagesExt_getClientNickname(connId, tmp, sizeof(tmp))) {
        tmp[sizeof(tmp) - 1] = '\0';
        return YAZMTCore_Utils_StrDup(tmp);
    }

    return NULL;
}
