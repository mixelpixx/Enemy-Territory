/*
===========================================================================
ET-RM renderer2 — BRIDGE, THEIRS-side translation unit (R2-2 / Task 3).

Includes ONLY the vendored ET:Legacy headers (their tr_public.h v10, their
q_shared.h subset, GLEW). Responsibilities:

  1. THE REFIMPORT TRANSLATION: build THEIR refimport_t member-by-member.
     Every member is one of:
       (a) PASSTHROUGH  — signature-identical to an our-ri service; forwards
                          through a BrdgOur_* neutral callback.
       (b) ADAPTER      — signature/semantics differ; translated here
                          (Cvar_Get -> proxy layer; Cmd_AddSystemCommand ->
                          our 2-arg Cmd_AddCommand; GLimp_Init -> our GLimp_Init
                          + glewInit + glconfig fill; GLimp_SwapFrame ->
                          GLimp_EndFrame; RealTime -> localtime; FS streaming
                          trio over our FS_ReadFile; zlib_* -> linked zlib).
       (c) SAFE STUB    — service our engine doesn't expose / owns elsewhere
                          (IN_*, Sys_GLimp/SetEnv, CL_VideoRecording/WriteAVI,
                          Cvar_CheckRange/SetDescription, CM_PointContents,
                          GLimp_SplashImage) — logged-once.

  2. CVAR PROXY LAYER: a bridge-owned registry of THEIR-layout cvar_t proxies,
     synced from our cvars on Cvar_Get and once per frame (BeginFrame wrap).

  3. REFEXPORT publishing: call ETL_GetRefAPI(REF_API_VERSION, &theirRI), then
     expose their refexport as the neutral brdgReExport_t table the ours-TU
     reads (translating the drifted members: BeginRegistration glconfig
     copy-back, BeginFrame(void), RegisterFont 4-arg, refEntity_t skeleton).

This TU MUST NOT include any src/renderer/ or src/cgame/ header — include
isolation (see tr2_bridge.h). Cross-TU calls use only BrdgOur_* + the neutral
table.

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

/* their headers — the v10 world */
#include "q_shared.h"          /* their cvar_t/qtime_t/refEntity_t/glconfig_t */
#include "tr_public.h"         /* their refimport_t/refexport_t v10 */

#include <GL/glew.h>

#include "zlib.h"

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "tr2_bridge.h"

/* ETL_GetRefAPI: their GetRefAPI, renamed by the build (-DGetRefAPI=ETL_GetRefAPI
 * on tr_init.c) so it doesn't collide with our exported GetRefAPI. */
extern refexport_t *ETL_GetRefAPI(int apiVersion, refimport_t *rimp);

/* ======================================================================== *
 *  Local state.
 * ======================================================================== */
static refimport_t   theirRI;       /* the refimport we hand to their renderer */
static refexport_t  *theirRE;       /* their refexport (their layout) */
static glconfig_t    bridgeGlconfig; /* THEIR-layout glconfig their renderer fills */

/* logged-once helper (routes through our Printf at developer level) */
static void LogOnce(int *flag, const char *msg)
{
	if (*flag)
	{
		return;
	}
	*flag = 1;
	BrdgOur_Print(1 /*PRINT_DEVELOPER*/, msg);
}

/* ======================================================================== *
 *  refimport: logging / fatal / timing  (PASSTHROUGH, but vararg-formatted
 *  here because BrdgOur_Print takes a finished string).
 * ======================================================================== */

static void QDECL imp_Printf(int printLevel, const char *fmt, ...)
{
	char    buf[4096];
	va_list ap;
	va_start(ap, fmt);
	Q_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	BrdgOur_Print(printLevel, buf);
}

static void QDECL imp_Error(int errorLevel, const char *fmt, ...)
{
	char    buf[4096];
	va_list ap;
	va_start(ap, fmt);
	Q_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	BrdgOur_Error(errorLevel, buf);   /* noreturn on the engine side */
}

static int imp_Milliseconds(void)
{
	return BrdgOur_Milliseconds();
}

static int imp_RealTime(qtime_t *qtime)
{
	time_t    t  = time(NULL);
	struct tm *lt = localtime(&t);
	if (qtime && lt)
	{
		qtime->tm_sec   = lt->tm_sec;
		qtime->tm_min   = lt->tm_min;
		qtime->tm_hour  = lt->tm_hour;
		qtime->tm_mday  = lt->tm_mday;
		qtime->tm_mon   = lt->tm_mon;
		qtime->tm_year  = lt->tm_year;
		qtime->tm_wday  = lt->tm_wday;
		qtime->tm_yday  = lt->tm_yday;
		qtime->tm_isdst = lt->tm_isdst;
	}
	return (int)t;
}

/* ======================================================================== *
 *  refimport: memory  (PASSTHROUGH)
 * ======================================================================== */

static void  imp_Hunk_Clear(void)                          { BrdgOur_HunkClear(); }
static void *imp_Hunk_Alloc(size_t size, ha_pref pref)     { return BrdgOur_HunkAlloc(size, (int)pref); }
static void *imp_Hunk_AllocateTempMemory(size_t size)      { return BrdgOur_HunkAllocTemp(size); }
static void  imp_Hunk_FreeTempMemory(void *block)          { BrdgOur_HunkFreeTemp(block); }
static void *imp_Z_Malloc(int bytes)                       { return BrdgOur_ZMalloc(bytes); }
static void  imp_Free(void *buf)                           { BrdgOur_Free(buf); }
static void  imp_Tag_Free(void)                            { BrdgOur_TagFree(); }

/* ======================================================================== *
 *  CVAR PROXY LAYER
 *
 *  Their renderer reads cvar fields (->value / ->integer / ->string /
 *  ->modified) directly off the cvar_t* it gets from Cvar_Get. Our cvar_t has
 *  a DIFFERENT layout (11 fields, int flags) than theirs (19 fields, 64-bit
 *  flags). So we never hand our cvar_t across: we keep a registry of
 *  THEIR-layout proxies, one per name, and sync them from our cvars.
 *
 *  SYNC DIRECTIONS (documented precisely):
 *    OURS -> PROXY (refresh): string, value, integer  — copied every frame and
 *        on Cvar_Get from the live our-cvar.
 *    modified: INDEPENDENT lifecycle. We cache our-cvar's modificationCount in
 *        the proxy registry; when it advances we set proxy->modified = qtrue.
 *        Their code clears proxy->modified locally (cv->modified = qfalse) after
 *        reacting — that clear stays LOCAL to the proxy and never touches our
 *        engine cvar (whose modified flag is engine-shared; clearing it could
 *        perturb engine logic). This gives their renderer a correct, private
 *        modified edge without corrupting engine state.
 *    PROXY -> OURS (write): only via Cvar_Set/SetValue adapters, which write
 *        through to our engine then refresh the proxy.
 *
 *  flags: their 64-bit cvarFlags_t is narrowed to our int on the way IN
 *  (Cvar_Get). The low bits (CVAR_ARCHIVE..CVAR_SERVERINFO_NOUPDATE, bits 0-13)
 *  share identical values between the two worlds; higher bits theirs defines
 *  (CVAR_SERVER_CREATED.. bit 14+) are logged-once and dropped — our engine has
 *  no equivalent and the renderer only needs them informationally.
 * ======================================================================== */

#define BRDG_MAX_CVARS 1024
/* our-side flag bits we can safely forward (bits 0..13 share values). */
#define BRDG_CVAR_FLAGMASK 0x00003FFFu

typedef struct
{
	void   *ourCvar;            /* opaque our-cvar_t* (stable across calls) */
	cvar_t  proxy;              /* THEIR-layout cvar handed to their renderer */
	int     lastModCount;       /* cached our modificationCount for edge detect */
	char    nameBuf[128];
	char    stringBuf[256];
} brdgCvarProxy_t;

static brdgCvarProxy_t g_cvars[BRDG_MAX_CVARS];
static int             g_numCvars;
static int             g_warnedHighFlags;

/* refresh proxy string/value/integer + modified-edge from the live our-cvar. */
static void Cvar_RefreshProxy(brdgCvarProxy_t *p)
{
	const char *s = "";
	float       v = 0.0f;
	int         iv = 0, modc = 0;

	BrdgOur_CvarRead(p->ourCvar, &s, &v, &iv, &modc);

	Q_strncpyz(p->stringBuf, s ? s : "", sizeof(p->stringBuf));
	p->proxy.string  = p->stringBuf;
	p->proxy.value   = v;
	p->proxy.integer = iv;

	if (modc != p->lastModCount)
	{
		p->proxy.modified = qtrue;
		p->lastModCount   = modc;
	}
}

static brdgCvarProxy_t *Cvar_FindProxy(const char *name)
{
	int i;
	for (i = 0; i < g_numCvars; ++i)
	{
		if (!Q_stricmp(g_cvars[i].nameBuf, name))
		{
			return &g_cvars[i];
		}
	}
	return NULL;
}

static cvar_t *imp_Cvar_Get(const char *name, const char *value, cvarFlags_t flags)
{
	brdgCvarProxy_t *p = Cvar_FindProxy(name);
	int              ourFlags = (int)(flags & BRDG_CVAR_FLAGMASK);

	if ((flags & ~(cvarFlags_t)BRDG_CVAR_FLAGMASK) != 0)
	{
		LogOnce(&g_warnedHighFlags,
		        "renderer2 bridge: dropping high cvar flag bits (>bit13) their world "
		        "defines but ours doesn't; low bits forwarded\n");
	}

	if (!p)
	{
		if (g_numCvars >= BRDG_MAX_CVARS)
		{
			BrdgOur_Error(0 /*ERR_FATAL*/, "renderer2 bridge: cvar proxy registry full");
			return NULL;
		}
		p = &g_cvars[g_numCvars++];
		memset(p, 0, sizeof(*p));
		Q_strncpyz(p->nameBuf, name, sizeof(p->nameBuf));
		p->ourCvar      = BrdgOur_CvarGet(name, value, ourFlags);
		p->proxy.name   = p->nameBuf;
		p->proxy.flags  = flags;
		p->lastModCount = -1;        /* force a modified edge on first refresh */
	}
	else
	{
		/* already proxied: ensure our engine sees this (idempotent) and OR flags */
		p->ourCvar     = BrdgOur_CvarGet(name, value, ourFlags);
		p->proxy.flags |= flags;
	}

	/* resetString aliases the live string buffer (their renderer reads it for
	 * display only; we never free our pointers, so this stays valid). */
	Cvar_RefreshProxy(p);
	p->proxy.resetString   = p->stringBuf;
	p->proxy.latchedString = NULL;
	return &p->proxy;
}

static void imp_Cvar_Set(const char *name, const char *value)
{
	brdgCvarProxy_t *p;
	BrdgOur_CvarSet(name, value);
	p = Cvar_FindProxy(name);
	if (p)
	{
		Cvar_RefreshProxy(p);
	}
}

static int g_warnedCheckRange;
static void imp_Cvar_CheckRange(cvar_t *cv, float minVal, float maxVal, qboolean shouldBeIntegral)
{
	(void)cv; (void)minVal; (void)maxVal; (void)shouldBeIntegral;
	LogOnce(&g_warnedCheckRange,
	        "renderer2 bridge: Cvar_CheckRange is a no-op (engine owns clamping)\n");
}

static int g_warnedSetDesc;
static void imp_Cvar_SetDescription(cvar_t *cv, const char *description)
{
	(void)cv; (void)description;
	LogOnce(&g_warnedSetDesc, "renderer2 bridge: Cvar_SetDescription is a no-op\n");
}

static int imp_Cvar_VariableIntegerValue(const char *var_name)
{
	return BrdgOur_CvarVariableIntegerValue(var_name);
}

/* called once per frame from the BeginFrame wrap — keep all proxies live. */
static void Cvar_SyncAll(void)
{
	int i;
	for (i = 0; i < g_numCvars; ++i)
	{
		Cvar_RefreshProxy(&g_cvars[i]);
	}
}

/* ======================================================================== *
 *  refimport: commands  (ADAPTER — drop description/completion)
 * ======================================================================== */

static void imp_Cmd_AddSystemCommand(const char *name, void (*cmd)(void),
                                     const char *description, void (*complete)(char *args, int argNum))
{
	(void)description; (void)complete;   /* our 2-arg Cmd_AddCommand has no slots */
	BrdgOur_CmdAddCommand(name, cmd);
}

static void imp_Cmd_RemoveSystemCommand(const char *name) { BrdgOur_CmdRemoveCommand(name); }
static int  imp_Cmd_Argc(void)                            { return BrdgOur_CmdArgc(); }
static char *imp_Cmd_Argv(int i)                          { return BrdgOur_CmdArgv(i); }
static void imp_Cmd_ExecuteText(int exec_when, const char *text)
{
	BrdgOur_CmdExecuteText(exec_when, text);
}

/* ======================================================================== *
 *  refimport: collision-debug  (SAFE STUB — menu has no collision model)
 * ======================================================================== */

static int g_warnedPointContents;
static int imp_CM_PointContents(const vec3_t p, clipHandle_t model)
{
	(void)p; (void)model;
	LogOnce(&g_warnedPointContents,
	        "renderer2 bridge: CM_PointContents stubbed to 0 (no clip model on the "
	        "DLL side; only used for decal/mark culling — safe at menu)\n");
	return 0;
}

static void imp_CM_DrawDebugSurface(void (*drawPoly)(int color, int numPoints, float *points))
{
	(void)drawPoly;   /* engine's CM_DrawDebugSurface is not on our v9 ri; no-op */
}

/* ======================================================================== *
 *  refimport: filesystem
 *  PASSTHROUGH for the members our v9 ri has; the streaming trio
 *  (FS_FOpenFileRead/FS_Read) our ri LACKS -> REAL adapters over FS_ReadFile:
 *  open reads the whole file into a bridge handle slot; Read memcpys from the
 *  offset; close frees. Bounded (a small handle table) and correct for the
 *  renderer's streaming reads (shader/skin parsing).
 * ======================================================================== */

static int  imp_FS_FileIsInPAK(const char *name, int *pChecksum) { return BrdgOur_FS_FileIsInPAK(name, pChecksum); }
static int  imp_FS_ReadFile(const char *name, void **buf)        { return BrdgOur_FS_ReadFile(name, buf); }
static void imp_FS_FreeFile(void *buf)                           { BrdgOur_FS_FreeFile(buf); }
static char **imp_FS_ListFiles(const char *n, const char *e, int *c) { return BrdgOur_FS_ListFiles(n, e, c); }
static void imp_FS_FreeFileList(char **list)                     { BrdgOur_FS_FreeFileList(list); }
static void imp_FS_WriteFile(const char *q, const void *b, int s) { BrdgOur_FS_WriteFile(q, b, s); }
static qboolean imp_FS_FileExists(const char *file)             { return BrdgOur_FS_FileExists(file) ? qtrue : qfalse; }

#define BRDG_MAX_FILES 16
typedef struct
{
	int    used;
	byte  *data;
	long   len;
	long   pos;
} brdgFile_t;
static brdgFile_t g_files[BRDG_MAX_FILES];

static long imp_FS_FOpenFileRead(const char *filename, fileHandle_t *file, qboolean uniqueFILE)
{
	int   i;
	void *buf = NULL;
	int   len;
	(void)uniqueFILE;

	/* PROBE PATH (file == NULL): every vendored caller that passes file==NULL
	 * (tr_bsp/tr_glsl/tr_image/tr_model/tr_font) uses this purely as an
	 * existence/length check and never reads through a handle — FS_Read is
	 * never called in the vendored tree today. Read+free immediately and
	 * return the length; do NOT occupy a handle slot (the old behavior leaked
	 * a slot + the whole file buffer per probe, exhausting the 16-slot table
	 * after 16 probes and making every later asset read as missing). */
	if (!file)
	{
		len = BrdgOur_FS_ReadFile(filename, &buf);
		if (buf) { BrdgOur_FS_FreeFile(buf); }
		return (len < 0) ? -1 : len;
	}

	len = BrdgOur_FS_ReadFile(filename, &buf);
	if (len < 0 || !buf)
	{
		*file = 0;
		if (buf) { BrdgOur_FS_FreeFile(buf); }
		return -1;
	}

	for (i = 0; i < BRDG_MAX_FILES; ++i)
	{
		if (!g_files[i].used)
		{
			g_files[i].used = 1;
			g_files[i].data = (byte *)buf;
			g_files[i].len  = len;
			g_files[i].pos  = 0;
			if (file) { *file = (fileHandle_t)(i + 1); }
			return len;
		}
	}

	BrdgOur_FS_FreeFile(buf);
	if (file) { *file = 0; }
	return -1;
}

static int imp_FS_Read(void *buffer, int len, fileHandle_t f)
{
	brdgFile_t *fh;
	long        avail;
	int         idx = (int)f - 1;

	if (idx < 0 || idx >= BRDG_MAX_FILES || !g_files[idx].used)
	{
		return 0;
	}
	fh    = &g_files[idx];
	avail = fh->len - fh->pos;
	if (len > avail) { len = (int)avail; }
	if (len <= 0)    { return 0; }
	memcpy(buffer, fh->data + fh->pos, (size_t)len);
	fh->pos += len;
	return len;
}

/* their refimport does not declare FS_FCloseFileRead; their tr_image loaders use
 * the open/read pair and the engine closes. We expose a close for completeness
 * used by our open adapter's lifetime, called from nowhere external — keep the
 * handle table from leaking by freeing on a full read is unsafe, so we free all
 * open handles at Shutdown (see Brdg_TheirsInit teardown is engine-driven).    */
static void Brdg_CloseAllFiles(void)
{
	int i;
	for (i = 0; i < BRDG_MAX_FILES; ++i)
	{
		if (g_files[i].used)
		{
			BrdgOur_FS_FreeFile(g_files[i].data);
			g_files[i].used = 0;
			g_files[i].data = NULL;
		}
	}
}

/* ======================================================================== *
 *  refimport: cinematics  (PASSTHROUGH)
 * ======================================================================== */

static void     imp_CIN_UploadCinematic(int handle)            { BrdgOur_CIN_UploadCinematic(handle); }
static int      imp_CIN_PlayCinematic(const char *a, int x, int y, int w, int h, int b)
{
	return BrdgOur_CIN_PlayCinematic(a, x, y, w, h, b);
}
static e_status imp_CIN_RunCinematic(int handle)
{
	return (e_status)BrdgOur_CIN_RunCinematic(handle);
}

/* ======================================================================== *
 *  refimport: AVI output  (SAFE STUB — no video capture in the DLL)
 * ======================================================================== */

static qboolean imp_CL_VideoRecording(void)                   { return qfalse; }
static void     imp_CL_WriteAVIVideoFrame(const byte *b, int s) { (void)b; (void)s; }

/* ======================================================================== *
 *  refimport: zlib  (ADAPTER -> linked zlib)  [only under FEATURE_PNG]
 * ======================================================================== */
#ifdef FEATURE_PNG
static int   imp_zlib_compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen)
{
	return compress(dest, destLen, source, sourceLen);
}
static uLong imp_zlib_crc32(uLong crc, const Bytef *buf, uInt len)
{
	return crc32(crc, buf, len);
}
#endif

/* ======================================================================== *
 *  refimport: Sys_* / input  (SAFE STUB — our engine owns these)
 * ======================================================================== */

static void imp_Sys_GLimpSafeInit(void) { }
static void imp_Sys_GLimpInit(void)     { }
static void imp_Sys_SetEnv(const char *name, const char *value) { (void)name; (void)value; }
static void imp_IN_Init(void)           { }   /* our engine owns input */
static void imp_IN_Shutdown(void)       { }
static void imp_IN_Restart(void)        { }

/* ======================================================================== *
 *  refimport: GLimp  (ADAPTER — the crux of the platform mapping)
 * ======================================================================== */

static int g_glewReady;

static void Brdg_FillGlconfigGL(glconfig_t *gc)
{
	const char *s;
	GLint       vp[4]   = { 0, 0, 0, 0 };
	GLint       maxTex  = 0;
	GLint       numExt  = 0;
	int         i;
	size_t      extLen  = 0;

	/* strings (core profile: VENDOR/RENDERER/VERSION are still valid; the
	 * EXTENSIONS string is deprecated -> build it via glGetStringi). */
	s = (const char *)glGetString(GL_VENDOR);
	Q_strncpyz(gc->vendor_string,   s ? s : "", sizeof(gc->vendor_string));
	s = (const char *)glGetString(GL_RENDERER);
	Q_strncpyz(gc->renderer_string, s ? s : "", sizeof(gc->renderer_string));
	s = (const char *)glGetString(GL_VERSION);
	Q_strncpyz(gc->version_string,  s ? s : "", sizeof(gc->version_string));

	gc->extensions_string[0] = '\0';
	glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);
	for (i = 0; i < numExt; ++i)
	{
		const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
		size_t      el;
		if (!e) { continue; }
		el = strlen(e);
		if (extLen + el + 2 >= sizeof(gc->extensions_string)) { break; }
		strcat(gc->extensions_string, e);
		strcat(gc->extensions_string, " ");
		extLen += el + 1;
	}

	/* GLSL version */
	s = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	Q_strncpyz(gc->shadingLanguageVersion, s ? s : "", sizeof(gc->shadingLanguageVersion));

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
	gc->maxTextureSize    = maxTex;
	gc->maxActiveTextures = 8;

	/* default-framebuffer viewport == drawable size right after context
	 * creation; a GL-pure way to learn width/height without SDL on this side. */
	glGetIntegerv(GL_VIEWPORT, vp);
	gc->vidWidth     = (vp[2] > 0) ? vp[2] : 640;
	gc->vidHeight    = (vp[3] > 0) ? vp[3] : 480;
	gc->windowWidth  = gc->vidWidth;
	gc->windowHeight = gc->vidHeight;
	gc->windowAspect = (gc->vidHeight != 0)
	                   ? (float)gc->vidWidth / (float)gc->vidHeight : 1.0f;
	gc->displayAspect = gc->windowAspect;

	gc->colorBits   = 32;
	gc->depthBits   = 24;
	gc->stencilBits = 8;
	gc->isFullscreen = qfalse;
	gc->deviceSupportsGamma = qtrue;
	gc->driverType   = GLDRV_ICD;
	gc->hardwareType = GLHW_GENERIC;
	gc->textureCompression = TC_NONE;

	gc->contextCombined = (3 << 8) | 3;   /* 3.3 */
	gc->glslMajorVersion = 3;
	gc->glslMinorVersion = 3;
}

static int g_warnedGlew;
static void imp_GLimp_Init(glconfig_t *glConfig, const char *glConfigString)
{
	GLenum err;
	(void)glConfigString;   /* we hardcode the 3.3-core request into our glimp */

	Brdg_Trace("imp_GLimp_Init: enter");
	/* 1. bring up the engine's SDL window + GL 3.3 core context (compat ladder
	 * lives engine-side). */
	BrdgOur_GLimp_Init(3, 3, 1 /*core*/, 0 /*debug*/);
	Brdg_Trace("imp_GLimp_Init: BrdgOur_GLimp_Init returned");

	/* 2. resolve GL entry points. GLEW under a core context needs experimental
	 * mode (else glGetStringi/extension queries it gates on the legacy string
	 * are skipped). */
	if (!g_glewReady)
	{
		glewExperimental = GL_TRUE;
		err = glewInit();
		if (err != GLEW_OK)
		{
			LogOnce(&g_warnedGlew, "renderer2 bridge: glewInit reported an error "
			        "(continuing; core entry points are usually still resolved)\n");
		}
		/* glewExperimental can leave a stale GL_INVALID_ENUM from its probe;
		 * swallow it so the renderer's first GL_CheckErrors is clean. */
		(void)glGetError();
		g_glewReady = 1;
	}
	Brdg_Trace("imp_GLimp_Init: glewInit done");

	/* 3. fill THEIR glconfig the way their engine normally would. */
	if (glConfig)
	{
		Brdg_FillGlconfigGL(glConfig);
	}

	/* 4. populate THEIR glConfig2 capability flags (textureNPOTAvailable,
	 * float/depth/draw-buffer caps, etc.) by running the renderer's own
	 * extension-detection. In stock ET:Legacy the engine calls
	 * re.InitOpenGLSubSystem() before BeginRegistration; our v9 engine never
	 * does, so without this glConfig2 stays all-zero. The most visible fallout
	 * was textureNPOTAvailable==qfalse, which makes R_CreateRenderImage round
	 * the screen-capture images (e.g. _currentRender) up to the next power of
	 * two (1920x1200 -> 2048x2048). RB_ColorCorrection then copies a 2048x2048
	 * region from the real 1920x1200 backbuffer but redraws it on a 1920x1200
	 * screen quad with 0..1 texcoords, squashing the entire 2D image (menu +
	 * cursor) toward the top-left while engine-side mouse hit-testing still
	 * uses true 1920x1200 — the reported "invisible barrier" near the bottom of
	 * the main menu. RE_InitOpenGl (re.InitOpenGL) builds the extension list and
	 * runs GLimp_InitExtensionsR2, which is where glConfig2's caps get set; it
	 * only queries GL (no context/window creation) so it is safe to call here
	 * after the context is live. */
	if (theirRE && theirRE->InitOpenGL)
	{
		Brdg_Trace("imp_GLimp_Init: calling theirRE->InitOpenGL (runs GLimp_InitExtensionsR2 -> fills glConfig2)");
		theirRE->InitOpenGL();
	}

	Brdg_Trace("imp_GLimp_Init: glconfig filled, returning");
}

static void imp_GLimp_Shutdown(void)   { BrdgOur_GLimp_Shutdown(); }
static void imp_GLimp_SwapFrame(void)  { BrdgOur_GLimp_EndFrame(); }
static void imp_GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256])
{
	BrdgOur_GLimp_SetGamma(red, green, blue);
}

static int g_warnedSplash;
static qboolean imp_GLimp_SplashImage(qboolean (*LoadSplashImage)(const char *name, byte *data,
                                      unsigned int size, unsigned int width, unsigned int height, uint8_t bytes))
{
	(void)LoadSplashImage;
	LogOnce(&g_warnedSplash, "renderer2 bridge: GLimp_SplashImage is a no-op\n");
	return qfalse;
}

/* ======================================================================== *
 *  Build their refimport.
 * ======================================================================== */
static void Brdg_BuildRefimport(void)
{
	memset(&theirRI, 0, sizeof(theirRI));

	theirRI.Printf                  = imp_Printf;
	theirRI.Error                   = imp_Error;
	theirRI.Milliseconds            = imp_Milliseconds;
	theirRI.RealTime                = imp_RealTime;

	theirRI.Hunk_Clear              = imp_Hunk_Clear;
	theirRI.Hunk_Alloc              = imp_Hunk_Alloc;
	theirRI.Hunk_AllocateTempMemory = imp_Hunk_AllocateTempMemory;
	theirRI.Hunk_FreeTempMemory     = imp_Hunk_FreeTempMemory;
	theirRI.Z_Malloc                = imp_Z_Malloc;
	theirRI.Free                    = imp_Free;
	theirRI.Tag_Free                = imp_Tag_Free;

	theirRI.Cvar_Get                = imp_Cvar_Get;
	theirRI.Cvar_Set                = imp_Cvar_Set;
	theirRI.Cvar_CheckRange         = imp_Cvar_CheckRange;
	theirRI.Cvar_SetDescription     = imp_Cvar_SetDescription;
	theirRI.Cvar_VariableIntegerValue = imp_Cvar_VariableIntegerValue;

	theirRI.Cmd_AddSystemCommand    = imp_Cmd_AddSystemCommand;
	theirRI.Cmd_RemoveSystemCommand = imp_Cmd_RemoveSystemCommand;
	theirRI.Cmd_Argc                = imp_Cmd_Argc;
	theirRI.Cmd_Argv                = imp_Cmd_Argv;
	theirRI.Cmd_ExecuteText         = imp_Cmd_ExecuteText;

	theirRI.CM_PointContents        = imp_CM_PointContents;
	theirRI.CM_DrawDebugSurface     = imp_CM_DrawDebugSurface;

	theirRI.FS_FileIsInPAK          = imp_FS_FileIsInPAK;
	theirRI.FS_ReadFile             = imp_FS_ReadFile;
	theirRI.FS_FreeFile             = imp_FS_FreeFile;
	theirRI.FS_ListFiles            = imp_FS_ListFiles;
	theirRI.FS_FreeFileList         = imp_FS_FreeFileList;
	theirRI.FS_WriteFile            = imp_FS_WriteFile;
	theirRI.FS_FileExists           = imp_FS_FileExists;
	theirRI.FS_FOpenFileRead        = imp_FS_FOpenFileRead;
	theirRI.FS_Read                 = imp_FS_Read;

	theirRI.CIN_UploadCinematic     = imp_CIN_UploadCinematic;
	theirRI.CIN_PlayCinematic       = imp_CIN_PlayCinematic;
	theirRI.CIN_RunCinematic        = imp_CIN_RunCinematic;

	theirRI.CL_VideoRecording       = imp_CL_VideoRecording;
	theirRI.CL_WriteAVIVideoFrame   = imp_CL_WriteAVIVideoFrame;

#ifdef FEATURE_PNG
	theirRI.zlib_compress           = imp_zlib_compress;
	theirRI.zlib_crc32              = imp_zlib_crc32;
#endif

	theirRI.Sys_GLimpSafeInit       = imp_Sys_GLimpSafeInit;
	theirRI.Sys_GLimpInit           = imp_Sys_GLimpInit;
	theirRI.Sys_SetEnv              = imp_Sys_SetEnv;
	theirRI.IN_Init                 = imp_IN_Init;
	theirRI.IN_Shutdown             = imp_IN_Shutdown;
	theirRI.IN_Restart              = imp_IN_Restart;

	theirRI.GLimp_Init              = imp_GLimp_Init;
	theirRI.GLimp_Shutdown          = imp_GLimp_Shutdown;
	theirRI.GLimp_SwapFrame         = imp_GLimp_SwapFrame;
	theirRI.GLimp_SetGamma          = imp_GLimp_SetGamma;
	theirRI.GLimp_SplashImage       = imp_GLimp_SplashImage;
}

/* ======================================================================== *
 *  REFEXPORT WRAP — neutral brdgReExport_t over their refexport.
 *
 *  Most members forward 1:1 (engine struct pointers come in as void* that are
 *  already laid out for their renderer — guaranteed by the layout checker).
 *  The drifted ones are translated here:
 *    - BeginRegistration: their renderer fills bridgeGlconfig; we copy the
 *      common-prefix fields into the engine's glconfig (our layout, by void*).
 *    - BeginFrame: their refexport is (void); we ignore the stereo arg.
 *    - RegisterFont: their 4-arg (name,size,void*,extended); fontInfo_t layout
 *      matches (layout-checked) so we pass the pointer with extended=qfalse.
 *    - AddRefEntityToScene / LerpTag: our refEntity_t lacks the trailing
 *      refSkeleton_t; translate into a their-layout entity with skeleton
 *      zeroed (SK_INVALID) — correct for the menu (no skeletal scene models).
 * ======================================================================== */

/* --- glconfig copy-back: their glconfig -> our glconfig (common prefix). ---
 * We list every OUR field explicitly (never memcpy across layouts). The our
 * glconfig is reached as void* (its layout lives in the ours-TU); to write it
 * field-by-field here we'd need our header — which we cannot include. SOLUTION:
 * the ours-TU exposes a typed copy via a neutral callback (declared in
 * tr2_bridge.h). We pass the source (their glconfig) field values as primitives. */

static void re_BeginRegistration(void *ourGlconfig)
{
	Brdg_Trace("re_BeginRegistration: calling theirRE->BeginRegistration");
	theirRE->BeginRegistration(&bridgeGlconfig);
	Brdg_Trace("re_BeginRegistration: theirRE->BeginRegistration returned");
	BrdgOur_GlconfigCopyBack(
		bridgeGlconfig.renderer_string, bridgeGlconfig.vendor_string,
		bridgeGlconfig.version_string, bridgeGlconfig.extensions_string,
		bridgeGlconfig.maxTextureSize, bridgeGlconfig.maxActiveTextures,
		bridgeGlconfig.colorBits, bridgeGlconfig.depthBits, bridgeGlconfig.stencilBits,
		/* enum-valued fields pass as int; values align across both glconfig_t
		 * definitions (classic q3 enums on both sides). */
		(int)bridgeGlconfig.driverType, (int)bridgeGlconfig.hardwareType,
		(int)bridgeGlconfig.deviceSupportsGamma, (int)bridgeGlconfig.textureCompression,
		bridgeGlconfig.vidWidth, bridgeGlconfig.vidHeight, bridgeGlconfig.windowAspect,
		bridgeGlconfig.displayFrequency, (int)bridgeGlconfig.isFullscreen, ourGlconfig);
}

static void re_Shutdown(int destroyWindow)
{
	theirRE->Shutdown((qboolean)destroyWindow);
	Brdg_CloseAllFiles();
}
static int  re_RegisterModel(const char *n)            { return theirRE->RegisterModel(n); }
static int  re_RegisterModelAllLODs(const char *n)     { return theirRE->RegisterModelAllLODs(n); }
static int  re_RegisterSkin(const char *n)             { return theirRE->RegisterSkin(n); }
static int  re_RegisterShader(const char *n)           { return theirRE->RegisterShader(n); }
static int  re_RegisterShaderNoMip(const char *n)      { return theirRE->RegisterShaderNoMip(n); }
static void re_RegisterFont(const char *fn, int ps, void *ourFontInfo)
{
	/* fontInfo_t layout matches (layout-checked); pass with extended=qfalse. */
	theirRE->RegisterFont(fn, ps, ourFontInfo, qfalse);
}
static void re_LoadWorld(const char *n)                { theirRE->LoadWorld(n); }
static int  re_GetSkinModel(int s, const char *t, char *n) { return theirRE->GetSkinModel(s, t, n); }
static int  re_GetShaderFromModel(int m, int sn, int wl)   { return theirRE->GetShaderFromModel(m, sn, wl); }
static void re_SetWorldVisData(const unsigned char *vis)   { theirRE->SetWorldVisData(vis); }
static void re_EndRegistration(void)                   { theirRE->EndRegistration(); }
static void re_ClearScene(void)                        { theirRE->ClearScene(); }

/* refEntity_t translation: copy the common prefix (identical layout up to but
 * excluding their trailing skeleton) and zero the skeleton. */
static void re_AddRefEntityToScene(const void *ourRefEntity)
{
	refEntity_t ent;
	memset(&ent, 0, sizeof(ent));
	/* offsetof(refEntity_t, skeleton) is exactly the size of OUR refEntity_t
	 * (its last field is entityNum, same as theirs before skeleton). The layout
	 * checker verifies the shared prefix; copy that many bytes. */
	memcpy(&ent, ourRefEntity, offsetof(refEntity_t, skeleton));
#if defined(USE_REFENTITY_ANIMATIONSYSTEM)
	ent.skeleton.type = SK_INVALID;
#endif
	theirRE->AddRefEntityToScene(&ent);
}

static int  re_LightForPoint(float *p, float *a, float *d, float *ld)
{
	return theirRE->LightForPoint(p, a, d, ld);
}
static void re_AddPolyToScene(int h, int nv, const void *v)
{
	theirRE->AddPolyToScene(h, nv, (const polyVert_t *)v);
}
static void re_AddPolysToScene(int h, int nv, const void *v, int np)
{
	theirRE->AddPolysToScene(h, nv, (const polyVert_t *)v, np);
}
static void re_AddLightToScene(const float *org, float radius, float intensity,
                               float r, float g, float b, int h, int flags)
{
	theirRE->AddLightToScene(org, radius, intensity, r, g, b, h, flags);
}
static void re_AddCoronaToScene(const float *org, float r, float g, float b,
                                float scale, int id, int visible)
{
	theirRE->AddCoronaToScene(org, r, g, b, scale, id, (qboolean)visible);
}
static void re_SetFog(int fogvar, int v1, int v2, float r, float g, float b, float dens)
{
	theirRE->SetFog(fogvar, v1, v2, r, g, b, dens);
}
static void re_RenderScene(const void *ourRefdef)
{
	theirRE->RenderScene((const refdef_t *)ourRefdef);
}
static void re_SetColor(const float *rgba)             { theirRE->SetColor(rgba); }
static void re_DrawStretchPic(float x, float y, float w, float h,
                              float s1, float t1, float s2, float t2, int sh)
{
	theirRE->DrawStretchPic(x, y, w, h, s1, t1, s2, t2, sh);
}
static void re_DrawRotatedPic(float x, float y, float w, float h,
                              float s1, float t1, float s2, float t2, int sh, float ang)
{
	theirRE->DrawRotatedPic(x, y, w, h, s1, t1, s2, t2, sh, ang);
}
static void re_DrawStretchPicGradient(float x, float y, float w, float h,
                                      float s1, float t1, float s2, float t2, int sh,
                                      const float *grad, int gradType)
{
	theirRE->DrawStretchPicGradient(x, y, w, h, s1, t1, s2, t2, sh, grad, gradType);
}
static void re_Add2dPolys(void *polys, int nv, int sh)
{
	theirRE->Add2dPolys((polyVert_t *)polys, nv, sh);
}
static void re_DrawStretchRaw(int x, int y, int w, int h, int cols, int rows,
                              const unsigned char *data, int client, int dirty)
{
	theirRE->DrawStretchRaw(x, y, w, h, cols, rows, data, client, (qboolean)dirty);
}
static void re_UploadCinematic(int w, int h, int cols, int rows,
                               const unsigned char *data, int client, int dirty)
{
	theirRE->UploadCinematic(w, h, cols, rows, data, client, (qboolean)dirty);
}
static void re_BeginFrame(int stereoFrame)
{
	(void)stereoFrame;          /* their BeginFrame is (void) */
	Cvar_SyncAll();             /* refresh all cvar proxies once per frame */
	theirRE->BeginFrame();
}
static void re_EndFrame(int *fe, int *be)             { theirRE->EndFrame(fe, be); }
static int  re_MarkFragments(int numPoints, const void *points, const float *projection,
                             int maxPoints, float *pointBuffer, int maxFragments, void *fragmentBuffer)
{
	return theirRE->MarkFragments(numPoints, (const vec3_t *)points, projection,
	                              maxPoints, pointBuffer, maxFragments, (markFragment_t *)fragmentBuffer);
}
static void re_ProjectDecal(int sh, int np, void *points, float *proj, float *color,
                            int lifeTime, int fadeTime)
{
	theirRE->ProjectDecal(sh, np, (vec3_t *)points, proj, color, lifeTime, fadeTime);
}
static void re_ClearDecals(void)                      { theirRE->ClearDecals(); }
static int  re_LerpTag(void *tag, const void *refent, const char *tagName, int startIndex)
{
	/* refent crosses uncopied here; their LerpTag reads only prefix fields
	 * (frame/oldframe/backlerp/hModel) — all in the shared prefix. Pass through;
	 * if a skeletal tag path is hit it would read ent->skeleton, but tags on the
	 * menu use frame models. Translate defensively via the same prefix copy. */
	refEntity_t ent;
	memset(&ent, 0, sizeof(ent));
	memcpy(&ent, refent, offsetof(refEntity_t, skeleton));
#if defined(USE_REFENTITY_ANIMATIONSYSTEM)
	ent.skeleton.type = SK_INVALID;
#endif
	return theirRE->LerpTag((orientation_t *)tag, &ent, tagName, startIndex);
}
static void re_ModelBounds(int m, float *mins, float *maxs) { theirRE->ModelBounds(m, mins, maxs); }
static void re_RemapShader(const char *o, const char *n, const char *off) { theirRE->RemapShader(o, n, off); }
static void re_DrawDebugPolygon(int color, int np, float *points) { theirRE->DrawDebugPolygon(color, np, points); }
static void re_DrawDebugText(const float *org, float r, float g, float b,
                             const char *text, int neverOcclude)
{
	theirRE->DrawDebugText(org, r, g, b, text, (qboolean)neverOcclude);
}
static int  re_GetEntityToken(char *buffer, int size)
{
	return theirRE->GetEntityToken(buffer, (size_t)size);
}
static void re_AddPolyBufferToScene(void *pb)         { theirRE->AddPolyBufferToScene((polyBuffer_t *)pb); }
static void re_SetGlobalFog(int restore, int dur, float r, float g, float b, float dfo)
{
	theirRE->SetGlobalFog((qboolean)restore, dur, r, g, b, dfo);
}
static int  re_inPVS(const float *p1, const float *p2) { return theirRE->inPVS(p1, p2); }
static void re_purgeCache(void)                       { theirRE->purgeCache(); }
static int  re_LoadDynamicShader(const char *name, const char *text)
{
	return theirRE->LoadDynamicShader(name, text);
}
static void re_RenderToTexture(int tid, int x, int y, int w, int h)
{
	theirRE->RenderToTexture(tid, x, y, w, h);
}
static int  re_GetTextureId(const char *name)         { return theirRE->GetTextureId(name); }
static void re_Finish(void)                           { theirRE->Finish(); }

static brdgReExport_t g_reExport;

static void Brdg_BuildReExport(void)
{
	memset(&g_reExport, 0, sizeof(g_reExport));
	g_reExport.Shutdown               = re_Shutdown;
	g_reExport.BeginRegistration      = re_BeginRegistration;
	g_reExport.RegisterModel          = re_RegisterModel;
	g_reExport.RegisterModelAllLODs   = re_RegisterModelAllLODs;
	g_reExport.RegisterSkin           = re_RegisterSkin;
	g_reExport.RegisterShader         = re_RegisterShader;
	g_reExport.RegisterShaderNoMip    = re_RegisterShaderNoMip;
	g_reExport.RegisterFont           = re_RegisterFont;
	g_reExport.LoadWorld              = re_LoadWorld;
	g_reExport.GetSkinModel           = re_GetSkinModel;
	g_reExport.GetShaderFromModel     = re_GetShaderFromModel;
	g_reExport.SetWorldVisData        = re_SetWorldVisData;
	g_reExport.EndRegistration        = re_EndRegistration;
	g_reExport.ClearScene             = re_ClearScene;
	g_reExport.AddRefEntityToScene    = re_AddRefEntityToScene;
	g_reExport.LightForPoint          = re_LightForPoint;
	g_reExport.AddPolyToScene         = re_AddPolyToScene;
	g_reExport.AddPolysToScene        = re_AddPolysToScene;
	g_reExport.AddLightToScene        = re_AddLightToScene;
	g_reExport.AddCoronaToScene       = re_AddCoronaToScene;
	g_reExport.SetFog                 = re_SetFog;
	g_reExport.RenderScene            = re_RenderScene;
	g_reExport.SetColor               = re_SetColor;
	g_reExport.DrawStretchPic         = re_DrawStretchPic;
	g_reExport.DrawRotatedPic         = re_DrawRotatedPic;
	g_reExport.DrawStretchPicGradient = re_DrawStretchPicGradient;
	g_reExport.Add2dPolys             = re_Add2dPolys;
	g_reExport.DrawStretchRaw         = re_DrawStretchRaw;
	g_reExport.UploadCinematic        = re_UploadCinematic;
	g_reExport.BeginFrame             = re_BeginFrame;
	g_reExport.EndFrame               = re_EndFrame;
	g_reExport.MarkFragments          = re_MarkFragments;
	g_reExport.ProjectDecal           = re_ProjectDecal;
	g_reExport.ClearDecals            = re_ClearDecals;
	g_reExport.LerpTag                = re_LerpTag;
	g_reExport.ModelBounds            = re_ModelBounds;
	g_reExport.RemapShader            = re_RemapShader;
	g_reExport.DrawDebugPolygon       = re_DrawDebugPolygon;
	g_reExport.DrawDebugText          = re_DrawDebugText;
	g_reExport.GetEntityToken         = re_GetEntityToken;
	g_reExport.AddPolyBufferToScene   = re_AddPolyBufferToScene;
	g_reExport.SetGlobalFog           = re_SetGlobalFog;
	g_reExport.inPVS                  = re_inPVS;
	g_reExport.purgeCache             = re_purgeCache;
	g_reExport.LoadDynamicShader      = re_LoadDynamicShader;
	g_reExport.RenderToTexture        = re_RenderToTexture;
	g_reExport.GetTextureId           = re_GetTextureId;
	g_reExport.Finish                 = re_Finish;
}

const brdgReExport_t *Brdg_GetReExport(void)
{
	return theirRE ? &g_reExport : NULL;
}

/* ======================================================================== *
 *  Init handshake (called from the ours-TU's GetRefAPI).
 * ======================================================================== */
int Brdg_TheirsInit(int engineApiVersion)
{
	char msg[512];
	(void)engineApiVersion;

	/* layout check FIRST — converts silent struct corruption into a clean fail. */
	if (!Brdg_LayoutCheck(msg, sizeof(msg)))
	{
		BrdgOur_Print(0 /*PRINT_ALL*/, "renderer2 bridge: LAYOUT CHECK FAILED:\n");
		BrdgOur_Print(0, msg);
		return 0;
	}

	Brdg_BuildRefimport();

	theirRE = ETL_GetRefAPI(REF_API_VERSION, &theirRI);
	if (!theirRE)
	{
		BrdgOur_Print(0, "renderer2 bridge: their GetRefAPI returned NULL\n");
		return 0;
	}

	Brdg_BuildReExport();
	return 1;
}
