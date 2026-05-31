# etrmded — the dedicated server.
#
# No SDL / renderer / sound: a minimal native platform layer plus null client/
# input/sound stubs. This is the fastest path to a runnable artifact and is what
# the Phase 3 feel-harness drives. The core is compiled here WITH -DDEDICATED.
#
# Windows-first (Phase 1). Linux/macOS dedicated come in Phase 2 via src/unix.

if(NOT BUILD_SERVER)
    return()
endif()

if(WIN32)
    set(DED_PLATFORM_SOURCES
        ${ETRM_SRC}/win32/win_main.c
        ${ETRM_SRC}/win32/win_net.c
        ${ETRM_SRC}/win32/win_shared.c
        ${ETRM_SRC}/win32/win_wndproc.c
        ${ETRM_SRC}/win32/win_syscon.c)
    set(DED_PLATFORM_LIBS
        winmm wsock32 ws2_32 iphlpapi gdi32 user32 ole32 advapi32 shell32)
else()
    message(STATUS "etrmded: non-Windows platform layer not wired yet (Phase 2)")
    return()
endif()

set(DED_NULL_SOURCES
    ${ETRM_SRC}/null/null_client.c
    ${ETRM_SRC}/null/null_input.c
    ${ETRM_SRC}/null/null_snddma.c
    ${ETRM_SRC}/qcommon/dl_main_stubs.c)

add_executable(etrmded
    ${ETRM_CORE_SOURCES}
    ${DED_PLATFORM_SOURCES}
    ${DED_NULL_SOURCES})

set_target_properties(etrmded PROPERTIES WIN32_EXECUTABLE ON)  # entry: WinMain + syscon window

target_compile_definitions(etrmded PRIVATE DEDICATED)
target_include_directories(etrmded PRIVATE
    ${ETRM_CORE_INCLUDE_DIRS}
    ${ETRM_SRC}/win32)

target_link_libraries(etrmded PRIVATE
    etrm_botlib
    etrm_splines
    ${DED_PLATFORM_LIBS})

etrm_apply_common_definitions(etrmded)
