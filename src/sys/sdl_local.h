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
** SDL_LOCAL.H
**
** Shared declarations for the SDL2-backed platform layer (window + GL
** context, and later input / sound). Created in Task B2.
*/

#ifndef __SDL_LOCAL_H__
#define __SDL_LOCAL_H__

#include <SDL.h>

// The window created by GLimp_Init (sdl_glimp.c). Returns NULL before the
// video subsystem is initialized. Used later by the DirectSound backend
// (Task C4) to obtain a native HWND via SDL_GetWindowWMInfo.
SDL_Window *Sys_GetSDLWindow( void );

// Pause/unpause the SDL audio device (focus muting). Defined in sdl_snd.c,
// called from sdl_input.c's IN_Activate.
void Sys_SndPause( qboolean pause );

#endif // __SDL_LOCAL_H__
