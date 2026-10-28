#include <steam/steamnetworkingsockets.h>
#include <iostream>
#include <argparse/argparse.hpp>
#include "clientservercommon.hpp"
#include "recompserver.hpp"

int main(int argc, char *argv[]) {
    uint16_t port;
    argparse::ArgumentParser program("Z64 Network Mod Server", NetworkMod::CURRENT_VERSION);
    program.add_argument("--port", "-p")
        .required()
        .help("Port to run the server on. (Range: 0-65535)")
        .metavar("NUMBER")
        .store_into(port);

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    std::cout << "Initializing GameNetworkingSockets..." << '\n';

    SteamDatagramErrMsg errMsg;
    if (GameNetworkingSockets_Init(nullptr, errMsg)) {
        NetworkMod::RecompServer server;
        server.run(port);
    } else {
        NetworkMod::printAndFlushOutput(errMsg);
    }

    std::cout << "Killing GameNetworkingSockets..." << '\n';
    GameNetworkingSockets_Kill();

    return 0;
}
