#ifndef CLIENTSERVERCOMMON_HPP
#define CLIENTSERVERCOMMON_HPP

#include <cstdint>
#include <vector>
#include <span>
#include <type_traits>
#include <string>
#include <unordered_map>
#include <chrono>
#include <string>
#include <limits>
#include <typeinfo>
#include <format>
#include "netmessages_api.h"
#include "recompinterop.h"

namespace NetworkMod {

const std::string CURRENT_VERSION("0.0.0-a4");

using GameId = std::string;
using MessageGroupName = std::string;

const GameId MAJORAS_MASK_GAME_ID("mm");

using Byte = uint8_t;
using ByteVec = std::vector<Byte>;
using ByteView = std::span<const Byte>;
using Z64SceneId = int16_t;

static_assert(std::numeric_limits<Byte>::radix == 2 && std::numeric_limits<Byte>::digits == 8);

using GenericMessageType = uint16_t;

enum MessageType : uint16_t {
    MSG_GN_NONE,
    MSG_SV_REQUEST_VERSION,
    MSG_CL_REPLY_VERSION,
    MSG_CL_REQUEST_LEADER,
    MSG_CL_RELEASE_LEADER,
    MSG_SV_LEADER_AVAILABLE,
    MSG_SV_PLAYER_CONNECTED,
    MSG_SV_PLAYER_DISCONNECTED,
    MSG_SV_CONSOLE_STR,
    MSG_SV_INFORM_CLIENT_ID,
    MSG_SV_LEADER_CHANGED,
    MSG_GN_UNUSED,
    MSG_GN_RECOMP_BLOB,
    MSG_CL_SUBSCRIBE_MSG_GROUP,
    MSG_CL_UNSUBSCRIBE_MSG_GROUP,
    MSG_GN_SCENE_CHANGE,
    MSG_TYPE_MAX,
};

using ConnectionId = uint32_t;

const ConnectionId BROADCAST_TO_ALL_CLIENTS = 0;
const ConnectionId SERVER_ID = 0;

struct Message {
    MessageType type = MSG_GN_NONE;
    ConnectionId connId = 0; // filled in by the server
    ByteVec data;

    ByteVec serialize() const;
    bool deserialize(ByteView serializedStruct);
};

struct HandshakeMessage {
    struct {
        std::string version;
    } base;

    // split the rest of the handshake off
    //
    // the extra portion is (de)serialized in a separate step
    // to ensure forwards/backwards compatibility when verifying
    // client/server version while allowing the extra portion to
    // change in layout
    struct {
        std::string room;
        std::string nickname;
        std::string gameId;
    } extra;

    // this boolean is set by deserialize
    //
    // it indicates whether the extra portion of the message
    // was successfully deserialized
    bool isExtraValid;

    ByteVec serialize(void) const;
    bool deserialize(ByteView serializedStruct);
};

struct SceneIdContainer {
    Z64SceneId id;

    ByteVec serialize(void) const;
    bool deserialize(ByteView serializedStruct);
};

struct MessageGroupContainer {
    std::string messageGroup;

    ByteVec serialize(void) const;
    bool deserialize(ByteView serializedStruct);
};

struct RecompBlobContainer {
    uint32_t flags;
    ByteVec data;
    std::string messageGroup;

    ByteVec serialize(void) const;
    bool deserialize(ByteView serializedStruct);

    bool isReliable() const { return !(flags & NET_MSG_FLG_UNRELIABLE); }
    bool isSceneRestricted() const { return (flags & NET_MSG_FLG_SCENE_RESTRICT); }
};

struct PlayerConnectedContainer {
    std::string nickname;

    ByteVec serialize(void) const;
    bool deserialize(ByteView serializedStruct);
};

// Leave a little bit of overhead for serialization
constexpr size_t MAX_MESSAGE_CB_SIZE = (MAX_RECOMP_NETWORK_DATA_SIZE + MAX_RECOMP_NETWORK_MESSAGE_GROUP_NAME_BUFFER_SIZE + (1024 * 2));

void printAndFlushOutput(const std::string &s);

struct ClientInfoCommon {
    ConnectionId id = 0;
    std::string nickname;
    std::string gameId;

    std::string nameAndId() const { return std::format("{}#{}", nickname, id); }
};

bool shouldSendHeartbeatMsg(const std::chrono::steady_clock::time_point &lastHeartbeatTime);

ByteView getByteViewOfString(const std::string &s);

ByteView getByteViewOfStringAsCString(const std::string &s);

} // namespace NetworkMod

#endif
