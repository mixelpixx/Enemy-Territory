/*
===========================================================================
ET-RM renderer2 — JPEG loader stub (R2-2 / Task 2).

ET:Legacy's src/renderercommon/tr_image_jpg.c targets a modern libjpeg
(libjpeg-turbo / libjpeg 8+): it calls `jpeg_mem_src()` (read from a memory
buffer). Our bundled libjpeg is jpeg-6b (1998), which predates `jpeg_mem_src`
(that API arrived in libjpeg 8). Compiling their loader against jpeg-6b headers
fails at the source level, so we EXCLUDE tr_image_jpg.c from the etrm_renderer2
build for this task and supply these stubs providing the same entry points
(R_LoadJPG / RE_SaveJPG / RE_SaveJPGToBuffer).

R_LoadJPG returns qfalse ("not loaded"). NOTE: their tr_image.c R_LoadImage
dispatches to a SINGLE loader by file extension (no try-every-loader fallthrough).
A named-but-unparseable ".jpg" makes their code Ren_Drop (existing-file error),
so this qfalse surfaces as a missing-image diagnostic. Real JPEG = R2-3.

DELIBERATE R2-3 REVISIT: the main menu is TGA-heavy, so the menu boots without
JPEG. Real JPEG (world textures, etc.) returns in R2-3 — either by upgrading the
bundled jpeg to a version exposing jpeg_mem_src, or by adapting their loader to
jpeg-6b's jpeg_stdio_src / a memory-source shim.

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "tr_local.h"

/**
 * @brief R_LoadJPG stub — JPEG DECODING is descoped for R2-2 (see file header).
 *
 * RM (R2-2 Task 4): their tr_image.c R_LoadImage dispatches to a single loader
 * by extension and Ren_Drops (fatal) when a NAMED .jpg fails to parse. At gl2
 * init CreateInternalShaders unconditionally loads gfx/misc/sunflare1.jpg (the
 * 3D sun-flare shader), so a qfalse return aborts the whole boot — before the
 * menu can ever draw. The main menu itself is TGA-only (verified: pak0 has no
 * menu-background JPEGs; the only ui/ JPEGs are post-match win-flag portraits),
 * so JPEG *content* is not needed for the menu exit bar.
 *
 * Instead of failing, return a valid 2x2 opaque-grey PLACEHOLDER so the named
 * image "loads" and init proceeds. This keeps the descope (no real JPEG decode
 * until R2-3) while unblocking boot. The placeholder only ever stands in for the
 * menu-invisible sunflare and the post-match portrait flags; real JPEG decode
 * (world textures etc.) is the R2-3 deliverable.
 */
qboolean R_LoadJPG(imageData_t *data, byte **pic, int *width, int *height, byte alphaByte)
{
	const int  w = 16, h = 16;
	byte      *out;
	int        i;
	(void)data;

	Ren_Developer("R_LoadJPG: JPEG decode is descoped (R2-2); returning a 16x16 grey "
	              "placeholder so init proceeds (real JPEG = R2-3)\n");

	/* CRITICAL: the returned pic is freed by R_FindImageFile via Com_Dealloc
	 * (== free). It MUST therefore be allocated with the matching Com_Allocate
	 * (== malloc), exactly like every real loader does via R_GetImageBuffer.
	 * Using ri.Z_Malloc here (engine zone) and letting their free() reclaim it
	 * corrupts the heap and hangs init. R_GetImageBuffer is the canonical
	 * loader-pic allocator (Com_Allocate-backed). */
	out = (byte *)R_GetImageBuffer(w * h * 4, BUFFER_IMAGE, "jpg-placeholder");
	if (!out)
	{
		*pic    = NULL;
		*width  = 0;
		*height = 0;
		return qfalse;
	}
	for (i = 0; i < w * h; ++i)
	{
		out[i * 4 + 0] = 128;
		out[i * 4 + 1] = 128;
		out[i * 4 + 2] = 128;
		out[i * 4 + 3] = alphaByte;
	}
	*pic    = out;
	*width  = w;
	*height = h;
	return qtrue;
}

/**
 * @brief RE_SaveJPGToBuffer stub — JPEG screenshot encoding is descoped for R2-2.
 * @returns 0 bytes written.
 */
size_t RE_SaveJPGToBuffer(byte *buffer, size_t bufSize, int quality, int image_width, int image_height, byte *image_buffer, int padding)
{
	(void)buffer;
	(void)bufSize;
	(void)quality;
	(void)image_width;
	(void)image_height;
	(void)image_buffer;
	(void)padding;

	Ren_Developer("RE_SaveJPGToBuffer: JPEG support is descoped in this build (R2-2); nothing written\n");
	return 0;
}

/**
 * @brief RE_SaveJPG stub — JPEG screenshot-to-file is descoped for R2-2.
 */
void RE_SaveJPG(const char *filename, int quality, int image_width, int image_height, byte *image_buffer, int padding)
{
	(void)filename;
	(void)quality;
	(void)image_width;
	(void)image_height;
	(void)image_buffer;
	(void)padding;

	Ren_Developer("RE_SaveJPG: JPEG support is descoped in this build (R2-2); not saved\n");
}
