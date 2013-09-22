/*
 * waves.c - Audiality 2 waveform management
 *
 * Copyright 2010-2012 David Olofson <david@olofson.net>
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "internals.h"


static int a2_sample_size(A2_sampleformats fmt)
{
	switch(fmt)
	{
	  case A2_I8:
		return 1;
	  case A2_I16:
		return 2;
	  case A2_I24:	/* (It's actually 8:24 fixed point...) */
	  case A2_I32:
	  case A2_F32:
		return 4;
	  default:
		return 0;
	}
}


A2_handle a2_WaveNew(A2_state *st, A2_wavetypes wt, unsigned period, int flags)
{
	A2_handle h;
	A2_wave *w = (A2_wave *)calloc(1, sizeof(A2_wave));
	if(!w)
		return -A2_OOMEMORY;
	w->type = wt;
	w->flags = flags;
	w->period = period;
	switch(w->type)
	{
	  case A2_WOFF:
	  case A2_WNOISE:
		break;
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		w->flags |= A2_UNPREPARED;
		break;
	}
	h = rchm_NewEx(&st->ss->hm, w, A2_TWAVE, flags | A2_APIOWNED, 1);
	if(h < 0)
	{
		free(w);
		return -h;
	}
	DBG(fprintf(stderr, "New wave %p %d\n", w, h);)
	return h;
}


/* Allocate buffers for 'length' samples at mip level 0 */
static A2_errors a2_wave_alloc(A2_wave *w, unsigned length)
{
	int i, miplevels;
	switch(w->type)
	{
	  case A2_WWAVE:
		miplevels = 1;
		break;
	  case A2_WMIPWAVE:
		miplevels = A2_MIPLEVELS;
		break;
	  default:
		return A2_OK;
	}
	for(i = 0; i < miplevels; ++i)
	{
		A2_wave_wave *ww = &w->d.wave;
		int size = (length + (1 << i) - 1) >> i;
		ww->size[i] = size;
		size = A2_WAVEPRE + size + A2_WAVEPOST;
		if(w->flags & A2_CLEAR)
			ww->data[i] = (int16_t *)calloc(size, sizeof(int16_t));
		else
			ww->data[i] = (int16_t *)malloc(size * sizeof(int16_t));
		if(!ww->data[i])
			return A2_OOMEMORY;
	}
	return A2_OK;
}


static void a2_wave_fix_pad(A2_wave *w, unsigned miplevel)
{
	int16_t *d = w->d.wave.data[miplevel];
	unsigned size = w->d.wave.size[miplevel];
	if((w->flags & A2_LOOPED) && size)
	{
		int i;
		memcpy(d, d + size, A2_WAVEPRE * 2);
		for(i = 0; i < A2_WAVEPOST; ++i)
			d[A2_WAVEPRE + size + i] = d[A2_WAVEPRE + i % size];
	}
	else
	{
		memset(d, 0, A2_WAVEPRE * 2);
		memset(d + A2_WAVEPRE + size, 0, A2_WAVEPOST * 2);
	}
}

static void a2_wave_render_mipmaps(A2_wave *w)
{
	int i;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		a2_wave_fix_pad(w, 0);
		if(w->type == A2_WMIPWAVE)
			break;
	  default:
		return;
	}
	for(i = 1; i < A2_MIPLEVELS; ++i)
	{
		int s;
		int16_t *sd = w->d.wave.data[i - 1] + A2_WAVEPRE;
		int16_t *d = w->d.wave.data[i] + A2_WAVEPRE;
		for(s = 0; s < w->d.wave.size[i]; ++s)
			d[s] = (((int)sd[s * 2] << 1) + sd[s * 2 - 1] +
					sd[s * 2 + 1]) >> 2;
		a2_wave_fix_pad(w, i);
	}
#if 0
	printf("-------------\n");
	for(i = 0; i < A2_MIPLEVELS; ++i)
	{
		int s;
		int16_t *d = w->d.wave.data[i] + A2_WAVEPRE;
		printf(" %d:\n", i);
		printf("(");
		for(s = 0; s < A2_WAVEPRE; ++s)
			printf("\t%d", d[s - A2_WAVEPRE]);
		printf("\t)\n");
		for(s = 0; s < w->d.wave.size[i]; ++s)
			printf("\t%d", d[s]);
		printf("\n");
		printf("(");
		for(s = 0; s < A2_WAVEPOST; ++s)
			printf("\t%d", d[w->d.wave.size[i] + s]);
		printf("\t)\n");
	}
#endif
}


/* Convert and write samples directly into mip level 0 of waveform. */
static A2_errors a2_do_write(A2_wave *w, unsigned offset,
		A2_sampleformats fmt, const void *data, unsigned length)
{
	int s;
	int size = w->d.wave.size[0];
	int16_t *d = w->d.wave.data[0] + A2_WAVEPRE + offset;
	if(offset + length > size)
		return A2_INDEXRANGE;
	switch(fmt)
	{
	  case A2_I8:
		for(s = 0; s < length; ++s)
			d[s] = ((int8_t *)data)[s] << 8;
		break;
	  case A2_I16:
		for(s = 0; s < length; ++s)
			d[s] = ((int16_t *)data)[s];
		break;
	  case A2_I24:
		for(s = 0; s < length; ++s)
			d[s] = ((int32_t *)data)[s] >> 16;
		break;
	  case A2_I32:
		for(s = 0; s < length; ++s)
			d[s] = ((int32_t *)data)[s] >> 16;
		break;
	  case A2_F32:
		for(s = 0; s < length; ++s)
			d[s] = ((float *)data)[s] * 32767.0f;
		break;
	  default:
		return A2_BADFORMAT;
	}
	return A2_OK;
}


static A2_errors a2_add_upload_buffer(A2_wave *w, unsigned offset,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_uploadbuffer *lastub = w->upload;
	A2_uploadbuffer *ub = (A2_uploadbuffer *)malloc(sizeof(A2_uploadbuffer));
	int ss = a2_sample_size(fmt);
	if(!ss)
		return -A2_BADFORMAT;
	if(!ub)
		return A2_OOMEMORY;
	ub->next = NULL;
	ub->data = malloc(size * ss);
	if(!ub->data)
	{
		free(ub);
		return A2_OOMEMORY;
	}
	ub->fmt = fmt;
	ub->offset = offset;
	ub->size = size;
	memcpy(ub->data, data, size * ss);
	if(lastub)
	{
		while(lastub->next)
			lastub = lastub->next;
		lastub->next = ub;
	}
	else
		w->upload = ub;
	return A2_OK;
}


/* Discard upload buffers without applying them */
static void a2_discard_upload_buffers(A2_wave *w)
{
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		while(w->upload)
		{
			A2_uploadbuffer *ub = w->upload;
			w->upload = ub->next;
			free(ub->data);
			free(ub);
		}
		return;
	  default:
		return;
	}
}


/* Apply and discard upload buffers */
static A2_errors a2_apply_upload_buffers(A2_wave *w)
{
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		while(w->upload)
		{
			A2_errors res;
			A2_uploadbuffer *ub = w->upload;
			if((res = a2_do_write(w, ub->offset, ub->fmt,
					ub->data, ub->size)))
			{
				a2_discard_upload_buffers(w);
				return res;
			}
			w->upload = ub->next;
			free(ub->data);
			free(ub);
		}
		return A2_OK;
	  default:
		return A2_OK;
	}
}


/*
 * Analyze buffered writes to determine total length of waveform in samples,
 * NOT including padding.
 */
static unsigned a2_calc_upload_length(A2_wave *w)
{
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
	  {
		A2_uploadbuffer *ub = w->upload;
		unsigned size = 0;
		while(ub)
		{
			unsigned end = ub->offset + ub->size;
			if(end > size)
				size = end;
			ub = ub->next;
		}
		return size;
	  }
	  default:
		return 0;
	}
}


A2_errors a2_WaveWrite(A2_state *st, A2_handle wave, unsigned offset,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_wave *w = a2_GetWave(st, wave);
	if(!w)
		return A2_BADWAVE;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
	  {
		int ss = a2_sample_size(fmt);
		if(!ss)
			return A2_BADFORMAT;
		size /= ss;
		if(w->flags & A2_UNPREPARED)
			return a2_add_upload_buffer(w, offset, fmt, data, size);
		else
			return a2_do_write(w, offset, fmt, data, size);
	  }
	  default:
		return A2_WRONGTYPE;
	}
}


A2_handle a2_WaveUpload(A2_state *st,
		A2_wavetypes wt, unsigned period, int flags,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_errors res;
	A2_handle h;
	A2_wave *w;
	int ss = a2_sample_size(fmt);
	if(!ss)
		return -A2_BADFORMAT;
	if((h = a2_WaveNew(st, wt, period, flags)) < 0)
		return h;
	if(!(w = a2_GetWave(st, h)))
		return A2_INTERNAL + 300; /* Wut!? We just created it...! */
	w->flags &= ~A2_UNPREPARED;	/* Shortcut! We alloc manually here. */
	if(!ss || !data || !size)
		return h;
	if((res = a2_wave_alloc(w, size / ss)) ||
			(res = a2_do_write(w, 0, fmt, data, size / ss)))
	{
		a2_Release(st, h);
		return res;
	}
	a2_wave_render_mipmaps(w);
	return h;
}


A2_errors a2_WavePrepare(A2_state *st, A2_handle wave)
{
	A2_errors res = A2_OK;
	A2_wave *w = a2_GetWave(st, wave);
	if(!w)
		return A2_BADWAVE;
	if(w->flags & A2_UNPREPARED)
	{
		res = a2_wave_alloc(w, a2_calc_upload_length(w));
		if(res == A2_OK)
			res = a2_apply_upload_buffers(w);
		w->flags &= ~A2_UNPREPARED;
	}
	a2_wave_render_mipmaps(w);
	return res;
}


static A2_handle a2_wave_upload(A2_state *st, A2_handle bank, const char *name,
		A2_wavetypes wt, unsigned period, int flags,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_errors res;
	A2_handle h = a2_WaveUpload(st, wt, period, flags | A2_LOCKED,
			fmt, data, size);
	if(h < 0)
		return h;
	if((res = a2_Export(st, bank, h, name)))
	{
		a2_Release(st, h);
		return -res;
	}
	return h;
}

#if 0
A2_handle a2_test_render(A2_state *st, A2_handle bank, const char *name,
		unsigned frames)
{
	A2_errors res;
	A2_handle h;
	int i;
	int s = 0;
	int16_t buf[SC_WPER];
	if((h = a2_WaveNew(st, A2_WMIPWAVE, SC_WPER, 0)) < 0)
		return h;
	while(s < frames)
	{
		for(i = 0; (i < SC_WPER) && (s < frames); ++i)
			buf[i] = sin(s++ * 2.0f * M_PI / 1000 +
					sin(s * .0001) * 10) * 32767.0f;
		if((res = a2_WaveWrite(st, h, s - i, A2_I16, buf,
				i * sizeof(int16_t))))
		{
			a2_Release(st, h);
			return -res;
		}
	}
	if((res = a2_WavePrepare(st, h)))
	{
		a2_Release(st, h);
		return -res;
	}
	if((res = a2_Export(st, bank, h, name)))
	{
		a2_Release(st, h);
		return -res;
	}
	return h;
}
#endif

A2_errors a2_InitWaves(A2_state *st, A2_handle bank)
{
	A2_errors res = 0;
	int i, s;
	int16_t buf[SC_WPER];

	/* "off" wave - dummy oscillator */
	res |= a2_wave_upload(st, bank, "off", A2_WOFF, 0, 0, 0, NULL, 0);

	/* 1..50% duty cycle pulse waves. ("square" is "pulse50") */
	for(i = 1; i <= 50; i += i < 10 ? 1 : 5)
	{
		char name[16];
		int s1 = (SC_WPER * i + 50) / 100;
		for(s = 0; s < s1; ++s)
			buf[s] = 32767;
		for(++s; s < SC_WPER; ++s)
			buf[s] = -32767;
		snprintf(name, sizeof(name), "pulse%d", i);
		res |= a2_wave_upload(st, bank, name, A2_WMIPWAVE, SC_WPER,
				A2_LOOPED, A2_I16, buf, sizeof(buf));
	}

	/* Sawtooth wave */
	for(s = 0; s < SC_WPER; ++s)
		buf[s] = s * 65534 / SC_WPER - 32767;
	res |= a2_wave_upload(st, bank, "saw", A2_WMIPWAVE, SC_WPER,
			A2_LOOPED, A2_I16, buf, sizeof(buf));

	/* Triangle wave */
	for(s = 0; s < SC_WPER / 2; ++s)
		buf[(5 * SC_WPER / 4 - s - 1) % SC_WPER] =
				buf[s + SC_WPER / 4] =
				s * 65534 * 2 / SC_WPER - 32767;
	res |= a2_wave_upload(st, bank, "triangle", A2_WMIPWAVE, SC_WPER,
			A2_LOOPED, A2_I16, buf, sizeof(buf));

	/* Sine wave, absolute sine, half sine and quarter sine */
	for(s = 0; s < SC_WPER; ++s)
		buf[s] = sin(s * 2.0f * M_PI / SC_WPER) * 32767.0f;
	res |= a2_wave_upload(st, bank, "sine", A2_WMIPWAVE, SC_WPER,
			A2_LOOPED, A2_I16, buf, sizeof(buf));
	for(s = SC_WPER / 2; s < SC_WPER; ++s)
		buf[s] = -buf[s];
	res |= a2_wave_upload(st, bank, "asine", A2_WMIPWAVE, SC_WPER,
			A2_LOOPED, A2_I16, buf, sizeof(buf));
	for(s = SC_WPER / 2; s < SC_WPER; ++s)
		buf[s] = 0;
	res |= a2_wave_upload(st, bank, "hsine", A2_WMIPWAVE, SC_WPER,
			A2_LOOPED, A2_I16, buf, sizeof(buf));
	for(s = 0; s < SC_WPER / 4; ++s)
		buf[s + SC_WPER / 2] = buf[s];
	res |= a2_wave_upload(st, bank, "qsine", A2_WMIPWAVE, SC_WPER,
			A2_LOOPED, A2_I16, buf, sizeof(buf));

	/* SID style noise generator - special oscillator */
	res |= (s = a2_wave_upload(st, bank, "noise", A2_WNOISE, 256,
			A2_LOOPED, 0, NULL, 0));
	if(s >= 0)
	{
		A2_wave *wave = a2_GetWave(st, s);
		if(wave)
			wave->d.noise.state = A2_NOISESEED;
	}
#if 0
	for(s = 0; s < SC_WPER; ++s)
		buf[s] = sin(s * (1 + s * .1) * .0005) * 32767;
	res |= a2_wave_upload(st, bank, "chirp", SC_WPER, A2_LOOPED,
			A2_I16, buf, sizeof(buf));
#endif
#if 0
	res |= a2_test_render(st, bank, "longsweep", 16777084);
#endif
	return res < 0 ? A2_OOMEMORY : A2_OK;
}


static RCHM_errors a2_WaveDestructor(RCHM_handleinfo *hi, void *td, RCHM_handle h)
{
	int i;
	A2_wave *w = (A2_wave *)hi->d.data;
	A2_state *st = (A2_state *)td;
	a2_InstaKillAllVoices(st);
	switch(w->type)
	{
	  case A2_WOFF:
	  case A2_WNOISE:
		break;
	  case A2_WWAVE:
		free(w->d.wave.data[0]);
		break;
	  case A2_WMIPWAVE:
		for(i = 0; i < A2_MIPLEVELS; ++i)
			free(w->d.wave.data[i]);
		break;
	}
	a2_discard_upload_buffers(w);
	free(w);
	return RCHM_OK;
}

A2_errors a2_RegisterWaveTypes(A2_state *st)
{
	RCHM_manager *m = &st->ss->hm;
	return rchm_RegisterType(m, A2_TWAVE, "wave", a2_WaveDestructor, st);
}


A2_wave *a2_GetWave(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi || (hi->typecode != A2_TWAVE))
		return NULL;
	return (A2_wave *)hi->d.data;
}
