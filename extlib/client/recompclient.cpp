#include "recompclient.hpp"
#include <iostream>
#include <cassert>
#include <algorithm>
#include <array>

using namespace NetworkMod;

static bool sIsFatallyErrored;

void NetworkMod::RecompClient::run(const SteamNetworkingIPAddr &serverAddr, const std::string &roomName, const std::string &nickname, const std::string &gameId) {
    if (mIsRunning) {
        // Don't init while the client is already connected
        return;
    }

    if (sIsFatallyErrored) {
        std::cout << "[ERROR] NetMessages: Can't connect to server after a fatal error! Do a full restart of your recomp client!\n";
        return;
    }

    initMembers();

    mRoomName = roomName;

    mSelfNickname = nickname;

    mGameId = gameId;

    mIsRunning = true;

    // Select instance to use.  For now we'll always use the default.
    mInterface = SteamNetworkingSockets();

    // Start connecting
    char addr[SteamNetworkingIPAddr::k_cchMaxString];

    // TODO: figure out why SteamNetworkingIPAddr::ToString causes a linker error in release mode on MacOS
    // serverAddr.ToString(addr, sizeof(addr), true);
    // std::cout << "Connecting to server at " << addr << '\n';

    std::cout << "[INFO] NetMessages: Attempting to connect to room '" << roomName << "'\n";

    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void *)steamNetConnectionStatusChangedCallback);
    mServerConnHandle = mInterface->ConnectByIPAddress(serverAddr, 1, &opt);
    if (mServerConnHandle == k_HSteamNetConnection_Invalid) {
        fatalError("[ERROR] NetMessages: Failed to create connection");
    }
}

void NetworkMod::RecompClient::doPolling() {
    if (mIsRunning) {
        pollIncomingMessages();
        pollConnectionStateChanges();
    }
}

void NetworkMod::RecompClient::sendMessage(ConnectionId target, NetMessagesFlag flags, ByteView data, const std::string &messageGroup) {
    if (mIsRunning) {
        int gnsFlags = (flags & NET_MSG_FLG_UNRELIABLE) ? k_nSteamNetworkingSend_Unreliable : k_nSteamNetworkingSend_Reliable;

        RecompBlobContainer blob{.flags = static_cast<uint32_t>(flags), .data{data.begin(), data.end()}, .messageGroup = messageGroup};

        sendMessageToServer(createMessage(target, MSG_GN_RECOMP_BLOB, blob.serialize()), gnsFlags);
    }
}

void NetworkMod::RecompClient::stop() {
    if (mIsRunning) {
        disconnectFromServer(0, nullptr, true);
    }
}

void NetworkMod::RecompClient::registerCallback(Callback callback) {
    mCallbacks.push_back(callback);
}

void NetworkMod::RecompClient::requestLeader(const std::string &messageGroup) {
    if (mIsRunning) {
        MessageGroupContainer leaderReq{.messageGroup = messageGroup};
        sendMessageToServer(createMessage(SERVER_ID, MSG_CL_REQUEST_LEADER, leaderReq.serialize()), k_nSteamNetworkingSend_Reliable);
    }
}

void NetworkMod::RecompClient::releaseLeader(const std::string &messageGroup) {
    if (mIsRunning) {
        MessageGroupContainer leaderRel{.messageGroup = messageGroup};
        sendMessageToServer(createMessage(SERVER_ID, MSG_CL_RELEASE_LEADER, leaderRel.serialize()), k_nSteamNetworkingSend_Reliable);
    }
}

void NetworkMod::RecompClient::subscribeToMessageGroup(const std::string &messageGroup) {
    if (mIsRunning) {
        MessageGroupContainer subscribeReq{.messageGroup = messageGroup};
        sendMessageToServer(createMessage(SERVER_ID, MSG_CL_SUBSCRIBE_MSG_GROUP, subscribeReq.serialize()), k_nSteamNetworkingSend_Reliable);
    }
}

void NetworkMod::RecompClient::unsubscribeFromMessageGroup(const std::string &messageGroup) {
    if (mIsRunning) {
        MessageGroupContainer unsubscribeReq{.messageGroup = messageGroup};
        sendMessageToServer(createMessage(SERVER_ID, MSG_CL_UNSUBSCRIBE_MSG_GROUP, unsubscribeReq.serialize()), k_nSteamNetworkingSend_Reliable);
    }
}

void NetworkMod::RecompClient::sendSceneUpdate(Z64SceneId id) {
    if (mIsRunning) {
        sendMessageToServer(createMessage(SERVER_ID, MSG_GN_SCENE_CHANGE, (SceneIdContainer{.id = id}).serialize()), k_nSteamNetworkingSend_Reliable);
    }
}

std::optional<const std::string *> NetworkMod::RecompClient::getClientNickname(ConnectionId id) {
    if (mIsRunning) {
        if (id == mSelfId) {
            return &mSelfNickname;
        } else {
            auto clientIt = mConnIdToClient.find(id);

            if (clientIt != mConnIdToClient.end()) {
                return &clientIt->second.nickname;
            }
        }
    }

    return std::nullopt;
}

void NetworkMod::RecompClient::steamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *info) {
    sCallbackInstance->onSteamNetConnectionStatusChanged(info);
}

void NetworkMod::RecompClient::initMembers() {
    mIsRunning = false;
    mServerConnHandle = k_HSteamNetConnection_Invalid;
    mInterface = nullptr;
    mConnIdToClient.clear();
    mLastSentHeartbeatTime = std::chrono::steady_clock::now();
    mCallbacks.clear();
    mRoomName = "";
    mSelfId = k_HSteamNetConnection_Invalid;
    mSelfNickname = "";
    mGameId = "";
}

void NetworkMod::RecompClient::fatalError(const std::string &s) {
    printAndFlushOutput(s);
    mIsRunning = false;
    sIsFatallyErrored = true;
}

Message NetworkMod::RecompClient::createMessage(ConnectionId target, MessageType msgType, ByteView data) {
    return {
        .type = msgType,
        .connId = target,
        .data = ByteVec(data.begin(), data.end()),
    };
}

void NetworkMod::RecompClient::addRemotePlayer(ConnectionId id, const std::string &nickname) {
    auto &client = mConnIdToClient[id];
    client.id = id;
    client.nickname = nickname;
}

void NetworkMod::RecompClient::removeRemotePlayer(ConnectionId id) {
    mConnIdToClient.erase(id);
}

void NetworkMod::RecompClient::sendMessageToServer(const Message &msg, int sendFlags) {
    ByteVec serializedMsg = msg.serialize();
    mInterface->SendMessageToConnection(mServerConnHandle, serializedMsg.data(), serializedMsg.size(), sendFlags, nullptr);
}

void NetworkMod::RecompClient::pollIncomingMessages() {
    Message svMsg;
    svMsg.data.reserve(MAX_MESSAGE_CB_SIZE);

    while (mIsRunning) {
        ISteamNetworkingMessage *incomingMsg = nullptr;

        int numMsgs = mInterface->ReceiveMessagesOnConnection(mServerConnHandle, &incomingMsg, 1);

        if (numMsgs == 0) {
            break;
        }

        if (numMsgs < 0) {
            fatalError("Error checking for messages");
            break;
        }

        assert(numMsgs == 1 && incomingMsg);

        if (incomingMsg->m_cbSize < MAX_MESSAGE_CB_SIZE) {
            if (svMsg.deserialize(std::span(static_cast<Byte *>(incomingMsg->m_pData), incomingMsg->m_cbSize))) {
                switch (svMsg.type) {
                    case MSG_SV_REQUEST_VERSION: {
                        HandshakeMessage handshake{
                            .base{
                                .version = CURRENT_VERSION},
                            .extra{
                                .room = mRoomName,
                                .nickname = mSelfNickname,
                                .gameId = mGameId,
                            },
                        };

                        Message m = createMessage(SERVER_ID, MSG_CL_REPLY_VERSION, handshake.serialize());
                        sendMessageToServer(m, k_nSteamNetworkingSend_Reliable);
                    } break;

                    case MSG_SV_CONSOLE_STR: {
                        std::string str(svMsg.data.begin(), svMsg.data.end());
                        std::cout << str << '\n';
                    } break;

                    case MSG_SV_PLAYER_CONNECTED: {
                        PlayerConnectedContainer connectedContainer;
                        if (connectedContainer.deserialize(svMsg.data)) {
                            addRemotePlayer(svMsg.connId, connectedContainer.nickname);
                            runCallbacks(RE_CL_EVT_REMOTE_PLAYER_ADDED, svMsg.connId, {}, {});
                        }
                    } break;

                    case MSG_SV_PLAYER_DISCONNECTED: {
                        removeRemotePlayer(svMsg.connId);
                        runCallbacks(RE_CL_EVT_REMOTE_PLAYER_REMOVED, svMsg.connId, {}, {});
                    } break;

                    case MSG_GN_RECOMP_BLOB: {
                        // this check is to handle the case where the message arrives before the join was sent
                        // this is needed because MSG_GN_RECOMP_BLOB can be sent unreliably
                        if (svMsg.connId == SERVER_ID || mConnIdToClient.contains(svMsg.connId)) {
                            // std::cout << "MSG_GN_RECOMP_BLOB: " << "connId: " << svMsg.connId << ", messageGroup: " << svMsg.messageGroup << "\n";
                            RecompBlobContainer blob;
                            if (blob.deserialize(svMsg.data)) {
                                runCallbacks(RE_CL_EVT_RECOMP_BLOB, svMsg.connId, blob.data, blob.messageGroup);
                            }
                        }
                    } break;

                    case MSG_SV_INFORM_CLIENT_ID: {
                        mSelfId = svMsg.connId;
                        runCallbacks(RE_CL_EVT_OWN_ID_RECEIVED, svMsg.connId, {}, {});
                    } break;

                    case MSG_SV_LEADER_CHANGED: {
                        MessageGroupContainer leaderChange;
                        if (leaderChange.deserialize(svMsg.data)) {
                            runCallbacks(RE_CL_EVT_LEADER_ID_RECEIVED, svMsg.connId, {}, leaderChange.messageGroup);
                        }
                    } break;

                    case MSG_SV_LEADER_AVAILABLE: {
                        MessageGroupContainer leaderChange;
                        if (leaderChange.deserialize(svMsg.data)) {
                            runCallbacks(RE_CL_EVT_LEADER_AVAILABLE, svMsg.connId, {}, leaderChange.messageGroup);
                        }
                    } break;

                    case MSG_GN_SCENE_CHANGE: {
                        SceneIdContainer s;
                        if (s.deserialize(svMsg.data)) {
                            uint16_t sceneId = static_cast<uint16_t>(s.id);

                            // write in big endian to write to recomp memory
                            std::array<Byte, 2> data{
                                static_cast<Byte>(sceneId >> 8),
                                static_cast<Byte>(sceneId),
                            };
                            runCallbacks(RE_CL_EVT_SCENE_UPDATE, svMsg.connId, data, {});
                        }
                    } break;

                    default: {
                    } break;
                }
            } else {
                std::cout << "[ERROR] Could not parse message from server.\n";

                if (mSelfId == k_HSteamNetConnection_Invalid) {
                    disconnectFromServer(k_ESteamNetConnectionEnd_Remote_BadProtocolVersion, "Failed to parse handshake message", true);
                }
            }
        }

        // We don't need this anymore.
        incomingMsg->Release();
    }
}

void NetworkMod::RecompClient::onSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
    // assert(info->m_hConn == mServerConnHandle || mServerConnHandle == k_HSteamNetConnection_Invalid);

    if (mServerConnHandle == info->m_hConn) {
        // What's the state of the connection?
        switch (info->m_info.m_eState) {
            case k_ESteamNetworkingConnectionState_None:
                // NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
                break;

            case k_ESteamNetworkingConnectionState_ClosedByPeer:
            case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
                mIsRunning = false;

                // Print an appropriate message
                if (info->m_eOldState == k_ESteamNetworkingConnectionState_Connecting) {
                    // Note: we could distinguish between a timeout, a rejected connection,
                    // or some other transport problem.
                    std::cout << "Could not connect to host. Reason: " << info->m_info.m_szEndDebug << '\n';
                } else if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
                    std::cout << "Lost connection to host. Reason:  " << info->m_info.m_szEndDebug << '\n';
                } else {
                    // NOTE: We could check the reason code for a normal disconnection
                    std::cout << "The host has closed the connection. Reason: %s" << info->m_info.m_szEndDebug << '\n';
                }

                // Clean up the connection.  This is important!
                // The connection is "closed" in the network sense, but
                // it has not been destroyed.  We must close it on our end, too
                // to finish up.  The reason information do not matter in this case,
                // and we cannot linger because it's already closed on the other end,
                // so we just pass 0's.
                disconnectFromServer(k_ESteamNetConnectionEnd_App_Generic, nullptr, false);
                break;
            }

            case k_ESteamNetworkingConnectionState_Connecting:
                // We will get this callback when we start connecting.
                // We can ignore this.
                break;

            case k_ESteamNetworkingConnectionState_Connected:
                std::cout << "[INFO] NetMessages: Connected to server OK" << '\n';
                break;

            default:
                // Silences -Wswitch
                break;
        }
    }
}

void NetworkMod::RecompClient::pollConnectionStateChanges() {
    sCallbackInstance = this;
    mInterface->RunCallbacks();
}

void NetworkMod::RecompClient::runCallbacks(RecompClientEvent evtId, ConnectionId connId, ByteView data, const std::string &messageGroup) {
    for (auto &callback : mCallbacks) {
        callback(evtId, connId, data, messageGroup);
    }
}

void NetworkMod::RecompClient::disconnectFromServer(int reason, const char *debugMsg, bool enableLinger) {
    mIsRunning = false;
    mInterface->CloseConnection(mServerConnHandle, 0, nullptr, true);
    runCallbacks(RE_CL_EVT_DISCONNECTED, SERVER_ID, {}, {});
}

// Ensure the linker can find the Zelda64Client::sCallbackInstance symbol
RecompClient *RecompClient::sCallbackInstance;
