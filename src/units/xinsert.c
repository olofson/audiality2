/*
 * xinsert.c - Audiality 2 External Insert unit
 *
 * Copyright (C) 2012 David Olofson <david@olofson.net>
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

#include "xinsert.h"
#include "internals.h"
#include <string.h>
#include <stdio.h>


static inline void run_callback(A2_unit *u, unsigned offset, unsigned frames,
		int32_t **bufs)
{
	A2_xinsert *insert = (A2_xinsert *)u;
	int32_t *bufp[A2_MAXCHANNELS];
	int i;
	for(i = 0; i < u->ninputs; ++i)
		bufp[i] = bufs[i] + offset;
	insert->callback(bufp, u->ninputs, frames, insert->userdata);
}

static void a2xi_ProcessTap(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_xinsert *insert = (A2_xinsert *)u;
	int i;
	if(insert->callback)
		run_callback(u, offset, frames, u->inputs);
	for(i = 0; i < u->ninputs; ++i)
		if(u->inputs[i] != u->outputs[i])
			memcpy(u->outputs[i] + offset,
					u->inputs[i] + offset,
					frames * sizeof(int));
}

static void a2xi_ProcessTapAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_xinsert *insert = (A2_xinsert *)u;
	int i;
	if(insert->callback)
		run_callback(u, offset, frames, u->inputs);
	for(i = 0; i < u->ninputs; ++i)
	{
		int s;
		int32_t *in = u->inputs[i];
		int32_t *out = u->outputs[i];
		for(s = offset; s < offset + frames; ++s)
			out[s] += in[s];
	}
}

static void a2xi_ProcessInsert(A2_unit *u, unsigned offset, unsigned frames)
{
	int32_t bufs[A2_MAXCHANNELS][A2_MAXFRAG];
	int32_t *bufp[A2_MAXCHANNELS];
	int i;
	/*
	 * The callback is going to overwrite the buffers (inplace processing
	 * only), so we need to use intermediate buffers for inputs that don't
	 * share buffers with the corresponding outputs!
	 */
	for(i = 0; i < u->ninputs; ++i)
		if(u->inputs[i] == u->outputs[i])
			bufp[i] = u->outputs[i];	/* Inplace! */
		else
			bufp[i] = bufs[i];	/* Intermediate buffer! */
	run_callback(u, offset, frames, bufp);
	for(i = 0; i < u->ninputs; ++i)
		if(bufp[i] != u->outputs[i])
		{
			/* Copy output from intermediate buffers as needed! */
			int s;
			int32_t *in = u->inputs[i];
			int32_t *out = u->outputs[i];
			for(s = offset; s < offset + frames; ++s)
				out[s] = in[s];
		}
}

static void a2xi_ProcessInsertAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	int32_t bufs[A2_MAXCHANNELS][A2_MAXFRAG];
	int32_t *bufp[A2_MAXCHANNELS];
	int i;
	/*
	 * The callback is going to overwrite the buffers (inplace processing
	 * only), so in this case, we need to use intermediate buffers for ALL
	 * inputs, so we can mix the callback's output into whatever buffers
	 * we've been pointed at.
	 */
	for(i = 0; i < u->ninputs; ++i)
		bufp[i] = bufs[i];
	run_callback(u, offset, frames, bufp);
	for(i = 0; i < u->ninputs; ++i)
	{
		int s;
		int32_t *in = bufs[i];
		int32_t *out = u->outputs[i];
		for(s = offset; s < offset + frames; ++s)
			out[s] += in[s];
	}
}


static A2_errors a2xi_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	A2_xinsert *insert = (A2_xinsert *)u;

	if(u->ninputs != u->noutputs)
		return A2_IODONTMATCH;

	/* Initialize private fields */
	insert->flags = flags;
	insert->callback = NULL;

	/* Install Process callback */
	if(flags & A2_PROCADD)
		u->Process = a2xi_ProcessTapAdd;
	else
		u->Process = a2xi_ProcessTap;

	return A2_OK;
}


static void a2xi_Deinitialize(A2_unit *u, A2_state *st)
{
	A2_xinsert *insert = (A2_xinsert *)u;
	if(insert->callback)
		insert->callback(NULL, 0, 0, insert->userdata);
}


static const A2_crdesc regs[] =
{
	{ NULL, NULL }
};


const A2_unitdesc a2_xinsert_unitdesc =
{
	"xinsert",		/* name */

	regs,			/* registers */

	1, A2_MAXCHANNELS,	/* [min,max]inputs */
	1, A2_MAXCHANNELS,	/* [min,max]outputs */

	sizeof(A2_xinsert),	/* instancesize */
	a2xi_Initialize,	/* Initialize */
	a2xi_Deinitialize	/* Deinitialize */
};


/*---------------------------------------------------------
	External insert API
---------------------------------------------------------*/

static A2_errors a2_xinsert_locked(A2_state *st, A2_voice *v,
		A2_xinsert_cb callback, void *userdata, int insert_callback)
{
	A2_unit *u;
	A2_xinsert *insert;

	/* Find first 'xinsert' unit */
	if(!(u = v->units))
		return A2_EXPUNIT;	/* Voice has no units! --> */
	while(u->descriptor != &a2_xinsert_unitdesc)
		if(!(u = u->next))
			return A2_NOXINSERT; /* No 'xinsert' unit found! --> */

	/* Install callback! */
	insert = (A2_xinsert *)u;
	if(insert->callback)
		insert->callback(NULL, 0, 0, insert->userdata);
	insert->callback = callback;
	insert->userdata = userdata;

	/* Select a suitable unit Process callback */
	if(insert_callback && callback)
	{
		if(insert->flags & A2_PROCADD)
			u->Process = a2xi_ProcessInsertAdd;
		else
			u->Process = a2xi_ProcessInsert;
	}
	else
	{
		/* NOTE: We use these for the "no callback" state as well! */
		if(insert->flags & A2_PROCADD)
			u->Process = a2xi_ProcessTapAdd;
		else
			u->Process = a2xi_ProcessTap;
	}
	return A2_OK;
}


A2_errors a2_Tap(A2_state *st, A2_handle voice, A2_xinsert_cb callback,
		void *userdata)
{
	A2_errors res;
	A2_voice *v = a2_GetVoice(st, voice);
	if(!v)
		return A2_BADVOICE;
	st->audio->Lock(st->audio);
	res = a2_xinsert_locked(st, v, callback, userdata, 0);
	st->audio->Unlock(st->audio);
	return res;
}

A2_errors a2_Insert(A2_state *st, A2_handle voice, A2_xinsert_cb callback,
		void *userdata)
{
	A2_errors res;
	A2_voice *v = a2_GetVoice(st, voice);
	if(!v)
		return A2_BADVOICE;
	st->audio->Lock(st->audio);
	res = a2_xinsert_locked(st, v, callback, userdata, 1);
	st->audio->Unlock(st->audio);
	return res;
}
