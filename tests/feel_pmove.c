/*
 * ET-RM feel harness: pmove determinism test.
 *
 * Pmove() is the pure movement function behind THE FEEL (accel, friction, jump,
 * air-control, strafe). It must be deterministic (client prediction depends on
 * it). This harness runs a fixed usercmd script through the REAL bg_pmove.c on a
 * flat world and prints a hash of the per-frame player state (origin, velocity,
 * viewangles, pm_flags). If a future change shifts the movement feel even one
 * bit, the hash changes -> regression caught.
 *
 * Built standalone (no CGAMEDLL/GAMEDLL) so it links the shared bg_* layer
 * directly. PM_GameType is supplied via -D by the build.
 */
#include <stdio.h>
#include <string.h>
#include "q_shared.h"
#include "bg_public.h"

// ---- world callbacks: infinite flat ground at z = 0 -----------------------
static void FlatTrace( trace_t *tr, const vec3_t start, const vec3_t mins,
					   const vec3_t maxs, const vec3_t end, int passEnt, int mask ) {
	float lo = ( mins ? mins[2] : 0 );      // bbox bottom offset
	float startFeet = start[2] + lo;
	float endFeet   = end[2]   + lo;

	memset( tr, 0, sizeof( *tr ) );
	tr->entityNum = ENTITYNUM_NONE;

	if ( startFeet >= 0 && endFeet < 0 ) {
		// crosses the ground plane going down
		float f = startFeet / ( startFeet - endFeet );
		tr->fraction = f;
		tr->endpos[0] = start[0] + f * ( end[0] - start[0] );
		tr->endpos[1] = start[1] + f * ( end[1] - start[1] );
		tr->endpos[2] = start[2] + f * ( end[2] - start[2] );
		tr->plane.normal[2] = 1.0f;
		tr->plane.dist = 0.0f;
		tr->contents = CONTENTS_SOLID;
		tr->entityNum = ENTITYNUM_WORLD;
		tr->surfaceFlags = 0;
		return;
	}
	if ( startFeet < 0 ) {
		// started inside the ground: allsolid/startsolid
		tr->fraction = 0.0f;
		tr->startsolid = tr->allsolid = qtrue;
		tr->plane.normal[2] = 1.0f;
		tr->contents = CONTENTS_SOLID;
		tr->entityNum = ENTITYNUM_WORLD;
		VectorCopy( start, tr->endpos );
		return;
	}
	// open air
	tr->fraction = 1.0f;
	VectorCopy( end, tr->endpos );
}

static int AirContents( const vec3_t point, int passEnt ) {
	return ( point[2] < 0 ) ? CONTENTS_SOLID : 0;
}

// ---- FNV-1a 64-bit running hash of the feel-critical state ----------------
static unsigned long long g_hash = 14695981039346656037ULL;
static void HashBytes( const void *p, int n ) {
	const unsigned char *b = (const unsigned char *)p;
	int i;
	for ( i = 0; i < n; i++ ) {
		g_hash ^= b[i];
		g_hash *= 1099511628211ULL;
	}
}
static void HashState( const playerState_t *ps ) {
	HashBytes( ps->origin, sizeof( ps->origin ) );
	HashBytes( ps->velocity, sizeof( ps->velocity ) );
	HashBytes( ps->viewangles, sizeof( ps->viewangles ) );
	HashBytes( &ps->pm_flags, sizeof( ps->pm_flags ) );
	HashBytes( &ps->pm_time, sizeof( ps->pm_time ) );
	HashBytes( &ps->groundEntityNum, sizeof( ps->groundEntityNum ) );
}

// One scripted command step: {duration in frames, fwd, side, up, deltaYaw}
typedef struct { int frames; signed char fwd, side, up; float yawRate; } script_t;
static const script_t g_script[] = {
	{ 40,    0,   0,   0,  0.0f },   // settle / fall to ground
	{ 80,  127,   0,   0,  0.0f },   // run forward (accel + ground friction)
	{ 10,  127,   0, 127,  0.0f },   // jump while running
	{120,  127, 127,   0,  3.0f },   // strafe-jump pattern: fwd+right + turning
	{ 60, -127,   0,   0,  0.0f },   // run backward (brake + reverse accel)
	{ 40,    0, 127,   0,  0.0f },   // pure strafe
	{ 30,    0,   0, 127,  0.0f },   // repeated jumps
	{ 60,  127,-127,   0, -3.0f },   // strafe-jump other way
};

int main( void ) {
	playerState_t ps;
	pmoveExt_t pmext;
	pmove_t pm;
	static bg_character_t character;     // zeroed; empty anim scripts -> anim calls no-op safely
	static animModelInfo_t animModelInfo;
	static int skills[16];               // pm->skill[] (zeroed = no skill upgrades)
	int s, f, frame = 0;
	const int FIXED_MSEC = 8;        // deterministic fixed timestep

	memset( &ps, 0, sizeof( ps ) );
	memset( &pmext, 0, sizeof( pmext ) );
	memset( &character, 0, sizeof( character ) );
	memset( &animModelInfo, 0, sizeof( animModelInfo ) );
	character.animModelInfo = &animModelInfo;

	ps.pm_type = PM_NORMAL;
	ps.stats[STAT_HEALTH] = 100;     // ALIVE (0 health => pmove treats as a corpse)
	ps.clientNum = 0;
	ps.origin[0] = 0;  ps.origin[1] = 0;  ps.origin[2] = 64;   // start a bit above ground
	ps.gravity = 800;
	ps.speed = 320;                  // standard run speed
	ps.groundEntityNum = ENTITYNUM_NONE;
	ps.viewangles[YAW] = 0;
	ps.weapon = WP_MP40;             // any valid weapon

	memset( &pm, 0, sizeof( pm ) );
	pm.ps = &ps;
	pm.pmext = &pmext;
	pm.character = &character;       // valid (empty) character so anim calls are safe
	pm.skill = skills;               // valid skill array (pmove dereferences pm->skill)
	pm.tracemask = MASK_PLAYERSOLID;
	pm.trace = FlatTrace;
	pm.pointcontents = AirContents;
	pm.pmove_fixed = 1;              // fixed-msec -> frame-rate independent + deterministic
	pm.pmove_msec = FIXED_MSEC;
	pm.noFootsteps = qfalse;
	pm.gametype = 4;                 // GT_WOLF-ish; movement is gametype-independent

	for ( s = 0; s < (int)( sizeof( g_script ) / sizeof( g_script[0] ) ); s++ ) {
		for ( f = 0; f < g_script[s].frames; f++ ) {
			pm.oldcmd = pm.cmd;
			memset( &pm.cmd, 0, sizeof( pm.cmd ) );
			frame++;
			pm.cmd.serverTime = frame * FIXED_MSEC;
			pm.cmd.forwardmove = g_script[s].fwd;
			pm.cmd.rightmove   = g_script[s].side;
			pm.cmd.upmove      = g_script[s].up;
			ps.viewangles[YAW] += g_script[s].yawRate;
			pm.cmd.angles[YAW] = ANGLE2SHORT( ps.viewangles[YAW] ) - ps.delta_angles[YAW];

			Pmove( &pm );
			HashState( &ps );
		}
	}

	printf( "ETRM_PMOVE_FEEL_HASH %016llx  frames=%d  finalPos=%.3f %.3f %.3f\n",
			g_hash, frame, ps.origin[0], ps.origin[1], ps.origin[2] );
	return 0;
}
