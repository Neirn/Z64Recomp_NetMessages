add_executable(yazmt_z64_netmessages_server main.cpp recompserver.cpp)

find_package(argparse CONFIG REQUIRED)

target_link_libraries(yazmt_z64_netmessages_server PRIVATE GameNetworkingSockets::static Z64RecompNetworkMod::common argparse::argparse)

target_include_directories(yazmt_z64_netmessages_server PUBLIC include)
