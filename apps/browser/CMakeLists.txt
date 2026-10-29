# tes3mp-ng change (major): built with Qt 6 like OpenMW 0.51's launcher, using AUTOMOC and AUTOUIC
set(BROWSER_UI
    ${CMAKE_SOURCE_DIR}/files/tes3mp/ui/Main.ui
    ${CMAKE_SOURCE_DIR}/files/tes3mp/ui/ServerInfo.ui
)

set(BROWSER
    main.cpp
    MainWindow.cpp
    ServerModel.cpp
    ServerInfoDialog.cpp
    MySortFilterProxyModel.cpp
    netutils/Utils.cpp
    netutils/QueryClient.cpp
    PingUpdater.cpp
    PingHelper.cpp
    QueryHelper.cpp
)

set(BROWSER_HEADER
    MainWindow.hpp
    ServerModel.hpp
    ServerInfoDialog.hpp
    MySortFilterProxyModel.hpp
    PingUpdater.hpp
    PingHelper.hpp
    QueryHelper.hpp
    netutils/Utils.hpp
    netutils/QueryClient.hpp
    Types.hpp
)

source_group(browser FILES ${BROWSER} ${BROWSER_HEADER})

if(WIN32)
    set(GUI_TYPE WIN32)
    list(APPEND BROWSER ${CMAKE_SOURCE_DIR}/files/tes3mp/tes3mp-ng.rc)
endif()

QT_ADD_RESOURCES(RCC_SRCS ${CMAKE_SOURCE_DIR}/files/launcher/launcher.qrc)

openmw_add_executable(tes3mp-browser
    ${GUI_TYPE}
    ${BROWSER}
    ${BROWSER_HEADER}
    ${BROWSER_UI}
    ${RCC_SRCS}
)

if (WIN32)
    INSTALL(TARGETS tes3mp-browser RUNTIME DESTINATION ".")
    target_sources(tes3mp-browser PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

target_link_libraries(tes3mp-browser
    openmw-mp
    components_qt
    Qt::Widgets
    Qt::Core
    Qt::Svg
)

if (MSVC)
    # RakNet headers trigger a lot of conversion warnings
    target_compile_options(tes3mp-browser PRIVATE /wd4244 /wd4267 /wd4996)
endif()

set_property(TARGET tes3mp-browser PROPERTY AUTOMOC ON)
set_property(TARGET tes3mp-browser PROPERTY AUTOUIC ON)
set_property(TARGET tes3mp-browser PROPERTY AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/files/tes3mp/ui)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(tes3mp-browser PRIVATE --coverage)
    target_link_libraries(tes3mp-browser gcov)
endif()
