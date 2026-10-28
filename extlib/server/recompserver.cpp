#include "recompserver.hpp"
#include <iostream>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <cstring>
#include <cassert>
#include <vector>
#include <span>
#include <string_view>
#include <format>
#include "recompinterop.h"

using namespace NetworkMod;

void NetworkMod::RecompServer::run(uint16_t port) {
    initMembers();

    mIsRunning = true;

    // Select instance to use.  For now we'll always use the default.
    // But we could use SteamGameServerNetworkingSockets() on Steam.
    mInterface = SteamNetworkingSockets();

    // Start listening
    SteamNetworkingIPAddr serverLocalAddr;
    serverLocalAddr.Clear();
    serverLocalAddr.m_port = port;
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void *)steamNetConnectionStatusChangedCallback);
    mListenSocket = mInterface->CreateListenSocketIP(serverLocalAddr, 1, &opt);

    static constexpr char PORT_LISTEN_ERROR_STR[] = "Failed to listen on port {}\n";

    if (mListenSocket == k_HSteamListenSocket_Invalid) {
        fatalError(std::format(PORT_LISTEN_ERROR_STR, port));
    }

    mPollGroup = mInterface->CreatePollGroup();
    if (mPollGroup == k_HSteamNetPollGroup_Invalid) {
        fatalError(std::format(PORT_LISTEN_ERROR_STR, port));
    } else {
        std::cout << "Server listening on port " << port << '\n';
    }

    while (mIsRunning) {
        pollIncomingMessages();
        pollConnectionStateChanges();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Close all the connections
    std::cout << "Closing connections...\n";
    for (auto it : mConnToClientMap) {
        // Send them one more goodbye message.  Note that we also have the
        // connection close reason as a place to send final data.  However,
        // that's usually best left for more diagnostic/debug text not actual
        // protocol strings.
        sendStringToClient(it.first, "[SERVER] NetMessages: Server is shutting down.  Goodbye.");

        // Close the connection.  We use "linger mode" to ask SteamNetworkingSockets
        // to flush this out and close gracefully.
        mInterface->CloseConnection(it.first, 0, "Server Shutdown", true);
    }
    mConnToClientMap.clear();

    mInterface->CloseListenSocket(mListenSocket);
    mListenSocket = k_HSteamListenSocket_Invalid;

    mInterface->DestroyPollGroup(mPollGroup);
    mPollGroup = k_HSteamNetPollGroup_Invalid;
}

void NetworkMod::RecompServer::steamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *connInfo) {
    sCallbackInstance->onSteamNetConnectionStatusChanged(connInfo);
}

void NetworkMod::RecompServer::initMembers() {
    mIsRunning = false;
    mListenSocket = k_HSteamListenSocket_Invalid;
    mPollGroup = k_HSteamNetPollGroup_Invalid;
    mInterface = nullptr;
    mConnToClientMap.clear();
    mLastSentHeartbeatTime = std::chrono::steady_clock::now();
    mNameToRoomMap.clear();
}

void NetworkMod::RecompServer::fatalError(const std::string &s) {
    printAndFlushOutput(s);
    mIsRunning = false;
}

Message NetworkMod::RecompServer::createMessage(MessageType type, ConnectionId connId, ByteView data) {
    return {
        .type = type,
        .connId = connId,
        .data = ByteVec(data.begin(), data.end()),
    };
}

Message NetworkMod::RecompServer::createStringMessage(const std::string &s) {
    return createMessage(MSG_SV_CONSOLE_STR, SERVER_ID, getByteViewOfString(s));
}

void NetworkMod::RecompServer::sendStringToClient(ConnectionId conn, const std::string &s) {
    sendMessageToClient(conn, createStringMessage(s), k_nSteamNetworkingSend_Reliable);
}

void NetworkMod::RecompServer::sendMessageToClient(ConnectionId conn, const Message &msg, int sendFlags) {
    ByteVec serializedMsg = msg.serialize();
    mInterface->SendMessageToConnection(conn, serializedMsg.data(), serializedMsg.size(), sendFlags, nullptr);
}

void NetworkMod::RecompServer::sendMessageToAllVerifiedClients(const Message &msg, int sendFlags, ConnectionId except) {
    for (auto &c : mConnToClientMap) {
        if (c.first != except && c.second.isVerified)
            sendMessageToClient(c.first, msg, sendFlags);
    }
}

void NetworkMod::RecompServer::approveClientVerification(ClientInfo &client, const RoomName &room) {
    client.isVerified = true;

    Message informClientIdMsg = createMessage(MSG_SV_INFORM_CLIENT_ID, client.id, {});
    sendMessageToClient(client.id, informClientIdMsg, k_nSteamNetworkingSend_Reliable);

    addClientToRoomOrCreateRoom(room, client);

    // Notify other players that someone has connected
    PlayerConnectedContainer connectedContainer{.nickname = client.nickname};
    Message joinMsg = createMessage(MSG_SV_PLAYER_CONNECTED, client.id, connectedContainer.serialize());
    sendMessageToRoom(*client.room, joinMsg, k_nSteamNetworkingSend_Reliable, client.id);
    sendStringToClient(client.id, std::format("[SERVER] NetMessages: You joined the room '{}' for game ID '{}' as {}", client.room->name, client.gameId, client.fullName));
    sendStringToRoom(*client.room, std::format("[SERVER] NetMessages: {} joined room '{}' for game ID '{}'", client.fullName, client.room->name, client.gameId), k_nSteamNetworkingSend_Reliable, client.id);

    // Tell verified client how many other players there are
    size_t numPlayers = client.room->clientHandles.size() - 1; // subtract 1 to account for newly verified client
    if (numPlayers == 1) {
        sendStringToClient(client.id, "[SERVER] NetMessages: There is 1 other client in the room");
    } else {
        sendStringToClient(client.id, std::format("[SERVER] NetMessages: There are {} other clients in the room", numPlayers));
    }

    // Notify client that other players exist
    for (auto connId : client.room->clientHandles) {
        if (connId != client.id) {
            auto clientIt = mConnToClientMap.find(connId);

            // if the client handles desync from the map, something has gone seriously wrong
            assert(clientIt != mConnToClientMap.end());

            PlayerConnectedContainer otherPlayerContainer{.nickname = clientIt->second.nickname};
            sendMessageToClient(client.id, createMessage(MSG_SV_PLAYER_CONNECTED, connId, otherPlayerContainer.serialize()), k_nSteamNetworkingSend_Reliable);
        }
    }
}

void NetworkMod::RecompServer::handleUnverifiedClientMessage(ClientInfo &client, const Message &msg) {
    if (msg.type == MSG_CL_REPLY_VERSION) {
        HandshakeMessage handshake;
        if (handshake.deserialize(msg.data)) {

            bool isVersionMatched = CURRENT_VERSION == handshake.base.version;

            if (isVersionMatched) {
                if (handshake.isExtraValid) {
                    constexpr int MAX_GAME_ID_LEN = 8;
                    if (handshake.extra.gameId.size() > 0 || handshake.extra.gameId.size() <= MAX_GAME_ID_LEN) {
                        client.nickname = handshake.extra.nickname;

                        if (client.nickname.size() == 0) {
                            client.nickname = "LINK";
                        }

                        client.fullName = client.nameAndId();

                        client.gameId = handshake.extra.gameId;

                        approveClientVerification(client, handshake.extra.room);
                    } else {
                        std::cout << "Invalid game ID length for client " << client.id << " (Must be 1-" << MAX_GAME_ID_LEN << " characters). Removing...";

                        sendStringToClient(client.id, "[SERVER] Failed to parse extra data in handshake message!");

                        removeClient(client.id, 0, "Unparsable extra handshake data", true);
                    }
                } else {
                    std::cout << "Failed to parse extra handshake data for client " << client.id << ". Removing...";

                    sendStringToClient(client.id, "[SERVER] Failed to parse extra data in handshake message!");

                    removeClient(client.id, 0, "Unparsable extra handshake data", true);
                }
            } else {
                std::cout << "Client " << client.id << " had a mismatched version (Client: " ;
                std::cout << handshake.base.version << ", Server: " << CURRENT_VERSION << "). Removing...\n";

                std::string errorMsg = std::format(
                    "[SERVER] Mismatched version!"
                    "Client version: {}, "
                    "Server version: {}."
                    "Disconnecting...",
                    handshake.base.version,
                    CURRENT_VERSION);

                sendStringToClient(client.id, errorMsg);

                removeClient(client.id, 0, "Protocol outdated", true);
            }
        } else {
            std::cout << "Failed to deserialize message from unverified client " << client.id << ". Removing client.\n";

            sendStringToClient(client.id, "[SERVER] Handshake could not be parsed. Rejected!");

            removeClient(client.id, 0, "Could not parse handshake", true);
        }
    }
}

// Based on Play_GetOriginalSceneId in MM decomp
Z64SceneId getOriginalSceneId(Z64SceneId scene) {
    constexpr Z64SceneId SCENE_STONE_TOWER_TEMPLE_INVERTED = 24;
    constexpr Z64SceneId SCENE_STONE_TOWER_TEMPLE_NORMAL = 22;
    constexpr Z64SceneId SCENE_SWAMP_CURED = 0;
    constexpr Z64SceneId SCENE_SWAMP_POISON = 69;
    constexpr Z64SceneId SCENE_MOUNTAIN_VILLAGE_SPRING = 90;
    constexpr Z64SceneId SCENE_MOUNTAIN_VILLAGE_WINTER = 80;
    constexpr Z64SceneId SCENE_GORON_VILLAGE_SPRING = 72;
    constexpr Z64SceneId SCENE_GORON_VILLAGE_WINTER = 77;
    constexpr Z64SceneId SCENE_PATH_TO_GORON_VILLAGE_SPRING = 94;
    constexpr Z64SceneId SCENE_PATH_TO_GORON_VILLAGE_WINTER = 93;
    constexpr Z64SceneId SCENE_STONE_TOWER_INVERTED = 89;
    constexpr Z64SceneId SCENE_STONE_TOWER_NORMAL = 88;

    switch (scene) {
        case SCENE_STONE_TOWER_TEMPLE_INVERTED:
            scene = SCENE_STONE_TOWER_TEMPLE_NORMAL;
            break;

        case SCENE_SWAMP_CURED:
            scene = SCENE_SWAMP_POISON;
            break;

        case SCENE_MOUNTAIN_VILLAGE_SPRING:
            scene = SCENE_MOUNTAIN_VILLAGE_WINTER;
            break;

        case SCENE_GORON_VILLAGE_SPRING:
            scene = SCENE_GORON_VILLAGE_WINTER;
            break;

        case SCENE_PATH_TO_GORON_VILLAGE_SPRING:
            scene = SCENE_PATH_TO_GORON_VILLAGE_WINTER;
            break;

        case SCENE_STONE_TOWER_INVERTED:
            scene = SCENE_STONE_TOWER_NORMAL;
            break;

        default:
            break;
    }

    return scene;
}

static void printUnkMessageGroup(const std::string &clientName, const MessageGroupName &group) {
    std::cout << "WARN: " << clientName << " tried to send to unknown message group '" << group << "'\n";
}

static void printClientNotInGroup(const std::string &clientName, const MessageGroupName &group) {
    std::cout << "WARN: " << clientName << " tried to send to group '" << group << "' that it is not in\n";
}

void NetworkMod::RecompServer::handleVerifiedClientMessage(ClientInfo &client, const Message &incomingMsg, bool isReliable) {
    assert(client.room != nullptr);

    switch (incomingMsg.type) {
        case MSG_GN_RECOMP_BLOB: {
            RecompBlobContainer blob;

            if (blob.deserialize(incomingMsg.data)) {
                auto groupOpt = extractMessageGroup(*client.room, blob.messageGroup);

                if (groupOpt) {
                    auto &group = *groupOpt.value();

                    if (group.clientHandles.contains(client.id)) {
                        Message outgoingMsg = createMessage(MSG_GN_RECOMP_BLOB, client.id, incomingMsg.data);

                        auto flags = isReliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;

                        if (incomingMsg.connId == BROADCAST_TO_ALL_CLIENTS) {
                            if (blob.isSceneRestricted()) {
                                sendMessageToMessageGroupInScene(outgoingMsg, flags, client, group, client.id);
                            } else {
                                sendMessageToMessageGroupByGame(outgoingMsg, flags, group, client.gameId, client.id);
                            }
                        } else if (group.clientHandles.contains(incomingMsg.connId)) {
                            auto it = mConnToClientMap.find(incomingMsg.connId);

                            // if the client's handle is in the group's clientHandles
                            // it should be in the client map
                            assert(it != mConnToClientMap.end());

                            if (it != mConnToClientMap.end()) {
                                bool sendMessage = false;

                                if (blob.isSceneRestricted()) {
                                    if (isClientsInSameScene(client, it->second) && client.gameId == it->second.gameId) {
                                        sendMessage = true;
                                    }
                                } else {
                                    sendMessage = true;
                                }

                                if (sendMessage) {
                                    sendMessageToClient(it->first, outgoingMsg, flags);
                                }
                            }
                        }
                    } else {
                        printClientNotInGroup(client.fullName, blob.messageGroup);
                    }
                } else {
                    printUnkMessageGroup(client.fullName, blob.messageGroup);
                }
            }
        } break;

        case MSG_CL_REQUEST_LEADER: {
            MessageGroupContainer leaderReq;

            if (leaderReq.deserialize(incomingMsg.data)) {
                auto groupOpt = extractMessageGroup(*client.room, leaderReq.messageGroup);

                if (groupOpt) {
                    auto &group = *groupOpt.value();

                    if (group.clientHandles.contains(client.id)) {
                        auto gameInfo = group.gameSpecificInfo[client.gameId];
                        if (gameInfo.leaderId == EMPTY_LEADER_ID) {
                            gameInfo.leaderId = client.id;
                            sendMessageToMessageGroupByGame(createMessage(MSG_SV_LEADER_CHANGED, gameInfo.leaderId, incomingMsg.data), k_nSteamNetworkingSend_Reliable, group, client.gameId);
                            std::cout << "Setting leader of message group " << group.name << " to " << gameInfo.leaderId << "\n";
                        }
                    } else {
                        printClientNotInGroup(client.fullName, leaderReq.messageGroup);
                    }
                } else {
                    printUnkMessageGroup(client.fullName, leaderReq.messageGroup);
                }
            }
        } break;

        case MSG_CL_RELEASE_LEADER: {
            MessageGroupContainer leaderRel;

            if (leaderRel.deserialize(incomingMsg.data)) {
                auto groupOpt = extractMessageGroup(*client.room, leaderRel.messageGroup);

                if (groupOpt) {
                    auto &group = *groupOpt.value();

                    if (group.clientHandles.contains(client.id)) {
                        if (group.gameSpecificInfo[client.gameId].leaderId == client.id) {
                            removeMessageGroupLeader(group, client.gameId);
                        }
                    } else {
                        printClientNotInGroup(client.fullName, leaderRel.messageGroup);
                    }
                } else {
                    printUnkMessageGroup(client.fullName, leaderRel.messageGroup);
                }
            }
        } break;

        case MSG_CL_SUBSCRIBE_MSG_GROUP: {
            // std::cout << "MSG_CL_SUBSCRIBE_MSG_GROUP: Client: " << client.id << ", group: " << incomingMsg.messageGroup << "\n";
            MessageGroupContainer subscribeContainer;

            if (subscribeContainer.deserialize(incomingMsg.data)) {
                auto groupOpt = extractOrCreateMessageGroup(*client.room, subscribeContainer.messageGroup);

                if (groupOpt) {
                    auto &group = *groupOpt.value();

                    addClientToMessageGroup(client, subscribeContainer.messageGroup);

                    auto leaderId = group.gameSpecificInfo[client.gameId].leaderId;

                    auto msgType = leaderId == EMPTY_LEADER_ID ? MSG_SV_LEADER_AVAILABLE : MSG_SV_LEADER_CHANGED;

                    sendMessageToClient(client.id, createMessage(msgType, leaderId, incomingMsg.data), k_nSteamNetworkingSend_Reliable);
                }
            }
        } break;

        case MSG_CL_UNSUBSCRIBE_MSG_GROUP: {
            MessageGroupContainer unsubscribeContainer;

            // std::cout << "MSG_CL_UNSUBSCRIBE_MSG_GROUP: Client: " << client.id << ", group: " << incomingMsg.messageGroup << "\n";
            if (unsubscribeContainer.deserialize(incomingMsg.data)) {
                removeClientFromMessageGroup(client, unsubscribeContainer.messageGroup);
            }
        } break;

        case MSG_GN_SCENE_CHANGE: {
            SceneIdContainer sceneUpdate;
            if (sceneUpdate.deserialize(incomingMsg.data)) {
                if (client.scene.id != sceneUpdate.id) {
                    client.scene.id = sceneUpdate.id;

                    std::cout << client.fullName << " moved to scene " << client.scene.id << "\n";

                    sendMessageToRoom(*client.room, createMessage(MSG_GN_SCENE_CHANGE, client.id, client.scene.serialize()), k_nSteamNetworkingSend_Reliable, client.id);
                }
            }
        } break;

        default: {
            std::cerr << "Received message with unhandled type " << incomingMsg.type << " from player " << client.id << '\n';
        } break;
    }
}

void NetworkMod::RecompServer::pollIncomingMessages() {
    Message clMsg;
    clMsg.data.reserve(MAX_MESSAGE_CB_SIZE);

    while (mIsRunning) {
        ISteamNetworkingMessage *incomingMsg = nullptr;

        int numMsgs = mInterface->ReceiveMessagesOnPollGroup(mPollGroup, &incomingMsg, 1);

        if (numMsgs == 0) {
            break;
        }

        if (numMsgs < 0) {
            fatalError("Error checking for messages\n");
            break;
        }

        assert(numMsgs == 1 && incomingMsg);

        auto clientIter = mConnToClientMap.find(incomingMsg->m_conn);

        assert(clientIter != mConnToClientMap.end());

        if (incomingMsg->m_cbSize <= MAX_MESSAGE_CB_SIZE) {
            ClientInfo &client = clientIter->second;

            if (clMsg.deserialize(std::span(static_cast<Byte *>(incomingMsg->m_pData), incomingMsg->m_cbSize))) {

                if (!client.isVerified) {
                    handleUnverifiedClientMessage(client, clMsg);
                } else {
                    handleVerifiedClientMessage(client, clMsg, incomingMsg->m_nFlags & k_nSteamNetworkingSend_Reliable);
                }
            } else {
                std::cout << "Message from client " << client.id << " failed to deserialize!\n";

                if (!client.isVerified) {
                    std::cout << "Disconnecting client " << client.id << " (Bad message protocol)\n";
                    removeClient(client.id, 0, "Message failed to deserialize", true);
                }
            }
        }

        incomingMsg->Release();
    }
}

void NetworkMod::RecompServer::addClient(const SteamNetConnectionStatusChangedCallback_t &connInfo) {
    ClientInfo &clientInfo = mConnToClientMap[connInfo.m_hConn];
    clientInfo.id = connInfo.m_hConn;
}

void NetworkMod::RecompServer::removeClient(ConnectionId id, int reason, const char *debugMsg, bool enableLinger) {
    auto clientIt = mConnToClientMap.find(id);

    if (clientIt != mConnToClientMap.end()) {
        while (clientIt->second.messageGroups.size() > 0) {
            removeClientFromMessageGroup(clientIt->second, **(clientIt->second.messageGroups.begin()));
        }

        if (clientIt->second.isVerified) {
            assert(clientIt->second.room != nullptr);
            sendMessageToRoom(*clientIt->second.room, createMessage(MSG_SV_PLAYER_DISCONNECTED, clientIt->second.id, {}), k_nSteamNetworkingSend_Reliable, clientIt->first);
            removeClientFromRoom(*clientIt->second.room, clientIt->second);
        }

        mInterface->CloseConnection(clientIt->first, reason, debugMsg, enableLinger);

        mConnToClientMap.erase(id);
    }
}

bool NetworkMod::RecompServer::isClientsInSameScene(const ClientInfo &a, const ClientInfo &b) const {
    if (a.gameId == MAJORAS_MASK_GAME_ID) {
        return getOriginalSceneId(a.scene.id) == getOriginalSceneId(b.scene.id);
    } else {
        return a.scene.id == b.scene.id;
    }

    return false;
}

void NetworkMod::RecompServer::addClientToRoom(Room &room, ClientInfo &client) {
    std::cout << "Adding " << client.fullName << " to room '" << room.name << "'\n";
    room.clientHandles.insert(client.id);
    client.room = &room;
}

void NetworkMod::RecompServer::addClientToRoomOrCreateRoom(const RoomName &roomName, ClientInfo &client) {
    auto roomIt = mNameToRoomMap.find(roomName);

    if (roomIt != mNameToRoomMap.end()) {
        addClientToRoom(roomIt->second, client);
    } else {
        std::cout << "Creating room '" << roomName << "'\n";

        auto &room = mNameToRoomMap[roomName];
        room.name = roomName;
        addClientToRoom(room, client);
    }
}

void NetworkMod::RecompServer::removeClientFromRoom(Room &room, ClientInfo &client) {
    if (room.clientHandles.contains(client.id)) {
        room.clientHandles.erase(client.id);
        client.room = nullptr;

        if (room.isEmpty()) {
            std::cout << "Room '" << room.name << "' is empty. Destroying...\n";

            mNameToRoomMap.erase(room.name);
        }
    }
}

void NetworkMod::RecompServer::sendMessageToRoom(const Room &room, const Message &msg, int sendFlags, ConnectionId except) {
    for (auto connId : room.clientHandles) {
        assert(mConnToClientMap.contains(connId));

        if (connId != except) {
            sendMessageToClient(connId, msg, sendFlags);
        }
    }
}

void NetworkMod::RecompServer::sendStringToRoom(const Room &room, const std::string &s, int sendFlags, ConnectionId except) {
    sendMessageToRoom(room, createStringMessage(s), sendFlags, except);
}

const size_t MAX_MESSAGE_GROUP_NAME_SIZE = MAX_RECOMP_NETWORK_MESSAGE_GROUP_NAME_BUFFER_SIZE - 1; // account for NULL terminator

std::optional<NetworkMod::RecompServer::MessageGroup *> NetworkMod::RecompServer::extractMessageGroup(Room &room, const MessageGroupName &groupName) {
    auto groupsIt = room.messageGroups.find(groupName);

    if (groupsIt != room.messageGroups.end()) {
        return &groupsIt->second;
    }
    return std::nullopt;
}

std::optional<NetworkMod::RecompServer::MessageGroup *> NetworkMod::RecompServer::extractOrCreateMessageGroup(Room &room, const MessageGroupName &groupName) {
    auto groupsIt = room.messageGroups.find(groupName);

    if (groupsIt != room.messageGroups.end()) {
        return &groupsIt->second;
    } else {
        auto &group = room.messageGroups[groupName];
        group.name = groupName;
        return &group;
    }

    return std::nullopt;
}

void NetworkMod::RecompServer::addClientToMessageGroup(ClientInfo &client, const MessageGroupName &groupName) {
    assert(client.room != nullptr);

    auto msgGroupIt = client.room->messageGroups.find(groupName);
    if (msgGroupIt != client.room->messageGroups.end()) {
        auto &group = msgGroupIt->second;
        group.clientHandles.insert(client.id);
        group.gameSpecificInfo[client.gameId].clientHandles.insert(client.id);
        client.messageGroups.insert(&group);
        std::cout << "Adding " << client.fullName << " to group " << groupName << '\n';
    }
}

void NetworkMod::RecompServer::removeClientFromMessageGroup(ClientInfo &client, const MessageGroupName &groupName) {
    assert(client.room != nullptr);

    auto msgGroupIt = client.room->messageGroups.find(groupName);
    if (msgGroupIt != client.room->messageGroups.end()) {
        removeClientFromMessageGroup(client, msgGroupIt->second);
    }
}

void NetworkMod::RecompServer::removeClientFromMessageGroup(ClientInfo &client, MessageGroup &group) {
    assert(client.room != nullptr);

    std::cout << "Removing " << client.fullName << " from group " << group.name << '\n';

    client.messageGroups.erase(&group);
    group.clientHandles.erase(client.id);

    auto &gameInfo = group.gameSpecificInfo[client.gameId];
    gameInfo.clientHandles.erase(client.id);

    if (group.isEmpty()) {
        client.room->messageGroups.erase(group.name);
    } else if (gameInfo.leaderId == client.id) {
        removeMessageGroupLeader(group, client.gameId);
    }
}

void NetworkMod::RecompServer::removeMessageGroupLeader(MessageGroup &group, const GameId &gameId) {
    auto &gameInfo = group.gameSpecificInfo[gameId];

    if (gameInfo.leaderId != EMPTY_LEADER_ID) {
        gameInfo.leaderId = EMPTY_LEADER_ID;

        MessageGroupContainer leaderAvailContainer{.messageGroup = group.name};

        sendMessageToMessageGroupByGame(createMessage(MSG_SV_LEADER_AVAILABLE, SERVER_ID, leaderAvailContainer.serialize()), k_nSteamNetworkingSend_Reliable, group, gameId);
        std::cout << "Removing leader of message group " << group.name << '\n';
    }
}

void NetworkMod::RecompServer::sendMessageToAllClientsInMessageGroup(const Message &msg, int sendFlags, const MessageGroup &group, ConnectionId except) {
    for (auto &connId : group.clientHandles) {
        if (connId != except) {
            sendMessageToClient(connId, msg, sendFlags);
        }
    }
}

void NetworkMod::RecompServer::sendMessageToMessageGroupByGame(const Message &msg, int sendFlags, const MessageGroup &group, const GameId &gameId, ConnectionId except) {
    auto gameInfo = group.gameSpecificInfo.find(gameId);
    for (auto &connId : gameInfo->second.clientHandles) {
        if (connId != except) {
            sendMessageToClient(connId, msg, sendFlags);
        }
    }
}

void NetworkMod::RecompServer::sendMessageToMessageGroupInScene(const Message &msg, int sendFlags, const ClientInfo &senderClient, const MessageGroup &group, ConnectionId except) {
    for (auto &connId : group.clientHandles) {
        if (connId != except) {
            auto it = mConnToClientMap.find(connId);

            assert(it != mConnToClientMap.end());

            if (isClientsInSameScene(senderClient, it->second)) {
                sendMessageToClient(connId, msg, sendFlags);
            }
        }
    }
}

void NetworkMod::RecompServer::onSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *connInfo) {
    // What's the state of the connection?
    switch (connInfo->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_None:
            // NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            // Locate the client.  Note that it should have been found, because this
            // is the only codepath where we remove clients (except on shutdown),
            // and connection change callbacks are dispatched in queue order.
            auto itClient = mConnToClientMap.find(connInfo->m_hConn);

            if (itClient != mConnToClientMap.end()) {
                // Select appropriate log messages
                const char *debugLogAction;
                std::string disconnectMsg;
                if (connInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
                    debugLogAction = "problem detected locally";
                    disconnectMsg = std::format("[SERVER] NetMessages: Lost connection to client {} ({})\n", itClient->second.fullName, connInfo->m_info.m_szEndDebug);
                } else {
                    // Note that here we could check the reason code to see if
                    // it was a "usual" connection or an "unusual" one.
                    debugLogAction = "closed by peer";
                    disconnectMsg = std::format("[SERVER] NetMessages: Client {} left the room.\n", itClient->second.fullName);
                }

                // Spew something to our own log.  Note that because we put their nick
                // as the connection description, it will show up, along with their
                // transport-specific data (e.g. their IP address)
                std::cout << "Connection " << connInfo->m_info.m_szConnectionDescription << ' ' << debugLogAction << ", reason " << connInfo->m_info.m_eEndReason << ": " << connInfo->m_info.m_szEndDebug << '\n';

                if (itClient->second.isVerified) {
                    // Send a message so everybody else knows what happened
                    sendStringToRoom(*itClient->second.room, disconnectMsg, k_nSteamNetworkingSend_Reliable, itClient->second.id);
                }

                // This function should call ISteamNetworkingSockets::CloseConnection internally
                removeClient(itClient->second.id, k_ESteamNetConnectionEnd_Misc_InternalError, debugLogAction, false);
            } else {
                // Client wasn't in map, but we still need to free its resources
                mInterface->CloseConnection(connInfo->m_hConn, 0, nullptr, false);
            }
            break;
        }

        case k_ESteamNetworkingConnectionState_Connecting: {
            // This must be a new connection
            assert(mConnToClientMap.find(connInfo->m_hConn) == mConnToClientMap.end());

            std::cout << "Connection request from " << connInfo->m_info.m_szConnectionDescription << '\n';

            // A client is attempting to connect
            // Try to accept the connection.
            if (mInterface->AcceptConnection(connInfo->m_hConn) != k_EResultOK) {
                // This could fail.  If the remote host tried to connect, but then
                // disconnected, the connection may already be half closed.  Just
                // destroy whatever we have on our side.
                mInterface->CloseConnection(connInfo->m_hConn, 0, nullptr, false);
                std::cout << "Can't accept connection.  (It was already closed?)" << '\n';
                break;
            }

            // Assign the poll group
            if (!mInterface->SetConnectionPollGroup(connInfo->m_hConn, mPollGroup)) {
                mInterface->CloseConnection(connInfo->m_hConn, 0, nullptr, false);
                std::cout << "Failed to set poll group?" << '\n';
                break;
            }

            // Add them to the client list
            addClient(*connInfo);

            Message verifReq = createMessage(MSG_SV_REQUEST_VERSION, SERVER_ID, {});
            sendMessageToClient(connInfo->m_hConn, verifReq, k_nSteamNetworkingSend_Reliable);
            break;
        }

        case k_ESteamNetworkingConnectionState_Connected:
            // We will get a callback immediately after accepting the connection.
            // Since we are the server, we can ignore this, it's not news to us.
            break;

        default:
            // Silences -Wswitch
            break;
    }
}

void NetworkMod::RecompServer::pollConnectionStateChanges() {
    sCallbackInstance = this;
    mInterface->RunCallbacks();
}

// Ensure the linker can find the RecompServer::sCallbackInstance symbol
RecompServer *RecompServer::sCallbackInstance;
