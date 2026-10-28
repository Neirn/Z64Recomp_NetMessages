#include "global.h"
#include "modding.h"
#include "networkconnection.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "networkmod.h"
#include "netmessages.h"
#include "logger.h"

static bool sIsConnected;
static bool sIsVerified;

static void NetworkConnection_init(void) {
    NetMessagesExt_init();
}

bool NetworkConnection_isConnected(void) {
    return sIsConnected && sIsVerified;
}

void NetworkConnection_markVerified(void) {
    sIsVerified = true;
}

static bool NetworkConnection_connectToServer(const char *ip, const char *roomName, const char *nickname) {
    sIsConnected = false;

    if (ip[0] != '\0') {
        sIsConnected = NetMessagesExt_connect(ip, roomName, nickname, "mm");
    } else {
        Logger_printError("Server IP address was empty!");
    }

    return sIsConnected;
}

void NetworkConnection_reset(void) {
    Logger_printInfo("Resetting networking...");
    gSelfConnectionId = 0;
    sIsConnected = false;
    sIsVerified = false;
}

RECOMP_CALLBACK("*", recomp_on_init) void initNetworkingOnRecompInit(void) {
    NetworkConnection_init();
}

void tryConnect(void) {
    if (!sIsConnected) {
        char *serverIp = recomp_get_config_string("server_ip");
        char *roomName = recomp_get_config_string("room_name");
        char *nickname = recomp_get_config_string("nickname");

        if (serverIp && roomName && nickname) {
            Logger_printInfo("Attempting to connect to server...");
            if (!NetworkConnection_connectToServer(serverIp, roomName, nickname)) {
                Logger_printError("Connection failed!");
            }
        } else {
            Logger_printError("Failed to parse a config string!");
        }

        if (serverIp) {
            recomp_free_config_string(serverIp);
        }

        if (roomName) {
            recomp_free_config_string(roomName);
        }

        if (nickname) {
            recomp_free_config_string(nickname);
        }
    }
}

RECOMP_CALLBACK("*", recomp_on_play_init) void connectToServerOnPlayInit(void) {
    tryConnect();
}

void tryConnectToServer_on_Player_Init(void) {
    tryConnect();
}

void pollNetwork_on_Graph_Update(void) {
    if (sIsConnected) {
        NetMessagesExt_doPolling();
    }
}
