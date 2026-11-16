cmake_minimum_required(VERSION 3.16)
project(porymap_playtest_boot C CXX)

set(MGBA_SOURCE_DIR "" CACHE PATH "Path to an mGBA source checkout")
if(NOT EXISTS "${MGBA_SOURCE_DIR}/include/mgba/core/core.h")
    message(FATAL_ERROR "Set MGBA_SOURCE_DIR to an mGBA source checkout")
endif()

# A headless core only; this does not build or install an emulator frontend.
foreach(option BUILD_QT BUILD_SDL BUILD_GL BUILD_GLES2 BUILD_GLES3 BUILD_SHARED
        BUILD_LIBRETRO BUILD_TEST BUILD_SUITE BUILD_LTO ENABLE_SCRIPTING M_CORE_GB
        USE_FFMPEG USE_LIBZIP USE_SQLITE3 USE_ELF USE_LZMA USE_PNG USE_LUA USE_JSON_C)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory("${MGBA_SOURCE_DIR}" mgba EXCLUDE_FROM_ALL)
add_executable(playtest_boot ../playtest_boot.c)
set_property(TARGET playtest_boot PROPERTY C_STANDARD 11)
target_link_libraries(playtest_boot PRIVATE mgba)
# Struct mCore depends on feature flags. Match the core's exact ABI, including
# directory support, rather than relying on a partially generated flags.h.
get_target_property(mgba_definitions mgba COMPILE_DEFINITIONS)
get_target_property(mgba_includes mgba INCLUDE_DIRECTORIES)
target_compile_definitions(playtest_boot PRIVATE ${mgba_definitions})
target_include_directories(playtest_boot PRIVATE ${mgba_includes})
