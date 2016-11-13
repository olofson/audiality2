/*
 * a2_waves.h - Audiality 2 waveform API and unit programming interface
 *
 * Copyright 2010-2015 David Olofson <david@olofson.net>
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from the
 * use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef A2_WAVES_H
#define A2_WAVES_H

#include "a2_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of waveform mipmap levels */
#define	A2_MIPLEVELS		10

/*
 * The definitions below define how the engine pads waves when preparing them.
 *
 * One-shot waves are padded with zeroes, whereas looped waves are "wrapped."
 *
 * (These values are based on A2_MAXFRAG, the requirements of the interpolators
 * in <Audiality2/dsp.h>, and the mipmapping resamplers of the builtin 'wtosc'
 * unit.)
 */

/* Number of samples before data[0] needed by interpolators */
#define	A2_INTERPRE	1

/* Number of samples after data[size - 1] needed by interpolators */
#define	A2_INTERPOST	2

/*
 * Maximum per-output-sample phase increment that can safely be used without
 * checking for end-of-wave while processing a fragment. (24:8 fixed point)
 * (So, 512 means that an oscillator can safely play a wave at twice the output
 * sample rate without checking for end/loop.)
 */
#define	A2_MAXPHINC	512

/* Number of pad samples before data[0] of any wave, any mip level */
#define	A2_WAVEPRE	A2_INTERPRE

/* Number of pad samples after data[size - 1] of any wave, any mip level */
#define	A2_WAVEPOST	\
		(A2_INTERPOST + ((A2_MAXFRAG * A2_MAXPHINC + 255) >> 8) + 1)

/*
 * Waveform period for full bandwidth down to 20 Hz fundamental. (The built-in
 * geometric waves are hardcoded to this period, and thus, this is the size
 * of their "mip level 0" waves, excluding padding.)
 */
#define	A2_WAVEPERIOD	2048


/*---------------------------------------------------------
	Wave data structure
---------------------------------------------------------*/

/* Type of waveform data */
typedef enum A2_wavetypes
{
	A2_WOFF = 0,		/* "off" wave - silence */
	A2_WNOISE,		/* "noise" wave - pitched S&H RNG */
	A2_WWAVE,		/* Plain waveform */
	A2_WMIPWAVE		/* Mipmapped waveform */
} A2_wavetypes;

/* A2_wave data for plain and mipmapped wavetables */
typedef struct A2_wave_wave
{
	int16_t		*data[A2_MIPLEVELS];	/* One buffer per mip level */
	unsigned	size[A2_MIPLEVELS];	/* Sizes EXCLUDING pre/post! */
} A2_wave_wave;

/* A2_object: Waveform with mipmaps */
typedef struct A2_wave
{
	A2_wavetypes	type;
	unsigned	flags;		/* A2_LOOPED etc */
	unsigned	period;		/* Fundamental period length */
	union {
		A2_wave_wave	wave;		/* A2WT_WAVE, A2WT_MIPWAVE */
	} d;
} A2_wave;


/*---------------------------------------------------------
	Wave management API
---------------------------------------------------------*/

typedef enum A2_waveflags
{
	A2_LOOPED =	0x00000100,	/* Waveform is looped */
	A2_NORMALIZE =	0x00010000,	/* Normalize waveform amplitude */
	A2_XFADE =	0x00040000,	/* Crossfade to make seamless */
	A2_REVMIX =	0x00080000,	/* Mix in reversed to make seemless */
	A2_CLEAR =	0x00100000,	/* Clear (silence) the waveform */
	A2_UNPREPARED =	0x01000000,	/* Not prepared - DO NOT PLAY! */
} A2_waveflags;

/*
 * Upload a waveform for use by wavetable oscillators. Returns the handle of
 * the waveform, or a negative error code.
 *
 * 'wt' is the type of wave to create, as defined by A2_wavetypes.
 *
 * 'period' is the number of sample frames in one period of the waveform's
 * fundamental frequency. (Used for pitch calculations.)
 *
 * 'flags' is a set of |'ed together flags from A2_waveflags;
 *	A2_LOOPED	Wave is looped. (Affects pre-processing and playback!)
 *	A2_NORMALIZE	Normalize ("maximize") amplitude in the conversion.
 *	A2_XFADE	Crossfade mix a copy offset by half the loop length.
 *	A2_REVMIX	Mix wave with a reversed version of itself.
 *	A2_CLEAR	Ignore 'data' (if any) and generate a silent waveform.
 *
 * A2_XFADE and A2_REVMIX are intended for looped waves, although they (sort
 * of) work on one-shot waves as well.
 *
 * 'fmt', 'data' and 'size': See a2_Write() in audiality2/stream.h.
 *
 * Returns the handle of the wave, or a negated A2_errors error code.
 *
 * NOTE:
 *	The returned handle can be opened with a2_StreamOpen() and used with
 *	the stream API (see a2_WaveNew()), but as the wave has been
 *	automatically prepared, it's not possible to change the length of it.
 */
A2_handle a2_UploadWave(A2_state *st,
		A2_wavetypes wt, unsigned period, int flags,
		A2_sampleformats fmt, const void *data, unsigned size);

/*
 * Allocate a waveform for use by wavetable oscillators.
 *
 * This call creates an empty, unprepared wave that can be written with the
 * desired amount of data using the stream API. (a2_OpenStream(), a2_Write(),
 * a2_SetPos() etc.) To actually apply the data written to the wave, use
 * a2_Flush(), or close the stream. This will render mipmaps and pad zones as
 * needed to play the wave correctly.
 *
 * The first flush (or stream close) will determine the length of the wave and
 * allocate the realtime playback buffers for it. The length of the wave cannot
 * be changed after this, and writing past the end of the wave will fail,
 * returning A2_INDEXRANGE. However, the wave can still be altered by further
 * stream writes, flushing again to apply the updates.
 *
 * NOTE:
 *	Flags A2_NORMALIZE, A2_XFADE and A2_REVMIX will result in undefined
 *	behavior if a wave is modified after the initial flush. Only use these
 *	flags for "write once" waves!
 *
 * 'period', 'flags': See a2_WaveUpload()!
 *
 * Returns the handle of the wave, or a negated A2_errors error code.
 */
A2_handle a2_NewWave(A2_state *st, A2_wavetypes wt, unsigned period,
		int flags);

/*
 * Get A2_wave struct from handle. Returns NULL if the handle is invalid, or if
 * the object is not a wave.
 */
A2_wave *a2_GetWave(A2_state *st, A2_handle handle);


/*---------------------------------------------------------
	Offline rendering
---------------------------------------------------------*/

/*
 * Create a wave as specified by 'wt', 'period' and 'flags', then run 'program'
 * off-line with the specified arguments, writing the output into the wave.
 *
 * If 'period' is 0, wave tuning will be configured so that a pitch of 0.0
 * plays the wave back at 'samplerate'.
 *
 * Rendering will stop after 'length' sample frames have been rendered, or if
 * 'length' is 0, when the output is silent.
 * 
 * The wave will be returned prepared and ready for use.
 *
 * Returns the handle of the rendered wave, or a negated A2_errors error code.
 */
A2_handle a2_RenderWave(A2_state *st,
		A2_wavetypes wt, unsigned period, int flags,
		unsigned samplerate, unsigned length, A2_property *props,
		A2_handle program, unsigned argc, int *argv);

#ifdef __cplusplus
};
#endif

#endif /* A2_WAVES_H */
