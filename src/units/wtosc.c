/*
 * wtosc.c - Audiality 2 wavetable oscillator unit
 *
 * Copyright 2010-2013 David Olofson <david@olofson.net>
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

#ifdef A2_HIFI
#	define	a2o_Inter	a2_Hermite
#else
#	define	a2o_Inter	a2_Lerp
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
	unsigned	phase;		/* Phase (24:8 fixp, 1.0/per) */
	unsigned	dphase;		/* Increment (8:24 fixp, 1.0/per) */
	int		noise;		/* Current noise sample (S&H) */
	A2_ramper	a;
	A2_wave		*wave;		/* Current waveform */
	A2_state	*state;		/* Needed when switching waveforms */
	int		samplerate;	/* Needed for pitch calculations */
} A2_wtosc;


static void a2o_OffAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_wtosc *o = (A2_wtosc *)u;
	a2_PrepareRamp(&o->a, frames);
	a2_RunRamp(&o->a, frames);
}

static void a2o_Off(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_wtosc *o = (A2_wtosc *)u;
	a2_PrepareRamp(&o->a, frames);
	a2_RunRamp(&o->a, frames);
	memset(u->outputs[0] + offset, 0, frames * sizeof(int));
}


static inline void a2o_noise(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	unsigned s, end = offset + frames;
	A2_wtosc *o = (A2_wtosc *)u;
	int32_t *out = u->outputs[0];
	unsigned dph = o->dphase >> 8;
	A2_wave_noise *wdata = &o->wave->d.noise;
	a2_PrepareRamp(&o->a, frames);
	for(s = offset; s < end; ++s)
	{
		unsigned nph = o->phase + dph;
		if((dph >= 32768) || ((nph ^ o->phase) >> 15))
			o->noise = a2_Noise(&wdata->state) - 32767;
		o->phase = nph;
		if(add)
			out[s] += o->noise * (o->a.value >> 10) >> 6;
		else
			out[s] = o->noise * (o->a.value >> 10) >> 6;
		a2_RunRamp(&o->a, 1);
	}
}

static void a2o_NoiseAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	a2o_noise(u, offset, frames, 1);
}

static void a2o_Noise(A2_unit *u, unsigned offset, unsigned frames)
{
	a2o_noise(u, offset, frames, 0);
}


static inline void a2o_wavetable(A2_unit *u, unsigned offset, unsigned frames,
		int add)
{
	unsigned s, mm, ph, dph;
	unsigned end = offset + frames;
	A2_wtosc *o = (A2_wtosc *)u;
	int32_t *out = u->outputs[0];
	A2_wave *w = o->wave;
	int16_t *d;
	if(o->dphase > 0x000fffff)
		dph = (o->dphase >> 8) * w->period >> 8;
	else
		dph = o->dphase * w->period >> 16;
	a2_PrepareRamp(&o->a, frames);
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
		a2_RunRamp(&o->a, frames);
		return;
	}
	d = w->d.wave.data[mm] + A2_WAVEPRE;
	for(s = offset; s < end; ++s)
	{
		int v = a2o_Inter(d, ph) + a2o_Inter(d, ph + (dph >> 1));
		if(add)
			out[s] += (int64_t)v * o->a.value >> (16 + 1);
		else
			out[s] = (int64_t)v * o->a.value >> (16 + 1);
		ph += dph;
		a2_RunRamp(&o->a, 1);
	}
	o->phase = ph << mm;
}

static void a2o_WavetableAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	a2o_wavetable(u, offset, frames, 1);
}

static void a2o_Wavetable(A2_unit *u, unsigned offset, unsigned frames)
{
	a2o_wavetable(u, offset, frames, 0);
}


static inline void a2o_wavetable_no_mip(A2_unit *u, unsigned offset,
		unsigned frames, int add)
{
	unsigned s, ph, dph;
	unsigned end = offset + frames;
	A2_wtosc *o = (A2_wtosc *)u;
	int32_t *out = u->outputs[0];
	A2_wave *w = o->wave;
	unsigned perfixp = w->d.wave.size[0] << 8;
	int16_t *d = w->d.wave.data[0] + A2_WAVEPRE;
	if(o->dphase > 0x000fffff)
		dph = (o->dphase >> 8) * w->period >> 8;
	else
		dph = o->dphase * w->period >> 16;
	a2_PrepareRamp(&o->a, frames);
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
				int v = a2o_Inter(d, ph) +
						a2o_Inter(d, ph + (dph >> 1));
				if(add)
					out[s] += (int64_t)v * o->a.value >>
							(16 + 1);
				else
					out[s] = (int64_t)v * o->a.value >>
							(16 + 1);
				ph += dph;
				ph %= perfixp;
				a2_RunRamp(&o->a, 1);
			}
		else
			for(s = offset; (s < end) && (ph < perfixp); ++s)
			{
				int v = a2o_Inter(d, ph) +
						a2o_Inter(d, ph + (dph >> 1));
				if(add)
					out[s] += (int64_t)v * o->a.value >>
							(16 + 1);
				else
					out[s] = (int64_t)v * o->a.value >>
							(16 + 1);
				ph += dph;
				a2_RunRamp(&o->a, 1);
			}
	}
	else
		for(s = offset; s < end; ++s)
		{
			int v = a2o_Inter(d, ph) +
					a2o_Inter(d, ph + (dph >> 1));
			if(add)
				out[s] += (int64_t)v * o->a.value >> (16 + 1);
			else
				out[s] = (int64_t)v * o->a.value >> (16 + 1);
			ph += dph;
			a2_RunRamp(&o->a, 1);
		}
	o->phase = ph;
}

static void a2o_WavetableNoMipAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	a2o_wavetable_no_mip(u, offset, frames, 1);
}

static void a2o_WavetableNoMip(A2_unit *u, unsigned offset, unsigned frames)
{
	a2o_wavetable_no_mip(u, offset, frames, 0);
}


static inline void a2_OscFrequency(A2_unit *u, int samplerate, float f)
{
	A2_wtosc *o = (A2_wtosc *)u;
	o->dphase = f * (65536.0f * 256.0f) / samplerate;
}


/*
 *	ph	desired phase, [0, 1], (16):16 fixp
 *	sst	SubSample Time, [0, 1], (24):8 fixp
 */
static inline void a2_OscPhase(A2_wtosc *o, int ph, unsigned sst)
{
	if(!o->wave)
	{
		o->phase = 0;
		return;
	}
	o->phase = ph * o->wave->period >> 8;
//FIXME: Is this correct...?
	o->phase -= (sst * o->dphase) * o->wave->period >> 16;
}


static A2_errors a2o_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	int *ur = u->registers;
	A2_wtosc *o = (A2_wtosc *)u;

	/* Internal state initialization */
	o->state = cfg->state;
	o->samplerate = cfg->samplerate;
	o->phase = 0;
	o->noise = 0;
	o->wave = NULL;
	a2_SetRamp(&o->a, 0, 0);
	a2_OscFrequency(u, o->samplerate, powf(2.0f,
			vms->r[R_TRANSPOSE] * (1.0f / 65536.0f)) * A2_MIDDLEC);

	/* Initialize VM registers */
	ur[A2OR_WAVE] = 0;
	ur[A2OR_PITCH] = 0;
	ur[A2OR_AMPLITUDE] = 0;
	ur[A2OR_PHASE] = 0;

	/* Install Process callback (Can change at run-time as needed!) */
	o->flags = flags;
	if(flags & A2_PROCADD)
		u->Process = a2o_OffAdd;
	else
		u->Process = a2o_Off;

	return A2_OK;
}

static void a2o_Wave(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_wtosc *o = (A2_wtosc *)u;
	A2_wavetypes wt = A2_WOFF;
	value >>= 16;
	if((o->wave = a2_GetWave(o->state, value)))
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
		if(o->flags & A2_PROCADD)
			u->Process = a2o_OffAdd;
		else
			u->Process = a2o_Off;
		break;
	  case A2_WNOISE:
		if(o->flags & A2_PROCADD)
			u->Process = a2o_NoiseAdd;
		else
			u->Process = a2o_Noise;
		break;
	  case A2_WWAVE:
		if(o->flags & A2_PROCADD)
			u->Process = a2o_WavetableNoMipAdd;
		else
			u->Process = a2o_WavetableNoMip;
		break;
	  case A2_WMIPWAVE:
		if(o->flags & A2_PROCADD)
			u->Process = a2o_WavetableAdd;
		else
			u->Process = a2o_Wavetable;
		break;
	}
}

static void a2o_Pitch(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_wtosc *o = (A2_wtosc *)u;
	a2_OscFrequency(u, o->samplerate,
			powf(2.0f, (value + vms->r[R_TRANSPOSE]) *
			(1.0f / 65536.0f)) * A2_MIDDLEC);
}

static void a2o_Amplitude(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_wtosc *o = (A2_wtosc *)u;
	a2_SetRamp(&o->a, value, frames);
}

static void a2o_Phase(A2_unit *u, A2_vmstate *vms, int value, int frames)
{
	A2_wtosc *o = (A2_wtosc *)u;
	a2_OscPhase(o, value, vms->timer && 0xff);
}

static const A2_crdesc regs[] =
{
	{ "w",		a2o_Wave		},	/* A2OR_WAVE */
	{ "p",		a2o_Pitch		},	/* A2OR_PITCH */
	{ "a",		a2o_Amplitude		},	/* A2OR_AMPLITUDE */
	{ "phase",	a2o_Phase		},	/* A2OR_PHASE */
	{ NULL,	NULL				}
};

const A2_unitdesc a2_wtosc_unitdesc =
{
	"wtosc",		/* name */

	regs,			/* registers */

	0,	0,		/* [min,max]inputs */
	1,	1,		/* [min,max]outputs */

	sizeof(A2_wtosc),	/* instancesize */
	a2o_Initialize,		/* Initialize */
	NULL			/* Deinitialize */
};
