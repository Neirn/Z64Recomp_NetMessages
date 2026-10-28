#ifndef RECOMPCLIENT_HPP
#define RECOMPCLIENT_HPP

#include <steam/steamnetworkingsockets.h>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include "clientservercommon.hpp"
#include "recompinterop.h"
#include "netmessages_api.h"

namespace NetworkMod {

class RecompClient {
public:
    using Callback = void (*)(RecompClientEvent evt, ConnectionId connId, ByteView data, const std::string &messageGroup);

    void run(const SteamNetworkingIPAddr &serverAddr, const std::string &roomName, const std::string &nickname, const std::string &gameId);
    void doPolling();
    void sendMessage(ConnectionId target, NetMessagesFlag flags, ByteView data, const std::string &messageGroup);
    void stop();
    void registerCallback(Callback callback);
    void requestLeader(const std::string &messageGroup);
    void releaseLeader(const std::string &messageGroup);
    void subscribeToMessageGroup(const std::string &messageGroup);
    void unsubscribeFromMessageGroup(const std::string &messageGroup);
    void sendSceneUpdate(Z64SceneId id);
    std::optional<const std::string *> getClientNickname(ConnectionId id);

private:
    static RecompClient *sCallbackInstance;

    static void steamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *info);

    struct RemotePlayerInfo : public ClientInfoCommon {};

    bool mIsRunning = false;
    ConnectionId mServerConnHandle = k_HSteamNetConnection_Invalid;
    ISteamNetworkingSockets *mInterface = nullptr;
    std::chrono::steady_clock::time_point mLastSentHeartbeatTime;
    std::unordered_map<ConnectionId, RemotePlayerInfo> mConnIdToClient;
    std::vector<Callback> mCallbacks;
    std::string mRoomName;
    ConnectionId mSelfId;
    std::string mSelfNickname;
    GameId mGameId;

    void initMembers();
    void fatalError(const std::string &s);
    Message createMessage(ConnectionId target, MessageType msgType, ByteView data);
    void addRemotePlayer(ConnectionId id, const std::string &nickname);
    void removeRemotePlayer(ConnectionId id);
    void sendMessageToServer(const Message &msg, int sendFlags);
    void pollIncomingMessages();
    void onSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *info);
    void pollConnectionStateChanges();
    void runCallbacks(RecompClientEvent evtId, ConnectionId connId, ByteView data, const std::string &messageGroup);
    void disconnectFromServer(int reason, const char *debugMsg, bool enableLinger);
};

} // namespace NetworkMod

#endif
