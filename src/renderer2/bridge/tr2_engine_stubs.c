/*
===========================================================================
ET-RM renderer2 — bridge-precursor engine stubs (R2-2 / Task 2).

The vendored renderer2 tree (and the vendored q_shared.c parser/string layer it
links against) calls a handful of ENGINE functions that, in ET:Legacy's build,
the engine provides and that, in OUR build, the bridge will route through the
refimport `ri` struct in Task 3:

    Com_Printf / Com_DPrintf  -> ri.Printf
    Com_Error                 -> ri.Error
    Com_BlockChecksum         -> ri.zlib_crc32 (or a real CRC) once the bridge exists

For Task 2 the goal is only that etrm_renderer2.dll LINKS. These are minimal,
SELF-CONTAINED, LOGGED precursor implementations so no engine .c is dragged in:
the print paths go to stderr, Com_Error aborts loudly, and Com_BlockChecksum
runs a small standalone CRC so callers (GLSL_GenerateCheckSum, shader caching)
get a stable value. Task 3's tr2_bridge.c REPLACES this TU entirely by wiring
these to `ri`.

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "q_shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/**
 * @brief Com_Printf precursor — forwards to stderr until the bridge wires ri.Printf.
 */
void QDECL Com_Printf(const char *fmt, ...)
{
	va_list argptr;
	va_start(argptr, fmt);
	vfprintf(stderr, fmt, argptr);
	va_end(argptr);
}

/**
 * @brief Com_DPrintf precursor — developer print to stderr until the bridge wires ri.Printf.
 */
void QDECL Com_DPrintf(const char *fmt, ...)
{
	va_list argptr;
	va_start(argptr, fmt);
	vfprintf(stderr, fmt, argptr);
	va_end(argptr);
}

/**
 * @brief Com_Error precursor — loud abort until the bridge wires ri.Error.
 */
void QDECL Com_Error(int code, const char *fmt, ...)
{
	va_list argptr;
	(void)code;

	fprintf(stderr, "renderer2 Com_Error (pre-bridge): ");
	va_start(argptr, fmt);
	vfprintf(stderr, fmt, argptr);
	va_end(argptr);
	fprintf(stderr, "\n");

	abort();
}

/**
 * @brief Com_BlockChecksum precursor — standalone CRC-32 (IEEE) so shader-cache
 *        checksums are stable. Replaced by ri-routed crc32 in Task 3.
 */
unsigned int Com_BlockChecksum(const void *buffer, size_t length)
{
	const unsigned char *p = (const unsigned char *)buffer;
	unsigned int         crc = 0xFFFFFFFFu;
	size_t               i;
	int                  k;

	for (i = 0; i < length; ++i)
	{
		crc ^= p[i];
		for (k = 0; k < 8; ++k)
		{
			unsigned int mask = (unsigned int)(-(int)(crc & 1u));
			crc = (crc >> 1) ^ (0xEDB88320u & mask);
		}
	}

	return ~crc;
}
