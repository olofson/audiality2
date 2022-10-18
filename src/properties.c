/*
 * properties.c - Audiality 2 Object property interface
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "internals.h"
#include "compiler.h"


A2_errors a2_GetStateProperty(A2_interface *i, A2_properties p, int *v)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	switch(p)
	{
	  /* A2_PGENERAL */
	  case A2_PCHANNELS:
		*v = st->config->channels;
		return A2_OK;
	  case A2_PFLAGS:
		*v = st->config->flags;
		return A2_OK;

	  /* A2_PSTATE */
	  case A2_PSAMPLERATE:
		*v = st->config->samplerate;
		return A2_OK;
	  case A2_PBUFFER:
		*v = st->config->buffer;
		return A2_OK;
	  case A2_PTIMESTAMPMARGIN:
		*v = ii->tsmargin;
		return A2_OK;
	  case A2_PTABSIZE:
		*v = st->ss->tabsize;
		return A2_OK;
	  case A2_POFFLINEBUFFER:
		*v = st->ss->offlinebuffer;
		return A2_OK;
	  case A2_PSILENCELEVEL:
		/* FIXME: Maybe just fail here? Almost invariably <1.0f. */
		*v = st->ss->silencelevel;
		return A2_OK;
	  case A2_PSILENCEWINDOW:
		*v = st->ss->silencewindow;
		return A2_OK;
	  case A2_PSILENCEGRACE:
		*v = st->ss->silencegrace;
		return A2_OK;
	  case A2_PLOGLEVELS:
		*v = ii->loglevels;
		return A2_OK;

	/*
	 * FIXME:
	 *	This might be confusing: These two are actually returning RNG
	 * 	*states*, as opposed to the initial seeds that were once set!
	 */
	  case A2_PRANDSEED:
		*v = st->randstate;
		return A2_OK;
	  case A2_PNOISESEED:
		*v = st->noisestate;
		return A2_OK;

	  /* A2_PSTATISTICS */
	  case A2_PACTIVEVOICES:
		*v = st->activevoices;
		return A2_OK;
	  case A2_PACTIVEVOICESMAX:
	  {
		unsigned av = st->activevoices;
		*v = st->activevoicesmax;
		if(av > *v)
			*v = av;
		return A2_OK;
	  }
	  case A2_PFREEVOICES:
		*v = st->totalvoices - st->activevoices;
		return A2_OK;
	  case A2_PTOTALVOICES:
		*v = st->totalvoices;
		return A2_OK;
	  case A2_PCPULOADAVG:
		*v = st->cpuloadavg;
		return A2_OK;
	  case A2_PCPULOADMAX:
		*v = st->cpuloadmax;
		return A2_OK;
	  case A2_PCPUTIMEAVG:
		*v = st->cputimeavg;
		return A2_OK;
	  case A2_PCPUTIMEMAX:
		*v = st->cputimemax;
		return A2_OK;
	  case A2_PINSTRUCTIONS:
		*v = st->instructions;
		return A2_OK;
	  case A2_PAPIMESSAGES:
		*v = st->apimessages;
		return A2_OK;
	  case A2_PTSMARGINAVG:
		if(st->tssamples)
			*v = st->tsavg;
		else
			*v = 0;
		return A2_OK;
	  case A2_PTSMARGINMIN:
		if(st->tssamples)
			*v = st->tsmin;
		else
			*v = 0;
		return A2_OK;
	  case A2_PTSMARGINMAX:
		if(st->tssamples)
			*v = st->tsmax;
		else
			*v = 0;
		return A2_OK;

	  default:
		return A2_NOTFOUND;
	}
}

A2_errors a2_GetStatePropertyf(A2_interface *i, A2_properties p, float *v)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	switch(p)
	{
	  case A2_PSILENCELEVEL:
		*v = st->ss->silencelevel;
		return A2_OK;

	  /* TODO: Move anything relevant here, for those decimals! */

	  default:
	  {
		int iv;
		A2_errors res = a2_GetStateProperty(i, p, &iv);
		if (res == A2_OK)
			*v = (float)iv;
		return res;
	  }
	}
}


A2_errors a2_GetProperty(A2_interface *i, A2_handle h, A2_properties p, int *v)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, h);
	if(!hi)
		return A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return A2_DEADHANDLE;

	switch(p)
	{
	  /* A2_PGENERAL */
	  case A2_PCHANNELS:
		switch((A2_otypes)hi->typecode)
		{
		  case A2_TBANK:
		  case A2_TPROGRAM:
		  case A2_TCONSTANT:
		  case A2_TSTRING:
		  case A2_TDETACHED:
		  case A2_TNEWVOICE:
			return A2_NOTFOUND;
		  case A2_TWAVE:
			*v = 1;	/* All we support at this point */
			return A2_OK;
		  case A2_TUNIT:
		  case A2_TSTREAM:
		  case A2_TXICLIENT:
			return A2_NOTIMPLEMENTED;
		  case A2_TVOICE:
		  {
			A2_voice *voice = (A2_voice *)hi->d.data;
			if(!voice)
				return A2_NOTFOUND;
			*v = voice->noutputs;
			return A2_OK;
		  }
		}
	  case A2_PFLAGS:
		switch((A2_otypes)hi->typecode)
		{
		  case A2_TBANK:
		  case A2_TCONSTANT:
		  case A2_TSTRING:
		  case A2_TDETACHED:
		  case A2_TNEWVOICE:
			return A2_NOTFOUND;
		  case A2_TWAVE:
		  {
			A2_wave *w = (A2_wave *)hi->d.data;
			*v = w->flags;
			return A2_OK;
		  }
		  case A2_TUNIT:
		  {
			const A2_unitdesc *ud = a2_GetUnitDescriptor(i, h);
			*v = ud->flags;
			return A2_OK;
		  }
		  case A2_TPROGRAM:
		  {
			A2_program *prg = (A2_program *)hi->d.data;
			*v = prg->vflags;	/* Only flags they have... */
			return A2_OK;
		  }
		  case A2_TSTREAM:
		  {
			A2_stream *s = (A2_stream *)hi->d.data;
			*v = s->flags;
			return A2_OK;
		  }
		  case A2_TXICLIENT:
		  {
			A2_xinsert_client *xic = (A2_xinsert_client *)
					hi->d.data;
			*v = xic->flags;
			return A2_OK;
		  }
		  case A2_TVOICE:
		  {
			A2_voice *voice = (A2_voice *)hi->d.data;
			if(!voice)
				return A2_NOTFOUND;
			*v = voice->flags;
			return A2_OK;
		  }
		}
	  case A2_PREFCOUNT:
		*v = hi->refcount;
		return A2_OK;

	  case A2_PSIZE:
		*v = a2_Size(i, h);
		return A2_OK;
	  case A2_PPOSITION:
		*v = a2_GetPosition(i, h);
		return A2_OK;
	  case A2_PAVAILABLE:
		*v = a2_Available(i, h);
		return A2_OK;
	  case A2_PSPACE:
		*v = a2_Space(i, h);
		return A2_OK;

	  default:
		return A2_NOTFOUND;
	}
}


A2_errors a2_SetStateProperty(A2_interface *i, A2_properties p, int v)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	switch(p)
	{
	  /* A2_PGENERAL */
	  case A2_PCHANNELS:
	  case A2_PFLAGS:
		return A2_READONLY;

	  /* A2_PSTATE */
	  case A2_PSAMPLERATE:
	  case A2_PBUFFER:
		return A2_READONLY;
	  case A2_PTIMESTAMPMARGIN:
		ii->tsmargin = v;
		return A2_OK;
	  case A2_PTABSIZE:
		if(v < 1)
			v = 8;
		st->ss->tabsize = v;
		return A2_OK;
	  case A2_POFFLINEBUFFER:
		st->ss->offlinebuffer = v;
		return A2_OK;
	  case A2_PSILENCELEVEL:
		/* FIXME: This one's not very useful as integer now... */
		st->ss->silencelevel = v;
		return A2_OK;
	  case A2_PSILENCEWINDOW:
		st->ss->silencewindow = v;
		return A2_OK;
	  case A2_PSILENCEGRACE:
		st->ss->silencegrace = v;
		return A2_OK;
	  case A2_PRANDSEED:
		st->randstate = v;
		return A2_OK;
	  case A2_PNOISESEED:
		st->noisestate = v;
		return A2_OK;
	  case A2_PLOGLEVELS:
		ii->loglevels = v;
		return A2_OK;

	  /* A2_PSTATISTICS */
	  case A2_PACTIVEVOICES:
	  case A2_PFREEVOICES:
	  case A2_PTOTALVOICES:
		return A2_READONLY;
	  case A2_PCPULOADAVG:
	  case A2_PCPULOADMAX:
	  case A2_PCPUTIMEAVG:
	  case A2_PCPUTIMEMAX:
		st->statreset = 1;
		return A2_OK;
	  case A2_PACTIVEVOICESMAX:
		st->activevoicesmax = 0;
		return A2_OK;
	  case A2_PINSTRUCTIONS:
		st->instructions = 0;
		return A2_OK;
	  case A2_PAPIMESSAGES:
		st->apimessages = 0;
		return A2_OK;
	  case A2_PTSMARGINAVG:
	  case A2_PTSMARGINMIN:
	  case A2_PTSMARGINMAX:
		st->tsstatreset = 1;
		return A2_OK;

	  default:
		return A2_NOTFOUND;
	}
}


A2_errors a2_SetStatePropertyf(A2_interface *i, A2_properties p, float v)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	switch(p)
	{
	  case A2_PSILENCELEVEL:
		st->ss->silencelevel = v;
		return A2_OK;

	  /* TODO: Move anything relevant here, for those decimals! */

	  default:
		return a2_SetStateProperty(i, p, (int)v);
	}
}


A2_errors a2_SetProperty(A2_interface *i, A2_handle h, A2_properties p, int v)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, h);
	if(!hi)
		return A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return A2_DEADHANDLE;

	switch(p)
	{
	  /* A2_PGENERAL */
	  case A2_PCHANNELS:
	  case A2_PFLAGS:
	  case A2_PREFCOUNT:
	  case A2_PSIZE:
	  case A2_PAVAILABLE:
	  case A2_PSPACE:
		return A2_READONLY;
	  case A2_PPOSITION:
		return a2_SetPosition(i, h, v);

	  default:
		return A2_NOTFOUND;
	}
}


A2_errors a2_SetProperties(A2_interface *i, A2_handle h, A2_property *props)
{
	int p;
	for(p = 0; props[p].property; ++p)
	{
		A2_errors res = a2_SetProperty(i, h, props[p].property,
				props[p].value);
		if(res)
			return res;
	}
	return A2_OK;
}


A2_errors a2_SetStateProperties(A2_interface *i, A2_property *props)
{
	int p;
	for(p = 0; props[p].property; ++p)
	{
		A2_errors res = a2_SetStateProperty(i, props[p].property,
				props[p].value);
		if(res)
			return res;
	}
	return A2_OK;
}
