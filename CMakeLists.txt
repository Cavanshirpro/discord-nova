cmake_minimum_required(VERSION 3.25)
project(discord_nova VERSION 0.1.0 DESCRIPTION "Native C++ Discord client with GUI and TUI" LANGUAGES CXX)

option(NOVA_BUILD_GUI "Build the Qt Widgets desktop client" ON)
option(NOVA_BUILD_TUI "Build the FTXUI terminal client" ON)
option(NOVA_BUILD_TESTS "Build offline tests" ON)
set(NOVA_DISCORD_SOURCE "" CACHE PATH "Use a local discord.cpp-self checkout instead of downloading")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()
include(FetchContent)
include(GNUInstallDirs)
find_package(Threads REQUIRED)

set(DISCORD_SELF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DISCORD_SELF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DISCORD_SELF_BUILD_LIVE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(DISCORD_SELF_NATIVE_MEDIA OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(NOVA_DISCORD_SOURCE)
    add_subdirectory("${NOVA_DISCORD_SOURCE}" "${CMAKE_BINARY_DIR}/discord-self" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(discord_self
        GIT_REPOSITORY https://github.com/Cavanshirpro/discord.cpp-self.git
        GIT_TAG a8681265e0c6ee58017639070c891d06d9624d8e) # v0.2.1
    FetchContent_MakeAvailable(discord_self)
endif()

add_library(nova_core STATIC src/core.cpp)
target_include_directories(nova_core PUBLIC include)
target_link_libraries(nova_core PUBLIC discord::self Threads::Threads)
target_compile_definitions(nova_core PUBLIC NOVA_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(nova_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(nova_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOVA_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
    qt_add_executable(discord-nova-gui src/gui.cpp)
    set_target_properties(discord-nova-gui PROPERTIES
        WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.cavanshirpro.discord-nova
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    target_link_libraries(discord-nova-gui PRIVATE nova_core Qt6::Widgets)
    install(TARGETS discord-nova-gui BUNDLE DESTINATION . COMPONENT Runtime RUNTIME DESTINATION bin COMPONENT Runtime)
endif()

if(NOVA_BUILD_TUI)
    set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG f921fad208912747c17d129a8ef75ec7624b6eec) # v7.0.3
    FetchContent_MakeAvailable(ftxui)
    add_executable(discord-nova-tui src/tui.cpp)
    target_link_libraries(discord-nova-tui PRIVATE nova_core ftxui::component)
    install(TARGETS discord-nova-tui RUNTIME DESTINATION bin COMPONENT Runtime)
endif()

if(NOVA_BUILD_TESTS)
    enable_testing()
    add_executable(nova_core_tests tests/core.cpp)
    target_link_libraries(nova_core_tests PRIVATE nova_core)
    add_test(NAME core COMMAND nova_core_tests)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_executable(nova_integration_tests tests/integration.cpp)
    target_link_libraries(nova_integration_tests PRIVATE nova_core)
    add_test(NAME loopback COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/loopback.py $<TARGET_FILE:nova_integration_tests>)
    set_tests_properties(core loopback PROPERTIES TIMEOUT 45)
    if(NOVA_BUILD_GUI)
        add_test(NAME gui_demo COMMAND discord-nova-gui --demo --smoke-test)
        set_tests_properties(gui_demo PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    endif()
    if(NOVA_BUILD_TUI)
        add_test(NAME tui_demo COMMAND discord-nova-tui --demo --smoke-test)
    endif()
endif()

install(FILES README.md LICENSE COPYING NOTICE THIRD_PARTY.md CHANGELOG.md DESTINATION share/doc/discord-nova COMPONENT Runtime)
