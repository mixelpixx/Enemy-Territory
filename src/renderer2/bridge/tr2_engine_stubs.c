/*
===========================================================================
ET-RM renderer2 — bridge engine glue (R2-2 / Task 3).

The vendored renderer2 tree (and the vendored q_shared.c parser/string layer it
links against) calls a handful of ENGINE functions that ET:Legacy's engine
provides. In OUR build these are routed through the bridge to our engine's
refimport `ri`:

    Com_Printf        -> BrdgOur_Print(PRINT_ALL,  ...)   -> ri.Printf
    Com_DPrintf       -> BrdgOur_Print(PRINT_DEVELOPER,...)-> ri.Printf
    Com_Error         -> BrdgOur_Error(...)               -> ri.Error (noreturn)
    Com_BlockChecksum -> zlib crc32 (zlib is linked)

This TU lives in the vendored CORE static lib (so the vendored code resolves
these symbols), but the BrdgOur_* shims it calls are defined in the bridge's
ours-TU (tr2_bridge_ours.c) — both are linked into the same etrm_renderer2.dll,
so the call resolves at link time. Before the bridge stores `ri` (i.e. between
DLL load and GetRefAPI), BrdgOur_* are safe no-ops (they guard on g_haveRI).

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "q_shared.h"          /* their QDECL, Q_vsnprintf, PRINT_* values */

#include "zlib.h"

#include <stdarg.h>

#include "tr2_bridge.h"        /* BrdgOur_Print / BrdgOur_Error (neutral) */

/**
 * @brief Com_Printf — routed to ri.Printf(PRINT_ALL, ...) via the bridge.
 */
void QDECL Com_Printf(const char *fmt, ...)
{
	char    buf[MAX_PRINT_MSG];
	va_list ap;
	va_start(ap, fmt);
	Q_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	BrdgOur_Print(PRINT_ALL, buf);
}

/**
 * @brief Com_DPrintf — routed to ri.Printf(PRINT_DEVELOPER, ...) via the bridge.
 */
void QDECL Com_DPrintf(const char *fmt, ...)
{
	char    buf[MAX_PRINT_MSG];
	va_list ap;
	va_start(ap, fmt);
	Q_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	BrdgOur_Print(PRINT_DEVELOPER, buf);
}

/**
 * @brief Com_Error — routed to ri.Error via the bridge (noreturn on the engine).
 */
void QDECL Com_Error(int code, const char *fmt, ...)
{
	char    buf[MAX_PRINT_MSG];
	va_list ap;
	va_start(ap, fmt);
	Q_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	BrdgOur_Error(code, buf);
	for (;;) { }   /* unreachable: ri.Error is noreturn */
}

/**
 * @brief Com_BlockChecksum — CRC-32 (IEEE) via the linked zlib, so shader-cache
 *        checksums (GLSL_GenerateCheckSum) are stable and standard.
 */
unsigned int Com_BlockChecksum(const void *buffer, size_t length)
{
	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, (const Bytef *)buffer, (uInt)length);
	return (unsigned int)crc;
}
