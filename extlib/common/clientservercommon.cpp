#include <iostream>
#include <bit>
#include <cassert>
#include <climits>
#include <array>
#include <cstdarg>
#include <alpaca/alpaca.h>
#include "clientservercommon.hpp"
#include "recompinterop.h"

static_assert(CHAR_BIT == 8);

// TODO: replace alpaca due to edge case bugs in serialization
//
// Variable length encoding breaks when integers have high values
constexpr auto ALPACA_OPTS = alpaca::options::fixed_length_encoding;

using namespace NetworkMod;

void NetworkMod::printAndFlushOutput(const std::string &s) {
    // I'm not sure flushing is actually doing anything here, so comment it out
    std::cerr << "NetMessages: " << s;
    // std::flush(std::cout);
    // std::flush(std::cerr);
}

bool NetworkMod::shouldSendHeartbeatMsg(const std::chrono::steady_clock::time_point &lastHeartbeatTime) {
    const int SECONDS_BETWEEN_HEARTBEATS = 10;

    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastHeartbeatTime).count() >= SECONDS_BETWEEN_HEARTBEATS;
}

ByteView NetworkMod::getByteViewOfString(const std::string &s) {
    const Byte *start = reinterpret_cast<const Byte *>(s.data());
    return ByteView(start, start + s.size());
};

ByteView NetworkMod::getByteViewOfStringAsCString(const std::string &s) {
    const Byte *start = reinterpret_cast<const Byte *>(s.c_str());
    return ByteView(start, start + s.size() + 1);
}

template <class T>
static ByteVec serializeStruct(const T &serializableStruct) {
    ByteVec v;
    alpaca::serialize<ALPACA_OPTS>(serializableStruct, v);
    return v;
}

template <class T>
static bool deserializeStruct(T &target, ByteView serializedStruct) {
    std::error_code error;

    target = alpaca::deserialize<ALPACA_OPTS, T>(serializedStruct, error);

    if (error) {
        std::cerr << "Failed to deserialize " << typeid(T).name() << " struct (" << error.message() << ")\n";
        return false;
    }

    return true;
}

ByteVec NetworkMod::SceneIdContainer::serialize(void) const {
    return serializeStruct(*this);
}

bool NetworkMod::SceneIdContainer::deserialize(ByteView serializedStruct) {
    return deserializeStruct(*this, serializedStruct);
}

ByteVec NetworkMod::Message::serialize() const {
    return serializeStruct(*this);
}

bool NetworkMod::Message::deserialize(ByteView serializedStruct) {
    return deserializeStruct(*this, serializedStruct);
}

ByteVec NetworkMod::RecompBlobContainer::serialize(void) const {
    return serializeStruct(*this);
}

bool NetworkMod::RecompBlobContainer::deserialize(ByteView serializedStruct) {
    return deserializeStruct(*this, serializedStruct);
}

ByteVec NetworkMod::MessageGroupContainer::serialize(void) const {
    return serializeStruct(*this);
}

bool NetworkMod::MessageGroupContainer::deserialize(ByteView serializedStruct) {
    return deserializeStruct(*this, serializedStruct);
}

// We use this intermediary struct to preserve compatibility for gracefully rejecting outdated versions
struct HandshakeMessageSerialized {
    std::string version;
    ByteVec extra;
};

ByteVec NetworkMod::HandshakeMessage::serialize(void) const {
    return serializeStruct(HandshakeMessageSerialized{.version = base.version, .extra = serializeStruct(extra)});
}

bool NetworkMod::HandshakeMessage::deserialize(ByteView serializedStruct) {
    HandshakeMessageSerialized interim;
    bool isVersionParsed = deserializeStruct(interim, serializedStruct);

    if (isVersionParsed) {
        base.version = interim.version;

        isExtraValid = deserializeStruct(extra, interim.extra) &&
                       extra.room.size() < MAX_RECOMP_NETWORK_MESSAGE_GROUP_NAME_BUFFER_SIZE &&
                       extra.nickname.size() < MAX_RECOMP_NETWORK_NICKNAME_BUFFER_SIZE;
    }

    return isVersionParsed;
}

ByteVec NetworkMod::PlayerConnectedContainer::serialize(void) const {
    return serializeStruct(*this);
}

bool NetworkMod::PlayerConnectedContainer::deserialize(ByteView serializedStruct) {
    return deserializeStruct(*this, serializedStruct);
}
