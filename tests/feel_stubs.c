/*
 * Stubs for the engine/trap externals the shared bg_* layer references but that
 * a standalone pmove harness doesn't have. Kept minimal and behaviourally safe:
 * trap_SnapVector uses the SAME rounding as the engine (lrint == round-to-
 * nearest, matching the original fistp) so movement results are faithful.
 *
 * Symbols are added here as the linker reports them.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include "q_shared.h"
#include "bg_public.h"

// Engine float->int vector snap (must match the real Sys_SnapVector: lrint).
void trap_SnapVector( float *v ) {
	v[0] = (float)lrint( v[0] );
	v[1] = (float)lrint( v[1] );
	v[2] = (float)lrint( v[2] );
}

void QDECL Com_Error( int level, const char *fmt, ... ) {
	va_list ap; char msg[2048];
	va_start( ap, fmt ); Q_vsnprintf( msg, sizeof( msg ), fmt, ap ); va_end( ap );
	fprintf( stderr, "Com_Error(%d): %s\n", level, msg );
	exit( 1 );
}

// --- parser / cvar / game traps used by the shared loaders (no data loaded;
//     fine for a movement test) ----------------------------------------------
int  trap_PC_LoadSource( const char *filename ) { return 0; }            // 0 = no source
int  trap_PC_FreeSource( int handle ) { return 0; }
int  trap_PC_ReadToken( int handle, pc_token_t *token ) { return 0; }    // 0 = EOF
int  trap_PC_SourceFileAndLine( int handle, char *filename, int *line ) { if(filename) filename[0]=0; if(line) *line=0; return 0; }
void trap_Cvar_VariableStringBuffer( const char *var, char *buf, int n ) { if(n>0) buf[0]=0; }
void trap_Cvar_Set( const char *var, const char *value ) {}
void ClientStoreSurfaceFlags( int clientNum, int surfaceFlags ) {}       // game-side footstep cache

vmCvar_t g_developer = { 0, 0, 0.0f, 0, "0" };

// --- animation layer stubs --------------------------------------------------
// Movement physics (origin/velocity) is independent of animation; these only
// set legs/torso anim state, which we don't hash. Stubbed so the harness needs
// no loaded character/anim data.
void BG_AnimUpdatePlayerStateConditions( pmove_t *pmove ) {}
int  BG_AnimScriptAnimation( playerState_t *ps, animModelInfo_t *a, scriptAnimMoveTypes_t mt, qboolean cont ) { return -1; }
int  BG_AnimScriptEvent( playerState_t *ps, animModelInfo_t *a, scriptAnimEventTypes_t ev, qboolean isReplay, qboolean force ) { return -1; }
int  BG_PlayAnimName( playerState_t *ps, animModelInfo_t *a, char *name, animBodyPart_t part, qboolean setTimer, qboolean isContinue, qboolean force ) { return 0; }
void BG_UpdateConditionValue( int client, int condition, int value, qboolean checkConversion ) {}
int  BG_GetConditionValue( int client, int condition, qboolean checkConversion ) { return 0; }

void QDECL Com_Printf( const char *fmt, ... ) {
	va_list ap;
	va_start( ap, fmt ); vprintf( fmt, ap ); va_end( ap );
}

// --- g_* cvars referenced by the shared layer under GAMEDLL -----------------
// Set to ordinary multiplayer values so no single-player movement branch fires
// (gametype != GT_SINGLE_PLAYER/COOP) and the SP movespeed scale (movespeed/127)
// is a no-op at 127.
vmCvar_t g_gametype  = { 0, 0, 5.0f, 5, "5" };     // GT_WOLF
vmCvar_t g_movespeed = { 0, 0, 127.0f, 127, "127" };
