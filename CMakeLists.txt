cmake_minimum_required(VERSION 3.16)

project(common_libs LANGUAGES CXX)

option(COMMON_LIBS_INSTALL "Enable install rules (for packaging)" OFF)

add_library(common_ecs INTERFACE)
add_library(common::ecs ALIAS common_ecs)

target_compile_features(common_ecs INTERFACE cxx_std_20)
target_include_directories(common_ecs INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ecs/include>
    $<INSTALL_INTERFACE:include>
)

add_library(common_net INTERFACE)
add_library(common::net ALIAS common_net)

target_compile_features(common_net INTERFACE cxx_std_20)
target_include_directories(common_net INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/net/include>
    $<INSTALL_INTERFACE:include>
)

add_library(common_engine INTERFACE)
add_library(common::engine ALIAS common_engine)

target_compile_features(common_engine INTERFACE cxx_std_20)
target_include_directories(common_engine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(common_engine INTERFACE common::ecs)
find_package(Lua 5.3 QUIET)

if (Lua_FOUND)
    if (TARGET Lua::Lua)
        target_link_libraries(common_engine INTERFACE Lua::Lua)
    else()
        target_include_directories(common_engine INTERFACE ${LUA_INCLUDE_DIR})
        target_link_libraries(common_engine INTERFACE ${LUA_LIBRARIES})
    endif()
else()
    message(FATAL_ERROR
    "Lua 5.3 introuvable. Installe liblua5.3-dev (Debian/Ubuntu) ou lua53 (vcpkg), "
    "ou rends Lua disponible à CMake via CMAKE_PREFIX_PATH.")
endif()

if (COMMON_LIBS_INSTALL)
    include(GNUInstallDirs)

    install(TARGETS common_ecs common_net common_engine
        EXPORT common_libs_targets
    )

    install(DIRECTORY ecs/include/    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY net/include/    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY engine/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT common_libs_targets
        NAMESPACE common::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/common_libs
    )
endif()

# -----------------------------------------------------------------------------
# Windows-specific configuration for common_net
#
# When this module is consumed on Windows (or cross-compiled via MinGW), we
# expose the Winsock dependency transitively and define a minimum supported
# Windows version.  These settings align with the r-type top-level build
# definitions to ensure consistent behaviour across platforms.
if (WIN32)
    target_link_libraries(common_net INTERFACE ws2_32)
    target_compile_definitions(common_net INTERFACE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
    )
endif()
