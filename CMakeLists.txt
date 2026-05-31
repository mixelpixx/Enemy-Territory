# =============================================================================
#  Enemy Territory RM  —  CMake build
#
#  Phase 1 goal: build the UNMODIFIED 2.60b engine, 64-bit, with the portable
#  QVM interpreter and an SDL2 platform layer, on a modern toolchain.
#
#  This file is grown incrementally (every commit must configure + compile):
#    [done]  bundled third-party libs: jpeg, botlib, splines
#    [todo]  engine core (qcommon + server + client + renderer)  -> etrm / etrmded
#    [todo]  src/sys SDL2 platform layer (ported from ioquake3, GPLv2)
#
#  The game/cgame/ui modules are NOT built here — vanilla ET ships them as QVM
#  bytecode inside the retail paks; the engine runs those through the
#  interpreter.  Native modules come in Phase 6.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(ETRM
    VERSION 0.1.0
    DESCRIPTION "Wolfenstein: Enemy Territory RM remaster"
    LANGUAGES C CXX)

# ----------------------------------------------------------------------------
#  Hard requirements
# ----------------------------------------------------------------------------
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "ET-RM is 64-bit only. Configure with a 64-bit toolchain "
        "(e.g. -A x64, or the Ninja generator from an x64 dev shell).")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)   # 2005-era C; do not force-fail on pre-C11 idioms
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

# Single, predictable output location for binaries + the SDL2 runtime DLL.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ----------------------------------------------------------------------------
#  Options  (USE_INTERNAL_* default ON in Phase 1; Phase 4 wires system libs)
# ----------------------------------------------------------------------------
option(USE_INTERNAL_JPEG     "Use the bundled libjpeg-6b"   ON)
option(USE_INTERNAL_FREETYPE "Use the bundled FreeType 2"   ON)
option(BUILD_CLIENT          "Build the client (etrm)"      ON)
option(BUILD_SERVER          "Build the dedicated server (etrmded)" ON)

include(CompilerWarnings)

set(ETRM_SRC "${CMAKE_SOURCE_DIR}/src")

# ----------------------------------------------------------------------------
#  Bundled third-party libraries (self-contained; no engine/platform deps)
# ----------------------------------------------------------------------------
add_subdirectory(src/jpeg-6)     # -> etrm_jpeg   (static)  [USE_INTERNAL_JPEG]
add_subdirectory(src/botlib)     # -> etrm_botlib (static, -DBOTLIB)
add_subdirectory(src/splines)    # -> etrm_splines(static, C++)

# ----------------------------------------------------------------------------
#  Engine targets  — added in subsequent Phase 1 commits.
# ----------------------------------------------------------------------------
# add_subdirectory(src/sys)      # SDL2 platform layer (todo)
# the etrm / etrmded executables (todo)

message(STATUS "ET-RM configured: ${CMAKE_SYSTEM_NAME} / ${CMAKE_C_COMPILER_ID} / "
               "${CMAKE_SIZEOF_VOID_P}*8-bit / build=${CMAKE_BUILD_TYPE}")
