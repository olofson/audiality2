/*
 * wtosc.c - Audiality 2 wavetable oscillator unit
 *
 * Copyright 2010-2017, 2022 David Olofson <david@olofson.net>
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

#include <string.h>
#include "wtosc.h"
#include "internals.h"

#ifdef A2_HIFI
/* Hermite interpolation with 2x oversampling */
static inline float wtosc_Inter(float *d, unsigned ph, unsigned dph)
{
	return (a2_Hermite(d, ph) + a2_Hermite(d, ph + (dph >> 1))) * 0.5f;
}
#elif (defined A2_LOFI)
/* Linear interpolation */
static inline float wtosc_Inter(float *d, unsigned ph, unsigned dph)
{
	return a2_Lerp(d, ph);
}
#else
/* Linear interpolation with 2x oversampling */
static inline float wtosc_Inter(float *d, unsigned ph, unsigned dph)
{
	return (a2_Lerp(d, ph) + a2_Lerp(d, ph + (dph >> 1))) * 0.5f;
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
	A2OR_PHASE
} A2O_cregisters;

typedef struct A2_wtosc
{
	A2_unit		header;
	unsigned	flags;		/* Init flags (for wave changing) */
	unsigned	dphase;		/* Increment (8:24 fixp, 1.0/period) */
	uint64_t	phase;		/* Phase (48:24 fixp, 1.0/sample) */
	float		noise;		/* Current noise sample (S&H) */
	int		p_ramping;	/* Previous state of 'p' ramper */
	float		basepitch;	/* Pitch of middle C (1.0/octave) */
	A2_ramper	p;		/* Linear pitch ramper */
	A2_ramper	a;		/* Amplitude ramper */
	A2_wave		*wave;		/* Current waveform */
	A2_interface	*interface;	/* For changing waves */
	float		*transpose;	/* Needed for pitch calculations */
} A2_wtosc;


static inline A2_wtosc *wtosc_cast(A2_unit *u)
{
	return (A2_wtosc *)u;
}


static inline void wtosc_run_pitch(A2_wtosc *o, unsigned frames)
{
	float lastv;
	a2_PrepareRamper(&o->p, frames);
	if(o->dphase && (!o->p.timer && !o->p_ramping))
		return;	/* No update needed */

	/* Use halfway value while still ramping */
	lastv = o->p.value;
	a2_RunRamper(&o->p, frames);

	/* We'll need an extra update after the end of a ramp! */
	o->p_ramping = (o->p.delta != 0.0f);

	/* Calculate new phase delta */
	o->dphase = a2_P2I((lastv + o->p.value) * 32768.0f);
}


static void wtosc_OffAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_wtosc *o = wtosc_cast(u);
	a2_PrepareRamper(&o->p, frames);
	a2_PrepareRamper(&o->a, frames);
	a2_RunRamper(&o->p, frames);
	a2_RunRamper(&o->a, frames);
}


static void wtosc_Off(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_wtosc *o = wtosc_cast(u);
	a2_PrepareRamper(&o->p, frames);
	a2_PrepareRamper(&o->a, frames);
	a2_RunRamper(&o->p, frames);
	a2_RunRamper(&o->a, frames);
	memset(u->outputs[0] + offset, 0, frames * sizeof(int));
}


static inline void wtosc_noise(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	A2_wtosc *o = wtosc_cast(u);
	unsigned s, end = offset + frames;
	float *out = u->outputs[0];
	A2_state *st = ((A2_interface_i *)o->interface)->state;
	uint32_t *nstate = &st->noisestate;
	wtosc_run_pitch(o, frames);
	a2_PrepareRamper(&o->a, frames);

	for(s = offset; s < end; ++s)
	{
		uint64_t nph = o->phase + o->dphase;
		if((o->dphase >= (1 << 23)) || ((nph ^ o->phase) >> 23))
			o->noise = a2_Noise(nstate);
		o->phase = nph;
		if(add)
			out[s] += o->noise * o->a.value;
		else
			out[s] = o->noise * o->a.value;
		a2_RunRamper(&o->a, 1);
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
#if DEBUG
	A2_LOG_DBG(o->interface, "wtosc: Wave %d unloaded while playing!",
			(int)u->registers[A2OR_WAVE]);
#endif
	o->wave = NULL;
	if(o->flags & A2_PROCADD)
		u->Process = wtosc_OffAdd;
	else
		u->Process = wtosc_Off;
	return 1;
}

/*
 * Inner loop inline.
 *	o	Oscillator struct
 *	d	Wave data
 *	out	Output buffer
 *	offset	Start index in output buffer
 *	frames	Number of output samples to render
 *	ph	Phase accumulator
 *	dph	Per output sample frame phase increment
 *	add	(flag) Adding mode
 *	looped	(flag) Wave is looped (ignored if wsize == 0)
 *	wsize	Size of wave. Pass 0 to disable loop/end checks.
 *
 * Returns the final state of the phase accumulator.
 */
static inline uint64_t wtosc_do_fragment(A2_wtosc *o, float *d, float *out,
		unsigned offset, unsigned frames, uint64_t ph, unsigned dph,
		int add, int looped, unsigned wsize)
{
	unsigned s;
	unsigned end = offset + frames;
	for(s = offset; s < end; ++s)
	{
		float v;
		if(wsize)
		{
			if(looped)
			{
				ph %= (uint64_t)wsize << 24;
			}
			else if((ph >> 24) >= wsize)
			{
				/*
				 * End of wave! Clear the rest of the output
				 * buffer, unless we're in adding mode.
				 */
				if(!add)
					memset(out + s, 0,
						(end - s) * sizeof(int));
				break;
			}
		}
		v = wtosc_Inter(d, ph >> 16, dph >> 16);
		if(add)
			out[s] += v * o->a.value;
		else
			out[s] = v * o->a.value;
		ph += dph;
		a2_RunRamper(&o->a, 1);
	}
	return ph;
}


static inline void wtosc_wavetable(A2_unit *u, unsigned offset,
		unsigned frames, int add)
{
	A2_wtosc *o = wtosc_cast(u);
	unsigned mm, dph;
	uint64_t ph;
	float *out = u->outputs[0];
	A2_wave *w = o->wave;
	if(wtosc_check_unloaded(u, w))
		return;

	wtosc_run_pitch(o, frames);
	dph = ((o->dphase + 255) >> 8) * w->period;
	a2_PrepareRamper(&o->a, frames);
	/* FIXME: Cache, or do something smarter... */
	for(mm = 0; (dph > (A2_MAXPHINC << 8)) &&
			(mm < A2_MIPLEVELS - 1); ++mm)
		dph >>= 1;
	ph = o->phase >> mm;
	dph = (uint64_t)o->dphase * w->period >> mm;

	if(w->flags & A2_LOOPED)
	{
		ph %= (uint64_t)w->d.wave.size[mm] << 24;
	}
	else if((ph >> 24) > (w->d.wave.size[mm] + A2_WAVEPRE))
	{
		if(!add)
			memset(out + offset, 0, frames * sizeof(int));
		return;		/* All played! */
	}

	if(dph > (A2_MAXPHINC << 16))
	{
		/* Pitch out of range! Output silence. */
		if(!add)
			memset(out + offset, 0, frames * sizeof(int));
		ph += (uint64_t)dph * frames;
		o->phase = ph << mm;
		a2_RunRamper(&o->a, frames);
	}
	else
	{
		o->phase = wtosc_do_fragment(o,
				w->d.wave.data[mm] + A2_WAVEPRE, out,
				offset, frames, ph, dph, add, 0, 0) << mm;
	}
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
	uint64_t dph;
	float *out = u->outputs[0];
	A2_wave *w = o->wave;
	float *d = w->d.wave.data[0] + A2_WAVEPRE;
	if(wtosc_check_unloaded(u, w))
		return;

	wtosc_run_pitch(o, frames);
	dph = (uint64_t)o->dphase * w->period;
	a2_PrepareRamper(&o->a, frames);

	if(dph >> 32)
	{
		/* Pitch out of range! Output silence. */
		if(!add)
			memset(out + offset, 0, frames * sizeof(int));
		o->phase += dph * frames;
		a2_RunRamper(&o->a, frames);
	}
	else if(dph > (A2_MAXPHINC << 16))
	{
		/*
		 * Too high pitch, so we need some extra logic to avoid reading
		 * off the end of the physical wavetable! (This cannot happen
		 * with mipmapped waveforms, as they safely go up to 11 octaves
		 * above the output sample rate, and are muted above that.)
		 */
		if(w->flags & A2_LOOPED)
			o->phase = wtosc_do_fragment(o, d, out, offset, frames,
					o->phase, dph,
					add, 1, w->d.wave.size[0]);
		else
			o->phase = wtosc_do_fragment(o, d, out, offset, frames,
					o->phase, dph,
					add, 0, w->d.wave.size[0]);
	}
	else
	{
		/* This inner loop won't check, so we need to check first! */
		if(w->flags & A2_LOOPED)
		{
			o->phase %= w->d.wave.size[0] << 24;
		}
		else if((o->phase >> 24) > (w->d.wave.size[0] + A2_WAVEPRE))
		{
			if(!add)
				memset(out + offset, 0, frames * sizeof(int));
			return;		/* All played! */
		}
		o->phase = wtosc_do_fragment(o, d, out, offset, frames,
				o->phase, dph,
				add, 0, 0);
	}
}


static void wtosc_WavetableNoMipAdd(A2_unit *u, unsigned offset,
		unsigned frames)
{
	wtosc_wavetable_no_mip(u, offset, frames, 1);
}


static void wtosc_WavetableNoMip(A2_unit *u, unsigned offset, unsigned frames)
{
	wtosc_wavetable_no_mip(u, offset, frames, 0);
}


/*
 *	ph	desired phase, 16:16 fixp; 1.0/period
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
	o->phase = (int64_t)ph * o->wave->period << 8;
}


static A2_errors wtosc_Initialize(A2_unit *u, A2_vmstate *vms, void *statedata,
		unsigned flags)
{
	A2_config *cfg = (A2_config *)statedata;
	A2_wtosc *o = wtosc_cast(u);
	float *ur = u->registers;

	/* Internal state initialization */
	o->interface = cfg->interface;
	o->basepitch = cfg->basepitch;
	o->transpose = vms->r + R_TRANSPOSE;
	o->noise = 0.0f;
	o->wave = NULL;
	a2_InitRamper(&o->a, 0.0f);
	a2_InitRamper(&o->p, *o->transpose + o->basepitch);
	o->dphase = a2_P2I(o->p.value * 65536.0f);
	o->p_ramping = 0;
	wtosc_set_phase(o, 0, vms->waketime & 0xff);

	/* Initialize VM registers */
	ur[A2OR_WAVE] = 0.0f;
	ur[A2OR_PITCH] = 0.0f;
	ur[A2OR_AMPLITUDE] = 0.0f;
	ur[A2OR_PHASE] = 0.0f;

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


static void wtosc_Wave(A2_unit *u, float v, unsigned start, unsigned dur)
{
	A2_wtosc *o = wtosc_cast(u);
	A2_wavetypes wt = A2_WOFF;
	if((o->wave = a2_GetWave(o->interface, v)))
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


static void wtosc_Pitch(A2_unit *u, float v, unsigned start, unsigned dur)
{
	A2_wtosc *o = wtosc_cast(u);
	a2_SetRamper(&o->p, v + *o->transpose + o->basepitch, start, dur);
	if(!dur)
		o->p_ramping = 1;	/* Force update for 'set'! */
}


static void wtosc_Amplitude(A2_unit *u, float v, unsigned start, unsigned dur)
{
	a2_SetRamper(&wtosc_cast(u)->a, v, start, dur);
}


static void wtosc_Phase(A2_unit *u, float v, unsigned start, unsigned dur)
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
	NULL,			/* coutputs */

	NULL,			/* constants */

	0,	0,		/* [min,max]inputs */
	1,	1,		/* [min,max]outputs */

	sizeof(A2_wtosc),	/* instancesize */
	wtosc_Initialize,	/* Initialize */
	NULL,			/* Deinitialize */

	wtosc_OpenState,	/* OpenState */
	NULL			/* CloseState */
};
