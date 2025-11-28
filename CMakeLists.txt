cmake_minimum_required(VERSION 3.16)
project(ecs_module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# ECS core library
# --------------------------------------------------------------------------

add_library(ecs_core
    src/ecs_world.hpp
    src/ecs_world.cpp
    src/lua_loader.hpp
    src/lua_loader.cpp
)

target_include_directories(ecs_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

# --------------------------------------------------------------------------
# Lua detection
# --------------------------------------------------------------------------

find_package(Lua QUIET)

if (Lua_FOUND AND TARGET Lua::Lua)
    message(STATUS "Using Lua target: Lua::Lua")
    target_link_libraries(ecs_core PUBLIC Lua::Lua)

else()
    message(STATUS "Lua::Lua NOT found — falling back to manual search")

    find_path(LUA_INCLUDE_DIR
        NAMES lua.h
        PATHS
            /usr/include
            /usr/local/include
            /usr/include/lua5.3
            /usr/include/lua5.4
    )

    find_library(LUA_LIBRARY
        NAMES lua5.4 lua5.3 lua
        PATHS
            /usr/lib
            /usr/lib64
            /usr/local/lib
    )

    if (NOT LUA_INCLUDE_DIR OR NOT LUA_LIBRARY)
        message(FATAL_ERROR "Lua not found. Install liblua5.3-dev or liblua5.4-dev.")
    endif()

    message(STATUS "Lua include dir: ${LUA_INCLUDE_DIR}")
    message(STATUS "Lua library:     ${LUA_LIBRARY}")

    target_include_directories(ecs_core PUBLIC ${LUA_INCLUDE_DIR})
    target_link_libraries(ecs_core PUBLIC ${LUA_LIBRARY})
endif()

# --------------------------------------------------------------------------
# Ncurses detection (NEW)
# --------------------------------------------------------------------------

find_package(Curses REQUIRED)

message(STATUS "Ncurses include: ${CURSES_INCLUDE_DIR}")
message(STATUS "Ncurses libs:    ${CURSES_LIBRARIES}")

# --------------------------------------------------------------------------
# Example executable
# --------------------------------------------------------------------------

add_executable(ecs_example src/main.cpp)

# Link example → ECS core + ncurses
target_link_libraries(ecs_example PRIVATE
    ecs_core
    ${CURSES_LIBRARIES}
)

target_include_directories(ecs_example PRIVATE
    ${CURSES_INCLUDE_DIR}
)

# --------------------------------------------------------------------------
# Install rules
# --------------------------------------------------------------------------

install(TARGETS ecs_core EXPORT ecs_coreTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY src/ DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
)
