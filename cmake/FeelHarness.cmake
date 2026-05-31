# Feel-preservation harness (Phase 3): standalone pmove determinism test.
#
# Links the REAL shared movement code (bg_pmove + bg_slidemove + the bg_* layer)
# against a tiny driver that runs a fixed usercmd script on a flat world and
# prints a hash of the per-frame player state. A golden hash is captured from a
# known-good (feel-validated) build; CI compares against it so any change that
# alters the movement feel is caught.
#
# Compiled WITHOUT CGAMEDLL/GAMEDLL so bg_pmove.c uses the plain shared headers;
# PM_GameType is supplied here (movement is gametype-independent).

if(NOT BUILD_FEEL_HARNESS)
    return()
endif()

# The anim layer (bg_animation/animgroup/character/sscript) is STUBBED in
# feel_stubs.c: it only sets legs/torso anim state, which does not affect the
# movement physics (origin/velocity) we hash — and it needs loaded character
# data we don't have. Keeping it out makes the harness robust and movement-pure.
set(FEEL_BG_SOURCES
    ${GM_DIR}/bg_pmove.c    ${GM_DIR}/bg_slidemove.c ${GM_DIR}/bg_misc.c
    ${GM_DIR}/bg_classes.c  ${GM_DIR}/bg_stats.c
    ${GM_DIR}/q_math.c      ${GM_DIR}/q_shared.c)

add_executable(etrm_feeltest
    ${CMAKE_SOURCE_DIR}/tests/feel_pmove.c
    ${CMAKE_SOURCE_DIR}/tests/feel_stubs.c
    ${FEEL_BG_SOURCES})

# GAMEDLL path of bg_pmove.c uses plain shared headers (no g_local.h coupling)
# and keeps pmove_t layout consistent across all harness TUs. The g_* cvars it
# references are provided by feel_stubs.c.
target_compile_definitions(etrm_feeltest PRIVATE GAMEDLL)
target_include_directories(etrm_feeltest PRIVATE ${GM_DIR} ${ETRM_SRC}/qcommon)
etrm_apply_common_definitions(etrm_feeltest)

# Regression gate: the golden hash is captured from the feel-validated build
# (user confirmed "it all felt like it did back in the day"). Any change that
# alters the movement physics changes this hash -> ctest fails. Re-baseline
# deliberately (and only deliberately) when intentionally changing the sim, or
# when moving to a different toolchain/arch (the hash is toolchain-specific).
#
# To run:  ctest --test-dir build -R pmove_feel --output-on-failure
set(ETRM_PMOVE_GOLDEN "c1a6e7471a5e279d" CACHE STRING "Golden pmove feel hash (x64/MSVC)")
enable_testing()
add_test(NAME pmove_feel COMMAND etrm_feeltest)
set_tests_properties(pmove_feel PROPERTIES
    PASS_REGULAR_EXPRESSION "ETRM_PMOVE_FEEL_HASH ${ETRM_PMOVE_GOLDEN}"
    FAIL_REGULAR_EXPRESSION "Com_Error")
