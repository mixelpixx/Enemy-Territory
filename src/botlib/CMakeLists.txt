# botlib — Quake3 Area Awareness System + bot library. Built with -DBOTLIB,
# matching SConscript.core (botlib_env adds the BOTLIB define).

set(BOTLIB_SOURCES
    be_aas_bspq3.c be_aas_cluster.c be_aas_debug.c   be_aas_entity.c
    be_aas_file.c  be_aas_main.c    be_aas_move.c    be_aas_optimize.c
    be_aas_reach.c be_aas_route.c   be_aas_routealt.c be_aas_routetable.c
    be_aas_sample.c
    be_ai_char.c   be_ai_chat.c     be_ai_gen.c      be_ai_goal.c
    be_ai_move.c   be_ai_weap.c     be_ai_weight.c
    be_ea.c        be_interface.c
    l_crc.c        l_libvar.c       l_log.c          l_memory.c
    l_precomp.c    l_script.c       l_struct.c)

add_library(etrm_botlib STATIC ${BOTLIB_SOURCES})
target_compile_definitions(etrm_botlib PRIVATE BOTLIB)
# botlib pulls game/ headers (q_shared.h, surfaceflags.h, botlib.h) and its own.
target_include_directories(etrm_botlib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRM_SRC}/game)
etrm_apply_common_definitions(etrm_botlib)
