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
 * @brief R_LoadJPG stub — JPEG decoding is descoped for R2-2 (see file header).
 * @returns qfalse always (image "not loaded").
 */
qboolean R_LoadJPG(imageData_t *data, byte **pic, int *width, int *height, byte alphaByte)
{
	(void)data;
	(void)pic;
	(void)width;
	(void)height;
	(void)alphaByte;

	Ren_Developer("R_LoadJPG: JPEG support is descoped in this build (R2-2); image not loaded\n");
	return qfalse;
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
