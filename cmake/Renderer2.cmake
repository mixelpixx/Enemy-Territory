# etrm_renderer2 — the opt-in modern renderer (ET:Legacy v2.84.0 renderer2,
# GLSL / GL 3.3 core), built as a DLL loaded at runtime by the cl_renderer
# dispatcher (exe-dir only).
#
# R2-2 / Task 3 (this file): the BRIDGE wires our engine (REF_API_VERSION 9) to
# the vendored ET:Legacy renderer2 (their v10 interface). The DLL now exports the
# bridge's GetRefAPI (no longer the R2-1 stub). Include isolation: the two header
# worlds (ours / theirs) define identically-named types, so the bridge is split
# into per-world TUs that never include each other's headers, joined by a neutral
# contract (bridge/tr2_bridge.h, primitive types only).
#
# Composition:
#   * shdr2h            — host tool that embeds the GLSL shaders into shaders.h
#   * etrm_glew         — vendored GLEW (static, GLEW_STATIC), GL entry points
#   * etrm_zlib         — zlib (FetchContent, pinned) for tr_public.h + zlib_*
#   * etrm_renderer2_core (STATIC) — ALL vendored renderer2 + renderercommon .c
#                          (minus tr_image_png.c / tr_image_jpg.c, excluded+stubbed)
#                          + the THEIRS-side bridge TUs (their header world):
#                          tr2_bridge_theirs.c, tr2_layout_theirs.c,
#                          tr2_engine_stubs.c, tr2_png_stub.c, tr2_jpg_stub.c.
#   * etrm_renderer2    (SHARED) — the OURS-side bridge TUs (our header world):
#                          tr2_bridge_ours.c (exported GetRefAPI), tr2_layout_ours.c,
#                          tr2_layout_check.c (neutral) — linking the core lib.
#
# EDIT-RULE NOTE: their tr_init.c defines GetRefAPI, which would collide with the
# stub's exported GetRefAPI. We rename THEIRS to ETL_GetRefAPI via a per-file
# compile definition (-DGetRefAPI=ETL_GetRefAPI) — a build-system rename, NOT a
# source edit — so the stub's symbol stays the one exported by the .def while
# their entry remains reachable internally for Task 3.
if(NOT WIN32 OR NOT BUILD_CLIENT)
    return()
endif()

set(R2_DIR    "${ETRM_SRC}/renderer2")
set(R2_ETL    "${R2_DIR}/etl")
set(R2_COMMON "${R2_DIR}/etl/common")
set(R2_HDR    "${R2_DIR}/etlhdr")
set(R2_GEN    "${CMAKE_BINARY_DIR}/r2include")   # generated shaders.h lands here

# ----------------------------------------------------------------------------
#  Third-party deps — GLEW (GL loader) + zlib (tr_public.h includes <zlib.h>
#  unconditionally and the refimport carries zlib_compress/zlib_crc32). Both are
#  pulled with FetchContent at pinned tags (same mechanism as SDL2). GLEW is
#  fetched as the pre-amalgamated SOURCE release (contains generated glew.c +
#  GL/glew.h) since the GLEW git repo ships no generated sources. THIRDPARTY
#  rows are logged in docs/THIRDPARTY.md.
# ----------------------------------------------------------------------------
include(FetchContent)

# --- zlib ---------------------------------------------------------------------
# Keep zlib's example/minigzip targets (and their ctest registration) out of our
# build so `ctest` stays our two tests (pmove_feel, fov_math). ZLIB_BUILD_EXAMPLES
# is honored by zlib >= 1.3; the explicit examples-dir guard covers older trees.
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    etrm_zlib_src
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1        # pinned; bump deliberately
    GIT_SHALLOW    TRUE)
message(STATUS "ET-RM: fetching zlib (v1.3.1) for renderer2 ...")
FetchContent_MakeAvailable(etrm_zlib_src)
# zlib's CMake generates zconf.h into its build dir and exposes the static lib
# target `zlibstatic`. Its include dirs (source + build) carry zlib.h/zconf.h.

# --- GLEW ---------------------------------------------------------------------
FetchContent_Declare(
    etrm_glew_src
    URL      https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip
    URL_HASH SHA256=a9046a913774395a095edcc0b0ac2d81c3aacca61787b39839b941e9be14e0d4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
message(STATUS "ET-RM: fetching GLEW (2.2.0 source release) for renderer2 ...")
FetchContent_MakeAvailable(etrm_glew_src)

# Build GLEW ourselves: the source release is plain glew.c + headers, no usable
# CMake target for our needs. Static, GLEW_STATIC, no GLU.
add_library(etrm_glew STATIC "${etrm_glew_src_SOURCE_DIR}/src/glew.c")
target_include_directories(etrm_glew PUBLIC "${etrm_glew_src_SOURCE_DIR}/include")
target_compile_definitions(etrm_glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
target_link_libraries(etrm_glew PUBLIC opengl32)
set_target_properties(etrm_glew PROPERTIES FOLDER "renderer2/thirdparty")

# ----------------------------------------------------------------------------
#  shdr2h — host tool: embeds the GLSL shaders + gldef into a C header.
#  C++; depends on vendor/tinydir.h (vendored). Mirrors ET:Legacy's
#  ETLBuildRenderer.cmake invocation: shdr2h LEGACY <glsl dir> <gldef> <out>.
# ----------------------------------------------------------------------------
add_executable(shdr2h "${R2_DIR}/glsl/shdr/shaders2h.cpp")
set_target_properties(shdr2h PROPERTIES FOLDER "renderer2/tools")
if(MSVC)
    target_compile_definitions(shdr2h PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

file(MAKE_DIRECTORY "${R2_GEN}")
add_custom_command(
    OUTPUT
        "${R2_GEN}/__shaders.h"          # fake output: forces the command to run
        "${R2_GEN}/shaders.h"
    COMMAND shdr2h LEGACY
        "${R2_DIR}/glsl"
        "${R2_DIR}/gldef/default.gldef"
        "${R2_GEN}/shaders.h"
    DEPENDS shdr2h
    COMMENT "renderer2: generating GLSL include r2include/shaders.h"
    VERBATIM)
add_custom_target(etrm_r2_shaders DEPENDS "${R2_GEN}/__shaders.h")
set_target_properties(etrm_r2_shaders PROPERTIES FOLDER "renderer2/tools")

# ----------------------------------------------------------------------------
#  Vendored source list — every renderer2 + renderercommon .c EXCEPT the two
#  loaders we deliberately exclude+stub for this task (see bridge/tr2_*_stub.c
#  for the rationale). PNG depends on engine puff.c; JPEG wants libjpeg 8+
#  (jpeg_mem_src) which our bundled jpeg-6b lacks.
# ----------------------------------------------------------------------------
file(GLOB R2_ETL_SRC    "${R2_ETL}/*.c")
file(GLOB R2_COMMON_SRC "${R2_COMMON}/*.c")
list(REMOVE_ITEM R2_COMMON_SRC
    "${R2_COMMON}/tr_image_png.c"
    "${R2_COMMON}/tr_image_jpg.c")

# ET:Legacy's shared q_math.c / q_shared.c — the math (vec3/mat4/quat/Matrix*/
# color tables) and parser/string utilities (COM_Parse*, Q_str*, va, Info_*)
# that the renderer tree calls. They are part of ET:Legacy's shared layer the
# renderer compiles+links against (the engine provides them in their build);
# vendored verbatim into etlsrc/ to close the link. They include only
# "q_shared.h" — no engine deps beyond Com_Printf/Com_Error, which the bridge
# supplies in Task 3 (precursor stubs in bridge/tr2_engine_stubs.c for now).
set(R2_QSHARED_SRC
    "${R2_DIR}/etlsrc/qcommon/q_math.c"
    "${R2_DIR}/etlsrc/qcommon/q_shared.c")

# Bridge TUs that see THEIR header world (etlhdr/) live in the CORE lib so they
# compile with the vendored include dirs + defines (FEATURE_RENDERER2 etc.).
# Include isolation: these must NEVER see our src/renderer or src/cgame headers.
#   tr2_bridge_theirs.c — their refimport build + refexport wrap + cvar proxies
#   tr2_layout_theirs.c — their-world layout table
#   tr2_engine_stubs.c  — Com_Printf/Error/DPrintf/BlockChecksum (their q_shared)
#   tr2_png_stub.c / tr2_jpg_stub.c — image-loader stubs (their tr_common/tr_local)
set(R2_BRIDGE_THEIRS_SRC
    "${R2_DIR}/bridge/tr2_bridge_theirs.c"
    "${R2_DIR}/bridge/tr2_layout_theirs.c"
    "${R2_DIR}/bridge/tr2_engine_stubs.c"
    "${R2_DIR}/bridge/tr2_png_stub.c"
    "${R2_DIR}/bridge/tr2_jpg_stub.c")

set(R2_CORE_SRC
    ${R2_ETL_SRC}
    ${R2_COMMON_SRC}
    ${R2_QSHARED_SRC}
    ${R2_BRIDGE_THEIRS_SRC})

# ----------------------------------------------------------------------------
#  Include-dir resolution for the vendored tree.
#
#  Upstream layout (preserved in etlhdr/): renderer2 .c files include
#  "../qcommon/x.h", "../renderercommon/x.h", and (transitively) "../game/x.h".
#  These resolve via the quote-include fallback "<Idir>/../<seg>/x.h": adding
#  -I etlhdr/qcommon makes "../qcommon/x" -> etlhdr/qcommon/x,
#  "../renderercommon/x" -> etlhdr/renderercommon/x, "../game/x" -> etlhdr/game/x.
#  We add all three subdirs (plus etlhdr root) so every "../seg/" shape resolves
#  regardless of which file includes it. etl/ and etl/common/ cover the sibling
#  ("tr_local.h") and same-dir includes. r2include carries <shaders.h>.
# ----------------------------------------------------------------------------
set(R2_INCLUDE_DIRS
    "${R2_ETL}"
    "${R2_COMMON}"
    "${R2_HDR}"
    "${R2_HDR}/qcommon"
    "${R2_HDR}/renderercommon"
    "${R2_HDR}/game"
    "${R2_GEN}"
    "${R2_DIR}/bridge")   # neutral tr2_bridge.h (theirs-side bridge TUs)

set(R2_DEFINES
    FEATURE_RENDERER2
    USE_REFENTITY_ANIMATIONSYSTEM=1
    # C_ONLY: select the portable-C paths over the 32-bit x86 inline-asm ones
    # (q_math.c BoxOnPlaneSide, tr_surface.c, q_platform.h endianness). The asm
    # blocks are `__declspec(naked)` MASM that does not assemble under MSVC x64;
    # ET:Legacy's own build defines C_ONLY on non-x86 for exactly this reason.
    C_ONLY)

# ----------------------------------------------------------------------------
#  etrm_renderer2_core — the vendored tree compiled to a static lib.
# ----------------------------------------------------------------------------
add_library(etrm_renderer2_core STATIC ${R2_CORE_SRC})
add_dependencies(etrm_renderer2_core etrm_r2_shaders)

target_include_directories(etrm_renderer2_core PUBLIC ${R2_INCLUDE_DIRS})
target_compile_definitions(etrm_renderer2_core PUBLIC ${R2_DEFINES})
target_link_libraries(etrm_renderer2_core PUBLIC
    etrm_glew
    zlibstatic
    opengl32)
set_target_properties(etrm_renderer2_core PROPERTIES FOLDER "renderer2")

# zlib's headers (source + generated zconf.h build dir).
target_include_directories(etrm_renderer2_core PUBLIC
    "${etrm_zlib_src_SOURCE_DIR}"
    "${etrm_zlib_src_BINARY_DIR}")

# EDIT-RULE rename: their tr_init.c::GetRefAPI -> ETL_GetRefAPI (build-system
# rename, not a source edit) so it does not collide with the stub's exported
# GetRefAPI. Reachable internally as ETL_GetRefAPI for Task 3.
set_source_files_properties("${R2_ETL}/tr_init.c"
    TARGET_DIRECTORY etrm_renderer2_core
    PROPERTIES COMPILE_DEFINITIONS "GetRefAPI=ETL_GetRefAPI")

# RELAXED warnings: this is GCC-targeted vendored code; do not let /W4 noise
# block the bring-up. (Errors still fail the build.)
if(MSVC)
    target_compile_definitions(etrm_renderer2_core PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
        WIN32
        _WINDOWS)
    target_compile_options(etrm_renderer2_core PRIVATE
        /W0
        /wd4244 /wd4267 /wd4996 /wd4101 /wd4018 /wd4133 /wd4047)
endif()

# ----------------------------------------------------------------------------
#  etrm_renderer2 — the DLL. The R2-1 stub (tr2_stub.c) is REPLACED by the
#  bridge's OURS-side TUs, which see only OUR engine headers (include isolation;
#  the THEIRS-side TUs live in the core lib above). The DLL still exports only
#  GetRefAPI (now the bridge's), via renderer2.def.
#    tr2_bridge_ours.c   — exported GetRefAPI, BrdgOur_* callbacks, our refexport
#    tr2_layout_ours.c   — our-world layout table
#    tr2_layout_check.c  — neutral checker (only tr2_bridge.h; no header world)
# ----------------------------------------------------------------------------
add_library(etrm_renderer2 SHARED
    "${R2_DIR}/bridge/tr2_bridge_ours.c"
    "${R2_DIR}/bridge/tr2_layout_ours.c"
    "${R2_DIR}/bridge/tr2_layout_check.c")

target_include_directories(etrm_renderer2 PRIVATE
    ${ETRM_SRC}/renderer
    ${ETRM_SRC}/qcommon
    ${ETRM_SRC}/game
    "${R2_DIR}/bridge")

# Pull in the whole vendored archive. /WHOLEARCHIVE keeps their GetRefAPI (now
# ETL_GetRefAPI) and all RE_* entry points alive in the DLL for Task 3 even
# though tr2_stub.c references none of them yet.
target_link_libraries(etrm_renderer2 PRIVATE etrm_renderer2_core)
set_target_properties(etrm_renderer2 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "etrm_renderer2"
    LINK_FLAGS "/DEF:\"${R2_DIR}/renderer2.def\" /WHOLEARCHIVE:etrm_renderer2_core")

etrm_apply_common_definitions(etrm_renderer2)

# ----------------------------------------------------------------------------
#  etrm_r2bind — bind harness (Task 3 exit-bar evidence + regression test).
#
#  Stands in for the engine's CL_InitRef dlopen path when retail assets are
#  absent: LoadLibrary etrm_renderer2.dll, resolve GetRefAPI, call it with a
#  mock refimport, assert a non-NULL populated refexport comes back (= layout
#  check passed + the boundary is wired). No GL context (that is Task 4).
# ----------------------------------------------------------------------------
add_executable(etrm_r2bind "${R2_DIR}/bridge/tr2_bind_harness.c")
add_dependencies(etrm_r2bind etrm_renderer2)
set_target_properties(etrm_r2bind PROPERTIES FOLDER "renderer2/tools")
if(MSVC)
    target_compile_definitions(etrm_r2bind PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_test(NAME r2_bridge_bind
    COMMAND etrm_r2bind "$<TARGET_FILE:etrm_renderer2>"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:etrm_renderer2>")
