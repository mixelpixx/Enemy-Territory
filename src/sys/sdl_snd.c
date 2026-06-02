/*
===========================================================================

Wolfenstein: Enemy Territory GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Wolfenstein: Enemy Territory GPL Source Code.
It is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
See <http://www.gnu.org/licenses/>.

===========================================================================
*/

/*
** SDL_SND.C
**
** SDL2 audio backend for the ET RM remaster. Implements the engine's
** SNDDMA_* contract (src/client/snd_local.h) using an SDL audio callback
** that drains the dma.buffer ring the mixer (snd_dma.c / snd_mix.c) writes.
** Replaces the DirectSound backend (win_snd.c). The engine mixer is unchanged.
**
** Adapted from ioquake3's code/sdl/snd_sdl.c (GPL-2.0-or-later).
*/

#include <SDL.h>

#include "../client/snd_local.h"
#include "sdl_local.h"

static int             dmapos     = 0;        // play cursor, in (channel-)samples
static int             dmasize    = 0;        // ring size in BYTES
static qboolean        snd_inited = qfalse;
static SDL_AudioDeviceID sdlAudioDevice = 0;  // opened device (0 = none)

cvar_t *s_sdlSamples;                   // device callback buffer (0 = auto)

/*
===============
SNDDMA_AudioCallback   (runs on SDL's audio thread)

Copy len bytes from the dma ring at the play cursor into SDL's stream,
wrapping the ring. Mirrors ioquake3's callback.
===============
*/
static void SNDDMA_AudioCallback( void *userdata, Uint8 *stream, int len ) {
	int pos = ( dmapos * ( dma.samplebits / 8 ) );
	if ( pos >= dmasize ) {
		dmapos = pos = 0;
	}

	if ( !snd_inited || !dma.buffer ) {
		memset( stream, '\0', len );
		return;
	} else {
		int tobufend = dmasize - pos;     // bytes to end of ring
		int len1 = len;
		int len2 = 0;

		if ( len1 > tobufend ) {
			len1 = tobufend;
			len2 = len - len1;
		}

		memcpy( stream, dma.buffer + pos, len1 );
		if ( len2 <= 0 ) {
			dmapos += ( len1 / ( dma.samplebits / 8 ) );
		} else {
			// wraparound
			memcpy( stream + len1, dma.buffer, len2 );
			dmapos = ( len2 / ( dma.samplebits / 8 ) );
		}
	}

	if ( dmapos >= dma.samples ) {
		dmapos = 0;
	}
}

static int SND_RoundUpPow2( int v ) {
	int p = 1;
	while ( p < v ) {
		p <<= 1;
	}
	return p;
}

/*
===============
SNDDMA_Init
===============
*/
qboolean SNDDMA_Init( void ) {
	SDL_AudioSpec desired, obtained;
	int freq, bits, channels, devSamples, ringSamples;

	if ( snd_inited ) {
		return qtrue;
	}

	if ( !s_sdlSamples ) {
		s_sdlSamples = Cvar_Get( "s_sdlSamples", "0", CVAR_ARCHIVE | CVAR_LATCH );
	}

	if ( !SDL_WasInit( SDL_INIT_AUDIO ) ) {
		if ( SDL_InitSubSystem( SDL_INIT_AUDIO ) < 0 ) {
			Com_Printf( "SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError() );
			return qfalse;
		}
	}

	// desired format from the existing ET cvars
	switch ( (int)s_khz->value ) {
	case 11: freq = 11025; break;
	case 44: freq = 44100; break;
	default: freq = 22050; break;
	}
	bits     = ( (int)s_bits->value == 8 ) ? 8 : 16;
	channels = ( (int)s_numchannels->value == 1 ) ? 1 : 2;

	// device callback buffer (power of two). auto-scale with frequency for low latency.
	if ( s_sdlSamples->integer > 0 ) {
		devSamples = SND_RoundUpPow2( s_sdlSamples->integer );
	} else {
		devSamples = ( freq <= 11025 ) ? 256 : ( freq <= 22050 ) ? 512 : 1024;
	}

	memset( &desired, 0, sizeof( desired ) );
	desired.freq     = freq;
	desired.format   = ( bits == 8 ) ? AUDIO_U8 : AUDIO_S16SYS;
	desired.channels = channels;
	desired.samples  = devSamples;
	desired.callback = SNDDMA_AudioCallback;

	// Open with SDL_OpenAudioDevice and DISALLOW a format change: ET's mixer
	// (snd_mix.c S_TransferPaintBuffer) only knows how to write 8- or 16-bit
	// PCM into dma.buffer. Modern backends (WASAPI) natively run 32-bit float;
	// if we let SDL hand us that obtained format, dma.samplebits becomes 32 and
	// the mixer's transfer falls through both branches (silence/garbage). By
	// omitting SDL_AUDIO_ALLOW_FORMAT_CHANGE, SDL builds an internal converter
	// from our S16/U8 ring to whatever the hardware wants, so obtained.format
	// stays exactly what we asked for. We still allow freq/channel changes (the
	// dma_t is filled from the obtained spec for those).
	sdlAudioDevice = SDL_OpenAudioDevice( NULL, SDL_FALSE, &desired, &obtained,
		SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE );
	if ( sdlAudioDevice == 0 ) {
		Com_Printf( "SDL_OpenAudio failed: %s\n", SDL_GetError() );
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		return qfalse;
	}

	// fill dma_t from the OBTAINED spec (SDL may have changed freq/channels)
	dma.channels         = obtained.channels;
	dma.samplebits       = SDL_AUDIO_BITSIZE( obtained.format );
	dma.speed            = obtained.freq;
	dma.submission_chunk = 1;

	// Ring buffer: power-of-two (channel-)samples, with headroom over the
	// device buffer. ET's mixer masks the sample position with (dma.samples-1),
	// so this MUST be a power of two (see Step 1).
	ringSamples = SND_RoundUpPow2( obtained.samples * obtained.channels * 8 );
	if ( ringSamples < 0x8000 ) {
		ringSamples = 0x8000;
	}
	dma.samples = ringSamples;
	dmasize     = dma.samples * ( dma.samplebits / 8 );
	dma.buffer  = (byte *)calloc( 1, dmasize );
	dmapos      = 0;

	Com_Printf( "SDL audio: %d Hz %d-bit %dch; dev buffer %d samples; ring %d samples\n",
				dma.speed, dma.samplebits, dma.channels, obtained.samples, dma.samples );

	SDL_PauseAudioDevice( sdlAudioDevice, 0 );
	snd_inited = qtrue;
	return qtrue;
}

/*
===============
SNDDMA_GetDMAPos
===============
*/
int SNDDMA_GetDMAPos( void ) {
	return dmapos;
}

/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown( void ) {
	if ( !snd_inited ) {
		return;
	}
	SDL_PauseAudioDevice( sdlAudioDevice, 1 );
	SDL_CloseAudioDevice( sdlAudioDevice );
	sdlAudioDevice = 0;
	SDL_QuitSubSystem( SDL_INIT_AUDIO );
	if ( dma.buffer ) {
		free( dma.buffer );
	}
	memset( (void *)&dma, 0, sizeof( dma ) );
	dmapos = dmasize = 0;
	snd_inited = qfalse;
}

/*
===============
SNDDMA_BeginPainting / SNDDMA_Submit

Serialize the main-thread mixer against the audio-thread callback.
===============
*/
void SNDDMA_BeginPainting( void ) {
	if ( snd_inited ) {
		SDL_LockAudioDevice( sdlAudioDevice );
	}
}

void SNDDMA_Submit( void ) {
	if ( snd_inited ) {
		SDL_UnlockAudioDevice( sdlAudioDevice );
	}
}

/*
===============
SNDDMA_Activate

Kept for the (still-compiled) win_wndproc.c contract. Focus-based muting is
driven through Sys_SndPause() from sdl_input.c's IN_Activate instead.
===============
*/
void SNDDMA_Activate( void ) {
}

/*
===============
Sys_SndPause

Pause/unpause the audio device (mute on focus loss). Declared in sdl_local.h,
called from sdl_input.c IN_Activate.
===============
*/
void Sys_SndPause( qboolean pause ) {
	if ( snd_inited ) {
		SDL_PauseAudioDevice( sdlAudioDevice, pause ? 1 : 0 );
	}
}
