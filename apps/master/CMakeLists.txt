project(masterserver)

set(SOURCE_FILES main.cpp MasterServer.cpp MasterServer.hpp RestServer.cpp RestServer.hpp)
if (WIN32)
    list(APPEND SOURCE_FILES ${CMAKE_SOURCE_DIR}/files/tes3mp/tes3mp-ng.rc)
endif()

add_executable(masterserver ${SOURCE_FILES})
target_link_libraries(masterserver openmw-mp)

option(BUILD_MASTER_TEST "build master server test program" OFF)

if(BUILD_MASTER_TEST)
    add_executable(ServerTest ServerTest.cpp)
    target_link_libraries(ServerTest openmw-mp)
endif()

if (MSVC)
    # RakNet headers trigger a lot of conversion warnings
    target_compile_options(masterserver PRIVATE /wd4244 /wd4267 /wd4996)
endif()

if (UNIX AND NOT APPLE)
    # Fix for not visible pthreads functions for linker with glibc 2.15
    target_link_libraries(masterserver ${CMAKE_THREAD_LIBS_INIT})
    if(BUILD_MASTER_TEST)
        target_link_libraries(ServerTest ${CMAKE_THREAD_LIBS_INIT})
    endif()
endif()

if(WIN32)
    target_link_libraries(masterserver ws2_32)
endif()
