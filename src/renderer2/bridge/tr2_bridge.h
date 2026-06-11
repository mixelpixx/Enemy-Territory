/*
===========================================================================
ET-RM renderer2 — BRIDGE neutral cross-TU contract (R2-2 / Task 3).

THE INCLUDE-ISOLATION PROBLEM
-----------------------------
The bridge must speak BOTH header worlds:
  * OURS   — src/renderer/tr_public.h (refimport_t/refexport_t v9, glimpParams_t),
             src/cgame/tr_types.h, src/game/q_shared.h.
  * THEIRS — src/renderer2/etlhdr/renderercommon/tr_public.h (v10) + their
             q_shared.h subset.
Both worlds define IDENTICALLY-NAMED but DIFFERENT types (refimport_t,
refexport_t, glconfig_t, cvar_t, qtime_t, fontInfo_t ...). A single TU cannot
include both — the second tr_public.h's `typedef struct {...} refexport_t;`
collides with the first.

ARCHITECTURE
------------
The bridge is split into per-world translation units that NEVER include each
other's headers, plus this NEUTRAL header which uses ONLY primitive C types,
void*, and function-pointer typedefs over those — NO engine types at all:

  tr2_bridge_ours.c   — includes OUR headers only. Defines the exported
                        GetRefAPI(int, our refimport*). Stores our `ri`. Builds
                        OUR refexport_t by wrapping the neutral fn-ptrs that
                        tr2_bridge_theirs.c published. Exposes our-ri access to
                        the theirs-TU through the Brdg_Our* callbacks below.

  tr2_bridge_theirs.c — includes THEIR headers only. Builds THEIR refimport_t
                        (passthroughs/adapters/stubs that call BACK into the
                        ours-TU via the Brdg_Our* callbacks), calls
                        ETL_GetRefAPI(10, &theirRI), and stores their refexport.
                        Publishes neutral fn-ptrs (Brdg_Re*) the ours-TU uses.

  tr2_layout_ours.c /
  tr2_layout_theirs.c — each emits a layout table (sizeof + field offsets) for
                        every struct that crosses the boundary by pointer. The
                        neutral checker (tr2_layout_check) compares them at init.

All cross-TU calls go through the function pointers / extern functions declared
here. Pointers to engine structs are carried as `void *` and re-cast on the
far side (the layout checker guarantees the cast is safe for pass-through types;
translated types are converted explicitly inside the owning TU).

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#ifndef TR2_BRIDGE_H
#define TR2_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ *
 *  OURS-TU -> THEIRS-TU : initialization handshake.
 *
 *  tr2_bridge_ours.c calls Brdg_TheirsInit() from inside GetRefAPI, passing
 *  the apiVersion the engine requested (9). The theirs-TU builds their
 *  refimport, calls ETL_GetRefAPI(THEIR_REF_API_VERSION, &theirRI), runs the
 *  layout check, and returns 1 on success (their refexport captured), 0 on
 *  failure (mismatched api / layout / null re) — in which case GetRefAPI
 *  returns NULL and the engine falls back to gl1.
 * ------------------------------------------------------------------------ */
int  Brdg_TheirsInit(int engineApiVersion);

/* ------------------------------------------------------------------------ *
 *  THEIRS-TU -> OURS-TU : our-ri access callbacks.
 *
 *  The theirs-TU cannot see OUR refimport_t, so it reaches our engine services
 *  exclusively through these neutral shims implemented in tr2_bridge_ours.c.
 *  Only the subset their refimport actually needs is exposed; signatures use
 *  primitives + void* only.
 * ------------------------------------------------------------------------ */

/* logging / fatal */
void  BrdgOur_Print(int printLevel, const char *msg);     /* pre-formatted text */
void  BrdgOur_Error(int errorLevel, const char *msg);     /* noreturn on engine side */

/* timing */
int   BrdgOur_Milliseconds(void);

/* hunk / zone memory (our ri takes int sizes; theirs passes size_t — clamped) */
void  BrdgOur_HunkClear(void);
void *BrdgOur_HunkAlloc(size_t size, int pref);
void *BrdgOur_HunkAllocTemp(size_t size);
void  BrdgOur_HunkFreeTemp(void *block);
void *BrdgOur_ZMalloc(int bytes);
void  BrdgOur_Free(void *buf);
void  BrdgOur_TagFree(void);

/* cvars — our cvar_t is opaque here; returned as void*. The cvar PROXY layer
 * (their-layout proxies) lives in the theirs-TU; these just reach our engine. */
void *BrdgOur_CvarGet(const char *name, const char *value, int flags);
void  BrdgOur_CvarSet(const char *name, const char *value);
int   BrdgOur_CvarVariableIntegerValue(const char *name);
/* read the live fields of an our-cvar by pointer (filled into out-params). */
void  BrdgOur_CvarRead(void *ourCvar,
                       const char **string,
                       float *value,
                       int *integer,
                       int *modificationCount);

/* commands */
void  BrdgOur_CmdAddCommand(const char *name, void (*fn)(void));
void  BrdgOur_CmdRemoveCommand(const char *name);
int   BrdgOur_CmdArgc(void);
char *BrdgOur_CmdArgv(int i);
void  BrdgOur_CmdExecuteText(int execWhen, const char *text);

/* filesystem (our v9 refimport) */
int   BrdgOur_FS_FileIsInPAK(const char *name, int *pChecksum);
int   BrdgOur_FS_ReadFile(const char *name, void **buf);
void  BrdgOur_FS_FreeFile(void *buf);
char **BrdgOur_FS_ListFiles(const char *name, const char *ext, int *num);
void  BrdgOur_FS_FreeFileList(char **list);
void  BrdgOur_FS_WriteFile(const char *qpath, const void *buffer, int size);
int   BrdgOur_FS_FileExists(const char *file);

/* cinematics */
void  BrdgOur_CIN_UploadCinematic(int handle);
int   BrdgOur_CIN_PlayCinematic(const char *arg0, int x, int y, int w, int h, int bits);
int   BrdgOur_CIN_RunCinematic(int handle);

/* glconfig copy-back: the theirs-TU passes the common-prefix values their
 * renderer filled into bridgeGlconfig; the ours-TU writes them field-by-field
 * into the engine's (our-layout) glconfig reached via ourGlconfig (void*). */
void  BrdgOur_GlconfigCopyBack(
	const char *renderer, const char *vendor, const char *version, const char *extensions,
	int maxTextureSize, int maxActiveTextures,
	int colorBits, int depthBits, int stencilBits,
	int vidWidth, int vidHeight, float windowAspect, int displayFrequency,
	int isFullscreen, void *ourGlconfig);

/* GL window/context platform services (engine owns the SDL window) */
void  BrdgOur_GLimp_Init(int major, int minor, int coreProfile, int debugContext);
void  BrdgOur_GLimp_Shutdown(void);
void  BrdgOur_GLimp_EndFrame(void);
void  BrdgOur_GLimp_SetGamma(unsigned char *red, unsigned char *green, unsigned char *blue);
void *BrdgOur_GL_GetProcAddress(const char *name);

/* ------------------------------------------------------------------------ *
 *  THEIRS-TU -> OURS-TU : their refexport, published as neutral fn-ptrs.
 *
 *  After Brdg_TheirsInit succeeds, the theirs-TU fills this table with thin
 *  wrappers around their refexport members (translating signatures/structs
 *  that drift). The ours-TU reads it to assemble OUR refexport_t. Engine
 *  struct pointers are passed as void* (pass-through types) or already
 *  translated inside the wrapper (drifted types).
 *
 *  Naming mirrors OUR refexport_t member set (the v9 shape the engine expects).
 *  Members our struct has but theirs lacks (SaveViewParms/RestoreViewParms) are
 *  filled by the ours-TU with logged-once no-ops and are NOT in this table.
 * ------------------------------------------------------------------------ */
typedef struct
{
	void (*Shutdown)(int destroyWindow);
	void (*BeginRegistration)(void *ourGlconfig);          /* translates glconfig copy-back */
	int  (*RegisterModel)(const char *name);
	int  (*RegisterModelAllLODs)(const char *name);
	int  (*RegisterSkin)(const char *name);
	int  (*RegisterShader)(const char *name);
	int  (*RegisterShaderNoMip)(const char *name);
	void (*RegisterFont)(const char *fontName, int pointSize, void *ourFontInfo);
	void (*LoadWorld)(const char *name);
	int  (*GetSkinModel)(int skinid, const char *type, char *name);
	int  (*GetShaderFromModel)(int modelid, int surfnum, int withlightmap);
	void (*SetWorldVisData)(const unsigned char *vis);
	void (*EndRegistration)(void);

	void (*ClearScene)(void);
	void (*AddRefEntityToScene)(const void *ourRefEntity);  /* translates refEntity_t */
	int  (*LightForPoint)(float *point, float *amb, float *dir, float *lightDir);
	void (*AddPolyToScene)(int hShader, int numVerts, const void *verts);
	void (*AddPolysToScene)(int hShader, int numVerts, const void *verts, int numPolys);
	void (*AddLightToScene)(const float *org, float radius, float intensity,
	                        float r, float g, float b, int hShader, int flags);
	void (*AddCoronaToScene)(const float *org, float r, float g, float b,
	                         float scale, int id, int visible);
	void (*SetFog)(int fogvar, int var1, int var2, float r, float g, float b, float density);
	void (*RenderScene)(const void *ourRefdef);             /* pass-through (layout checked) */

	void (*SetColor)(const float *rgba);
	void (*DrawStretchPic)(float x, float y, float w, float h,
	                       float s1, float t1, float s2, float t2, int hShader);
	void (*DrawRotatedPic)(float x, float y, float w, float h,
	                       float s1, float t1, float s2, float t2, int hShader, float angle);
	void (*DrawStretchPicGradient)(float x, float y, float w, float h,
	                               float s1, float t1, float s2, float t2, int hShader,
	                               const float *gradientColor, int gradientType);
	void (*Add2dPolys)(void *polys, int numverts, int hShader);
	void (*DrawStretchRaw)(int x, int y, int w, int h, int cols, int rows,
	                       const unsigned char *data, int client, int dirty);
	void (*UploadCinematic)(int w, int h, int cols, int rows,
	                        const unsigned char *data, int client, int dirty);

	void (*BeginFrame)(int stereoFrame);                    /* theirs is (void) */
	void (*EndFrame)(int *frontEndMsec, int *backEndMsec);

	int  (*MarkFragments)(int numPoints, const void *points, const float *projection,
	                      int maxPoints, float *pointBuffer,
	                      int maxFragments, void *fragmentBuffer);
	void (*ProjectDecal)(int hShader, int numPoints, void *points,
	                     float *projection, float *color, int lifeTime, int fadeTime);
	void (*ClearDecals)(void);

	int  (*LerpTag)(void *tag, const void *refent, const char *tagName, int startIndex);
	void (*ModelBounds)(int model, float *mins, float *maxs);
	void (*RemapShader)(const char *oldShader, const char *newShader, const char *offsetTime);
	void (*DrawDebugPolygon)(int color, int numpoints, float *points);
	void (*DrawDebugText)(const float *org, float r, float g, float b,
	                      const char *text, int neverOcclude);
	int  (*GetEntityToken)(char *buffer, int size);
	void (*AddPolyBufferToScene)(void *pPolyBuffer);
	void (*SetGlobalFog)(int restore, int duration, float r, float g, float b, float depthForOpaque);
	int  (*inPVS)(const float *p1, const float *p2);
	void (*purgeCache)(void);
	int  (*LoadDynamicShader)(const char *shadername, const char *shadertext);
	void (*RenderToTexture)(int textureid, int x, int y, int w, int h);
	int  (*GetTextureId)(const char *imagename);
	void (*Finish)(void);
} brdgReExport_t;

/* The theirs-TU's published table; valid only after Brdg_TheirsInit() == 1. */
extern const brdgReExport_t *Brdg_GetReExport(void);

/* ------------------------------------------------------------------------ *
 *  LAYOUT CHECKER (neutral).
 *
 *  Each per-world layout TU exposes a function returning its table of
 *  {name, size, offsets...} entries. The neutral checker walks both, matching
 *  by name, and reports the first mismatch. Returns 1 on full agreement.
 *  On mismatch it fills *outMsg with a human-readable description.
 * ------------------------------------------------------------------------ */
#define BRDG_LAYOUT_MAX_FIELDS 8

typedef struct
{
	const char *name;                          /* type name, e.g. "refdef_t" */
	size_t      size;                          /* sizeof(type) */
	int         numFields;                     /* number of tracked offsets */
	const char *fieldName[BRDG_LAYOUT_MAX_FIELDS];
	size_t      fieldOff[BRDG_LAYOUT_MAX_FIELDS];
} brdgLayoutEntry_t;

/* implemented in tr2_layout_ours.c / tr2_layout_theirs.c */
const brdgLayoutEntry_t *Brdg_LayoutOurs(int *count);
const brdgLayoutEntry_t *Brdg_LayoutTheirs(int *count);

/* implemented in tr2_layout_check.c — 1 = all pass-through types agree. */
int  Brdg_LayoutCheck(char *outMsg, size_t outMsgSize);

#ifdef __cplusplus
}
#endif

#endif /* TR2_BRIDGE_H */
