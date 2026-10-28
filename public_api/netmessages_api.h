#ifndef NETMESSAGES_API_H
#define NETMESSAGES_API_H

typedef enum NetMessagesFlag {
    NET_MSG_FLG_NONE = 0,

    // Messages may arrive out of order or not at all
    NET_MSG_FLG_UNRELIABLE = 1,

    // Data messages will only be forwarded by the server
    // if the client is in the same scene
    //
    // Scenes like the poison swamp and clear swamp are considered
    // the same scene for the purposes of this flag; to see which
    // other scenes are handled this way, look at
    // Play_GetOriginalSceneId in the decomp
    NET_MSG_FLG_SCENE_RESTRICT = (1 << 1),
} NetMessagesFlag;

typedef enum NetMessagesCallbackEvent {
    NET_MSG_CB_NONE,

    // Received when another client uses NetMessages_sendDataMessage
    NET_MSG_CB_DATA_MSG,

    // Received when subscribing to a group with no leader or
    // when the group leader calls NetMessages_releaseLeader or
    // when the group leader disconnects from the server
    NET_MSG_CB_LEADER_AVAILABLE,

    // Received when subscribing to a group with a leader
    // or when one of the group members calls NetMessages_requestBecomeLeader
    NET_MSG_CB_LEADER_ID_RECEIVED,
} NetMessagesCallbackEvent;

#ifndef NETMESSAGES_API_ENUM_ONLY

#include "stdbool.h"
#include "PR/ultratypes.h"
#include "attributes.h"
#include "modding.h"

#ifdef NETMESSAGES_LOCAL_API
#define NETMESSAGES_MOD_NAME "."
#define NETMESSAGES_IMPORT(b) b
#else
#define NETMESSAGES_MOD_NAME "yazmt_z64_netmessages"
#define NETMESSAGES_IMPORT(b) RECOMP_IMPORT(NETMESSAGES_MOD_NAME, b)
#endif

typedef unsigned long NetMessagesGroupHandle;

#define NET_MSG_GROUP_INVALID ((NetMessagesGroupHandle)0)

typedef unsigned long NetMessagesConnectionId;

#define NET_MSG_CONN_ID_SERVER ((NetMessagesConnectionId)0)
#define NET_MSG_CONN_ID_BROADCAST ((NetMessagesConnectionId)0)

// A function that is called when a NetMessagesCallbackEvent is received over the network
//
// Args:
//   - h            : The NetMessagesGroupHandle that received an event
//
//   - connId       : Contextual NetMessagesConnectionId
//                      * NET_MSG_CB_DATA_MSG           : holds the sender of the data message
//                      * NET_MSG_CB_LEADER_AVAILABLE   : holds NET_MSG_CONN_ID_SERVER
//                      * NET_MSG_CB_LEADER_ID_RECEIVED : holds the NetMessagesConnectionId of the group's leader,
//                                                        can be own id
//
//   - event        : Holds the NetMessagesCallbackEvent that triggered this call of the function
//
//   - callbackData : Holds a pointer to the data included with this message, currently is either NULL or
//                    the data sent by NetMessages_sendDataMessage
typedef void NetMessages_CallbackFunc(NetMessagesGroupHandle h, NetMessagesConnectionId connId, NetMessagesCallbackEvent event, const void *callbackData);

// Creates a new message group and returns a handle to it. Returns NET_MSG_GROUP_INVALID if registration fails
//
// Args:
//   - messageId        : A string identifier for this message group. It should be unique from all other message groups,
//                        including groups registered by other mods. It may be up to 31 bytes in length
//
//   - flags:           : Flags for this message group that may modify how it is handled. See the NetMessagesFlag enum
//
//   - callback         : Function that will be called when an event for this message group is received.
//
//   - callbackDataSize : Size of the data that will be sent by NetMessages_sendDataMessage, usually the size of a struct
NETMESSAGES_IMPORT(NetMessagesGroupHandle NetMessages_registerMessageGroup(const char *messageId, NetMessagesFlag flags, NetMessages_CallbackFunc callback, size_t callbackDataSize));

// Sends a message over the network to one or all clients in the same message group
//
// Triggers a NET_MSG_CB_DATA_MSG event on the receiving client(s)
//
// Args:
//   - h          : Group to send this message to
//
//   - receiverId : The client(s) this message should be sent to
//                  Pass in NET_MSG_CONN_ID_BROADCAST to send it to all clients in the group
//                  Pass in a specific connection ID to send it to a particular client in the group
//
//   - data       : The data to send to the other client(s)
//                  callbackData in the NetMessages_CallbackFunc will point to this
NETMESSAGES_IMPORT(bool NetMessages_sendDataMessage(NetMessagesGroupHandle h, NetMessagesConnectionId receiverId, const void *data));

// Sets subscription status of a message group
//
// If a message group is unsubscribed from, this client can neither send nor receive messages relating to it
//
// Re-subscribing to a group will trigger either a NET_MSG_CB_LEADER_AVAILABLE or a NET_MSG_CB_LEADER_ID_RECEIVED
// event, depending on whether the group has a leader
//
// By default, registering a message groups subscribes the client to it
//
// Returns true if the subscription was sent to the server, false otherwise
NETMESSAGES_IMPORT(bool NetMessages_setSubscribed(NetMessagesGroupHandle h, bool isSubscribed));

// Writes this client's connection ID to out
//
// Returns true if the client ID was written, false otherwise (e.g. if not connected to server)
NETMESSAGES_IMPORT(bool NetMessages_getOwnConnectionId(NetMessagesConnectionId *out));

// Writes the leader's connection ID to out
//
// Returns true if the ID was written to out, false otherwise (e.g. there is no leader or the client is not subscribed to the group)
NETMESSAGES_IMPORT(bool NetMessages_getLeaderConnectionId(NetMessagesGroupHandle h, NetMessagesConnectionId *out));

// Sends a request to the server to become the leader of this message group
//
// This client is not guaranteed to become leader if this request is sent, but
// if leader status is obtained, then the NET_MSG_CB_LEADER_ID_RECEIVED event
// will be fired with this client's connection ID as the new leader
//
// Returns true if the request was sent, false otherwise
NETMESSAGES_IMPORT(bool NetMessages_requestBecomeLeader(NetMessagesGroupHandle h));

// Sends a request to relinquish this client's leadership status for this message group to the server
//
// Does nothing if there is no connection to the server or this client is not the leader
//
// Returns true if the request was sent, false otherwise
NETMESSAGES_IMPORT(bool NetMessages_releaseLeader(NetMessagesGroupHandle h));

// Returns true if the client is currently connected to the server and is allowed to send messages
NETMESSAGES_IMPORT(bool NetMessages_isConnectedToServer(void));

// Returns a new string that contains the nickname of the client associated with the passed in connection ID
//
// Will return NULL if the client cannot be found
//
// Caller is responsible for calling recomp_free on this string
NETMESSAGES_IMPORT(char *NetMessages_getClientNickname(NetMessagesConnectionId connId));

// Message groups can only be safely created during or after this callback
//
// This callback has no arguments
//
// Example:
//   NETMESSAGES_ON_REGISTER_MESSAGE_GROUPS void foo(void) {}
#define NETMESSAGES_ON_REGISTER_MESSAGE_GROUPS RECOMP_CALLBACK(NETMESSAGES_MOD_NAME, onRegisterMessageGroups)

// This callback is run after the server is connected to, and the local client can now send/receive messages
//
// Note that this callback can run multiple times since a client can connect/disconnect multiple times
//
// Args (for type, see example):
//   - id : Contains this client's connection ID
//
// Example:
//   NETMESSAGES_ON_SERVER_CONNECT void foo(NetMessagesConnectionId ownId) {}
#define NETMESSAGES_ON_SERVER_CONNECT RECOMP_CALLBACK(NETMESSAGES_MOD_NAME, onConnectToServer)

// This callback is run when the local client disconnects from the server
//
// Note that this callback can run multiple times since a client can connect/disconnect multiple times
//
// Args (for type, see example):
//   - id : Contains this client's connection ID
//
// Example:
//   NETMESSAGES_ON_SERVER_DISCONNECT void foo(NetMessagesConnectionId ownId) {}
#define NETMESSAGES_ON_SERVER_DISCONNECT RECOMP_CALLBACK(NETMESSAGES_MOD_NAME, onDisconnectFromServer)

// This callback is run when a remote client joins the server
//
// Keep in mind that a client joining does not necessarily mean it shares any message groups with this client
//
// Args (for type, see example):
//   - id : Contains the new client's connection ID
//
// Example:
//   NETMESSAGES_ON_REMOTE_CLIENT_JOINED void foo(NetMessagesConnectionId id) {}
#define NETMESSAGES_ON_REMOTE_CLIENT_JOINED RECOMP_CALLBACK(NETMESSAGES_MOD_NAME, onRemoteClientJoined)

// This callback is run when a remote client leaves the server
//
// A client that this callback is run for is removed from all message groups
//
// Args (for type, see example):
//   - id : The client that left
//
// Example:
//   NETMESSAGES_ON_REMOTE_CLIENT_REMOVED void foo(NetMessagesConnectionId id) {}
#define NETMESSAGES_ON_REMOTE_CLIENT_REMOVED RECOMP_CALLBACK(NETMESSAGES_MOD_NAME, onRemoteClientRemoved)

// This callback is run when a remote client changes which scene it is in
//
// Args (for types, see example):
//   - connId  : The connection ID of the client that moved
//
//   - sceneId : SceneId of the new scene
//
// Example:
//   NETMESSAGES_ON_REMOTE_CLIENT_SCENE_RECEIVE void foo(NetMessagesConnectionId connId, s16 sceneId) {}
#define NETMESSAGES_ON_REMOTE_CLIENT_SCENE_RECEIVE RECOMP_CALLBACK(NETMESSAGES_MOD_NAME, onRemoteClientSceneReceive)

// This callback is run when the local client sends a scene change notification to the server
//
// In other words, this only runs when the scene is changed *while connected to the server*
//
// Args (for types, see example):
//   - connId  : This client's connection ID
//
//   - sceneId : SceneId of the new scene
//
// Example:
//   NETMESSAGES_ON_LOCAL_CLIENT_SCENE_SEND void foo(NetMessagesConnectionId connId, s16 sceneId) {}
#define NETMESSAGES_ON_LOCAL_CLIENT_SCENE_SEND RECOMP_CALLBACK(NETMESSAGES_MOD_NAME, onLocalClientSceneSend)

#endif

#endif
