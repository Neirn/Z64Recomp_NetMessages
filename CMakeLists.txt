cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Z64RecompNetMessages VERSION 0.1.0 LANGUAGES C CXX)

find_package(GameNetworkingSockets CONFIG REQUIRED)

add_compile_definitions(NETMESSAGES_API_ENUM_ONLY)
include_directories(include_shared)
include_directories(public_api)

# Needed for Linux
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(extlib/common)
add_subdirectory(extlib/server)
add_subdirectory(extlib/client)
