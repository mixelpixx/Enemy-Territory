/*
===========================================================================
ET-RM renderer2 — BRIDGE layout table, THEIRS world (R2-2 / Task 3).

Counterpart to tr2_layout_ours.c, emitting the SAME set of struct sizes/offsets
as seen in the VENDORED ET:Legacy headers. The neutral checker matches entries
by name and compares.

Includes THEIR headers only (include isolation — see tr2_bridge.h).

refEntity_t special case: their struct appends refSkeleton_t (skeleton) under
USE_REFENTITY_ANIMATIONSYSTEM, so their sizeof differs from ours. We report
their FULL sizeof in `size` (informational) but also expose
offsetof(refEntity_t, skeleton) as the field named "skeleton": the checker
asserts OUR sizeof(refEntity_t) == THEIR offsetof(skeleton), which is exactly
the precondition the prefix-copy translation relies on.

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "q_shared.h"          /* their refEntity_t/refdef_t/polyVert_t/... */
#include "tr_types.h"          /* their polyBuffer_t/glfog_t/refdef_t */

#include "tr2_bridge.h"

#include <stddef.h>

static const brdgLayoutEntry_t g_theirs[] =
{
	{
		"refdef_t", sizeof(refdef_t), 5,
		{ "x", "vieworg", "time", "areamask", "glfog" },
		{ offsetof(refdef_t, x), offsetof(refdef_t, vieworg), offsetof(refdef_t, time),
		  offsetof(refdef_t, areamask), offsetof(refdef_t, glfog) }
	},
	{
		"polyVert_t", sizeof(polyVert_t), 3,
		{ "xyz", "st", "modulate" },
		{ offsetof(polyVert_t, xyz), offsetof(polyVert_t, st), offsetof(polyVert_t, modulate) }
	},
	{
		"polyBuffer_t", sizeof(polyBuffer_t), 5,
		{ "xyz", "st", "color", "numVerts", "indicies" },
		{ offsetof(polyBuffer_t, xyz), offsetof(polyBuffer_t, st), offsetof(polyBuffer_t, color),
		  offsetof(polyBuffer_t, numVerts), offsetof(polyBuffer_t, indicies) }
	},
	{
		"glfog_t", sizeof(glfog_t), 4,
		{ "mode", "color", "start", "density" },
		{ offsetof(glfog_t, mode), offsetof(glfog_t, color), offsetof(glfog_t, start),
		  offsetof(glfog_t, density) }
	},
	{
		"fontInfo_t", sizeof(fontInfo_t), 3,
		{ "glyphs", "glyphScale", "name" },
		/* their last field is datName (== our name); compare by position. */
		{ offsetof(fontInfo_t, glyphs), offsetof(fontInfo_t, glyphScale), offsetof(fontInfo_t, datName) }
	},
	{
		"markFragment_t", sizeof(markFragment_t), 2,
		{ "firstPoint", "numPoints" },
		{ offsetof(markFragment_t, firstPoint), offsetof(markFragment_t, numPoints) }
	},
	{
		"orientation_t", sizeof(orientation_t), 2,
		{ "origin", "axis" },
		{ offsetof(orientation_t, origin), offsetof(orientation_t, axis) }
	},
	{
		/* report FULL their-size; the checker compares the shared prefix via
		 * field offsets + the skeleton-boundary special case. */
		"refEntity_t", sizeof(refEntity_t), 6,
		{ "reType", "hModel", "axis", "origin", "entityNum", "skeleton" },
		{ offsetof(refEntity_t, reType), offsetof(refEntity_t, hModel), offsetof(refEntity_t, axis),
		  offsetof(refEntity_t, origin), offsetof(refEntity_t, entityNum),
#if defined(USE_REFENTITY_ANIMATIONSYSTEM)
		  offsetof(refEntity_t, skeleton)
#else
		  sizeof(refEntity_t)
#endif
		}
	},
};

const brdgLayoutEntry_t *Brdg_LayoutTheirs(int *count)
{
	if (count) { *count = (int)(sizeof(g_theirs) / sizeof(g_theirs[0])); }
	return g_theirs;
}
