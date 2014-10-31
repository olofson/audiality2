/*
 * wtosc.c - Audiality 2 wavetable oscillator unit
 *
 * Copyright 2010-2014 David Olofson <david@olofson.net>
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

#include <math.h>
#include <string.h>
#include "wtosc.h"
#include "waves.h"
#include "dsp.h"
#include "internals.h"

/* NOTE: These all return doubled amplitude samples! */
#ifdef A2_HIFI
/* Hermite interpolation with 2x oversampling */
static inline int wtosc_Inter(int16_t *d, unsigned ph, unsigned dph)
{
	return a2_Hermite(d, ph) + a2_Hermite(d, ph + (dph >> 1));
}
#elif (defined A2_LOFI)
/* Linear interpolation */
static inline int wtosc_Inter(int16_t *d, unsigned ph, unsigned dph)
{
	return a2_Lerp(d, ph) << 1;
}
#else
/* Linear interpolation with 2x oversampling */
static inline int wtosc_Inter(int16_t *d, unsigned ph, unsigned dph)
{
	return a2_Lerp(d, ph) + a2_Lerp(d, ph + (dph >> 1));
}
#endif

/*
 * Maximum supported number of sample frames in a wave.
 * 
 * NOTE:
 *	As it is, this needs to leave room for the phase accumulator to run
 *	slightly beyond the end of the wave, or the stop/loop logic won't work!
 */
#define	A2_WTOSC_MAXLENGTH	(0x01000000 - A2_WAVEPRE - A2_WAVEPOST)

/* Control register frame enumeration */
typedef enum A2O_cregisters
{
	A2OR_WAVE = 0,
	A2OR_PITCH,
	A2OR_AMPLITUDE,
	A2OR_PHASE,
	A2OR_REGISTERS
} A2O_cregisters;

typedef struct A2_wtosc
{
	A2_unit		header;
	unsigned	flags;		/* Init flags (for wave changing) */
	unsigned	phase;		/* Phase (24:8 fixp, 1.0/sample) */
	unsigned	dphase;		/* Increment (8:24 fixp, 1.0/period) */
	int		noise;		/* Current noise sample (S&H) */
	A2_ramper	a;
	A2_wave		*wave;		/* Current waveform */
	A2_state	*state;		/* Needed when switching waveforms */
	int		*transpose;	/* Needed for pitch calculations */
	float		onedivfs;	/* Sample rate conversion factor */
} A2_wtosc;


static inline A2_wtosc *wtosc_cast(A2_unit *u)
{
	return (A2_wtosc *)u;
}


static void wtosc_OffAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_wtosc *o = wtosc_cast(u);
	a2_RamperPrepare(&o->a, frames);
	a2_RamperRun(&o->a, frames);
}

static void wtosc_Off(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_wtosc *o = wtosc_cast(u);
	a2_RamperPrepare(&o->a, frames);
	a2_RamperRun(&o->a, frames);
	memset(u->outputs[0] + offset, 0, frames * sizeof(int));
}


static inline void wtosc_noise(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	A2_wtosc *o = wtosc_cast(u);
	unsigned s, end = offset + frames;
	int32_t *out = u->outputs[0];
	unsigned dph = o->dphase >> 8;
	uint32_t *nstate = &o->state->noisestate;
	a2_RamperPrepare(&o->a, frames);
	for(s = offset; s < end; ++s)
	{
		unsigned nph = o->phase + dph;
		if((dph >= 32768) || ((nph ^ o->phase) >> 15))
			o->noise = a2_Noise(nstate) - 32767;
		o->phase = nph;
		if(add)
			out[s] += o->noise * (o->a.value >> 10) >> 6;
		else
			out[s] = o->noise * (o->a.value >> 10) >> 6;
		a2_RamperRun(&o->a, 1);
	}
}

static void wtosc_NoiseAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	wtosc_noise(u, offset, frames, 1);
}

static void wtosc_Noise(A2_unit *u, unsigned offset, unsigned frames)
{
	wtosc_noise(u, offset, frames, 0);
}


/* Handle waveforms that have just been unloaded */
static inline int wtosc_check_unloaded(A2_unit *u, A2_wave *w)
{
	A2_wtosc *o = wtosc_cast(u);
	if(w->d.wave.size[0])
		return 0;
	o->wave = NULL;
	if(o->flags & A2_PROCADD)
		u->Process = wtosc_OffAdd;
	else
		u->Process = wtosc_Off;
	return 1;
}


static inline void wtosc_wavetable(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	A2_wtosc *o = wtosc_cast(u);
	unsigned s, mm, ph, dph;
	unsigned end = offset + frames;
	int32_t *out = u->outputs[0];
	A2_wave *w = o->wave;
	int16_t *d;
	if(wtosc_check_unloaded(u, w))
		return;
	if(o->dphase > 0x000fffff)
		dph = (o->dphase >> 8) * w->period >> 8;
	else
		dph = o->dphase * w->period >> 16;
	a2_RamperPrepare(&o->a, frames);
	/* FIXME: Cache, or do something smarter... */
	for(mm = 0; (dph > A2_MAXPHINC) && (mm < A2_MIPLEVELS - 1); ++mm)
		dph >>= 1;
	ph = o->phase >> mm;
	if(w->flags & A2_LOOPED)
		ph %= w->d.wave.size[mm] << 8;
	else if((ph >> 8) > (w->d.wave.size[mm] + A2_WAVEPRE))
	{
		if(!add)
			memset(out + offset, 0, frames * sizeof(int));
		return;		/* All played! */
	}
	if(dph > A2_MAXPHINC)
	{
		if(!add)
			memset(out + offset, 0, frames * sizeof(int));
		ph += dph * frames;
		o->phase = ph << mm;
		a2_RamperRun(&o->a, frames);
		return;
	}
	d = w->d.wave.data[mm] + A2_WAVEPRE;
	for(s = offset; s < end; ++s)
	{
		int v = wtosc_Inter(d, ph, dph);
		if(add)
			out[s] += (int64_t)v * o->a.value >> (16 + 1);
		else
			out[s] = (int64_t)v * o->a.value >> (16 + 1);
		ph += dph;
		a2_RamperRun(&o->a, 1);
	}
	o->phase = ph << mm;
}

static void wtosc_WavetableAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	wtosc_wavetable(u, offset, frames, 1);
}

static void wtosc_Wavetable(A2_unit *u, unsigned offset, unsigned frames)
{
	wtosc_wavetable(u, offset, frames, 0);
}


static inline void wtosc_wavetable_no_mip(A2_unit *u, unsigned offset,
		unsigned frames, int add)
{
	A2_wtosc *o = wtosc_cast(u);
	unsigned s, ph, dph;
	unsigned end = offset + frames;
	int32_t *out = u->outputs[0];
	A2_wave *w = o->wave;
	unsigned perfixp = w->d.wave.size[0] << 8;
	int16_t *d = w->d.wave.data[0] + A2_WAVEPRE;
	if(wtosc_check_unloaded(u, w))
		return;
	if(o->dphase > 0x000fffff)
		dph = (o->dphase >> 8) * w->period >> 8;
	else
		dph = o->dphase * w->period >> 16;
	a2_RamperPrepare(&o->a, frames);
	ph = o->phase;
	if(w->flags & A2_LOOPED)
		ph %= perfixp;
	else if((ph >> 8) > (w->d.wave.size[0] + A2_WAVEPRE))
	{
		if(!add)
			memset(out + offset, 0, frames * sizeof(int));
		return;		/* All played! */
	}
	if(dph > A2_MAXPHINC)
	{
		/*
		 * Too high pitch, so we need some extra logic to avoid reading
		 * off the end of the physical wavetable! (This cannot happen
		 * with mipmapped waveforms, as they safely go up to 11 octaves
		 * above the output sample rate, and are muted above that.)
		 */
		if(w->flags & A2_LOOPED)
			for(s = offset; s < end; ++s)
			{
				int v = wtosc_Inter(d, ph, dph);
				if(add)
					out[s] += (int64_t)v * o->a.value >>
							(16 + 1);
				else
					out[s] = (int64_t)v * o->a.value >>
							(16 + 1);
				ph += dph;
				ph %= perfixp;
				a2_RamperRun(&o->a, 1);
			}
		else
			for(s = offset; (s < end) && (ph < perfixp); ++s)
			{
				int v = wtosc_Inter(d, ph, dph);
				if(add)
					out[s] += (int64_t)v * o->a.value >>
							(16 + 1);
				else
					out[s] = (int64_t)v * o->a.value >>
							(16 + 1);
				ph += dph;
				a2_RamperRun(&o->a, 1);
			}
	}
	else
		for(s = offset; s < end; ++s)
		{
			int v = wtosc_Inter(d, ph, dph);
			if(add)
				out[s] += (int64_t)v * o->a.value >> (16 + 1);
			else
				out[s] = (int64_t)v * o->a.value >> (16 + 1);
			ph += dph;
			a2_RamperRun(&o->a, 1);
		}
	o->phase = ph;
}

static void wtosc_WavetableNoMipAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	wtosc_wavetable_no_mip(u, offset, frames, 1);
}

static void wtosc_WavetableNoMip(A2_unit *u, unsigned offset, unsigned frames)
{
	wtosc_wavetable_no_mip(u, offset, frames, 0);
}


static inline int wtosc_f2dphase(A2_wtosc *o, float f)
{
	return f * o->onedivfs + 0.5f;
}


/*
 *	ph	desired phase, [0, 1], (16):16 fixp
 *	sst	SubSample Time, [0, 1], (24):8 fixp
 */
static inline void wtosc_set_phase(A2_wtosc *o, int ph, unsigned sst)
{
	if(!o->wave)
	{
		o->phase = 0;
		return;
	}
	ph += sst * (o->dphase >> 8) >> 8;
	o->phase = ph * o->wave->period >> 8;
}


static A2_errors wtosc_Initialize(A2_unit *u, A2_vmstate *vms, void *statedata,
		unsigned flags)
{
	A2_config *cfg = (A2_config *)statedata;
	A2_wtosc *o = wtosc_cast(u);
	int *ur = u->registers;

	/* Internal state initialization */
	o->state = cfg->state;
	o->transpose = vms->r + R_TRANSPOSE;
	o->onedivfs = 16777216.0f / cfg->samplerate;
	o->noise = 0;
	o->wave = NULL;
	a2_RamperInit(&o->a, 0);
	o->dphase = wtosc_f2dphase(o, powf(2.0f,
			(*o->transpose) * (1.0f / 65536.0f)) * A2_MIDDLEC);
	wtosc_set_phase(o, 0, vms->waketime & 0xff);

	/* Initialize VM registers */
	ur[A2OR_WAVE] = 0;
	ur[A2OR_PITCH] = 0;
	ur[A2OR_AMPLITUDE] = 0;
	ur[A2OR_PHASE] = 0;

	/* Install Process callback (Can change at run-time as needed!) */
	o->flags = flags;
	if(flags & A2_PROCADD)
		u->Process = wtosc_OffAdd;
	else
		u->Process = wtosc_Off;

	return A2_OK;
}


static A2_errors wtosc_OpenState(A2_config *cfg, void **statedata)
{
	*statedata = cfg;
	return A2_OK;
}


static void wtosc_Wave(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_wtosc *o = wtosc_cast(u);
	A2_wavetypes wt = A2_WOFF;
	v >>= 16;
	if((o->wave = a2_GetWave(o->state, v)))
		wt = o->wave->type;
	switch(wt)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		if(o->wave->d.wave.size[0] > A2_WTOSC_MAXLENGTH)
		{
/* FIXME: Error/warning message here! */
			wt = A2_WOFF;
		}
		break;
	  default:
		break;
	}
	switch(wt)
	{
	  default:
/* FIXME: Error/warning message here! */
	  case A2_WOFF:
		o->wave = NULL;
		if(o->flags & A2_PROCADD)
			u->Process = wtosc_OffAdd;
		else
			u->Process = wtosc_Off;
		break;
	  case A2_WNOISE:
		if(o->flags & A2_PROCADD)
			u->Process = wtosc_NoiseAdd;
		else
			u->Process = wtosc_Noise;
		break;
	  case A2_WWAVE:
		if(o->flags & A2_PROCADD)
			u->Process = wtosc_WavetableNoMipAdd;
		else
			u->Process = wtosc_WavetableNoMip;
		break;
	  case A2_WMIPWAVE:
		if(o->flags & A2_PROCADD)
			u->Process = wtosc_WavetableAdd;
		else
			u->Process = wtosc_Wavetable;
		break;
	}
}

static void wtosc_Pitch(A2_unit *u, int v, unsigned start, unsigned dur)
{
	A2_wtosc *o = wtosc_cast(u);
	o->dphase = wtosc_f2dphase(o, powf(2.0f,
			(v + *o->transpose) * (1.0f / 65536.0f)) * A2_MIDDLEC);
}

static void wtosc_Amplitude(A2_unit *u, int v, unsigned start, unsigned dur)
{
	a2_RamperSet(&wtosc_cast(u)->a, v, start, dur);
}

static void wtosc_Phase(A2_unit *u, int v, unsigned start, unsigned dur)
{
	wtosc_set_phase(wtosc_cast(u), v, start);
}


static const A2_crdesc regs[] =
{
	{ "w",		wtosc_Wave		},	/* A2OR_WAVE */
	{ "p",		wtosc_Pitch		},	/* A2OR_PITCH */
	{ "a",		wtosc_Amplitude		},	/* A2OR_AMPLITUDE */
	{ "phase",	wtosc_Phase		},	/* A2OR_PHASE */
	{ NULL,	NULL				}
};

const A2_unitdesc a2_wtosc_unitdesc =
{
	"wtosc",		/* name */

	0,			/* flags */

	regs,			/* registers */

	0,	0,		/* [min,max]inputs */
	1,	1,		/* [min,max]outputs */

	sizeof(A2_wtosc),	/* instancesize */
	wtosc_Initialize,	/* Initialize */
	NULL,			/* Deinitialize */

	wtosc_OpenState,	/* OpenState */
	NULL			/* CloseState */
};
