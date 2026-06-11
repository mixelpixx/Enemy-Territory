/*
===========================================================================
ET-RM renderer2 — BRIDGE, OURS-side translation unit (R2-2 / Task 3).

Includes ONLY our engine headers (refimport_t/refexport_t v9, glimpParams_t,
tr_types.h, q_shared.h). Responsibilities:

  1. Export GetRefAPI(int apiVersion, refimport_t *ourRI) — the DLL's sole
     linker export (via renderer2.def). Stores ourRI, hands control to the
     theirs-TU (Brdg_TheirsInit) which builds their refimport, calls their
     GetRefAPI, and runs the layout check.
  2. Implement the BrdgOur_* neutral callbacks the theirs-TU uses to reach our
     engine services (it cannot see our refimport_t).
  3. Assemble OUR refexport_t from the neutral brdgReExport_t table the
     theirs-TU published, wrapping each member back into our v9 shapes.
     Members our struct has but theirs lacks (SaveViewParms / RestoreViewParms)
     are filled with logged-once no-ops here.

This TU MUST NOT include any etlhdr/ header — that is the include-isolation
contract (see tr2_bridge.h).

GPLv3 (ET-RM original glue; no third-party code).
===========================================================================
*/

#include "../../game/q_shared.h"     /* qboolean/vec types, PRINT_*, ha_pref */
#include "../../renderer/tr_public.h" /* OUR refimport_t/refexport_t v9, glimpParams_t */

#include "tr2_bridge.h"

#include <string.h>

/* local NUL-terminated bounded copy — avoids depending on the vendored
 * Q_strncpyz (which lives in the core lib compiled against THEIR headers). */
static void Brdg_strncpyz(char *dst, const char *src, size_t size)
{
	if (!dst || size == 0)
	{
		return;
	}
	if (!src)
	{
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, size - 1);
	dst[size - 1] = '\0';
}

/* GLimp_* are declared in our renderer's tr_local.h; the DLL provides them via
 * the refimport (ri). We only ever call them through ri, so no extern needed. */

/* ------------------------------------------------------------------------ *
 *  Our-side state.
 * ------------------------------------------------------------------------ */
static refimport_t ri;          /* the engine's v9 refimport (copied by value) */
static refexport_t re;          /* the v9 refexport we hand back to the engine */
static qboolean    g_haveRI = qfalse;

/* one-shot logging helper for stub paths */
static void Brdg_LogOnce(qboolean *flag, const char *msg)
{
	if (*flag)
	{
		return;
	}
	*flag = qtrue;
	if (g_haveRI && ri.Printf)
	{
		ri.Printf(PRINT_DEVELOPER, "%s", msg);
	}
}

/* ======================================================================== *
 *  BrdgOur_* — neutral callbacks into our engine (called from theirs-TU).
 * ======================================================================== */

void BrdgOur_Print(int printLevel, const char *msg)
{
	if (g_haveRI && ri.Printf)
	{
		/* msg is already fully formatted by the theirs-TU; pass as literal. */
		ri.Printf(printLevel, "%s", msg);
	}
}

void BrdgOur_Error(int errorLevel, const char *msg)
{
	if (g_haveRI && ri.Error)
	{
		ri.Error(errorLevel, "%s", msg);
	}
	/* ri.Error is noreturn on the engine side; if somehow absent, spin-fail. */
	for (;;) { }
}

int BrdgOur_Milliseconds(void)
{
	return (g_haveRI && ri.Milliseconds) ? ri.Milliseconds() : 0;
}

void BrdgOur_HunkClear(void)
{
	if (ri.Hunk_Clear) { ri.Hunk_Clear(); }
}

void *BrdgOur_HunkAlloc(size_t size, int pref)
{
	/* our Hunk_Alloc takes (int size, ha_pref). renderer2 allocations are well
	 * under INT_MAX; clamp defensively. In _DEBUG builds q_shared.h defines
	 * HUNK_DEBUG, so the engine fills Hunk_AllocDebug (extra label/file/line). */
#ifdef HUNK_DEBUG
	return ri.Hunk_AllocDebug
	       ? ri.Hunk_AllocDebug((int)size, (ha_pref)pref, "renderer2-bridge", __FILE__, __LINE__)
	       : NULL;
#else
	return ri.Hunk_Alloc ? ri.Hunk_Alloc((int)size, (ha_pref)pref) : NULL;
#endif
}

void *BrdgOur_HunkAllocTemp(size_t size)
{
	return ri.Hunk_AllocateTempMemory ? ri.Hunk_AllocateTempMemory((int)size) : NULL;
}

void BrdgOur_HunkFreeTemp(void *block)
{
	if (ri.Hunk_FreeTempMemory) { ri.Hunk_FreeTempMemory(block); }
}

void *BrdgOur_ZMalloc(int bytes)
{
	return ri.Z_Malloc ? ri.Z_Malloc(bytes) : NULL;
}

void BrdgOur_Free(void *buf)
{
	if (ri.Free) { ri.Free(buf); }
}

void BrdgOur_TagFree(void)
{
	if (ri.Tag_Free) { ri.Tag_Free(); }
}

void *BrdgOur_CvarGet(const char *name, const char *value, int flags)
{
	return ri.Cvar_Get ? (void *)ri.Cvar_Get(name, value, flags) : NULL;
}

void BrdgOur_CvarSet(const char *name, const char *value)
{
	if (ri.Cvar_Set) { ri.Cvar_Set(name, value); }
}

int BrdgOur_CvarVariableIntegerValue(const char *name)
{
	/* our v9 refimport lacks Cvar_VariableIntegerValue; derive it via Cvar_Get
	 * with a NULL default (Cvar_Get returns the existing cvar unchanged when it
	 * already exists; for a non-existent name it would create it at "" => 0). */
	cvar_t *cv;
	if (!ri.Cvar_Get)
	{
		return 0;
	}
	cv = ri.Cvar_Get(name, "", 0);
	return cv ? cv->integer : 0;
}

void BrdgOur_CvarRead(void *ourCvar, const char **string, float *value,
                      int *integer, int *modificationCount)
{
	cvar_t *cv = (cvar_t *)ourCvar;
	if (!cv)
	{
		if (string)            { *string = ""; }
		if (value)             { *value = 0.0f; }
		if (integer)           { *integer = 0; }
		if (modificationCount) { *modificationCount = 0; }
		return;
	}
	if (string)            { *string = cv->string; }
	if (value)             { *value = cv->value; }
	if (integer)           { *integer = cv->integer; }
	if (modificationCount) { *modificationCount = cv->modificationCount; }
}

void BrdgOur_CmdAddCommand(const char *name, void (*fn)(void))
{
	if (ri.Cmd_AddCommand) { ri.Cmd_AddCommand(name, fn); }
}

void BrdgOur_CmdRemoveCommand(const char *name)
{
	if (ri.Cmd_RemoveCommand) { ri.Cmd_RemoveCommand(name); }
}

int BrdgOur_CmdArgc(void)
{
	return ri.Cmd_Argc ? ri.Cmd_Argc() : 0;
}

char *BrdgOur_CmdArgv(int i)
{
	return ri.Cmd_Argv ? ri.Cmd_Argv(i) : (char *)"";
}

void BrdgOur_CmdExecuteText(int execWhen, const char *text)
{
	if (ri.Cmd_ExecuteText) { ri.Cmd_ExecuteText(execWhen, text); }
}

int BrdgOur_FS_FileIsInPAK(const char *name, int *pChecksum)
{
	return ri.FS_FileIsInPAK ? ri.FS_FileIsInPAK(name, pChecksum) : -1;
}

int BrdgOur_FS_ReadFile(const char *name, void **buf)
{
	return ri.FS_ReadFile ? ri.FS_ReadFile(name, buf) : -1;
}

void BrdgOur_FS_FreeFile(void *buf)
{
	if (ri.FS_FreeFile) { ri.FS_FreeFile(buf); }
}

char **BrdgOur_FS_ListFiles(const char *name, const char *ext, int *num)
{
	return ri.FS_ListFiles ? ri.FS_ListFiles(name, ext, num) : NULL;
}

void BrdgOur_FS_FreeFileList(char **list)
{
	if (ri.FS_FreeFileList) { ri.FS_FreeFileList(list); }
}

void BrdgOur_FS_WriteFile(const char *qpath, const void *buffer, int size)
{
	if (ri.FS_WriteFile) { ri.FS_WriteFile(qpath, buffer, size); }
}

int BrdgOur_FS_FileExists(const char *file)
{
	return (ri.FS_FileExists && ri.FS_FileExists(file)) ? 1 : 0;
}

void BrdgOur_CIN_UploadCinematic(int handle)
{
	if (ri.CIN_UploadCinematic) { ri.CIN_UploadCinematic(handle); }
}

int BrdgOur_CIN_PlayCinematic(const char *arg0, int x, int y, int w, int h, int bits)
{
	return ri.CIN_PlayCinematic ? ri.CIN_PlayCinematic(arg0, x, y, w, h, bits) : -1;
}

int BrdgOur_CIN_RunCinematic(int handle)
{
	return ri.CIN_RunCinematic ? (int)ri.CIN_RunCinematic(handle) : 0;
}

void BrdgOur_GLimp_Init(int major, int minor, int coreProfile, int debugContext)
{
	glimpParams_t params;
	params.majorVersion = major;
	params.minorVersion = minor;
	params.coreProfile  = coreProfile ? qtrue : qfalse;
	params.debugContext = debugContext ? qtrue : qfalse;
	if (ri.GLimp_Init) { ri.GLimp_Init(&params); }
}

void BrdgOur_GLimp_Shutdown(void)
{
	if (ri.GLimp_Shutdown) { ri.GLimp_Shutdown(); }
}

void BrdgOur_GLimp_EndFrame(void)
{
	if (ri.GLimp_EndFrame) { ri.GLimp_EndFrame(); }
}

void BrdgOur_GLimp_SetGamma(unsigned char *red, unsigned char *green, unsigned char *blue)
{
	if (ri.GLimp_SetGamma) { ri.GLimp_SetGamma(red, green, blue); }
}

void *BrdgOur_GL_GetProcAddress(const char *name)
{
	return ri.GL_GetProcAddress ? ri.GL_GetProcAddress(name) : NULL;
}

/* glconfig copy-back: write the common-prefix fields their renderer filled into
 * the engine's (OUR-layout) glconfig. Every OUR field is assigned explicitly —
 * never a memcpy across the two layouts. Our-only fields the renderer doesn't
 * report (displayFrequency from theirs, windowAspect) are set; fields with no
 * source (stereoEnabled, NV/ATI vendor bits) are left as the engine had them. */
void BrdgOur_GlconfigCopyBack(
	const char *renderer, const char *vendor, const char *version, const char *extensions,
	int maxTextureSize, int maxActiveTextures,
	int colorBits, int depthBits, int stencilBits,
	int vidWidth, int vidHeight, float windowAspect, int displayFrequency,
	int isFullscreen, void *ourGlconfig)
{
	glconfig_t *gc = (glconfig_t *)ourGlconfig;
	if (!gc)
	{
		return;
	}
	Brdg_strncpyz(gc->renderer_string,   renderer   ? renderer   : "", sizeof(gc->renderer_string));
	Brdg_strncpyz(gc->vendor_string,     vendor     ? vendor     : "", sizeof(gc->vendor_string));
	Brdg_strncpyz(gc->version_string,    version    ? version    : "", sizeof(gc->version_string));
	Brdg_strncpyz(gc->extensions_string, extensions ? extensions : "", sizeof(gc->extensions_string));
	gc->maxTextureSize    = maxTextureSize;
	gc->maxActiveTextures = maxActiveTextures;
	gc->colorBits         = colorBits;
	gc->depthBits         = depthBits;
	gc->stencilBits       = stencilBits;
	gc->vidWidth          = vidWidth;
	gc->vidHeight         = vidHeight;
	gc->windowAspect      = windowAspect;
	gc->displayFrequency  = displayFrequency;
	gc->isFullscreen      = isFullscreen ? qtrue : qfalse;
}

/* ======================================================================== *
 *  OUR refexport_t assembly — wrap the neutral table back into v9 shapes.
 *
 *  Most members are 1:1 (the wrapper just forwards; engine struct pointers are
 *  passed straight through as void*). The few that drift are translated inside
 *  the theirs-TU wrapper; here we only re-type the C signature.
 * ======================================================================== */

static const brdgReExport_t *X;     /* the theirs-TU published table */

/* ---- members theirs lacks: logged-once no-ops --------------------------- */
static qboolean g_warnedSaveView;
static void RE_SaveViewParms_stub(void)
{
	Brdg_LogOnce(&g_warnedSaveView,
	             "renderer2 bridge: SaveViewParms is not implemented in renderer2 "
	             "(no-op; revisit for R2-3 parity)\n");
}
static qboolean g_warnedRestoreView;
static void RE_RestoreViewParms_stub(void)
{
	Brdg_LogOnce(&g_warnedRestoreView,
	             "renderer2 bridge: RestoreViewParms is not implemented in renderer2 "
	             "(no-op; revisit for R2-3 parity)\n");
}

/* ---- thin forwarders (our v9 signatures -> neutral table) --------------- */

static void w_Shutdown(qboolean destroyWindow)            { X->Shutdown((int)destroyWindow); }
static void w_BeginRegistration(glconfig_t *config)       { X->BeginRegistration((void *)config); }
static qhandle_t w_RegisterModel(const char *n)           { return X->RegisterModel(n); }
static qhandle_t w_RegisterModelAllLODs(const char *n)    { return X->RegisterModelAllLODs(n); }
static qhandle_t w_RegisterSkin(const char *n)            { return X->RegisterSkin(n); }
static qhandle_t w_RegisterShader(const char *n)          { return X->RegisterShader(n); }
static qhandle_t w_RegisterShaderNoMip(const char *n)     { return X->RegisterShaderNoMip(n); }
static void w_RegisterFont(const char *fn, int ps, fontInfo_t *f)
{
	X->RegisterFont(fn, ps, (void *)f);
}
static void w_LoadWorld(const char *n)                    { X->LoadWorld(n); }
static qboolean w_GetSkinModel(qhandle_t s, const char *t, char *n)
{
	return (qboolean)X->GetSkinModel(s, t, n);
}
static qhandle_t w_GetShaderFromModel(qhandle_t m, int sn, int wl)
{
	return X->GetShaderFromModel(m, sn, wl);
}
static void w_SetWorldVisData(const byte *vis)            { X->SetWorldVisData(vis); }
static void w_EndRegistration(void)                       { X->EndRegistration(); }

static void w_ClearScene(void)                            { X->ClearScene(); }
static void w_AddRefEntityToScene(const refEntity_t *re_) { X->AddRefEntityToScene((const void *)re_); }
static int  w_LightForPoint(vec3_t p, vec3_t a, vec3_t d, vec3_t ld)
{
	return X->LightForPoint(p, a, d, ld);
}
static void w_AddPolyToScene(qhandle_t h, int nv, const polyVert_t *v)
{
	X->AddPolyToScene(h, nv, (const void *)v);
}
static void w_AddPolysToScene(qhandle_t h, int nv, const polyVert_t *v, int np)
{
	X->AddPolysToScene(h, nv, (const void *)v, np);
}
static void w_AddLightToScene(const vec3_t org, float radius, float intensity,
                              float r, float g, float b, qhandle_t h, int flags)
{
	X->AddLightToScene(org, radius, intensity, r, g, b, h, flags);
}
static void w_AddCoronaToScene(const vec3_t org, float r, float g, float b,
                               float scale, int id, qboolean visible)
{
	X->AddCoronaToScene(org, r, g, b, scale, id, (int)visible);
}
static void w_SetFog(int fogvar, int v1, int v2, float r, float g, float b, float d)
{
	X->SetFog(fogvar, v1, v2, r, g, b, d);
}
static void w_RenderScene(const refdef_t *fd)             { X->RenderScene((const void *)fd); }

static void w_SetColor(const float *rgba)                 { X->SetColor(rgba); }
static void w_DrawStretchPic(float x, float y, float w, float h,
                             float s1, float t1, float s2, float t2, qhandle_t sh)
{
	X->DrawStretchPic(x, y, w, h, s1, t1, s2, t2, sh);
}
static void w_DrawRotatedPic(float x, float y, float w, float h,
                             float s1, float t1, float s2, float t2, qhandle_t sh, float ang)
{
	X->DrawRotatedPic(x, y, w, h, s1, t1, s2, t2, sh, ang);
}
static void w_DrawStretchPicGradient(float x, float y, float w, float h,
                                     float s1, float t1, float s2, float t2, qhandle_t sh,
                                     const float *grad, int gradType)
{
	X->DrawStretchPicGradient(x, y, w, h, s1, t1, s2, t2, sh, grad, gradType);
}
static void w_Add2dPolys(polyVert_t *polys, int nv, qhandle_t sh)
{
	X->Add2dPolys((void *)polys, nv, sh);
}
static void w_DrawStretchRaw(int x, int y, int w, int h, int cols, int rows,
                             const byte *data, int client, qboolean dirty)
{
	X->DrawStretchRaw(x, y, w, h, cols, rows, data, client, (int)dirty);
}
static void w_UploadCinematic(int w, int h, int cols, int rows,
                              const byte *data, int client, qboolean dirty)
{
	X->UploadCinematic(w, h, cols, rows, data, client, (int)dirty);
}
static void w_BeginFrame(stereoFrame_t stereoFrame)       { X->BeginFrame((int)stereoFrame); }
static void w_EndFrame(int *fe, int *be)                  { X->EndFrame(fe, be); }

static int  w_MarkFragments(int numPoints, const vec3_t *points, const vec3_t projection,
                            int maxPoints, vec3_t pointBuffer,
                            int maxFragments, markFragment_t *fragmentBuffer)
{
	return X->MarkFragments(numPoints, (const void *)points, (const float *)projection,
	                        maxPoints, (float *)pointBuffer, maxFragments, (void *)fragmentBuffer);
}
static void w_ProjectDecal(qhandle_t sh, int np, vec3_t *points, vec4_t proj,
                           vec4_t color, int lifeTime, int fadeTime)
{
	X->ProjectDecal(sh, np, (void *)points, (float *)proj, (float *)color, lifeTime, fadeTime);
}
static void w_ClearDecals(void)                           { X->ClearDecals(); }

static int  w_LerpTag(orientation_t *tag, const refEntity_t *refent,
                      const char *tagName, int startIndex)
{
	return X->LerpTag((void *)tag, (const void *)refent, tagName, startIndex);
}
static void w_ModelBounds(qhandle_t m, vec3_t mins, vec3_t maxs)
{
	X->ModelBounds(m, mins, maxs);
}
static void w_RemapShader(const char *o, const char *n, const char *off)
{
	X->RemapShader(o, n, off);
}
static void w_DrawDebugPolygon(int color, int np, float *points)
{
	X->DrawDebugPolygon(color, np, points);
}
static void w_DrawDebugText(const vec3_t org, float r, float g, float b,
                            const char *text, qboolean neverOcclude)
{
	X->DrawDebugText(org, r, g, b, text, (int)neverOcclude);
}
static qboolean w_GetEntityToken(char *buffer, int size)
{
	return (qboolean)X->GetEntityToken(buffer, size);
}
static void w_AddPolyBufferToScene(polyBuffer_t *pb)      { X->AddPolyBufferToScene((void *)pb); }
static void w_SetGlobalFog(qboolean restore, int dur, float r, float g, float b, float dfo)
{
	X->SetGlobalFog((int)restore, dur, r, g, b, dfo);
}
static qboolean w_inPVS(const vec3_t p1, const vec3_t p2) { return (qboolean)X->inPVS(p1, p2); }
static void w_purgeCache(void)                            { X->purgeCache(); }
static qboolean w_LoadDynamicShader(const char *name, const char *text)
{
	return (qboolean)X->LoadDynamicShader(name, text);
}
static void w_RenderToTexture(int tid, int x, int y, int w, int h)
{
	X->RenderToTexture(tid, x, y, w, h);
}
static int  w_GetTextureId(const char *name)              { return X->GetTextureId(name); }
static void w_Finish(void)                                { X->Finish(); }

/* ======================================================================== *
 *  GetRefAPI — the DLL's sole export.
 * ======================================================================== */

/* exported via renderer2.def (matches the R2-1 stub's plain declaration). */
refexport_t *GetRefAPI(int apiVersion, refimport_t *rimp)
{
	if (!rimp)
	{
		return NULL;
	}

	/* capture our engine's refimport before anything else uses it. */
	ri       = *rimp;
	g_haveRI = qtrue;

	if (apiVersion != REF_API_VERSION)
	{
		if (ri.Printf)
		{
			ri.Printf(PRINT_ALL,
			          "renderer2 bridge: mismatched REF_API_VERSION (engine %d, mine %d) — "
			          "falling back to gl1\n", apiVersion, REF_API_VERSION);
		}
		return NULL;
	}

	/* hand off to the theirs-TU: it builds their refimport, calls their
	 * GetRefAPI(10, ...), and runs the layout check. */
	if (!Brdg_TheirsInit(apiVersion))
	{
		if (ri.Printf)
		{
			ri.Printf(PRINT_ALL,
			          "renderer2 bridge: theirs-side init failed — falling back to gl1\n");
		}
		return NULL;
	}

	X = Brdg_GetReExport();
	if (!X)
	{
		return NULL;
	}

	memset(&re, 0, sizeof(re));

	re.Shutdown               = w_Shutdown;
	re.BeginRegistration      = w_BeginRegistration;
	re.RegisterModel          = w_RegisterModel;
	re.RegisterModelAllLODs   = w_RegisterModelAllLODs;
	re.RegisterSkin           = w_RegisterSkin;
	re.RegisterShader         = w_RegisterShader;
	re.RegisterShaderNoMip    = w_RegisterShaderNoMip;
	re.RegisterFont           = w_RegisterFont;
	re.LoadWorld              = w_LoadWorld;
	re.GetSkinModel           = w_GetSkinModel;
	re.GetShaderFromModel     = w_GetShaderFromModel;
	re.SetWorldVisData        = w_SetWorldVisData;
	re.EndRegistration        = w_EndRegistration;
	re.ClearScene             = w_ClearScene;
	re.AddRefEntityToScene    = w_AddRefEntityToScene;
	re.LightForPoint          = w_LightForPoint;
	re.AddPolyToScene         = w_AddPolyToScene;
	re.AddPolysToScene        = w_AddPolysToScene;
	re.AddLightToScene        = w_AddLightToScene;
	re.AddCoronaToScene       = w_AddCoronaToScene;
	re.SetFog                 = w_SetFog;
	re.RenderScene            = w_RenderScene;
	re.SaveViewParms          = RE_SaveViewParms_stub;     /* theirs lacks it */
	re.RestoreViewParms       = RE_RestoreViewParms_stub;  /* theirs lacks it */
	re.SetColor               = w_SetColor;
	re.DrawStretchPic         = w_DrawStretchPic;
	re.DrawRotatedPic         = w_DrawRotatedPic;
	re.DrawStretchPicGradient = w_DrawStretchPicGradient;
	re.Add2dPolys             = w_Add2dPolys;
	re.DrawStretchRaw         = w_DrawStretchRaw;
	re.UploadCinematic        = w_UploadCinematic;
	re.BeginFrame             = w_BeginFrame;
	re.EndFrame               = w_EndFrame;
	re.MarkFragments          = w_MarkFragments;
	re.ProjectDecal           = w_ProjectDecal;
	re.ClearDecals            = w_ClearDecals;
	re.LerpTag                = w_LerpTag;
	re.ModelBounds            = w_ModelBounds;
	re.RemapShader            = w_RemapShader;
	re.DrawDebugPolygon       = w_DrawDebugPolygon;
	re.DrawDebugText          = w_DrawDebugText;
	re.GetEntityToken         = w_GetEntityToken;
	re.AddPolyBufferToScene   = w_AddPolyBufferToScene;
	re.SetGlobalFog           = w_SetGlobalFog;
	re.inPVS                  = w_inPVS;
	re.purgeCache             = w_purgeCache;
	re.LoadDynamicShader      = w_LoadDynamicShader;
	re.RenderToTexture        = w_RenderToTexture;
	re.GetTextureId           = w_GetTextureId;
	re.Finish                 = w_Finish;

	if (ri.Printf)
	{
		ri.Printf(PRINT_ALL, "renderer2 bridge: GetRefAPI bound (engine v%d <-> renderer2) OK\n",
		          apiVersion);
	}

	return &re;
}
