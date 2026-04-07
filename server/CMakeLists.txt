add_executable(z64_networkmod_server server.cpp)

target_link_libraries(z64_networkmod_server GameNetworkingSockets::shared)

install(TARGETS z64_networkmod_server RUNTIME DESTINATION bin)
