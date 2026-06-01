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
    # client-only native I/O
    ${ETRM_SRC}/win32/win_snd.c      # DirectSound
    # SDL2 video + input platform layer — replaces win_glimp/win_qgl/win_gamma
    # and win_input (DirectInput); Task B (video) + Task C (input/raw mouse)
    ${ETRM_SRC}/sys/sdl_glimp.c      # SDL2 window + GL context
    ${ETRM_SRC}/sys/sdl_qgl.c        # GL proc table via SDL_GL_GetProcAddress
    ${ETRM_SRC}/sys/sdl_input.c      # SDL2 keyboard + mouse (raw relative mouse)
    ${ETRM_SRC}/qcommon/dl_main_stubs.c)

add_executable(etrm
    ${ETRM_CORE_SOURCES}
    ${CLIENT_PLATFORM_SOURCES})

set_target_properties(etrm PROPERTIES WIN32_EXECUTABLE ON)  # WinMain

target_include_directories(etrm PRIVATE
    ${ETRM_CORE_INCLUDE_DIRS}
    ${ETRM_SRC}/win32
    ${ETRM_SRC}/sys)

target_link_libraries(etrm PRIVATE
    etrm_renderer
    etrm_client
    etrm_jpeg
    etrm_botlib
    etrm_splines
    SDL2::SDL2
    opengl32 gdi32 user32 winmm wsock32 ws2_32 iphlpapi
    ole32 advapi32 dsound comctl32)

# ET has its own WinMain (win_main.c, WIN32_EXECUTABLE). Tell SDL not to
# define/hook main — do NOT link SDL2main, or WinMain would conflict. We call
# SDL_SetMainReady() in GLimp_Init instead.
target_compile_definitions(etrm PRIVATE SDL_MAIN_HANDLED)

etrm_apply_common_definitions(etrm)

# Stage the SDL2 runtime DLL next to the executable (SDL2d.dll in Debug — the
# generator expression resolves the right name automatically).
add_custom_command(TARGET etrm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:etrm>)
