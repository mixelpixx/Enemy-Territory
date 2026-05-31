# Engine core: server + qcommon + shared game math.
#
# Built as a STATIC library so it can be compile-verified before the platform,
# client, and renderer layers exist (unresolved externals are fine until link).
# Source lists are copied verbatim from SConscript.core.
#
# vm_x86.c is the 32-bit-x86 JIT only; it is OMITTED here (and VM_Create() forces
# the bytecode interpreter on non-x86). The x86_64/ARM JIT returns in Phase 2.

set(SV_DIR  "${ETRM_SRC}/server")
set(QC_DIR  "${ETRM_SRC}/qcommon")
set(GM_DIR  "${ETRM_SRC}/game")

set(SERVER_SOURCES
    ${SV_DIR}/sv_bot.c      ${SV_DIR}/sv_ccmds.c   ${SV_DIR}/sv_client.c
    ${SV_DIR}/sv_game.c     ${SV_DIR}/sv_init.c    ${SV_DIR}/sv_main.c
    ${SV_DIR}/sv_net_chan.c ${SV_DIR}/sv_snapshot.c ${SV_DIR}/sv_world.c)

set(QCOMMON_SOURCES
    ${QC_DIR}/cm_load.c   ${QC_DIR}/cm_patch.c  ${QC_DIR}/cm_polylib.c
    ${QC_DIR}/cm_test.c   ${QC_DIR}/cm_trace.c  ${QC_DIR}/cmd.c
    ${QC_DIR}/common.c    ${QC_DIR}/cvar.c      ${QC_DIR}/files.c
    ${QC_DIR}/huffman.c   ${QC_DIR}/md4.c       ${QC_DIR}/msg.c
    ${QC_DIR}/net_chan.c  ${QC_DIR}/unzip.c     ${QC_DIR}/vm.c
    ${QC_DIR}/vm_interpreted.c)

# Shared math/util compiled into the engine (NOT the QVM-side bg_* sim code).
set(SHARED_SOURCES
    ${GM_DIR}/q_shared.c
    ${GM_DIR}/q_math.c)

add_library(etrm_core STATIC
    ${SERVER_SOURCES}
    ${QCOMMON_SOURCES}
    ${SHARED_SOURCES})

target_include_directories(etrm_core PUBLIC
    ${QC_DIR} ${GM_DIR} ${SV_DIR}
    ${ETRM_SRC}/client ${ETRM_SRC}/renderer
    ${ETRM_SRC}/cgame  ${ETRM_SRC}/ui
    ${ETRM_SRC}/botlib)

etrm_apply_common_definitions(etrm_core)
