# TES3MP shared networking library, used by the client (openmw), the server (tes3mp-server)
# and the master server.

file(GLOB_RECURSE OPENMW_MP_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

add_library(openmw-mp STATIC ${OPENMW_MP_FILES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "openmw-mp" FILES ${OPENMW_MP_FILES})

target_include_directories(openmw-mp PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/extern)
target_link_libraries(openmw-mp PUBLIC components RakNet::RakNet)

if (MSVC)
    # RakNet headers trigger a lot of conversion warnings
    target_compile_options(openmw-mp PRIVATE /wd4244 /wd4267 /wd4996)
endif()

if (OPENMW_PRECOMPILED_HEADERS)
    target_precompile_headers(openmw-mp PRIVATE <string> <vector> <map>)
endif()
