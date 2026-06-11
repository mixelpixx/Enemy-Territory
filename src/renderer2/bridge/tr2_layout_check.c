/*
===========================================================================
ET-RM renderer2 — BRIDGE layout checker, NEUTRAL (R2-2 / Task 3).

Compares the OURS and THEIRS layout tables (each emitted by its own per-world
TU) at bridge init. Includes ONLY tr2_bridge.h (primitive types) — neither
header world — so it can reference both tables without a type collision.

Rules:
  * Every OURS entry must have a same-named THEIRS entry.
  * For PASS-THROUGH types: sizeof must match exactly, and every named field
    offset must match.
  * For refEntity_t (PREFIX-COPIED): OUR sizeof(refEntity_t) must equal THEIR
    offsetof(skeleton) (the shared-prefix boundary the translation copies), and
    every shared prefix field offset must match. Their full sizeof is allowed to
    differ (they append the skeleton).

On the first violation it writes a description into outMsg and returns 0.
Returns 1 when all checks pass.

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "tr2_bridge.h"

#include <string.h>
#include <stdio.h>

static const brdgLayoutEntry_t *FindByName(const brdgLayoutEntry_t *tbl, int n, const char *name)
{
	int i;
	for (i = 0; i < n; ++i)
	{
		if (strcmp(tbl[i].name, name) == 0)
		{
			return &tbl[i];
		}
	}
	return NULL;
}

static size_t FieldOffsetByName(const brdgLayoutEntry_t *e, const char *name, int *found)
{
	int i;
	for (i = 0; i < e->numFields; ++i)
	{
		if (strcmp(e->fieldName[i], name) == 0)
		{
			*found = 1;
			return e->fieldOff[i];
		}
	}
	*found = 0;
	return 0;
}

int Brdg_LayoutCheck(char *outMsg, size_t outMsgSize)
{
	int ours_n = 0, theirs_n = 0, i, j;
	const brdgLayoutEntry_t *ours   = Brdg_LayoutOurs(&ours_n);
	const brdgLayoutEntry_t *theirs = Brdg_LayoutTheirs(&theirs_n);

	for (i = 0; i < ours_n; ++i)
	{
		const brdgLayoutEntry_t *o = &ours[i];
		const brdgLayoutEntry_t *t = FindByName(theirs, theirs_n, o->name);
		int isRefEntity = (strcmp(o->name, "refEntity_t") == 0);

		if (!t)
		{
			snprintf(outMsg, outMsgSize,
			         "  type '%s' present in OURS but missing in THEIRS table\n", o->name);
			return 0;
		}

		if (isRefEntity)
		{
			/* prefix-boundary: OUR sizeof must equal THEIR offsetof(skeleton). */
			int    found = 0;
			size_t theirSkel = FieldOffsetByName(t, "skeleton", &found);
			if (!found)
			{
				snprintf(outMsg, outMsgSize,
				         "  refEntity_t: THEIRS table lacks 'skeleton' boundary field\n");
				return 0;
			}
			if (o->size != theirSkel)
			{
				snprintf(outMsg, outMsgSize,
				         "  refEntity_t: OUR sizeof=%lu != THEIR offsetof(skeleton)=%lu "
				         "(shared-prefix copy would be wrong)\n",
				         (unsigned long)o->size, (unsigned long)theirSkel);
				return 0;
			}
		}
		else
		{
			if (o->size != t->size)
			{
				snprintf(outMsg, outMsgSize,
				         "  type '%s': sizeof mismatch (ours=%lu theirs=%lu)\n",
				         o->name, (unsigned long)o->size, (unsigned long)t->size);
				return 0;
			}
		}

		/* compare shared prefix field offsets by name (skip the boundary field). */
		for (j = 0; j < o->numFields; ++j)
		{
			int    found = 0;
			size_t toff  = FieldOffsetByName(t, o->fieldName[j], &found);
			if (!found)
			{
				/* OUR 'name' vs THEIR 'datName' for fontInfo_t: compared by
				 * position fallback below. */
				if (strcmp(o->name, "fontInfo_t") == 0 && strcmp(o->fieldName[j], "name") == 0)
				{
					found = 1;
					toff  = FieldOffsetByName(t, "datName", &found);
				}
			}
			if (!found)
			{
				snprintf(outMsg, outMsgSize,
				         "  type '%s': field '%s' not found in THEIRS table\n",
				         o->name, o->fieldName[j]);
				return 0;
			}
			if (toff != o->fieldOff[j])
			{
				snprintf(outMsg, outMsgSize,
				         "  type '%s': field '%s' offset mismatch (ours=%lu theirs=%lu)\n",
				         o->name, o->fieldName[j],
				         (unsigned long)o->fieldOff[j], (unsigned long)toff);
				return 0;
			}
		}
	}

	if (outMsgSize) { outMsg[0] = '\0'; }
	return 1;
}
