# Locate RakNet (TES3MP uses the CrabNet fork: https://github.com/TES3MP/CrabNet)
#
# Hints: RAKNET_ROOT (CMake or environment variable) pointing at an install prefix
# that contains include/raknet/RakPeer.h and lib/RakNetLibStatic[d].lib
#
# Defines:
#  RakNet_FOUND
#  RakNet_INCLUDES  - the include directory (already pointing at include/raknet)
#  RakNet_LIBRARY   - libraries to link
#  RakNet::RakNet   - imported target

set(_RAKNET_HINTS ${RAKNET_ROOT} $ENV{RAKNET_ROOT})

find_library(RakNet_LIBRARY_RELEASE NAMES RakNetLibStatic
    HINTS ${_RAKNET_HINTS} PATH_SUFFIXES lib lib/Release
    PATHS ENV LD_LIBRARY_PATH ENV LIBRARY_PATH /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib /opt/local/lib)

find_library(RakNet_LIBRARY_DEBUG NAMES RakNetLibStaticd RakNetLibStaticD
    HINTS ${_RAKNET_HINTS} PATH_SUFFIXES lib lib/Debug
    PATHS ENV LD_LIBRARY_PATH ENV LIBRARY_PATH /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib /opt/local/lib)

find_path(RakNet_INCLUDE_ROOT raknet/RakPeer.h
    HINTS ${_RAKNET_HINTS} PATH_SUFFIXES include
    PATHS ENV CPATH /usr/include /usr/local/include /opt/local/include)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RakNet DEFAULT_MSG RakNet_LIBRARY_RELEASE RakNet_INCLUDE_ROOT)

if(RakNet_FOUND)
    set(RakNet_INCLUDES ${RakNet_INCLUDE_ROOT}/raknet)
    if(NOT RakNet_LIBRARY_DEBUG)
        set(RakNet_LIBRARY_DEBUG ${RakNet_LIBRARY_RELEASE})
    endif()
    set(RakNet_LIBRARY optimized ${RakNet_LIBRARY_RELEASE} debug ${RakNet_LIBRARY_DEBUG})
    if(WIN32)
        list(APPEND RakNet_LIBRARY ws2_32)
    endif()

    if(NOT TARGET RakNet::RakNet)
        add_library(RakNet::RakNet STATIC IMPORTED)
        set_target_properties(RakNet::RakNet PROPERTIES
            IMPORTED_LOCATION "${RakNet_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_RELEASE "${RakNet_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_DEBUG "${RakNet_LIBRARY_DEBUG}"
            IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
            MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
            INTERFACE_INCLUDE_DIRECTORIES "${RakNet_INCLUDES}")
        if(WIN32)
            set_property(TARGET RakNet::RakNet APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32)
        endif()
    endif()
endif()

mark_as_advanced(RakNet_LIBRARY_RELEASE RakNet_LIBRARY_DEBUG RakNet_INCLUDE_ROOT)
