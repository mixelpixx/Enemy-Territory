# cgame + ui — the client-side game modules, built as native 64-bit DLLs
# (cgame_mp_x86_64.dll, ui_mp_x86_64.dll) loaded by the client via Sys_LoadDll.
# Source lists + defines (CGAMEDLL / UIDLL) + .def exports match SConscript.cgame
# and SConscript.ui. Each module compiles its own copy of the shared bg_*/q_*
# code (and cgame also pulls ui/ui_shared.c), exactly as the SConscripts do.

if(NOT WIN32 OR NOT BUILD_CLIENT)
    return()
endif()

set(CG_DIR "${ETRM_SRC}/cgame")
set(UI_DIR "${ETRM_SRC}/ui")

# ---- cgame ----------------------------------------------------------------
file(GLOB CGAME_OWN_SOURCES "${CG_DIR}/*.c")
# cg_panelhandling.c is #included by another TU (not in SConscript.cgame's list);
# compiling it standalone duplicates symbols.
list(REMOVE_ITEM CGAME_OWN_SOURCES "${CG_DIR}/cg_panelhandling.c")
set(CGAME_SHARED_SOURCES
    ${GM_DIR}/bg_animation.c ${GM_DIR}/bg_animgroup.c ${GM_DIR}/bg_character.c
    ${GM_DIR}/bg_classes.c   ${GM_DIR}/bg_fov.c       ${GM_DIR}/bg_misc.c
    ${GM_DIR}/bg_pmove.c
    ${GM_DIR}/bg_slidemove.c ${GM_DIR}/bg_sscript.c   ${GM_DIR}/bg_stats.c
    ${GM_DIR}/bg_tracemap.c  ${GM_DIR}/q_math.c       ${GM_DIR}/q_shared.c
    ${UI_DIR}/ui_shared.c)

add_library(cgame SHARED ${CGAME_OWN_SOURCES} ${CGAME_SHARED_SOURCES})
set_target_properties(cgame PROPERTIES
    PREFIX "" OUTPUT_NAME "cgame_mp_x86_64"
    LINK_FLAGS "/DEF:\"${CG_DIR}/cgame.def\"")
target_compile_definitions(cgame PRIVATE CGAMEDLL)
target_include_directories(cgame PRIVATE ${CG_DIR} ${GM_DIR} ${UI_DIR} ${ETRM_SRC}/qcommon)
etrm_apply_common_definitions(cgame)

# ---- ui -------------------------------------------------------------------
file(GLOB UI_OWN_SOURCES "${UI_DIR}/*.c")
set(UI_SHARED_SOURCES
    ${GM_DIR}/bg_campaign.c ${GM_DIR}/bg_classes.c ${GM_DIR}/bg_misc.c
    ${GM_DIR}/q_math.c      ${GM_DIR}/q_shared.c)

add_library(ui SHARED ${UI_OWN_SOURCES} ${UI_SHARED_SOURCES})
set_target_properties(ui PROPERTIES
    PREFIX "" OUTPUT_NAME "ui_mp_x86_64"
    LINK_FLAGS "/DEF:\"${UI_DIR}/ui.def\"")
target_compile_definitions(ui PRIVATE UIDLL)
target_include_directories(ui PRIVATE ${UI_DIR} ${GM_DIR} ${ETRM_SRC}/qcommon)
etrm_apply_common_definitions(ui)
