# ShowcaseMap.cmake — package the R2-6 renderer showcase into rm_showcase.pk3 on build.
#
# rm_showcase is project-original authored content (no retail art) that demonstrates
# the renderer2 mapper hooks on the modern renderer (cl_renderer gl2):
#   - sun / world shadows  (a parallel sun light -> RL_DIRECTIONAL cascaded shadows)
#   - per-pixel materials   (normal / specular / parallax on the three display panels)
#   - deluxe lightmaps       (compiled with q3map2 -deluxe)
#
# Built exactly like the other RM paks (zz_rm_ui.pk3 / rm_bin.pk3): a ZIP via the
# portable `cmake -E tar --format=zip`, deposited in build/bin/etmain. ET loads
# etmain/*.pk3, so it ships next to the retail data; `devmap rm_showcase` under gl2
# shows the features. The compiled .bsp is committed (rm_showcase/maps/), so building
# and playing need no map compiler; q3map2 is only required to re-author the map
# (see rm_showcase/tools/ and docs/MAPPER-HOOKS.md).
#
# WORKING_DIRECTORY = rm_showcase/ so entries store with the right internal paths
# (maps/rm_showcase.bsp, textures/rm_showcase/..., scripts/rm_showcase.shader).

if(NOT BUILD_CLIENT)
    return()
endif()

set(RM_SHOWCASE_SRC_DIR     "${CMAKE_SOURCE_DIR}/rm_showcase")
set(RM_SHOWCASE_PAK_OUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/etmain")
set(RM_SHOWCASE_PAK         "${RM_SHOWCASE_PAK_OUT_DIR}/rm_showcase.pk3")

# Entries to store, relative to rm_showcase/ (these become the pak's internal paths).
set(RM_SHOWCASE_ENTRIES
    maps/rm_showcase.bsp
    scripts/rm_showcase.shader
    textures/rm_showcase/rivet_d.tga
    textures/rm_showcase/rivet_n.tga
    textures/rm_showcase/rivet_s.tga
    textures/rm_showcase/block_d.tga
    textures/rm_showcase/block_n.tga
    textures/rm_showcase/block_s.tga
    textures/rm_showcase/stud_d.tga
    textures/rm_showcase/stud_n.tga
    textures/rm_showcase/stud_s.tga)

# Re-pack when any shipped showcase file changes (the .map source / tools are not packed).
file(GLOB_RECURSE RM_SHOWCASE_SOURCES CONFIGURE_DEPENDS
    "${RM_SHOWCASE_SRC_DIR}/maps/*.bsp"
    "${RM_SHOWCASE_SRC_DIR}/scripts/*"
    "${RM_SHOWCASE_SRC_DIR}/textures/*")

add_custom_command(
    OUTPUT  "${RM_SHOWCASE_PAK}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RM_SHOWCASE_PAK_OUT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${RM_SHOWCASE_PAK}" --format=zip ${RM_SHOWCASE_ENTRIES}
    DEPENDS ${RM_SHOWCASE_SOURCES}
    WORKING_DIRECTORY "${RM_SHOWCASE_SRC_DIR}"
    COMMENT "Packing RM showcase map -> ${RM_SHOWCASE_PAK}"
    VERBATIM)

add_custom_target(rm_showcase ALL DEPENDS "${RM_SHOWCASE_PAK}")
