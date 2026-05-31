# renderer1 — the original fixed-function OpenGL renderer.
#
# In vanilla ET this is statically linked into the engine binary (NOT a loadable
# DLL — that split waits for Phase 7's opt-in renderer2). Built here as a static
# lib so it can be compile-verified and then linked into the etrm client.
# Source list matches SConscript.core's renderer_string.

set(RENDERER_SOURCES
    tr_animation_mdm.c tr_animation_mds.c tr_backend.c   tr_bsp.c
    tr_cmds.c          tr_cmesh.c         tr_curve.c      tr_decals.c
    tr_flares.c        tr_font.c          tr_image.c      tr_init.c
    tr_light.c         tr_main.c          tr_marks.c      tr_mesh.c
    tr_model.c         tr_noise.c         tr_scene.c      tr_shade.c
    tr_shade_calc.c    tr_shader.c        tr_shadows.c    tr_sky.c
    tr_surface.c       tr_world.c)

add_library(etrm_renderer STATIC ${RENDERER_SOURCES})

target_include_directories(etrm_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRM_SRC}/qcommon
    ${ETRM_SRC}/game)
# tr_image.c decodes JPEG via the bundled libjpeg headers.
target_include_directories(etrm_renderer PRIVATE ${ETRM_SRC}/jpeg-6)

etrm_apply_common_definitions(etrm_renderer)
