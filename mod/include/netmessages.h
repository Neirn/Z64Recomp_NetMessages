#ifndef NETMESSAGES_H
#define NETMESSAGES_H

#include "netmessages_api.h"
#include "stdbool.h"

typedef struct NetworkMessageInfo {
    char *strId;
    NetMessagesGroupHandle handle;
    NetMessagesFlag flags;
    NetMessages_CallbackFunc *callback;
    size_t callbackDataSize;
    NetMessagesConnectionId leader;
    bool isActive;
} NetworkMessageInfo;

extern NetMessagesConnectionId gSelfConnectionId;

#define EMPTY_MESSAGE_LEADER ((NetMessagesConnectionId)0)

NetworkMessageInfo *NetMessages_getMessageInfoFromString(const char *s);
void NetMessages_onConnect(void);
void NetMessages_onDisconnect(void);

#endif
