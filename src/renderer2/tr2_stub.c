/*
===========================================================================
ET-RM renderer2 — STUB (increment R2-1).

Proves the opt-in renderer-DLL path end to end: the dispatcher loads this
DLL, calls GetRefAPI, gets NULL back, and falls back to the built-in gl1
renderer. The real ET:Legacy-based GLSL renderer replaces this in R2-2.

GPLv3 (ET-RM original; no third-party code).
===========================================================================
*/

#include "../game/q_shared.h"   // qboolean/vec types for tr_types.h, PRINT_ALL
#include "../renderer/tr_public.h"

refexport_t *GetRefAPI( int apiVersion, refimport_t *rimp ) {
	if ( rimp && rimp->Printf ) {
		rimp->Printf( PRINT_ALL,
					  "renderer2 STUB loaded (api %d requested, mine %d) — real GLSL renderer lands in R2-2; falling back to gl1\n",
					  apiVersion, REF_API_VERSION );
	}
	return NULL;   // not a real renderer yet — dispatcher falls back to gl1
}
