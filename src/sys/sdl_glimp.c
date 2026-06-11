/*
===========================================================================

Wolfenstein: Enemy Territory GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Wolfenstein: Enemy Territory GPL Source Code (Wolf ET Source Code).

Wolf ET Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Wolf ET Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wolf ET Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Wolf: ET Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Wolf ET Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
** SDL_GLIMP.C
**
** SDL2-backed replacement for win_glimp.c / win_gamma.c. Creates the window
** and OpenGL context through SDL2 and fills glConfig exactly the way the
** legacy Win32/WGL layer did, so the (unmodified) renderer sees an identical
** glConfig. Created for the ET-RM "Modern Display & Input" Task B2.
**
** Implements the renderer's platform contract (see tr_local.h):
**   GLimp_Init / GLimp_Shutdown / GLimp_EndFrame
**   GLimp_SetGamma / GLimp_LogComment
**   GLimp_SpawnRenderThread / GLimp_RendererSleep / GLimp_FrontEndSleep /
**   GLimp_WakeRenderer / GLimp_RenderThreadWrapper  (single-threaded stubs)
*/

#include <assert.h>
#include <SDL.h>
#include "../renderer/tr_local.h"
#include "../qcommon/qcommon.h"
#include "../win32/glw_win.h"
#include "sdl_local.h"

//
// QGL proc-table loader (sdl_qgl.c)
//
void     QGL_EnableLogging( qboolean enable );
qboolean QGL_Init( const char *dllname );
void     QGL_Shutdown( void );

//
// glw_state is shared with sdl_qgl.c (it only reads log_fp / hinstOpenGL).
// Defined here, mirroring win_glimp.c which owned the definition.
//
glwstate_t glw_state;

// Referenced by the renderer (tr_main.c qglListBase). The WGL font-bitmap
// display lists are gone under SDL; renderer console text using this base is
// a no-op (list 0). Kept at 0 to satisfy the extern in tr_local.h.
int gl_NormalFontBase = 0;

//
// SDL window + GL context state
//
static SDL_Window   *s_window  = NULL;
static SDL_GLContext s_context  = NULL;
static qboolean      s_gammaSupported = qfalse;

// Local Q_stristr (case-insensitive substring) — mirrors win_glimp.c.
static const char *Q_stristr( const char *s, const char *find ) {
	register char c, sc;
	register size_t len;

	if ( ( c = *find++ ) != 0 ) {
		if ( c >= 'a' && c <= 'z' ) {
			c -= ( 'a' - 'A' );
		}
		len = strlen( find );
		do
		{
			do
			{
				if ( ( sc = *s++ ) == 0 ) {
					return NULL;
				}
				if ( sc >= 'a' && sc <= 'z' ) {
					sc -= ( 'a' - 'A' );
				}
			} while ( sc != c );
		} while ( Q_stricmpn( s, find, len ) != 0 );
		s--;
	}
	return s;
}

/*
** GLW_InitExtensions
**
** Mirrors win_glimp.c's GLW_InitExtensions, but resolves the ARB/EXT entry
** points through SDL_GL_GetProcAddress instead of wglGetProcAddress. The
** WGL-only extensions (WGL_EXT_swap_control via qwglSwapIntervalEXT,
** WGL_3DFX_gamma_control) are dropped — SDL owns swap interval and gamma.
*/
static void GLW_InitExtensions( void ) {
	if ( !r_allowExtensions->integer ) {
		ri.Printf( PRINT_ALL, "*** IGNORING OPENGL EXTENSIONS ***\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	// GL_EXT_texture_compression_s3tc
	glConfig.textureCompression = TC_NONE;
	if ( strstr( glConfig.extensions_string, "GL_EXT_texture_compression_s3tc" ) ) {
		if ( r_ext_compressed_textures->integer ) {
			glConfig.textureCompression = TC_EXT_COMP_S3TC;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_compression_s3tc\n" );
		} else {
			glConfig.textureCompression = TC_NONE;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_compression_s3tc\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );
	}

	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qfalse;
	if ( strstr( glConfig.extensions_string, "EXT_texture_env_add" ) ) {
		if ( r_ext_texture_env_add->integer ) {
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		} else {
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
	}

	// swap control: SDL owns this (SDL_GL_SetSwapInterval, applied in
	// GLimp_Init and GLimp_EndFrame). Force a set next frame.
	r_swapInterval->modified = qtrue;

	// GL_ARB_multitexture
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	if ( strstr( glConfig.extensions_string, "GL_ARB_multitexture" ) ) {
		if ( r_ext_multitexture->integer ) {
			qglMultiTexCoord2fARB = ( PFNGLMULTITEXCOORD2FARBPROC ) SDL_GL_GetProcAddress( "glMultiTexCoord2fARB" );
			qglActiveTextureARB = ( PFNGLACTIVETEXTUREARBPROC ) SDL_GL_GetProcAddress( "glActiveTextureARB" );
			qglClientActiveTextureARB = ( PFNGLCLIENTACTIVETEXTUREARBPROC ) SDL_GL_GetProcAddress( "glClientActiveTextureARB" );

			if ( qglActiveTextureARB ) {
				qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &glConfig.maxActiveTextures );

				if ( glConfig.maxActiveTextures > 1 ) {
					ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
				} else {
					qglMultiTexCoord2fARB = NULL;
					qglActiveTextureARB = NULL;
					qglClientActiveTextureARB = NULL;
					ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
				}
			}
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
	}

	// GL_EXT_compiled_vertex_array
	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;
	if ( strstr( glConfig.extensions_string, "GL_EXT_compiled_vertex_array" ) && ( glConfig.hardwareType != GLHW_RIVA128 ) ) {
		if ( r_ext_compiled_vertex_array->integer ) {
			ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
			qglLockArraysEXT = ( void ( APIENTRY * )( int, int ) )SDL_GL_GetProcAddress( "glLockArraysEXT" );
			qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) )SDL_GL_GetProcAddress( "glUnlockArraysEXT" );
			if ( !qglLockArraysEXT || !qglUnlockArraysEXT ) {
				ri.Error( ERR_VID_FATAL, "bad getprocaddress" );
			}
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

	// GL_NV_fog_distance
	if ( strstr( glConfig.extensions_string, "GL_NV_fog_distance" ) ) {
		if ( r_ext_NV_fog_dist->integer ) {
			glConfig.NVFogAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_NV_fog_distance\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_NV_fog_distance\n" );
			ri.Cvar_Set( "r_ext_NV_fog_dist", "0" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_NV_fog_distance not found\n" );
		ri.Cvar_Set( "r_ext_NV_fog_dist", "0" );
	}

	// GL_EXT_texture_filter_anisotropic
	if ( Q_stristr( glConfig.extensions_string, "GL_EXT_texture_filter_anisotropic" ) ) {
		if ( r_ext_texture_filter_anisotropic->integer ) {
			glConfig.anisotropicAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_filter_anisotropic\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_filter_anisotropic\n" );
			ri.Cvar_Set( "r_ext_texture_filter_anisotropic", "0" );
		}
	} else {
		ri.Printf( PRINT_ALL, "... GL_EXT_texture_filter_anisotropic not found\n" );
		ri.Cvar_Set( "r_ext_texture_filter_anisotropic", "0" );
	}
}

/*
** GLimp_SetGamma
**
** Hardware gamma via SDL. Builds 16-bit ramps from the byte tables
** (ramp[i] = table[i] << 8) and calls SDL_SetWindowGammaRamp. If the platform
** rejects it, log once and return — the renderer's software gamma path still
** applies.
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] ) {
	Uint16 r[256], g[256], b[256];
	int i;
	static qboolean warned = qfalse;

	if ( !glConfig.deviceSupportsGamma || r_ignorehwgamma->integer || !s_window ) {
		return;
	}

	for ( i = 0; i < 256; i++ ) {
		r[i] = ( ( ( Uint16 ) red[i] ) << 8 ) | red[i];
		g[i] = ( ( ( Uint16 ) green[i] ) << 8 ) | green[i];
		b[i] = ( ( ( Uint16 ) blue[i] ) << 8 ) | blue[i];
	}

	if ( SDL_SetWindowGammaRamp( s_window, r, g, b ) < 0 ) {
		if ( !warned ) {
			Com_Printf( "SDL_SetWindowGammaRamp failed: %s\n", SDL_GetError() );
			warned = qtrue;
		}
		// give up on hardware gamma for the rest of the session
		glConfig.deviceSupportsGamma = qfalse;
	}
}

/*
** Build a space-delimited "WxH" list of the display's unique modes and publish
** it as r_availableModes for the UI resolution picker. Detected dynamically so
** the picker reflects what THIS monitor supports. Display 0 (matches the
** desktop-mode query above); deduped; sorted descending by area.
*/
static void GLimp_PublishAvailableModes( void ) {
	// Sized to the cvar value limit, NOT bigger: r_availableModes is read by the
	// UI VM, and Cvar_Update ERR_DROPs a vmCvar whose string exceeds
	// MAX_CVAR_VALUE_STRING. The append loop caps against sizeof(buf)-1, so this
	// also bounds the published value to the contract (truncating extra modes on
	// rare many-resolution displays rather than crashing the client).
	char	buf[MAX_CVAR_VALUE_STRING];
	int		nmodes, i, n;
	struct { int w, h; } list[96];
	int		count = 0;

	nmodes = SDL_GetNumDisplayModes( 0 );
	for ( i = 0; i < nmodes && count < (int)( sizeof( list ) / sizeof( list[0] ) ); i++ ) {
		SDL_DisplayMode m;
		int j, dup = 0;
		if ( SDL_GetDisplayMode( 0, i, &m ) != 0 ) {
			continue;
		}
		if ( m.w < 640 || m.h < 480 ) {        // skip ancient tiny modes
			continue;
		}
		for ( j = 0; j < count; j++ ) {
			if ( list[j].w == m.w && list[j].h == m.h ) { dup = 1; break; }
		}
		if ( !dup ) {
			list[count].w = m.w;
			list[count].h = m.h;
			count++;
		}
	}
	// insertion-sort descending by pixel area (SDL lists largest-first already,
	// but don't rely on it)
	for ( i = 1; i < count; i++ ) {
		int kw = list[i].w, kh = list[i].h, j = i - 1;
		while ( j >= 0 && ( list[j].w * list[j].h ) < ( kw * kh ) ) {
			list[j + 1] = list[j];
			j--;
		}
		list[j + 1].w = kw; list[j + 1].h = kh;
	}
	buf[0] = '\0';
	for ( i = 0, n = 0; i < count; i++ ) {
		char entry[32];
		Com_sprintf( entry, sizeof( entry ), "%s%dx%d", ( n ? " " : "" ), list[i].w, list[i].h );
		if ( strlen( buf ) + strlen( entry ) >= sizeof( buf ) - 1 ) {
			break;                              // cap to cvar buffer
		}
		Q_strcat( buf, sizeof( buf ), entry );
		n++;
	}
	ri.Cvar_Set( "r_availableModes", buf );
	Com_RMTrace( "GLimp: r_availableModes (%d modes) = %s", n, buf );
}

/*
** GLW_CreateWindowAndContext
**
** RM: one attempt at the SDL_CreateWindow -> SDL_GL_CreateContext ->
** SDL_GL_MakeCurrent sequence, factored out of GLimp_Init so the
** core-profile fallback ladder can retry it once. On failure returns qfalse
** with the failing stage name and the SDL error text captured for the caller;
** any partial s_window/s_context state is left as-is (the caller cleans up
** before a retry, and the legacy params==NULL path goes straight to ri.Error
** exactly as before).
*/
static qboolean GLW_CreateWindowAndContext( int width, int height, Uint32 flags,
											const char **failedStage,
											char *errText, int errTextSize ) {
	{ Com_RMTrace( "GLimp_Init: SDL_CreateWindow %dx%d flags=0x%x...", width, height, (unsigned)flags ); }

	s_window = SDL_CreateWindow( "Wolfenstein: Enemy Territory",
								 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
								 width, height, flags );
	if ( !s_window ) {
		*failedStage = "SDL_CreateWindow";
		Q_strncpyz( errText, SDL_GetError(), errTextSize );
		return qfalse;
	}

	{ Com_RMTrace( "GLimp_Init: SDL_GL_CreateContext..." ); }

	s_context = SDL_GL_CreateContext( s_window );
	if ( !s_context ) {
		*failedStage = "SDL_GL_CreateContext";
		Q_strncpyz( errText, SDL_GetError(), errTextSize );
		return qfalse;
	}

	if ( SDL_GL_MakeCurrent( s_window, s_context ) < 0 ) {
		*failedStage = "SDL_GL_MakeCurrent";
		Q_strncpyz( errText, SDL_GetError(), errTextSize );
		return qfalse;
	}

	return qtrue;
}

/*
** GLimp_Init
**
** Creates the SDL window + GL context, loads GL via QGL_Init(SDL), and fills
** glConfig exactly as win_glimp.c did. params is an optional GL-context
** request from the renderer (DLL renderers); NULL = legacy default
** compatibility context (renderer1's path, identical to pre-v9 behavior).
*/
/*
** GLimp_RegisterPlatformCvars
**
** RM (R2-2 Task 4): this SDL platform layer (engine-resident, shared by gl1 and
** gl2) dereferences a set of display/context cvar GLOBALS (r_mode, r_fullscreen,
** r_colorbits, ... — declared in tr_local.h, defined in the gl1 renderer). Under
** gl1 those globals are populated by the built-in renderer's R_Register before
** GLimp_Init runs. Under `cl_renderer gl2` the built-in R_Register never runs, so
** the globals are NULL and GLimp_Init crashes the moment it reads r_mode->integer.
**
** This helper registers exactly those platform-shared cvars (using the gl1
** defaults/flags verbatim) into the very same globals, but ONLY when they are
** still NULL. Under gl1 every global is already non-NULL, so the whole block is
** skipped and gl1 stays byte-identical. The engine Cvar_Get returns the same
** cvar_t* for a name regardless of caller, so when the gl2 renderer registers its
** own r_mode etc. through the bridge proxy it shares this very cvar.
*/
static void GLimp_RegisterPlatformCvars( void ) {
	if ( !r_mode ) {
		r_mode = ri.Cvar_Get( "r_mode", "-2", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_fullscreen ) {
		r_fullscreen = ri.Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
	}
	if ( !r_colorbits ) {
		r_colorbits = ri.Cvar_Get( "r_colorbits", "0", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_depthbits ) {
		r_depthbits = ri.Cvar_Get( "r_depthbits", "0", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_stencilbits ) {
		r_stencilbits = ri.Cvar_Get( "r_stencilbits", "0", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_ignorehwgamma ) {
		r_ignorehwgamma = ri.Cvar_Get( "r_ignorehwgamma", "0", CVAR_ARCHIVE | CVAR_LATCH );
	}
	if ( !r_swapInterval ) {
		r_swapInterval = ri.Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE );
	}
	if ( !r_drawBuffer ) {
		r_drawBuffer = ri.Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT );
	}
	if ( !r_logFile ) {
		r_logFile = ri.Cvar_Get( "r_logFile", "0", CVAR_CHEAT );
	}
	if ( !r_allowExtensions ) {
		r_allowExtensions = ri.Cvar_Get( "r_allowExtensions", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_ext_compressed_textures ) {
		r_ext_compressed_textures = ri.Cvar_Get( "r_ext_compressed_textures", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_ext_multitexture ) {
		r_ext_multitexture = ri.Cvar_Get( "r_ext_multitexture", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_ext_compiled_vertex_array ) {
		r_ext_compiled_vertex_array = ri.Cvar_Get( "r_ext_compiled_vertex_array", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_ext_texture_env_add ) {
		r_ext_texture_env_add = ri.Cvar_Get( "r_ext_texture_env_add", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_ext_texture_filter_anisotropic ) {
		r_ext_texture_filter_anisotropic = ri.Cvar_Get( "r_ext_texture_filter_anisotropic", "0", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
	if ( !r_ext_NV_fog_dist ) {
		r_ext_NV_fog_dist = ri.Cvar_Get( "r_ext_NV_fog_dist", "0", CVAR_ARCHIVE | CVAR_LATCH | CVAR_UNSAFE );
	}
}

void GLimp_Init( const glimpParams_t *params ) {
	char buf[1024];
	cvar_t *lastValidRenderer;
	int width, height;
	float windowAspect = 0;
	Uint32 flags;
	int colorbits, depthbits, stencilbits;
	int realDepth = 0, realStencil = 0;

	// gl2: ensure the platform-shared display/context cvar globals exist before
	// any are dereferenced below (no-op under gl1 — already registered).
	GLimp_RegisterPlatformCvars();

	lastValidRenderer = ri.Cvar_Get( "r_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE );

	ri.Printf( PRINT_ALL, "Initializing OpenGL subsystem (SDL2)\n" );

	{ Com_RMTrace( "GLimp_Init: SDL window + GL context..." ); }

	// because the client uses SDL_MAIN_HANDLED (ET has its own WinMain)
	SDL_SetMainReady();

	// Make the process per-monitor DPI-aware BEFORE initializing SDL video.
	// Without this, on a display with Windows scaling != 100%, SDL reports the
	// scaled *logical* desktop size (e.g. 1536x960 for a 1920x1200 panel at
	// 125%), so r_mode -2 ("desktop resolution") would render below native.
	// permonitorv2 makes SDL_GetDesktopDisplayMode / the window use true pixels.
	SDL_SetHint( SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2" );

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) ) {
		if ( SDL_InitSubSystem( SDL_INIT_VIDEO ) < 0 ) {
			ri.Error( ERR_VID_FATAL, "GLimp_Init() - SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError() );
		}
	}

	//
	// resolution:
	//   r_mode == -2  -> use desktop display resolution (ioquake3 convention)
	//   r_mode == -1  -> custom (r_customwidth/height, via R_GetModeInfo)
	//   r_mode >=  0  -> the vidmode table (via R_GetModeInfo)
	// SDL VIDEO is initialized above, so SDL_GetDesktopDisplayMode is valid here.
	//
	{
		SDL_DisplayMode desktop;
		qboolean haveDesktop = ( SDL_GetDesktopDisplayMode( 0, &desktop ) == 0 ) ? qtrue : qfalse;

		if ( !haveDesktop ) {
			Com_RMTrace( "GLimp_Init: SDL_GetDesktopDisplayMode failed: %s", SDL_GetError() );
			// safe fallback if the desktop query is unavailable
			desktop.w = 800;
			desktop.h = 600;
		}

		if ( r_mode->integer == -2 ) {
			width  = desktop.w;
			height = desktop.h;
			windowAspect = ( height != 0 ) ? ( (float)width / (float)height ) : 0.0f;
		} else if ( !R_GetModeInfo( &width, &height, &windowAspect, r_mode->integer ) ) {
			ri.Printf( PRINT_ALL, "...invalid mode %d, falling back to desktop resolution\n", r_mode->integer );
			width  = desktop.w;
			height = desktop.h;
			windowAspect = ( height != 0 ) ? ( (float)width / (float)height ) : 0.0f;
		}

		Com_RMTrace( "GLimp_Init: desktop=%dx%d, r_mode=%d -> selected %dx%d (%s)",
					 desktop.w, desktop.h, r_mode->integer, width, height,
					 r_fullscreen->integer ? "FS" : "W" );
	}

	// SDL video is inited above; publish the display's supported modes for the
	// UI resolution picker (r_availableModes).
	GLimp_PublishAvailableModes();

	ri.Printf( PRINT_ALL, "...setting mode %d: %d %d %s\n", r_mode->integer, width, height,
			   r_fullscreen->integer ? "FS" : "W" );

	//
	// GL attributes (mirror win_glimp's PFD intent: RGBA8, 24-bit depth,
	// 8-bit stencil, double buffered).
	//
	colorbits = r_colorbits->integer;
	if ( colorbits == 0 ) {
		colorbits = 24;
	}
	if ( r_depthbits->integer == 0 ) {
		depthbits = ( colorbits > 16 ) ? 24 : 16;
	} else {
		depthbits = r_depthbits->integer;
	}
	stencilbits = r_stencilbits->integer;
	if ( depthbits < 24 ) {
		stencilbits = 0;
	}

	// RM: optional context-profile request from the renderer (DLL renderers).
	// NULL = legacy default-compatibility context — renderer1's path, identical
	// to the pre-v9 behavior (no attributes set here at all).
	if ( params ) {
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, params->majorVersion );
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, params->minorVersion );
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK,
							 params->coreProfile ? SDL_GL_CONTEXT_PROFILE_CORE
												 : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
		if ( params->debugContext ) {
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
		}
		Com_RMTrace( "GLimp_Init: context request %d.%d %s",
					 params->majorVersion, params->minorVersion,
					 params->coreProfile ? "core" : "compat" );
	}

	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, depthbits );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, stencilbits );

	flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
	if ( r_fullscreen->integer ) {
		// desktop fullscreen (borderless at desktop res) — full exclusive
		// mode-set is handled in a later task (B3). Keep simple + safe.
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	{
		const char *failedStage = "";
		char errText[256];
		qboolean ok;

		errText[0] = '\0';
		ok = GLW_CreateWindowAndContext( width, height, flags, &failedStage, errText, sizeof( errText ) );

		// RM: fallback ladder — if the renderer requested a core profile and it
		// failed, retry ONCE with a compatibility profile.
		if ( !ok && params && params->coreProfile ) {
			ri.Printf( PRINT_ALL, "...core profile context failed (%s: %s), retrying with compatibility profile\n",
					   failedStage, errText );
			if ( s_context ) {
				SDL_GL_DeleteContext( s_context );
				s_context = NULL;
			}
			if ( s_window ) {
				SDL_DestroyWindow( s_window );
				s_window = NULL;
			}
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY );
			ok = GLW_CreateWindowAndContext( width, height, flags, &failedStage, errText, sizeof( errText ) );
		}

		if ( !ok ) {
			if ( params ) {
				ri.Error( ERR_VID_FATAL, "GLimp_Init() - %s failed: %s (try cl_renderer gl1)\n", failedStage, errText );
			} else {
				// legacy (params==NULL) failure path — message identical to pre-v9
				ri.Error( ERR_VID_FATAL, "GLimp_Init() - %s failed: %s\n", failedStage, errText );
			}
		}
	}

	// vsync
	SDL_GL_SetSwapInterval( r_swapInterval ? r_swapInterval->integer : 0 );

	// ET draws its own cursor in menus; hide the OS cursor. Done here (not only
	// in IN_Init) so a vid_restart — which re-runs GLimp_Init but not IN_Init,
	// and whose GLimp_Shutdown re-enabled the cursor — keeps it hidden.
	SDL_ShowCursor( SDL_DISABLE );

	{ Com_RMTrace( "GLimp_Init: QGL_Init (load GL via SDL)..." ); }

	// load GL function pointers via SDL_GL_GetProcAddress
	if ( !QGL_Init( NULL ) ) {
		ri.Error( ERR_VID_FATAL, "GLimp_Init() - QGL_Init failed\n" );
	}

	{ Com_RMTrace( "GLimp_Init: GL context up; querying GL strings..." ); }

	//
	// fill glConfig exactly like win_glimp.c
	//
	glConfig.driverType = GLDRV_ICD;        // SDL uses the system ICD
	glConfig.hardwareType = GLHW_GENERIC;

	// drawable size (may exceed window size on HiDPI) is the true framebuffer
	SDL_GL_GetDrawableSize( s_window, &glConfig.vidWidth, &glConfig.vidHeight );
	if ( glConfig.vidWidth <= 0 || glConfig.vidHeight <= 0 ) {
		glConfig.vidWidth = width;
		glConfig.vidHeight = height;
	}
	glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;

	glConfig.isFullscreen = ( r_fullscreen->integer != 0 );
	glConfig.stereoEnabled = qfalse;

	// report the bit depths SDL actually gave us
	{
		int rb = 0, gb = 0, bb = 0;
		SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &rb );
		SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &gb );
		SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &bb );
		SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &realDepth );
		SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &realStencil );
		glConfig.colorBits = rb + gb + bb;
		if ( glConfig.colorBits <= 0 ) {
			glConfig.colorBits = colorbits;
		}
	}
	glConfig.depthBits = realDepth ? realDepth : depthbits;
	glConfig.stencilBits = realStencil;

	// display refresh
	{
		SDL_DisplayMode dm;
		int disp = SDL_GetWindowDisplayIndex( s_window );
		if ( disp >= 0 && SDL_GetCurrentDisplayMode( disp, &dm ) == 0 ) {
			glConfig.displayFrequency = dm.refresh_rate;
		}
	}

	// GL strings.
	// RM (R2-2 Task 4): under a CORE-profile context (the gl2 renderer requests
	// 3.3 core via glimpParams), the legacy glGetString(GL_EXTENSIONS) enum is
	// removed and returns NULL — feeding NULL to Q_strncpyz fatals ("NULL src").
	// Null-guard every GL string. The compat/default (gl1, params==NULL) path is
	// unchanged: those strings are non-NULL there, so the guards never fire and
	// the legacy GL_EXTENSIONS string is still captured byte-identically. In a
	// core context the engine glConfig.extensions_string is left empty here; the
	// gl2 renderer builds its own extension list via glGetStringi in the bridge,
	// so this engine-side copy is informational only under gl2.
	{
		const char *glVendor   = (const char *)qglGetString( GL_VENDOR );
		const char *glRenderer = (const char *)qglGetString( GL_RENDERER );
		const char *glVersion  = (const char *)qglGetString( GL_VERSION );
		const char *glExt      = (const char *)qglGetString( GL_EXTENSIONS );

		Q_strncpyz( glConfig.vendor_string, glVendor ? glVendor : "", sizeof( glConfig.vendor_string ) );
		Q_strncpyz( glConfig.renderer_string, glRenderer ? glRenderer : "", sizeof( glConfig.renderer_string ) );
		Q_strncpyz( glConfig.version_string, glVersion ? glVersion : "", sizeof( glConfig.version_string ) );
		Q_strncpyz( glConfig.extensions_string, glExt ? glExt : "", sizeof( glConfig.extensions_string ) );
		if ( glExt && strlen( glExt ) >= sizeof( glConfig.extensions_string ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: GL extensions string too long, truncated\n" );
		}
	}

	//
	// chipset specific configuration (mirror win_glimp.c)
	//
	Q_strncpyz( buf, glConfig.renderer_string, sizeof( buf ) );
	Q_strlwr( buf );

	if ( Q_stricmp( lastValidRenderer->string, glConfig.renderer_string ) ) {
		glConfig.hardwareType = GLHW_GENERIC;
		ri.Cvar_Set( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST" );
		ri.Cvar_Set( "r_picmip", "1" );
	}

	ri.Cvar_Set( "r_highQualityVideo", "1" );
	ri.Cvar_Set( "r_lastValidRenderer", glConfig.renderer_string );

	GLW_InitExtensions();

	//
	// hardware gamma: probe support via SDL get/set ramp round-trip
	//
	glConfig.deviceSupportsGamma = qfalse;
	if ( !r_ignorehwgamma->integer ) {
		Uint16 r[256], g[256], b[256];
		if ( SDL_GetWindowGammaRamp( s_window, r, g, b ) == 0 ) {
			glConfig.deviceSupportsGamma = qtrue;
			s_gammaSupported = qtrue;
		} else {
			ri.Printf( PRINT_ALL, "...hardware gamma not available: %s\n", SDL_GetError() );
		}
	}

	{ Com_RMTrace( "GLimp_Init: done (%dx%d, %s)", glConfig.vidWidth, glConfig.vidHeight,
				   glConfig.renderer_string ); }
}

/*
** GLimp_EndFrame
*/
void GLimp_EndFrame( void ) {
	// re-apply swap interval if changed
	if ( r_swapInterval->modified ) {
		r_swapInterval->modified = qfalse;
		if ( !glConfig.stereoEnabled ) {
			SDL_GL_SetSwapInterval( r_swapInterval->integer );
		}
	}

	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 ) {
		if ( s_window ) {
			SDL_GL_SwapWindow( s_window );
		}
	}

	// check logging
	QGL_EnableLogging( r_logFile->integer );
}

/*
** GLimp_Shutdown
*/
void GLimp_Shutdown( void ) {
	ri.Printf( PRINT_ALL, "Shutting down OpenGL subsystem (SDL2)\n" );

	// Release mouse capture / relative-grab and restore the OS cursor WHILE the
	// window still exists. On quit, GLimp_Shutdown runs before IN_Shutdown, so
	// releasing here prevents a stale Windows ClipCursor from confining the
	// desktop cursor after the game exits. IN_DeactivateMouse keeps sdl_input's
	// grab state in sync, so mouselook re-engages cleanly after a vid_restart.
	{
		extern void IN_DeactivateMouse( void );
		IN_DeactivateMouse();
	}
	if ( s_window ) {
		SDL_SetWindowGrab( s_window, SDL_FALSE );
	}
	SDL_ShowCursor( SDL_ENABLE );

	// shutdown the QGL proc table
	QGL_Shutdown();

	if ( s_context ) {
		SDL_GL_DeleteContext( s_context );
		s_context = NULL;
	}
	if ( s_window ) {
		SDL_DestroyWindow( s_window );
		s_window = NULL;
	}

	// close the r_logFile if QGL opened one
	if ( glw_state.log_fp ) {
		fclose( glw_state.log_fp );
		glw_state.log_fp = 0;
	}

	if ( SDL_WasInit( SDL_INIT_VIDEO ) ) {
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}

	s_gammaSupported = qfalse;

	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glState, 0, sizeof( glState ) );
}

/*
** GLimp_LogComment
*/
void GLimp_LogComment( char *comment ) {
	if ( glw_state.log_fp ) {
		fprintf( glw_state.log_fp, "%s", comment );
	}
}

/*
** Sys_GetSDLWindow — used later (Task C4) to give DirectSound a native HWND.
*/
SDL_Window *Sys_GetSDLWindow( void ) {
	return s_window;
}

/*
===============
Sys_GL_GetProcAddress

GL entry-point resolution for renderer DLLs (refimport GL_GetProcAddress).
Wraps SDL so renderer DLLs never link SDL themselves.
===============
*/
void *Sys_GL_GetProcAddress( const char *name ) {
	return SDL_GL_GetProcAddress( name );
}

/*
===========================================================================

SMP acceleration  (single-threaded stubs — ET-RM runs r_smp 0)

These match win_glimp.c's contract but never spawn a render thread, so the
front end always drives the back end directly. GLimp_SpawnRenderThread
returns qfalse so the renderer stays single-threaded.

===========================================================================
*/

void ( *glimpRenderThread )( void );

void GLimp_RenderThreadWrapper( void ) {
}

qboolean GLimp_SpawnRenderThread( void ( *function )( void ) ) {
	glimpRenderThread = function;
	// single-threaded: tell the renderer SMP is unavailable
	return qfalse;
}

void *GLimp_RendererSleep( void ) {
	return NULL;
}

void GLimp_FrontEndSleep( void ) {
}

void GLimp_WakeRenderer( void *data ) {
	(void)data;
}
