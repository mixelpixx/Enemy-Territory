# etrm_renderer2 — the opt-in modern renderer, built as a DLL loaded at runtime
# by the cl_renderer dispatcher (exe-dir only). R2-1 ships a STUB that proves
# the load/fallback path; the ET:Legacy-based GLSL renderer replaces it in R2-2.
if(NOT WIN32 OR NOT BUILD_CLIENT)
    return()
endif()

add_library(etrm_renderer2 SHARED
    ${ETRM_SRC}/renderer2/tr2_stub.c)

set_target_properties(etrm_renderer2 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "etrm_renderer2"
    LINK_FLAGS "/DEF:\"${ETRM_SRC}/renderer2/renderer2.def\"")

target_include_directories(etrm_renderer2 PRIVATE
    ${ETRM_SRC}/renderer
    ${ETRM_SRC}/qcommon
    ${ETRM_SRC}/game)

etrm_apply_common_definitions(etrm_renderer2)
