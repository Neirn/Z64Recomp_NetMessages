#include <steam/steamnetworkingsockets.h>
#include <steam/steamnetworkingsockets_flat.h>
#include <iostream>
#include <deque>
#include "lib_recomp.hpp"
#include "recompclient.hpp"
#include "clientservercommon.hpp"
#include <format>
#include <cctype>
#include <algorithm>
#include "netmessages_api.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

extern "C" RECOMP_EXPORT int recomp_api_version = RECOMP_API_VERSION;

using namespace NetworkMod;

static RecompClient sLocalClient;

struct ClientEvent {
    RecompClientEvent evtId;
    ConnectionId connId;
    ByteVec data;
    std::string messageGroup;
};

std::deque<ClientEvent> sPendingEvents;

static void clientEventHandler(RecompClientEvent evt, ConnectionId connId, ByteView data, const std::string &messageGroup) {
    // std::cout << "Event handler called. evt: " << evt << ", connId: " << connId << ", messageGroup: '" << messageGroup << "'\n";
    sPendingEvents.push_back(ClientEvent{evt, connId, {data.begin(), data.end()}, messageGroup});
}

RECOMP_DLL_FUNC(NetMessagesExt_init) {
    static bool isInitialized = false;

    if (!isInitialized) {
        SteamDatagramErrMsg errMsg;

        std::cout << "Initializing GameNetworkingSockets...\n";
        if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
            printAndFlushOutput(errMsg);
        } else {
            isInitialized = true;
            std::cout << "GameNetworkingSockets initialized!\n";
        }
    }

    RECOMP_RETURN(int32_t, isInitialized);
}

static std::string trim(const std::string &s) {
    auto start = s.begin();
    auto end = s.end();

    while (start != end && iswspace(*start)) {
        start++;
    }

    if (end != start) {
        end--;
    }

    while (end != start && iswspace(*end)) {
        end--;
    }

    if (end != s.end()) {
        end++;
    }

    return {start, end};
}

std::string getRealIPString(const std::string &ip) {
    std::string ret;

    if (ip.size() > 0) {
        auto lastChar = ip.end() - 1;
        auto seperator = lastChar;

        while (seperator > ip.begin() && *seperator != ':') {
            seperator--;
        }

        if (seperator != ip.begin() && seperator != lastChar) {
            std::string hostName = trim({ip.begin(), seperator});
            std::string port = trim({seperator + 1, ip.end()});

            // std::cerr << "hostName: " << hostName << '\n';
            // std::cerr << "port: " << port << '\n';

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo *res = nullptr;

            if (getaddrinfo(hostName.c_str(), nullptr, &hints, &res) == 0) {
                addrinfo *curr = res;
                while (curr) {
                    char realIp[INET6_ADDRSTRLEN] = {0};

                    if (curr->ai_family == AF_INET) {
                        if (inet_ntop(curr->ai_family, &(((sockaddr_in *)(curr->ai_addr))->sin_addr), realIp, sizeof(realIp))) {
                            ret = std::format("{}:{}", std::string(realIp), port);
                            break;
                        }
                    } else if (curr->ai_family == AF_INET6) {
                        if (inet_ntop(curr->ai_family, &(((sockaddr_in6 *)(curr->ai_addr))->sin6_addr), realIp, sizeof(realIp))) {
                            ret = std::format("[{}]:{}", std::string(realIp), port);
                            break;
                        }
                    }

                    curr = curr->ai_next;
                }
            }

            freeaddrinfo(res);
        }
    }

    return ret;
}

RECOMP_DLL_FUNC(NetMessagesExt_connect) {
    SteamNetworkingIPAddr addr;
    addr.Clear();

    std::string ip = RECOMP_ARG_STR(0);

    bool isValid = true;

    // TODO: figure out why SteamNetworkingIPAddr::ParseString causes a linker error in release mode on MacOS
    // For now, using the flat version of the function does the job...
    if (!SteamAPI_SteamNetworkingIPAddr_ParseString(&addr, ip.c_str())) {
        auto ip2 = getRealIPString(ip);

        addr.Clear();

        if (!SteamAPI_SteamNetworkingIPAddr_ParseString(&addr, ip2.c_str())) {
            std::cerr << "[ERROR] NetMessages: Could not parse server IP or port " << ip << " (DNS Lookup result: " << ip2 << ")" << '\n';
            isValid = false;
        } else {
            // std::cout << "(DNS Lookup result: " << ip2 << ")" << '\n';
            // std::cout << addr.m_port << '\n';
        }
    }

    if (isValid) {
        std::string roomName = trim(RECOMP_ARG_STR(1));
        std::string nickname = trim(RECOMP_ARG_STR(2));
        std::string gameId = trim(RECOMP_ARG_STR(3));

        if (roomName.size() < (MAX_RECOMP_NETWORK_ROOM_NAME_BUFFER_SIZE)) {
            sLocalClient.run(addr, roomName, nickname, gameId);
            sLocalClient.registerCallback(clientEventHandler);
        } else {
            std::cout << "[ERROR] NetMessages: Room name too long! (Received: " << roomName.size()
                      << ", max: (" << MAX_RECOMP_NETWORK_ROOM_NAME_BUFFER_SIZE - 1 << ")\n";
            isValid = false;
        }
    }

    RECOMP_RETURN(uint32_t, isValid);
}

static bool writeToRecompBuffer(uint8_t *rdram, recomp_context *ctx, PTR(char) dest, size_t destBufSize, ByteView src, size_t destOffset = 0) {
    if (src.size() == 0) {
        return true;
    }

    if (!dest) {
        return false;
    }

    if (destBufSize < (src.size() + destOffset)) {
        return false;
    }

    for (uint64_t i = 0; i < src.size(); i++) {
        MEM_BU(dest, i + destOffset) = src[i];
    }

    return true;
}

static bool readFromRecompBuffer(uint8_t *rdram, recomp_context *ctx, PTR(char) src, size_t srcSize, ByteVec &dest) {
    if (srcSize == 0) {
        return true;
    }

    if (!src) {
        return false;
    }

    if (dest.size() < srcSize) {
        return false;
    }

    for (uint64_t i = 0; i < srcSize; ++i) {
        dest[i] = MEM_BU(src, i);
    }

    return true;
}

RECOMP_DLL_FUNC(NetMessagesExt_doPolling) {
    sLocalClient.doPolling();
}

RECOMP_DLL_FUNC(NetMessagesExt_disconnect) {
    sLocalClient.stop();
}

RECOMP_DLL_FUNC(NetMessagesExt_isClientEventPending) {
    RECOMP_RETURN(uint32_t, !sPendingEvents.empty());
}

RECOMP_DLL_FUNC(NetMessagesExt_retrieveNextClientEvent) {
    bool ret = false;

    if (!sPendingEvents.empty()) {
        // see NetMessagesExt_RetrieveClientEventArg in mod/include/networkmod.h
        // recompStruct[0]: write ClientEvent here
        // recompStruct[1]: write sender's ConnectionId here
        // recompStruct[2]: write message data size here
        //
        // recompStruct[3]: read data buffer size from here
        // recompStruct[4]: read message group buffer size from here
        //
        // TODO: figure out if this can be done more cleanly
        PTR(uint32_t) recompStruct = RECOMP_ARG(PTR(uint32_t), 0);
        PTR(void) dataBuf = RECOMP_ARG(PTR(void), 1);
        PTR(char) messageGroupBuf = RECOMP_ARG(PTR(char), 2);

        if (recompStruct && dataBuf && messageGroupBuf) {
            size_t destBufSize = MEM_W(recompStruct, sizeof(uint32_t) * 3ULL);
            size_t messageGroupBufSize = MEM_W(recompStruct, sizeof(uint32_t) * 4ULL);

            auto &event = *sPendingEvents.begin();

            if (writeToRecompBuffer(rdram, ctx, dataBuf, destBufSize, event.data) &&
                writeToRecompBuffer(rdram, ctx, messageGroupBuf, messageGroupBufSize, getByteViewOfStringAsCString(event.messageGroup))) {
                MEM_W(recompStruct, sizeof(uint32_t) * 0ULL) = event.evtId;
                MEM_W(recompStruct, sizeof(uint32_t) * 1ULL) = event.connId;
                MEM_W(recompStruct, sizeof(uint32_t) * 2ULL) = event.data.size();
                ret = true;
            } else {
                std::cerr << "Could not retrieve client event! (error writing to ouput buffer(s))" << '\n';
            }
        } else {
            std::cerr << "Could not retrieve client event! (passed in argument was NULL)" << '\n';
        }

        sPendingEvents.pop_front();
    }

    RECOMP_RETURN(uint32_t, ret);
}

RECOMP_DLL_FUNC(NetMessagesExt_sendMessage) {
    bool ret = false;

    // see NetMessagesExt_SendMessageArg in mod/include/networkmod.h
    // messageInfo[0] target ConnectionId
    // messageInfo[1] reliability bool
    // messageInfo[2] payload size
    PTR(uint32_t) messageInfo = RECOMP_ARG(PTR(uint32_t), 0);
    PTR(char) rawData = RECOMP_ARG(PTR(char), 1);
    std::string messageGroup = RECOMP_ARG(PTR(char), 2) ? RECOMP_ARG_STR(2) : "";

    if (messageInfo) {
        uint32_t dataSize = MEM_W(messageInfo, 2LL * sizeof(uint32_t));
        if (dataSize <= MAX_RECOMP_NETWORK_DATA_SIZE) {
            ByteVec data(dataSize);
            if (readFromRecompBuffer(rdram, ctx, rawData, dataSize, data)) {
                ConnectionId target = MEM_W(messageInfo, 0LL * sizeof(uint32_t));
                NetMessagesFlag flags = static_cast<NetMessagesFlag>(MEM_W(messageInfo, 1LL * sizeof(uint32_t)));

                sLocalClient.sendMessage(target, flags, data, messageGroup);
                ret = true;
            } else {
                std::cerr << __func__ << ": Error reading recomp data.";
            }
        } else {
            std::cerr << __func__ << ": Recomp data too large! (" << dataSize << " > " << MAX_RECOMP_NETWORK_DATA_SIZE << ")" << '\n';
        }
    } else {
        std::cerr << __func__ << ": Passed in NULL messageInfo!\n";
    }

    RECOMP_RETURN(int32_t, ret);
}

RECOMP_DLL_FUNC(NetMessagesExt_requestLeader) {
    sLocalClient.requestLeader(RECOMP_ARG_STR(0));
}

RECOMP_DLL_FUNC(NetMessagesExt_releaseLeader) {
    sLocalClient.releaseLeader(RECOMP_ARG_STR(0));
}

RECOMP_DLL_FUNC(NetMessagesExt_subscribeToMessageGroup) {
    sLocalClient.subscribeToMessageGroup(RECOMP_ARG_STR(0));
}

RECOMP_DLL_FUNC(NetMessagesExt_unsubscribeFromMessageGroup) {
    sLocalClient.unsubscribeFromMessageGroup(RECOMP_ARG_STR(0));
}

RECOMP_DLL_FUNC(NetMessagesExt_sendSceneIdUpdate) {
    sLocalClient.sendSceneUpdate(static_cast<Z64SceneId>(RECOMP_ARG(int32_t, 0)));
}

RECOMP_DLL_FUNC(NetMessagesExt_getClientNickname) {
    bool ret = false;

    ConnectionId id = RECOMP_ARG(uint32_t, 0);
    PTR(char) buf = RECOMP_ARG(PTR(char), 1);
    uint32_t bufSize = RECOMP_ARG(uint32_t, 2);

    auto nameOpt = sLocalClient.getClientNickname(id);

    if (nameOpt) {
        const std::string &nickname = *nameOpt.value();

        ret = writeToRecompBuffer(rdram, ctx, buf, bufSize, getByteViewOfStringAsCString(nickname));
    }

    RECOMP_RETURN(bool, ret);
}
