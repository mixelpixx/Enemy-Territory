# client — clientside game/render glue, console, input dispatch, cinematics,
# and the software sound mixer. Source list matches SConscript.core's
# client_string. The DMA sound backend (SNDDMA_*) is supplied by the platform
# layer (SDL on the modern path); snd_mix.c's x86 asm is excluded on x64 (C path).

set(CLIENT_SOURCES
    cl_cgame.c   cl_cin.c     cl_console.c cl_input.c   cl_keys.c
    cl_main.c    cl_net_chan.c cl_parse.c  cl_scrn.c    cl_ui.c
    snd_adpcm.c  snd_dma.c    snd_mem.c    snd_mix.c    snd_wavelet.c)

add_library(etrm_client STATIC ${CLIENT_SOURCES})

target_include_directories(etrm_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRM_SRC}/qcommon
    ${ETRM_SRC}/game
    ${ETRM_SRC}/renderer)

etrm_apply_common_definitions(etrm_client)
