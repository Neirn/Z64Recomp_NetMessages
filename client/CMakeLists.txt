add_library(z64_networkmod_client SHARED client.cpp )

target_link_libraries(z64_networkmod_client GameNetworkingSockets::static)

target_include_directories(z64_networkmod_client PRIVATE include)

install(TARGETS z64_networkmod_client
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
