/*
===========================================================================
ET-RM renderer2 — jpeg-6b memory-source compatibility shim (R2-3 / Task 1).

See tr2_jpeg_compat.h for the rationale. This is the standard libjpeg memory
source manager (the public ioquake3 adapter over jpeg-6b): a jpeg_source_mgr
whose buffer is the whole input image, with a one-shot fill_input_buffer that
synthesizes a fake EOI marker if the decoder ever runs past the end of a
truncated stream (so corrupt JPEGs fail gracefully instead of reading OOB).

GPLv3 (ET-RM original glue; standard libjpeg source-manager pattern).
===========================================================================
*/

#include "tr2_jpeg_compat.h"

/* tr_common.h gives us Ren_Print (-> ri.Printf) for the logged-once save-path
 * notice. Safe to co-include with jpeglib.h here: the renderer2 header world
 * pulls in no windows.h, so jpeg-6b's `boolean` typedef does not clash. */
#include "tr_common.h"

#include <jerror.h>

/* Expanded data source object for memory input (classic libjpeg pattern). */
typedef struct
{
	struct jpeg_source_mgr pub;     /* public fields */
	JOCTET                 eoi[2];  /* synthetic end-of-image marker */
} my_source_mgr;

typedef my_source_mgr *my_src_ptr;

/* Called by jpeg_read_header before any data is actually read. Nothing to do:
 * the entire stream was already handed to us in jpeg_mem_src. */
static void init_source(j_decompress_ptr cinfo)
{
	(void)cinfo;
}

/* Called whenever the in-memory buffer has been emptied. Because we point the
 * source manager at the WHOLE image up front, reaching here means the JPEG was
 * truncated. We emit a fake EOI marker (the documented libjpeg recovery) so the
 * decoder terminates cleanly instead of running off the end of the buffer. */
static boolean fill_input_buffer(j_decompress_ptr cinfo)
{
	my_src_ptr src = (my_src_ptr)cinfo->src;

	WARNMS(cinfo, JWRN_JPEG_EOF);

	src->eoi[0] = (JOCTET)0xFF;
	src->eoi[1] = (JOCTET)JPEG_EOI;

	src->pub.next_input_byte = src->eoi;
	src->pub.bytes_in_buffer = 2;

	return TRUE;
}

/* Skip num_bytes worth of data. For a whole-buffer source this is just pointer
 * arithmetic; if the skip runs past the end fill_input_buffer handles recovery. */
static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	my_src_ptr src = (my_src_ptr)cinfo->src;

	if (num_bytes > 0)
	{
		if ((unsigned long)num_bytes > src->pub.bytes_in_buffer)
		{
			num_bytes = (long)src->pub.bytes_in_buffer;
		}
		src->pub.next_input_byte += (size_t)num_bytes;
		src->pub.bytes_in_buffer -= (size_t)num_bytes;
	}
}

/* Called by jpeg_finish_decompress after all data has been read. Nothing to
 * release: the buffer is owned by the caller. */
static void term_source(j_decompress_ptr cinfo)
{
	(void)cinfo;
}

/*
 * ---------------------------------------------------------------------------
 * SAVE-PATH DECISION (R2-3 / Task 1): JPEG ENCODE is unavailable under gl2.
 *
 * tr_image_jpg.c's save path (RE_SaveJPGToBuffer) drives the libjpeg COMPRESSOR.
 * jpeg-6b's compression entry points jpeg_start_compress / jpeg_write_scanlines
 * live in jcapistd.c, which was NEVER vendored into our tree (the bundled
 * jpeg-6b is a DECODE-oriented subset — gl1's tr_image.c has no JPEG save, so
 * the original SConscript.core omitted jcapistd.c). Enabling encode would mean
 * vendoring a new libjpeg compression TU (+deps) into the SHARED etrm_jpeg
 * target, which also feeds gl1 — out of scope for this task and a needless
 * change to the gl1 link surface.
 *
 * Per the plan (decode is the must-have; the save path may be stubbed with a
 * logged-once message — screenshots stay TGA), we satisfy the two missing
 * compressor symbols with logged-once no-op stubs. RE_SaveJPG[ToBuffer] then
 * links and runs without crashing but writes no JPEG bytes; jpeg_write_scanlines
 * advances next_scanline so RE_SaveJPGToBuffer's write loop terminates cleanly.
 * The engine screenshot path falls back to TGA (always available).
 * ---------------------------------------------------------------------------
 */

/* NB: jpeglib.h #defines jpeg_start_compress -> jStrtCompress and
 * jpeg_write_scanlines -> jWrtScanlines (the jpeg-6b short-symbol scheme), so
 * these definitions emit exactly the symbols tr_image_jpg.c's save path calls. */
void jpeg_start_compress(j_compress_ptr cinfo, boolean write_all_tables)
{
	static int warned = 0;

	(void)write_all_tables;

	(void)cinfo;

	if (!warned)
	{
		warned = 1;
		Ren_Print(S_COLOR_YELLOW "WARNING: JPEG screenshot save is unavailable under gl2 "
		          "(jpeg-6b ships no encoder); use TGA screenshots instead.\n");
	}

	/* Put the compressor into a state where the caller's write loop runs to
	 * completion without touching the (absent) coefficient pipeline. */
	cinfo->next_scanline = 0;
}

JDIMENSION jpeg_write_scanlines(j_compress_ptr cinfo, JSAMPARRAY scanlines, JDIMENSION num_lines)
{
	(void)scanlines;

	/* No encoding: just advance past the supplied lines so the RE_SaveJPGToBuffer
	 * loop (while next_scanline < image_height) terminates. No bytes emitted. */
	cinfo->next_scanline += num_lines;

	return num_lines;
}

void jpeg_mem_src(j_decompress_ptr cinfo, const unsigned char *inbuffer, unsigned long insize)
{
	my_src_ptr src;

	/* Allocate the source manager once, in the permanent pool, exactly like
	 * jpeg_stdio_src would. (No re-use across images: each R_LoadJPG call uses
	 * a fresh decompress object.) */
	if (cinfo->src == NULL)
	{
		cinfo->src = (struct jpeg_source_mgr *)
		             (*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_PERMANENT,
		                                        sizeof(my_source_mgr));
	}

	src = (my_src_ptr)cinfo->src;

	src->pub.init_source       = init_source;
	src->pub.fill_input_buffer = fill_input_buffer;
	src->pub.skip_input_data   = skip_input_data;
	src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
	src->pub.term_source       = term_source;

	/* Point straight at the caller's buffer — no copy. */
	src->pub.bytes_in_buffer = (size_t)insize;
	src->pub.next_input_byte = (const JOCTET *)inbuffer;
}
