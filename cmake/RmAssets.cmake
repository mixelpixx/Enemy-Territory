# RmAssets.cmake — package the RM UI override assets into zz_rm_ui.pk3 on build.
#
# The rm/ tree holds our override resources (currently a single FOV-slider menu
# at rm/ui/options_customise_game.menu). ET loads etmain/*.pk3 with precedence to
# alphabetically-later names, so a pak named "zz_rm_ui.pk3" wins over the retail
# pak0.pk3 without touching the user's retail data. scripts/play.bat points
# fs_homepath at build/bin, so we drop the pak in build/bin/etmain.
#
# A pk3 is just a ZIP. cmake -E tar with --format=zip is fully portable (no
# PowerShell), and using WORKING_DIRECTORY = rm/ stores the entries with the
# desired internal path (ui/options_customise_game.menu, NOT rm/ui/...).
#
# scripts/pack_rm_assets.ps1 remains as a convenience wrapper; the build no
# longer depends on it.

if(NOT BUILD_CLIENT)
    return()
endif()

set(RM_ASSET_SRC_DIR "${CMAKE_SOURCE_DIR}/rm")
set(RM_PAK_OUT_DIR   "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/etmain")
set(RM_PAK           "${RM_PAK_OUT_DIR}/zz_rm_ui.pk3")

# Entries to store in the pak, as paths relative to rm/ (these become the pak's
# internal paths). Listing the known files keeps stray files out of the pak.
set(RM_PAK_ENTRIES
    ui/options_customise_game.menu)

# DEPENDS list: glob the rm/ source so adding menu files later just works.
file(GLOB_RECURSE RM_ASSET_SOURCES CONFIGURE_DEPENDS "${RM_ASSET_SRC_DIR}/*")

add_custom_command(
    OUTPUT  "${RM_PAK}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RM_PAK_OUT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${RM_PAK}" --format=zip ${RM_PAK_ENTRIES}
    DEPENDS ${RM_ASSET_SOURCES}
    WORKING_DIRECTORY "${RM_ASSET_SRC_DIR}"
    COMMENT "Packing RM UI assets -> ${RM_PAK}"
    VERBATIM)

add_custom_target(rm_assets ALL DEPENDS "${RM_PAK}")
