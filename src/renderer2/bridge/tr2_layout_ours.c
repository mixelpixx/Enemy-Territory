/*
===========================================================================
ET-RM renderer2 — BRIDGE layout table, OURS world (R2-2 / Task 3).

Emits the sizeof + key field offsets of every struct that crosses the bridge
boundary BY POINTER UNTRANSLATED (or whose shared prefix is copied), as seen in
OUR engine headers. The neutral checker (tr2_layout_check.c) compares this
against the THEIRS table at bridge init and ri.Error's on any mismatch — turning
silent cross-world struct corruption into a clean, loud failure.

Includes OUR headers only (include isolation — see tr2_bridge.h).

  Pass-through (pointer crosses uncopied): refdef_t, polyVert_t, polyBuffer_t,
    glfog_t, fontInfo_t, markFragment_t, orientation_t.
  Prefix-copied (translated): refEntity_t — we track its size and the offsets
    that bound the shared prefix (entityNum is the last shared field; our whole
    struct ends there). The checker asserts OUR sizeof(refEntity_t) ==
    THEIR offsetof(refEntity_t, skeleton), so the prefix memcpy is exact.

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "../../game/q_shared.h"
#include "../../renderer/tr_public.h"   /* pulls in cgame/tr_types.h */

#include "tr2_bridge.h"

#include <stddef.h>

static const brdgLayoutEntry_t g_ours[] =
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
		{ offsetof(fontInfo_t, glyphs), offsetof(fontInfo_t, glyphScale), offsetof(fontInfo_t, name) }
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
		/* refEntity_t: OUR struct ends at the shared prefix (no skeleton). The
		 * checker special-cases this name: OUR sizeof must equal THEIR
		 * offsetof(skeleton). We also track the prefix field offsets. */
		"refEntity_t", sizeof(refEntity_t), 5,
		{ "reType", "hModel", "axis", "origin", "entityNum" },
		{ offsetof(refEntity_t, reType), offsetof(refEntity_t, hModel), offsetof(refEntity_t, axis),
		  offsetof(refEntity_t, origin), offsetof(refEntity_t, entityNum) }
	},
};

const brdgLayoutEntry_t *Brdg_LayoutOurs(int *count)
{
	if (count) { *count = (int)(sizeof(g_ours) / sizeof(g_ours[0])); }
	return g_ours;
}
