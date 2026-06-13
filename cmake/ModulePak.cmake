# ModulePak.cmake — package the client game modules into rm_bin.pk3 on build.
#
# RM ships its game logic as loose native DLLs in build/bin, but a pure server
# (sv_pure 1, the Host Game / listen-server default) requires the cgame module
# to come from a .pk3 — otherwise it aborts with "Failed to locate cgame DLL for
# pure server mode". We package the two CLIENT modules (cgame + ui) into a pak
# that ships with the build so the server's pure check finds them and a matched
# client loads its own local copy. (qagame is server-only — NOT packaged here.)
#
# The DLLs MUST be stored at the pak ROOT with their BARE filenames: the engine's
# FS_ReadFile resolves the module by the bare qpath "cgame_mp_x86_64.dll" with no
# directory — a subdir would make the lookup miss.
#
# The loose DLLs stay in build/bin (the sv_pure 0 / dev path loads them directly);
# this only ADDS a pak, it relocates nothing.
#
# A pk3 is just a ZIP. cmake -E tar --format=zip is fully portable (no
# PowerShell), and WORKING_DIRECTORY = build/bin stores the loose DLLs there with
# the desired bare internal names (cgame_mp_x86_64.dll, NOT bin/cgame_...).

if(NOT WIN32 OR NOT BUILD_CLIENT)
    return()
endif()

set(RM_BIN_PAK_OUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/etmain")
set(RM_BIN_PAK         "${RM_BIN_PAK_OUT_DIR}/rm_bin.pk3")

# Entries stored in the pak, relative to build/bin (these become the pak's bare
# internal paths). These are the cgame/ui OUTPUT_NAMEs from ClientModules.cmake.
set(RM_BIN_PAK_ENTRIES
    cgame_mp_x86_64.dll
    ui_mp_x86_64.dll)

add_custom_command(
    OUTPUT  "${RM_BIN_PAK}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RM_BIN_PAK_OUT_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${RM_BIN_PAK}" --format=zip ${RM_BIN_PAK_ENTRIES}
    DEPENDS cgame ui
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMENT "Packing RM client modules -> ${RM_BIN_PAK}"
    VERBATIM)

add_custom_target(rm_bin ALL DEPENDS "${RM_BIN_PAK}")
