cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Z64Recomp_NetworkMod VERSION 0.1.0 LANGUAGES C CXX)

find_package(GameNetworkingSockets)

add_subdirectory(server)
add_subdirectory(client)
