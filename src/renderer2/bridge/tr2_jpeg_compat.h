/*
===========================================================================
ET-RM renderer2 — jpeg-6b memory-source compatibility shim (R2-3 / Task 1).

ET:Legacy's vendored etl/common/tr_image_jpg.c (R_LoadJPG) reads JPEGs from a
memory buffer via jpeg_mem_src(), an API that arrived in libjpeg 8. Our bundled
decoder is jpeg-6b (1998), which ships jpeg_stdio_src() only — no memory source.

This shim provides the classic ~70-line jpeg_source_mgr-over-a-memory-buffer
adapter (the well-known ioquake3 implementation over jpeg-6b) plus a
jpeg_mem_src() wrapper with the libjpeg-8 signature, so tr_image_jpg.c compiles
and decodes unmodified.

Only the DECODE path needs this. tr_image_jpg.c's SAVE path (RE_SaveJPGToBuffer)
carries its OWN jpeg_destination_mgr (jpegDest/my_destination_mgr) and never
calls jpeg_mem_dest, so it compiles against jpeg-6b as-is — no dest shim needed.

GPLv3 (ET-RM original glue; the mem-src pattern is the public ioquake3 adapter,
itself a thin standard libjpeg source manager — no third-party code vendored).
===========================================================================
*/

#ifndef TR2_JPEG_COMPAT_H
#define TR2_JPEG_COMPAT_H

#include <stdio.h>
#include <jpeglib.h>

/**
 * @brief libjpeg-8-compatible memory data source for jpeg-6b.
 *
 * Prepares the decompressor to read all of its input from the in-memory buffer
 * [inbuffer, inbuffer+insize). Mirrors libjpeg 8's jpeg_mem_src signature.
 *
 * @param[in] cinfo    initialized decompress object
 * @param[in] inbuffer pointer to the JPEG byte stream
 * @param[in] insize   length of the byte stream in bytes
 */
void jpeg_mem_src(j_decompress_ptr cinfo, const unsigned char *inbuffer, unsigned long insize);

#endif /* TR2_JPEG_COMPAT_H */
