# qagame — the server game module, built as a native 64-bit DLL
# (qagame_mp_x86_64.dll) loaded by the engine via Sys_LoadDll/VMI_NATIVE.
#
# Source = all of game/ (the qagame_string in SConscript.game: bg_*, g_*,
# q_shared/q_math, g_syscalls.c) + the bot AI in botai/. Exports vmMain and
# dllEntry via game/game.def. Output sits next to the binaries so the engine's
# final LoadLibrary(filename) fallback finds it.

if(NOT WIN32)
    message(STATUS "qagame: non-Windows module build not wired yet (Phase 2)")
    return()
endif()

file(GLOB QAGAME_GAME_SOURCES  "${ETRM_SRC}/game/*.c")
file(GLOB QAGAME_BOTAI_SOURCES "${ETRM_SRC}/botai/*.c")

add_library(qagame SHARED ${QAGAME_GAME_SOURCES} ${QAGAME_BOTAI_SOURCES})

set_target_properties(qagame PROPERTIES
    PREFIX ""
    OUTPUT_NAME "qagame_mp_x86_64"
    LINK_FLAGS "/DEF:\"${ETRM_SRC}/game/game.def\"")

target_compile_definitions(qagame PRIVATE GAMEDLL)   # SConscript.game define

target_include_directories(qagame PRIVATE
    ${ETRM_SRC}/game
    ${ETRM_SRC}/botai
    ${ETRM_SRC}/qcommon)

etrm_apply_common_definitions(qagame)
