/*
 * waves.c - Audiality 2 waveform management
 *
 * Copyright 2010-2014, 2016-2017, 2022 David Olofson <david@olofson.net>
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

/* Buffer queue for uploading via the stream API */
typedef struct A2_uploadbuffer A2_uploadbuffer;
struct A2_uploadbuffer
{
	A2_uploadbuffer		*next;
	void			*data;
	A2_sampleformats	fmt;
	unsigned		offset;
	unsigned		size;
};


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
			ww->data[i] = (float *)calloc(size, sizeof(float));
		else
			ww->data[i] = (float *)malloc(size * sizeof(float));
		if(!ww->data[i])
			return A2_OOMEMORY;
	}
	return A2_OK;
}


static void a2_fix_pad(A2_wave *w, unsigned miplevel)
{
	float *d = w->d.wave.data[miplevel];
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

static void a2_render_mipmaps(A2_wave *w)
{
	int i;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		a2_fix_pad(w, 0);
		if(w->type == A2_WMIPWAVE)
			break;
	  default:
		return;
	}
	for(i = 1; i < A2_MIPLEVELS; ++i)
	{
		int s;
		float *sd = w->d.wave.data[i - 1] + A2_WAVEPRE;
		float *d = w->d.wave.data[i] + A2_WAVEPRE;
		for(s = 0; s < w->d.wave.size[i]; ++s)
			d[s] = .5f * sd[s * 2] +
					.25f * (sd[s * 2 - 1] + sd[s * 2 + 1]);
		a2_fix_pad(w, i);
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


/* Convert and write with no normalization or other processing. */
static A2_errors a2_do_write(A2_wave *w, unsigned offset, float gain,
		A2_sampleformats fmt, const void *data, unsigned length)
{
	int s;
	int size = w->d.wave.size[0];
	float *d = w->d.wave.data[0] + A2_WAVEPRE + offset;
	if(offset + length > size)
		return A2_INDEXRANGE;
	if(gain == 1.0f)
		switch(fmt)
		{
		  case A2_I8:
			for(s = 0; s < length; ++s)
				d[s] = ((int8_t *)data)[s] * A2_ONEDIV128;
			break;
		  case A2_I16:
			for(s = 0; s < length; ++s)
				d[s] = ((int16_t *)data)[s] * A2_ONEDIV32K;
			break;
		  case A2_I24:
			for(s = 0; s < length; ++s)
				d[s] = ((int32_t *)data)[s] * A2_ONEDIV8M;
			break;
		  case A2_I32:
			for(s = 0; s < length; ++s)
				d[s] = ((int32_t *)data)[s] * A2_ONEDIV2G;
			break;
		  case A2_F32:
			for(s = 0; s < length; ++s)
				d[s] = ((float *)data)[s];
			break;
		  default:
			return A2_BADFORMAT;
		}
	else
	{
		switch(fmt)
		{
		  case A2_I8:
			gain *= A2_ONEDIV128;
			break;
		  case A2_I16:
			gain *= A2_ONEDIV32K;
			break;
		  case A2_I24:
			gain *= A2_ONEDIV8M;
			break;
		  case A2_I32:
			gain *= A2_ONEDIV2G;
			break;
		  case A2_F32:
			break;
		  default:
			return A2_BADFORMAT;
		}
		switch(fmt)
		{
		  case A2_I8:
			for(s = 0; s < length; ++s)
				d[s] = ((int8_t *)data)[s] * gain;
			break;
		  case A2_I16:
			for(s = 0; s < length; ++s)
				d[s] = ((int16_t *)data)[s] * gain;
			break;
		  case A2_I24:
			for(s = 0; s < length; ++s)
				d[s] = ((int32_t *)data)[s] * gain;
			break;
		  case A2_I32:
			for(s = 0; s < length; ++s)
				d[s] = ((int32_t *)data)[s] * gain;
			break;
		  case A2_F32:
			for(s = 0; s < length; ++s)
				d[s] = ((float *)data)[s] * gain;
			break;
		  default:
			return A2_BADFORMAT;
		}
	}
	return A2_OK;
}


/* Calculate gain factor for normalizing the specified data */
static float a2_normalize_gain(A2_sampleformats fmt, const void *data,
		unsigned length)
{
	int s;
	switch(fmt)
	{
	  case A2_I8:
	  {
		int8_t peak = 0;
		int8_t *d = (int8_t *)data;
		for(s = 0; s < length; ++s)
			if(d[s] > peak)
				peak = d[s];
			else if(-d[s] > peak)
				peak = -d[s];
		if(!peak)
			return 1.0f;
		return 127.0f / peak;
	  }
	  case A2_I16:
	  {
		int16_t peak = 0;
		int16_t *d = (int16_t *)data;
		for(s = 0; s < length; ++s)
			if(d[s] > peak)
				peak = d[s];
			else if(-d[s] > peak)
				peak = -d[s];
		if(!peak)
			return 1.0f;
		return 32767.0f / peak;
	  }
	  case A2_I24:
	  case A2_I32:
	  {
		int32_t peak = 0;
		int32_t *d = (int32_t *)data;
		for(s = 0; s < length; ++s)
			if(d[s] > peak)
				peak = d[s];
			else if(-d[s] > peak)
				peak = -d[s];
		if(!peak)
			return 1.0f;
		if(fmt == A2_I24)
			return 32767.0f * 256.0f / peak;
		else
			return 32767.0f * 65536.0f / peak;
	  }
	  case A2_F32:
	  {
		float peak = 0.0f;
		float *d = (float *)data;
		for(s = 0; s < length; ++s)
			if(d[s] > peak)
				peak = d[s];
			else if(-d[s] > peak)
				peak = -d[s];
		if(!peak)
			return 1.0f;
		return 1.0f / peak;
	  }
	  default:
		return 1.0f;	/* Internal error! */
	}
}


/* Apply A2_XFAD and/or A2_REVMIX */
static A2_errors a2_postprocess(A2_wave *w)
{
	int i;
	int size = w->d.wave.size[0];
	int sh = size / 2;
	float *d = w->d.wave.data[0] + A2_WAVEPRE;
	if(w->flags & A2_REVMIX)
	{
		/* Generate the first half */
		for(i = 0; i < sh; ++i)
			d[i] = (d[i] + d[size - i]) * 0.5f;

		/* The second half is the first half reversed! */
		for(i = 0; i < sh; ++i)
			d[size - i] = d[i];
	}
	if(w->flags & A2_XFADE)
	{
		double g = 0.0f;
		double dg = 1.0f / sh;

		/* Apply triangular window */
		for(i = 0; i < sh; ++i, g += dg)
			d[i] *= g;
		for( ; i < size; ++i, g -= dg)
			d[i] *= g;

		/* Overlap-add */
		/* FIXME: Is this correct for odd sizes? */
		for(i = 0; i < sh; ++i)
			d[i] += d[i + sh];
		for( ; i < size; ++i)
			d[i] = d[i - sh];
	}
	return A2_OK;
}


static A2_errors a2_add_upload_buffer(A2_stream *str,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_uploadbuffer *lastub = (A2_uploadbuffer *)str->streamdata;
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
	ub->offset = str->position;
	ub->size = size;
	str->position += size;
	memcpy(ub->data, data, size * ss);
	if(lastub)
	{
		while(lastub->next)
			lastub = lastub->next;
		lastub->next = ub;
	}
	else
		str->streamdata = ub;
	return A2_OK;
}


/* Discard upload buffers without applying them */
static void a2_discard_upload_buffers(A2_stream *str)
{
	A2_wave *w = (A2_wave *)str->targetobject;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		while(str->streamdata)
		{
			A2_uploadbuffer *ub = (A2_uploadbuffer *)str->streamdata;
			str->streamdata = ub->next;
			free(ub->data);
			free(ub);
		}
		return;
	  default:
		return;
	}
}


/* Calculate the gain for normalizing all uploaded buffers */
static float a2_calc_upload_gain(A2_stream *str)
{
	A2_uploadbuffer *ub = (A2_uploadbuffer *)str->streamdata;
	float gain = 1000.0f;
	while(ub)
	{
		float bg = a2_normalize_gain(ub->fmt, ub->data, ub->size);
		if(bg < gain)
			gain = bg;
		ub = ub->next;
	}
	return gain;
}

/* Apply and discard upload buffers */
static A2_errors a2_apply_upload_buffers(A2_stream *str)
{
	A2_wave *w = (A2_wave *)str->targetobject;
	float gain;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		if(w->flags & A2_NORMALIZE)
			gain = a2_calc_upload_gain(str);
		else
			gain = 1.0f;
		while(str->streamdata)
		{
			A2_errors res;
			A2_uploadbuffer *ub = (A2_uploadbuffer *)str->streamdata;
			if((res = a2_do_write(w, ub->offset, gain,
					ub->fmt, ub->data, ub->size)))
			{
				a2_discard_upload_buffers(str);
				return res;
			}
			str->streamdata = ub->next;
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
static unsigned a2_calc_upload_length(A2_stream *str)
{
	A2_wave *w = (A2_wave *)str->targetobject;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
	  {
		A2_uploadbuffer *ub = (A2_uploadbuffer *)str->streamdata;
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


static A2_errors a2_wave_stream_write(A2_stream *str,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_wave *w = (A2_wave *)str->targetobject;
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
			return a2_add_upload_buffer(str, fmt, data, size);
		else
		{
			A2_errors res = a2_do_write(w, str->position, 1.0f,
					fmt, data, size);
			if(res)
				return res;
			str->position += size;
		}
	  }
	  default:
		return A2_WRONGTYPE;
	}
}


static A2_errors a2_wave_stream_flush(A2_stream *str)
{
	A2_wave *w = (A2_wave *)str->targetobject;
	A2_errors res = A2_OK;
	if(w->flags & A2_UNPREPARED)
	{
		res = a2_wave_alloc(w, a2_calc_upload_length(str));
		if(res == A2_OK)
			res = a2_apply_upload_buffers(str);
		a2_postprocess(w);
		w->flags &= ~A2_UNPREPARED;
	}
	a2_render_mipmaps(w);
	return res;
}



/* OpenStream() method for A2_TWAVE objects */
static A2_errors a2_wave_stream_open(A2_stream *str, A2_handle h)
{
	str->Write = a2_wave_stream_write;
	str->Flush = a2_wave_stream_flush;	/* Also used for a2_Close() */
	return A2_OK;
}


static A2_handle a2_upload_export(A2_interface *i, A2_handle bank,
		const char *name,
		A2_wavetypes wt, unsigned period, int flags,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_errors res;
	A2_handle h;
	DBG(A2_LOG_DBG(i, "a2_upload_export('%s')", name);)
	h = a2_UploadWave(i, wt, period, flags | A2_LOCKED,
			fmt, data, size);
	if(h < 0)
		return h;
	if((res = a2_Export(i, bank, h, name)))
	{
		a2_Release(i, h);
		return -res;
	}
	return h;
}


A2_handle a2_UploadWave(A2_interface *i,
		A2_wavetypes wt, unsigned period, int flags,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_errors res;
	A2_handle h;
	A2_wave *w;
	float gain;
	int ss = a2_sample_size(fmt);
	if(!ss)
		return -A2_BADFORMAT;
	size /= ss;
	if((h = a2_NewWave(i, wt, period, flags)) < 0)
		return h;
	if(!(w = a2_GetWave(i, h)))
		return A2_INTERNAL + 300; /* Wut!? We just created it...! */
	w->flags &= ~A2_UNPREPARED;	/* Shortcut! We alloc manually here. */
	if(!ss || !data || !size)
		return h;
	if(w->flags & A2_NORMALIZE)
		gain = a2_normalize_gain(fmt, data, size);
	else
		gain = 1.0f;
	if((res = a2_wave_alloc(w, size)) ||
			(res = a2_do_write(w, 0, gain, fmt, data, size)))
	{
		a2_Release(i, h);
		return res;
	}
	a2_postprocess(w);
	a2_render_mipmaps(w);
#if 1
	{
		float min = 0;
		float max = 0;
		for(int p = 0; p < w->d.wave.size[0]; ++p) {
			float s = w->d.wave.data[0][p];
			if(s < min)
				min = s;
			if(s > max)
				max = s;
		}
		printf("a2_UploadWave(): handle: %u, length: %d, min: %f, max:%f\n",
				h, w->d.wave.size[0], min, max);
	}
#endif
	return h;
}


A2_handle a2_NewWave(A2_interface *i, A2_wavetypes wt, unsigned period,
		int flags)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
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
	  default:
	  	return -A2_EXPWAVETYPE;
	}
	h = rchm_NewEx(&st->ss->hm, w, A2_TWAVE, flags | A2_APIOWNED, 1);
	if(h < 0)
	{
		free(w);
		return -h;
	}
	DBG(A2_LOG_DBG(i, "New wave %p %d", w, h);)
	return h;
}


/* TODO: Do this in F32, since that's now the native wave format! */
A2_errors a2_InitWaves(A2_interface *i, A2_handle bank)
{
	int j, s, h;
	float buf[A2_WAVEPERIOD];

	/* "off" wave - dummy oscillator */
	h = a2_upload_export(i, bank, "off", A2_WOFF, 0, 0, 0, NULL, 0);
	if(h < 0)
		return -h;

	/* 1..50% duty cycle pulse waves. ("square" is "pulse50") */
	for(j = 1; j <= 50; j += j < 10 ? 1 : 5)
	{
		char name[16];
		int s1 = (A2_WAVEPERIOD * j + 50) / 100;
		for(s = 0; s < s1; ++s)
			buf[s] = 1.0f;
		for(++s; s < A2_WAVEPERIOD; ++s)
			buf[s] = -1.0f;
		snprintf(name, sizeof(name), "pulse%d", j);
		h = a2_upload_export(i, bank, name, A2_WMIPWAVE, A2_WAVEPERIOD,
				A2_LOOPED, A2_F32, buf, sizeof(buf));
		if(h < 0)
			return -h;
	}

	/* Sawtooth wave */
	for(s = 0; s < A2_WAVEPERIOD; ++s)
		buf[s] = s * 2.0f / A2_WAVEPERIOD - 1.0f;
	h = a2_upload_export(i, bank, "saw", A2_WMIPWAVE, A2_WAVEPERIOD,
			A2_LOOPED, A2_F32, buf, sizeof(buf));
	if(h < 0)
		return -h;

	/* Triangle wave */
	for(s = 0; s < A2_WAVEPERIOD / 2; ++s)
		buf[(5 * A2_WAVEPERIOD / 4 - s - 1) % A2_WAVEPERIOD] =
				buf[s + A2_WAVEPERIOD / 4] =
				s * 4.0f / A2_WAVEPERIOD - 1.0f;
	h = a2_upload_export(i, bank, "triangle", A2_WMIPWAVE, A2_WAVEPERIOD,
			A2_LOOPED, A2_F32, buf, sizeof(buf));
	if(h < 0)
		return -h;

	/* Sine wave, absolute sine, half sine and quarter sine */
	for(s = 0; s < A2_WAVEPERIOD; ++s)
		buf[s] = sin(s * 2.0f * M_PI / A2_WAVEPERIOD);
	h = a2_upload_export(i, bank, "sine", A2_WMIPWAVE, A2_WAVEPERIOD,
			A2_LOOPED, A2_F32, buf, sizeof(buf));
	if(h < 0)
		return -h;

	for(s = A2_WAVEPERIOD / 2; s < A2_WAVEPERIOD; ++s)
		buf[s] = -buf[s];
	h = a2_upload_export(i, bank, "asine", A2_WMIPWAVE, A2_WAVEPERIOD,
			A2_LOOPED, A2_F32, buf, sizeof(buf));
	if(h < 0)
		return -h;

	for(s = A2_WAVEPERIOD / 2; s < A2_WAVEPERIOD; ++s)
		buf[s] = 0.0f;
	h = a2_upload_export(i, bank, "hsine", A2_WMIPWAVE, A2_WAVEPERIOD,
			A2_LOOPED, A2_F32, buf, sizeof(buf));
	if(h < 0)
		return -h;

	for(s = 0; s < A2_WAVEPERIOD / 4; ++s)
		buf[s + A2_WAVEPERIOD / 2] = buf[s];
	h = a2_upload_export(i, bank, "qsine", A2_WMIPWAVE, A2_WAVEPERIOD,
			A2_LOOPED, A2_F32, buf, sizeof(buf));
	if(h < 0)
		return -h;

	/* SID style noise generator - special oscillator */
	h = a2_upload_export(i, bank, "noise", A2_WNOISE, 256,
			A2_LOOPED, 0, NULL, 0);
	if(h < 0)
		return -h;
	return A2_OK;
}


static void a2_free_wave_cb(A2_state *st, void *userdata)
{
	free(userdata);
}

/* Stop any oscillators using 'w', and ensure that 'w' is freed eventually. */
static void a2_discard_wave(A2_state *st, A2_wave *w)
{
	a2_LockAllStates(st);
	w->d.wave.size[0] = 0;
	a2_UnlockAllStates(st);
	a2_WhenAllHaveProcessed(st, a2_free_wave_cb, w);
}


static RCHM_errors a2_wave_destructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	int i;
	A2_wave *w = (A2_wave *)hi->d.data;
	A2_state *st = ((A2_typeinfo *)ti)->state;
	if(hi->userbits & A2_LOCKED)
		return RCHM_REFUSE;
	switch(w->type)
	{
	  case A2_WOFF:
	  case A2_WNOISE:
		free(w);
		break;
	  case A2_WWAVE:
	  	a2_discard_wave(st, w);
		free(w->d.wave.data[0]);
		break;
	  case A2_WMIPWAVE:
	  	a2_discard_wave(st, w);
		for(i = 0; i < A2_MIPLEVELS; ++i)
			free(w->d.wave.data[i]);
		break;
	}
	return RCHM_OK;
}

A2_errors a2_RegisterWaveTypes(A2_state *st)
{
	return a2_RegisterType(st, A2_TWAVE, "wave", a2_wave_destructor,
			a2_wave_stream_open);
}


A2_wave *a2_GetWave(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi || (hi->typecode != A2_TWAVE))
		return NULL;
#ifdef DEBUG
	/* Should be impossible, as the wave destructor will never refuse! */
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return NULL;
#endif
	return (A2_wave *)hi->d.data;
}
