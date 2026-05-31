# etrm — the client (single monolithic binary; renderer statically linked, as
# vanilla ET). PHASE 1 STRATEGY: link on ET's proven NATIVE Win32 platform first
# (win_glimp/WGL, win_input/DirectInput, win_snd/DirectSound, win_qgl), fixing
# only 64-bit issues — same low-risk path that got etrmded running. The SDL2
# swap (src/sys) then replaces glimp/input/sound behind the same interface,
# validated against this known-working baseline (ETe's native+SDL model).
#
# Curl HTTP download is deferred (dl_main_stubs.c); renderer2 is Phase 7.

if(NOT BUILD_CLIENT)
    return()
endif()

if(NOT WIN32)
    message(STATUS "etrm: non-Windows client not wired yet (Phase 2 / SDL layer)")
    return()
endif()

set(CLIENT_PLATFORM_SOURCES
    # native core (shared with dedicated, compiled here WITHOUT -DDEDICATED)
    ${ETRM_SRC}/win32/win_main.c
    ${ETRM_SRC}/win32/win_net.c
    ${ETRM_SRC}/win32/win_shared.c
    ${ETRM_SRC}/win32/win_wndproc.c
    ${ETRM_SRC}/win32/win_syscon.c
    # client-only native I/O (the SDL2 swap targets these five)
    ${ETRM_SRC}/win32/win_input.c    # DirectInput
    ${ETRM_SRC}/win32/win_glimp.c    # WGL context
    ${ETRM_SRC}/win32/win_qgl.c      # GL proc table
    ${ETRM_SRC}/win32/win_gamma.c
    ${ETRM_SRC}/win32/win_snd.c      # DirectSound
    ${ETRM_SRC}/qcommon/dl_main_stubs.c)

add_executable(etrm
    ${ETRM_CORE_SOURCES}
    ${CLIENT_PLATFORM_SOURCES})

set_target_properties(etrm PROPERTIES WIN32_EXECUTABLE ON)  # WinMain

target_include_directories(etrm PRIVATE
    ${ETRM_CORE_INCLUDE_DIRS}
    ${ETRM_SRC}/win32)

target_link_libraries(etrm PRIVATE
    etrm_renderer
    etrm_client
    etrm_jpeg
    etrm_botlib
    etrm_splines
    opengl32 gdi32 user32 winmm wsock32 ws2_32 iphlpapi
    ole32 advapi32 dinput8 dsound dxguid comctl32)

etrm_apply_common_definitions(etrm)
