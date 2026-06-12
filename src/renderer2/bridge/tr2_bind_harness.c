/*
===========================================================================
ET-RM renderer2 — bridge BIND HARNESS (R2-2 / Task 3 exit-bar evidence).

Stands in for the engine's CL_InitRef dlopen path when retail assets (pak0.pk3,
default.cfg) are not present on the build machine — so we can still prove the
exit bar: a gl2 GetRefAPI binding that gets PAST the bind (the bridge returns a
non-NULL refexport the engine would accept).

It LoadLibrary's etrm_renderer2.dll exactly as the engine's Sys_LoadRendererDll
does, resolves GetRefAPI, and calls it with a MINIMAL mock refimport (the subset
the bind path touches: Printf for diagnostics — the bind itself makes no GL or
FS calls; their ETL_GetRefAPI only memsets + assigns RE_ pointers, and our
GetRefAPI runs the layout check + builds the tables). A non-NULL return with a
populated refexport (spot-checked) means the boundary is wired and the layout
checker passed; NULL or a crash means it is not.

This binary does NOT create a GL context — GLimp/GL bring-up is Task 4. The
harness is registered as a CTest so the bind path is regression-checked.

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

/* mock refimport: only the fields the bind path may touch. Layout MUST match
 * our refimport_t HEAD well enough that GetRefAPI's `ri = *rimp` + Printf works.
 * We mirror the real struct's leading members (Printf, Error, Milliseconds) and
 * pad the rest with NULLs — the bind path guards every other pointer (`if
 * (ri.X)`), so NULLs are safe. To avoid pulling our engine headers (and the
 * include-isolation tangle) we declare a compatible prefix by hand and over-
 * allocate the tail. */

typedef void (*fn_t)(void);

/* Generously sized so `ri = *rimp` (a full refimport_t copy) reads valid memory.
 * Our refimport_t has well under 64 function pointers. */
typedef struct
{
	void (*Printf)(int level, const char *fmt, ...);
	void (*Error)(int level, const char *fmt, ...);
	int  (*Milliseconds)(void);
	fn_t pad[64];
} mockRefimport_t;

static void mock_Printf(int level, const char *fmt, ...)
{
	va_list ap;
	(void)level;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static void mock_Error(int level, const char *fmt, ...)
{
	va_list ap;
	(void)level;
	fprintf(stderr, "[mock ri.Error] ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

static int mock_Milliseconds(void) { return 0; }

#define REF_API_VERSION_EXPECTED 10  /* our engine's version (v10: + CM_PointContents) */

int main(int argc, char **argv)
{
	const char     *dllPath = (argc > 1) ? argv[1] : "etrm_renderer2.dll";
	HMODULE         h;
	void           *proc;
	void           *(*pGetRefAPI)(int, void *);
	void           *re;
	mockRefimport_t ri;
	void          **reSlots;
	int             i, nonNull = 0;

	memset(&ri, 0, sizeof(ri));
	ri.Printf       = mock_Printf;
	ri.Error        = mock_Error;
	ri.Milliseconds = mock_Milliseconds;

	h = LoadLibraryA(dllPath);
	if (!h)
	{
		fprintf(stderr, "HARNESS FAIL: LoadLibrary('%s') -> %lu\n",
		        dllPath, (unsigned long)GetLastError());
		return 2;
	}

	proc = (void *)GetProcAddress(h, "GetRefAPI");
	if (!proc)
	{
		fprintf(stderr, "HARNESS FAIL: GetProcAddress('GetRefAPI') -> %lu\n",
		        (unsigned long)GetLastError());
		return 3;
	}
	pGetRefAPI = (void *(*)(int, void *))proc;

	re = pGetRefAPI(REF_API_VERSION_EXPECTED, &ri);
	if (!re)
	{
		fprintf(stderr, "HARNESS FAIL: GetRefAPI returned NULL (bind/layout check failed)\n");
		return 4;
	}

	/* spot-check the returned refexport is populated (first ~12 slots non-NULL:
	 * Shutdown/BeginRegistration/RegisterModel/... are all wired). */
	reSlots = (void **)re;
	for (i = 0; i < 12; ++i)
	{
		if (reSlots[i]) { ++nonNull; }
	}
	if (nonNull < 10)
	{
		fprintf(stderr, "HARNESS FAIL: refexport sparsely populated (%d/12 non-NULL)\n", nonNull);
		return 5;
	}

	printf("HARNESS PASS: GetRefAPI bound; refexport populated (%d/12 leading slots)\n", nonNull);
	return 0;
}
