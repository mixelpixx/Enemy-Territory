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
** SDL_INPUT.C
**
** SDL2-backed keyboard + mouse input layer for the ET RM remaster.
** Replaces win_input.c (DirectInput / Win32 raw cursor). The SDL video
** layer (sdl_glimp.c) owns the window + GL context; this file owns input
** events and pumps them through ET's Sys_QueEvent contract:
**
**   keys / mouse buttons / wheel : Sys_QueEvent( 0, SE_KEY,  keynum, down, 0, NULL )
**   text                         : Sys_QueEvent( 0, SE_CHAR, ch,     0,    0, NULL )
**   mouse motion (accumulated)   : Sys_QueEvent( 0, SE_MOUSE, dx,    dy,   0, NULL )
**
** Mouse sensitivity / acceleration is NOT touched here — it stays in the
** shared cl_input.c so feel is unchanged. We only deliver raw deltas.
**
** Raw relative mouse: when the game wants mouse-look (in-game, no key
** catcher, window focused) we use SDL_SetRelativeMouseMode(SDL_TRUE),
** which gives raw deltas bypassing the OS pointer acceleration. When a
** menu / console is up we release to a free cursor.
**
** Portions adapted from ioquake3's code/sdl/sdl_input.c (GPL-2.0-or-later).
*/

#include <SDL.h>

#include "../client/client.h"
#include "sdl_local.h"

cvar_t *in_mouse;

// Legacy symbol: the retired Win32 GL window proc (win_wndproc.c, still
// compiled but never driven now that SDL owns the window) references
// `directInput` as extern. It used to live in win_input.c. We keep the
// definition here so the dead wndproc still links; it is always false.
qboolean directInput = qfalse;

static qboolean in_initialized = qfalse;
static qboolean mouseActive    = qfalse;   // relative/grabbed mouse currently engaged
qboolean        in_appactive   = qtrue;    // window has input focus (referenced by win_wndproc.c contract)

// old mouse-button bitmask, used by the (now-vestigial) IN_MouseEvent contract
static int      oldButtonState = 0;

/*
===============
IN_TranslateSDLToETKey

Map an SDL keysym to an ET keyNum_t (see src/ui/keycodes.h). Adapted from
ioquake3's IN_TranslateSDLToQ3Key, but ET has no K_CONSOLE / K_SUPER /
K_WORLD_* — the console toggle key ('`'/'~') is delivered as its literal
ASCII value and recognised specially in cl_keys.c.
===============
*/
static int IN_TranslateSDLToETKey( SDL_Keysym *keysym ) {
	int key = 0;

	if ( keysym->scancode >= SDL_SCANCODE_1 && keysym->scancode <= SDL_SCANCODE_0 ) {
		// number keys, by physical position (top-row 1..0)
		if ( keysym->scancode == SDL_SCANCODE_0 ) {
			key = '0';
		} else {
			key = '1' + ( keysym->scancode - SDL_SCANCODE_1 );
		}
	} else if ( keysym->sym >= SDLK_SPACE && keysym->sym < SDLK_DELETE ) {
		// ASCII-range printable symbols map directly to their lowercase ascii.
		// This includes '`' (0x60) and '~' which cl_keys.c treats as the
		// hard-coded console toggle key.
		key = (int)keysym->sym;
	} else {
		switch ( keysym->sym ) {
		case SDLK_PAGEUP:       key = K_PGUP; break;
		case SDLK_KP_9:         key = K_KP_PGUP; break;
		case SDLK_PAGEDOWN:     key = K_PGDN; break;
		case SDLK_KP_3:         key = K_KP_PGDN; break;
		case SDLK_KP_7:         key = K_KP_HOME; break;
		case SDLK_HOME:         key = K_HOME; break;
		case SDLK_KP_1:         key = K_KP_END; break;
		case SDLK_END:          key = K_END; break;
		case SDLK_KP_4:         key = K_KP_LEFTARROW; break;
		case SDLK_LEFT:         key = K_LEFTARROW; break;
		case SDLK_KP_6:         key = K_KP_RIGHTARROW; break;
		case SDLK_RIGHT:        key = K_RIGHTARROW; break;
		case SDLK_KP_2:         key = K_KP_DOWNARROW; break;
		case SDLK_DOWN:         key = K_DOWNARROW; break;
		case SDLK_KP_8:         key = K_KP_UPARROW; break;
		case SDLK_UP:           key = K_UPARROW; break;
		case SDLK_ESCAPE:       key = K_ESCAPE; break;
		case SDLK_KP_ENTER:     key = K_KP_ENTER; break;
		case SDLK_RETURN:       key = K_ENTER; break;
		case SDLK_TAB:          key = K_TAB; break;
		case SDLK_F1:           key = K_F1; break;
		case SDLK_F2:           key = K_F2; break;
		case SDLK_F3:           key = K_F3; break;
		case SDLK_F4:           key = K_F4; break;
		case SDLK_F5:           key = K_F5; break;
		case SDLK_F6:           key = K_F6; break;
		case SDLK_F7:           key = K_F7; break;
		case SDLK_F8:           key = K_F8; break;
		case SDLK_F9:           key = K_F9; break;
		case SDLK_F10:          key = K_F10; break;
		case SDLK_F11:          key = K_F11; break;
		case SDLK_F12:          key = K_F12; break;
		case SDLK_F13:          key = K_F13; break;
		case SDLK_F14:          key = K_F14; break;
		case SDLK_F15:          key = K_F15; break;

		case SDLK_BACKSPACE:    key = K_BACKSPACE; break;
		case SDLK_KP_PERIOD:    key = K_KP_DEL; break;
		case SDLK_DELETE:       key = K_DEL; break;
		case SDLK_PAUSE:        key = K_PAUSE; break;

		case SDLK_LSHIFT:
		case SDLK_RSHIFT:       key = K_SHIFT; break;

		case SDLK_LCTRL:
		case SDLK_RCTRL:        key = K_CTRL; break;

		case SDLK_RGUI:
		case SDLK_LGUI:         key = K_COMMAND; break;

		case SDLK_RALT:
		case SDLK_LALT:         key = K_ALT; break;

		case SDLK_KP_5:         key = K_KP_5; break;
		case SDLK_INSERT:       key = K_INS; break;
		case SDLK_KP_0:         key = K_KP_INS; break;
		case SDLK_KP_MULTIPLY:  key = K_KP_STAR; break;
		case SDLK_KP_PLUS:      key = K_KP_PLUS; break;
		case SDLK_KP_MINUS:     key = K_KP_MINUS; break;
		case SDLK_KP_DIVIDE:    key = K_KP_SLASH; break;
		case SDLK_KP_EQUALS:    key = K_KP_EQUALS; break;

		case SDLK_CAPSLOCK:     key = K_CAPSLOCK; break;
		case SDLK_NUMLOCKCLEAR: key = K_KP_NUMLOCK; break;

		default:
			// Fall back to the keysym if it is in the low ASCII range.
			if ( keysym->sym >= 0 && keysym->sym < 128 ) {
				key = (int)keysym->sym;
			}
			break;
		}
	}

	return key;
}

/*
===============
IN_TranslateSDLButton

Map an SDL mouse button id to an ET keyNum_t.
===============
*/
static int IN_TranslateSDLButton( Uint8 button ) {
	switch ( button ) {
	case SDL_BUTTON_LEFT:   return K_MOUSE1;
	case SDL_BUTTON_RIGHT:  return K_MOUSE2;
	case SDL_BUTTON_MIDDLE: return K_MOUSE3;
	case SDL_BUTTON_X1:     return K_MOUSE4;
	case SDL_BUTTON_X2:     return K_MOUSE5;
	default:                return 0;
	}
}

/*
===============
IN_WantGrab

Returns qtrue when the game wants raw, locked mouse-look: window focused,
in_mouse enabled, and no key catcher (console / UI / message) is active.
Mirrors win_input.c's old IN_Frame deactivation logic.
===============
*/
static qboolean IN_WantGrab( void ) {
	if ( !in_mouse || !in_mouse->integer ) {
		return qfalse;
	}
	if ( !in_appactive ) {
		return qfalse;
	}
	// any UI/console/message catcher means the user wants a free cursor
	if ( cls.keyCatchers & ( KEYCATCH_CONSOLE | KEYCATCH_UI | KEYCATCH_MESSAGE ) ) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
IN_ActivateMouse

Engage SDL relative (grabbed, raw-delta) mouse mode.
===============
*/
static void IN_ActivateMouse( void ) {
	if ( mouseActive ) {
		return;
	}
	SDL_SetRelativeMouseMode( SDL_TRUE );
	// drain any spurious accumulated delta from before the grab
	SDL_GetRelativeMouseState( NULL, NULL );
	mouseActive = qtrue;
}

/*
===============
IN_DeactivateMouse

Release to a free, visible cursor.
===============
*/
void IN_DeactivateMouse( void ) {
	if ( !mouseActive ) {
		return;
	}
	SDL_SetRelativeMouseMode( SDL_FALSE );
	mouseActive = qfalse;
}

/*
===============
IN_Activate

Called when the main window gains or loses input focus (also referenced by
the legacy win_wndproc.c contract).
===============
*/
void IN_Activate( qboolean active ) {
	in_appactive = active;
	if ( !active ) {
		IN_DeactivateMouse();
	}
}

/*
===============
IN_MouseEvent

Vestigial Win32-message-pump contract. The SDL window has its own wndproc,
so the legacy win_wndproc.c (compiled but driving the now-nonexistent Win32
GL window) will never call this in practice. Kept as a real symbol so the
old wndproc still links, and harmless if ever invoked.
===============
*/
void IN_MouseEvent( int mstate ) {
	int i;

	for ( i = 0; i < 5; i++ ) {
		if ( ( mstate & ( 1 << i ) ) && !( oldButtonState & ( 1 << i ) ) ) {
			Sys_QueEvent( 0, SE_KEY, K_MOUSE1 + i, qtrue, 0, NULL );
		}
		if ( !( mstate & ( 1 << i ) ) && ( oldButtonState & ( 1 << i ) ) ) {
			Sys_QueEvent( 0, SE_KEY, K_MOUSE1 + i, qfalse, 0, NULL );
		}
	}
	oldButtonState = mstate;
}

/*
===============
IN_ProcessEvents

Drain the SDL event queue, translating each event to ET's Sys_QueEvent
contract. Mouse motion is accumulated and emitted as a single SE_MOUSE.
===============
*/
static void IN_ProcessEvents( void ) {
	SDL_Event e;
	int       dx = 0, dy = 0;
	int       key;

	while ( SDL_PollEvent( &e ) ) {
		switch ( e.type ) {
		case SDL_KEYDOWN:
			key = IN_TranslateSDLToETKey( &e.key.keysym );
			if ( key ) {
				Sys_QueEvent( 0, SE_KEY, key, qtrue, 0, NULL );
			}
			break;

		case SDL_KEYUP:
			key = IN_TranslateSDLToETKey( &e.key.keysym );
			if ( key ) {
				Sys_QueEvent( 0, SE_KEY, key, qfalse, 0, NULL );
			}
			break;

		case SDL_TEXTINPUT:
			{
				// SDL delivers UTF-8; ET's char path is byte-oriented. Queue
				// each byte in the low-ascii range as a SE_CHAR (sufficient
				// for the original ET behaviour; high bytes are dropped).
				const char *p = e.text.text;
				while ( *p ) {
					unsigned char ch = (unsigned char)*p++;
					if ( ch < 128 ) {
						Sys_QueEvent( 0, SE_CHAR, ch, 0, 0, NULL );
					}
				}
			}
			break;

		case SDL_MOUSEMOTION:
			// Feed motion deltas in BOTH states: grabbed (in-game raw look)
			// AND ungrabbed (menu/console cursor). SDL delivers xrel/yrel in
			// either mode; the UI moves its own drawn cursor from these SE_MOUSE
			// deltas, so gating on mouseActive left menus with a dead mouse.
			dx += e.motion.xrel;
			dy += e.motion.yrel;
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			key = IN_TranslateSDLButton( e.button.button );
			if ( key ) {
				Sys_QueEvent( 0, SE_KEY, key,
					( e.type == SDL_MOUSEBUTTONDOWN ) ? qtrue : qfalse, 0, NULL );
			}
			break;

		case SDL_MOUSEWHEEL:
			{
				int wkey = ( e.wheel.y > 0 ) ? K_MWHEELUP :
				           ( e.wheel.y < 0 ) ? K_MWHEELDOWN : 0;
				if ( wkey ) {
					Sys_QueEvent( 0, SE_KEY, wkey, qtrue, 0, NULL );
					Sys_QueEvent( 0, SE_KEY, wkey, qfalse, 0, NULL );
				}
			}
			break;

		case SDL_WINDOWEVENT:
			switch ( e.window.event ) {
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				IN_Activate( qtrue );
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				IN_Activate( qfalse );
				break;
			case SDL_WINDOWEVENT_MINIMIZED:
				IN_Activate( qfalse );
				break;
			case SDL_WINDOWEVENT_RESTORED:
				IN_Activate( qtrue );
				break;
			default:
				break;
			}
			break;

		case SDL_QUIT:
			Com_Quit_f();
			break;

		default:
			break;
		}
	}

	// emit one accumulated mouse-motion event
	if ( dx || dy ) {
		Sys_QueEvent( 0, SE_MOUSE, dx, dy, 0, NULL );
	}
}

/*
===============
IN_Frame

Called once per frame (from win_main.c's main loop). Manages grab state
then pumps + translates the SDL event queue. This is the single input pump
for the SDL build.
===============
*/
void IN_Frame( void ) {
	if ( !in_initialized ) {
		return;
	}

	// decide grab state for this frame
	if ( IN_WantGrab() ) {
		IN_ActivateMouse();
	} else {
		IN_DeactivateMouse();
	}

	IN_ProcessEvents();
}

/*
===============
IN_ClearStates
===============
*/
void IN_ClearStates( void ) {
	oldButtonState = 0;
	if ( in_initialized ) {
		// drop any pending relative delta so a re-grab doesn't snap the view
		SDL_GetRelativeMouseState( NULL, NULL );
	}
}

/*
===============
IN_Init
===============
*/
void IN_Init( void ) {
	Com_RMTrace( "IN_Init (SDL2): begin" );

	// in_mouse kept as a recognised cvar; CVAR_LATCH preserved from win_input.c
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE | CVAR_LATCH );

	// SDL relative-mouse: prefer raw input (bypasses OS pointer accel).
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0" );

	// deliver SE_CHAR via SDL_TEXTINPUT
	SDL_StartTextInput();

	// ET draws its own cursor in menus; hide the OS cursor so we don't show
	// two. (In relative/grabbed mode SDL hides it anyway; this covers menus.)
	SDL_ShowCursor( SDL_DISABLE );

	oldButtonState = 0;
	mouseActive    = qfalse;
	in_appactive   = qtrue;
	in_initialized = qtrue;

	Com_RMTrace( "IN_Init (SDL2): done (in_mouse=%d)", in_mouse ? in_mouse->integer : -1 );
	Com_Printf( "SDL2 input initialized.\n" );
}

/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown( void ) {
	if ( !in_initialized ) {
		return;
	}
	IN_DeactivateMouse();
	SDL_StopTextInput();
	in_initialized = qfalse;
	Com_RMTrace( "IN_Shutdown (SDL2)" );
}

/*
===============
IN_Move

ET calls IN_Move( usercmd_t * ) to layer extra (joystick / trackball)
movement on top of the keyboard move cmd. No joystick support in the SDL
layer yet — no-op stub so the client links.
===============
*/
void IN_Move( usercmd_t *cmd ) {
	(void)cmd;
}

/*
===============
IN_JoystickCommands

Declared in win_local.h; no joystick support yet — no-op stub.
===============
*/
void IN_JoystickCommands( void ) {
}

/*
===============
IN_DeactivateWin32Mouse

Legacy Win32 symbol referenced by win_local.h / old call sites. With SDL
owning the cursor this is equivalent to releasing the SDL grab.
===============
*/
void IN_DeactivateWin32Mouse( void ) {
	IN_DeactivateMouse();
}
