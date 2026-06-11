/*
===========================================================================
ET-RM renderer2 — PNG loader stub (R2-2 / Task 2).

ET:Legacy's src/renderercommon/tr_image_png.c is a self-contained PNG decoder
that depends on the engine's puff.c inflate implementation (`#include
"../qcommon/puff.h"`, calls `puff()`). puff.c is an engine source file, not a
renderer file, so it is NOT part of the vendored renderer2 closure. Rather than
drag an engine .c into the DLL (which the edit rules forbid) or wire a third
inflate path, we EXCLUDE tr_image_png.c from the etrm_renderer2 build and supply
these stubs providing the same entry points (R_LoadPNG / RE_SavePNG) so the rest
of the tree links.

R_LoadPNG signals "not loaded" by returning qfalse without touching *pic. NOTE:
in their tr_image.c, R_LoadImage dispatches to a SINGLE loader chosen by the
file extension (it does not try every loader in turn). When a ".png" is named
but cannot be decoded, their code Ren_Drop's (an existing-but-unparseable file
is treated as an error), so a returned qfalse here surfaces as a missing-image
diagnostic rather than a silent fallthrough. Real PNG support = R2-3.

DELIBERATE R2-3 REVISIT: retail ET assets are TGA/JPG; the main menu does not
require PNG. Real PNG support returns in R2-3 (either by vendoring puff.c behind
the bridge, or routing through the bundled zlib inflate).

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "tr_common.h"

/**
 * @brief R_LoadPNG stub — PNG decoding is descoped for R2-2 (see file header).
 * @returns qfalse always (image "not loaded"), mirroring the other loaders'
 *          failure signal so R_LoadImage falls through cleanly.
 */
qboolean R_LoadPNG(imageData_t *data, byte **pic, int *width, int *height, byte alphaByte)
{
	(void)data;
	(void)pic;
	(void)width;
	(void)height;
	(void)alphaByte;

	Ren_Developer("R_LoadPNG: PNG support is descoped in this build (R2-2); image not loaded\n");
	return qfalse;
}

/**
 * @brief RE_SavePNG stub — screenshot-to-PNG is descoped for R2-2.
 */
void RE_SavePNG(char *filename, int image_width, int image_height, unsigned char *image_buffer, int padding)
{
	(void)filename;
	(void)image_width;
	(void)image_height;
	(void)image_buffer;
	(void)padding;

	Ren_Developer("RE_SavePNG: PNG support is descoped in this build (R2-2); not saved\n");
}
