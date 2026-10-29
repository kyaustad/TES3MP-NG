add_subdirectory(shaders)
add_subdirectory(data)
add_subdirectory(data-mw)
add_subdirectory(lua_api)

if (NOT DEFINED OPENMW_RESOURCES_ROOT)
    return()
endif()

copy_resource_file("launcher/images/openmw.png" "${OPENMW_RESOURCES_ROOT}" "resources/openmw.png")
copy_resource_file("tes3mp/tes3mp-ng_logo.png" "${OPENMW_RESOURCES_ROOT}" "resources/mygui/tes3mp-ng_logo.png") # tes3mp addition

