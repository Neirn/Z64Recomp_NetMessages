#ifndef RECOMPSERVER_HPP
#define RECOMPSERVER_HPP

#include <steam/steamnetworkingsockets.h>
#include <cstdint>
#include <chrono>
#include <string>
#include <unordered_set>
#include <functional>
#include <optional>
#include "clientservercommon.hpp"
#include "netmessages_api.h"

namespace NetworkMod {

class RecompServer {
public:
    void run(uint16_t port);

private:
    static constexpr ConnectionId EMPTY_LEADER_ID = 0;

    static RecompServer *sCallbackInstance;

    static void steamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *connInfo);

    struct MessageGroupPerGameInfo {
        std::unordered_set<ConnectionId> clientHandles;
        ConnectionId leaderId = EMPTY_LEADER_ID;
    };

    struct MessageGroup {
        MessageGroupName name;
        std::unordered_set<ConnectionId> clientHandles;

        std::unordered_map<GameId, MessageGroupPerGameInfo> gameSpecificInfo;

        bool isEmpty() { return clientHandles.size() == 0; }
    };

    using RoomName = std::string;

    struct Room {
        RoomName name;
        std::unordered_set<ConnectionId> clientHandles;
        std::unordered_map<MessageGroupName, MessageGroup> messageGroups;

        bool isEmpty() const { return clientHandles.size() == 0; }
    };

    struct ClientInfo : public ClientInfoCommon {
        std::string fullName;
        SceneIdContainer scene{-1};
        std::unordered_set<MessageGroup *> messageGroups;
        Room *room = nullptr;
        bool isVerified = false;
    };

    bool mIsRunning;
    HSteamListenSocket mListenSocket;
    HSteamNetPollGroup mPollGroup;
    ISteamNetworkingSockets *mInterface;
    std::chrono::steady_clock::time_point mLastSentHeartbeatTime;
    std::unordered_map<ConnectionId, ClientInfo> mConnToClientMap;
    std::unordered_map<std::string, Room> mNameToRoomMap;

    void initMembers();
    void fatalError(const std::string &s);
    Message createMessage(MessageType type, ConnectionId connId, ByteView data);
    Message createStringMessage(const std::string &s);
    void sendStringToClient(ConnectionId conn, const std::string &s);
    void sendMessageToClient(ConnectionId conn, const Message &msg, int sendFlags);
    void sendMessageToAllVerifiedClients(const Message &msg, int sendFlags, ConnectionId except = k_HSteamNetConnection_Invalid);

    void approveClientVerification(ClientInfo &client, const RoomName &room);

    void handleUnverifiedClientMessage(ClientInfo &client, const Message &msg);
    void handleVerifiedClientMessage(ClientInfo &client, const Message &msg, bool isReliable);

    void pollIncomingMessages();

    void addClient(const SteamNetConnectionStatusChangedCallback_t &connInfo);
    void removeClient(ConnectionId connHandle, int reason, const char *debugMsg, bool enableLinger);

    bool isClientsInSameScene(const ClientInfo &a, const ClientInfo &b) const;

    void addClientToRoom(Room &room, ClientInfo &client);
    void addClientToRoomOrCreateRoom(const RoomName &room, ClientInfo &client);
    void removeClientFromRoom(Room &room, ClientInfo &client);
    void sendMessageToRoom(const Room &room, const Message &msg, int sendFlags, ConnectionId except = k_HSteamNetConnection_Invalid);
    void sendStringToRoom(const Room &room, const std::string &s, int sendFlags, ConnectionId except = k_HSteamNetConnection_Invalid);

    std::optional<MessageGroup *> extractMessageGroup(Room &room, const MessageGroupName &groupName);
    std::optional<MessageGroup *> extractOrCreateMessageGroup(Room &roomName, const MessageGroupName &groupName);
    void addClientToMessageGroup(ClientInfo &client, const MessageGroupName &groupName);
    void removeClientFromMessageGroup(ClientInfo &client, const MessageGroupName &groupName);
    void removeClientFromMessageGroup(ClientInfo &client, MessageGroup& group);
    void removeMessageGroupLeader(MessageGroup &group, const GameId &gameId);
    void sendMessageToAllClientsInMessageGroup(const Message &msg, int sendFlags, const MessageGroup &group, ConnectionId except = k_HSteamNetConnection_Invalid);
    void sendMessageToMessageGroupByGame(const Message &msg, int sendFlags, const MessageGroup &group, const GameId &gameId, ConnectionId except = k_HSteamNetConnection_Invalid);
    void sendMessageToMessageGroupInScene(const Message &msg, int sendFlags, const ClientInfo &senderClient, const MessageGroup &group, ConnectionId except = k_HSteamNetConnection_Invalid);

    void onSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *connInfo);
    void pollConnectionStateChanges();
};

}; // namespace NetworkMod

#endif
