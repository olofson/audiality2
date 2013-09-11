/*
 * waves.h - Audiality 2 waveform API and unit programming interface
 *
 * Copyright (C) 2010-2012 David Olofson <david@olofson.net>
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

#include "audiality2.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* Pseudo-random numbers */
#define	A2_NOISESEED	16576


/*---------------------------------------------------------
	Wave data structure
---------------------------------------------------------*/

/* Type of waveform data */
typedef enum A2_wavetypes
{
	A2WT_OFF = 0,		/* "off" wave - silence */
	A2WT_NOISE,		/* "noise" wave - pitched S&H RNG */
	A2WT_WAVE,		/* Plain waveform */
	A2WT_MIPWAVE		/* Mipmapped waveform */
} A2_wavetypes;

/* A2_wave data for plain and mipmapped wavetables */
typedef struct A2_wave_wave
{
	int16_t		*data[A2_MIPLEVELS];	/* One buffer per mip level */
	unsigned	size[A2_MIPLEVELS];	/* Sizes EXCLUDING pre/post! */
} A2_wave_wave;

/* A2_wave data for noise generators */
typedef struct A2_wave_noise
{
	uint32_t	state;
} A2_wave_noise;

typedef struct A2_uploadbuffer A2_uploadbuffer;

/* A2_object: Waveform with mipmaps */
typedef struct A2_wave
{
	A2_wavetypes	type;
	unsigned	flags;		/* A2_LOOPED, A2_NOMIP etc */
	unsigned	period;		/* Fundamental period length */
	union {
		A2_wave_noise	noise;		/* A2WT_NOISE */
		A2_wave_wave	wave;		/* A2WT_WAVE, A2WT_MIPWAVE */
	} d;
	A2_uploadbuffer	*upload;	/* (Used when uploading) */
} A2_wave;


/*---------------------------------------------------------
	Wave management API
---------------------------------------------------------*/

typedef enum A2_waveflags
{
	A2_LOOPED =	0x00000100,	/* Waveform is looped */
/*TODO*/A2_NORMALIZE =	0x00010000,	/* Normalize waveform amplitude */
/*TODO*/A2_WRAP =	0x00020000,	/* Wrap/mix until finished (???????) */
/*TODO*/A2_XFADE =	0x00040000,	/* Crossfade to make seamless */
/*TODO*/A2_REVMIX =	0x00080000,	/* Mix in reversed to make seemless */
	A2_CLEAR =	0x00100000,	/* Clear (silence) the waveform */
	A2_UNPREPARED =	0x01000000,	/* Not prepared - DO NOT PLAY! */
} A2_waveflags;


/*
 * Upload a waveform for use by wavetable oscillators. Returns the handle of the
 * waveform, or a negative error code.
 *
 * 'wt' is the type of wave to create, as defined by A2_wavetypes.
 *
 * 'period' is the number of sample frames in one period of the waveform's
 * fundamental frequency. (Used for pitch calculations.)
 *
 * 'flags' is a set of |'ed together flags from A2_waveflags;
 *	A2_LOOPED	Wave is looped. (Affects pre-processing and playback!)
TODO:	A2_NORMALIZE	Normalize ("maximize") amplitude in the conversion.
TODO:	A2_XFADE	Crossfade mix a copy offset by half the loop length.
TODO:	A2_REVMIX	Mix wave with a reversed version of itself.
 *	A2_CLEAR	Ignore 'data' (if any) and generate a silent waveform.
 * A2_XFADE and A2_REVMIX are intended for looped waves, although they (sort of)
 * work on one-shot waves as well.
 *
 * 'fmt' is the sample format code for the 'data'.
 *
 * 'data' points to the raw waveform data to convert and upload. If NULL, the
 * resulting waveform will be allocated, and the contents left undefined. If
 * the A2_CLEAR flag is set, 'data' is ignored, and the waveform is cleared.
 * 
 * 'size' is the size in BYTES of 'data'. (This is still used to calculate the
 * waveform size when 'data' is NULL and/or A2_CLEAR is used.)
 */
A2_handle a2_WaveUpload(A2_state *st,
		A2_wavetypes wt, unsigned period, int flags,
		A2_sampleformats fmt, const void *data, unsigned size);


/*
 * Allocate a waveform for use by wavetable oscillators. Returns the handle of
 * the waveform, or a negative error code.
 * 
 * 'period', 'flags': See a2_UploadWave()!
 *
 * 'length' is the length of the waveform (mipmap level 0) in sample frames.
 */
A2_handle a2_WaveNew(A2_state *st, A2_wavetypes wt, unsigned period, int flags);

/*
 * Upload raw waveform data to an existing wave.
 * 
 * 'wave' is the waveform index as returned by a2_WaveAlloc() or
 * a2_UploadWave().
 *
 * 'fmt', 'data', 'size': See a2_UploadWave()!
 *
 * 'offset' is the first sample frame of the waveform to write.
 *
 * NOTE:
 *	Once a2_WavePrepare() has been called on a wave:
 *
 *		* The size of the wave is fixed! a2_WaveWrite() can be used for
 *		  modifying the wave in place, but writing past the end of the
 *		  wave will fail, returning A2_INDEXRANGE.
 *
 * 		* Flags A2_NORMALIZE, A2_WRAP, A2_XFADE and A2_REVMIX will
 *		  result in undefined behavior if a2_WaveWrite() is called.
 */
A2_errors a2_WaveWrite(A2_state *st, A2_handle wave, unsigned offset,
		A2_sampleformats fmt, const void *data, unsigned size);

/*
 * Prepare a waveform for playback after it's been modified via a2_WaveWrite().
 * This calculates mipmaps and pad zones as needed for correct interpolation.
 */
A2_errors a2_WavePrepare(A2_state *st, A2_handle wave);


/*
 * Get A2_wave struct from handle. Returns NULL if the handle is invalid, or if
 * the object is not a wave.
 */
A2_wave *a2_GetWave(A2_state *st, A2_handle handle);

#ifdef __cplusplus
};
#endif

#endif /* A2_WAVES_H */
