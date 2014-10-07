/*
 * properties.c - Audiality 2 Object property interface
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "dsp.h"
#include "internals.h"
#include "compiler.h"


A2_errors a2_GetProperty(A2_state *st, A2_handle h, A2_properties p, int *v)
{
	switch(p)
	{
	  case A2_PSIZE:
	  	*v = a2_Size(st, h);
	  	return A2_OK;
	  case A2_PPOSITION:
	  	*v = a2_GetPosition(st, h);
	  	return A2_OK;
	  case A2_PAVAILABLE:
	  	*v = a2_Available(st, h);
	  	return A2_OK;
	  case A2_PSPACE:
	  	*v = a2_Space(st, h);
	  	return A2_OK;

	  case A2_PSAMPLERATE:
		*v = st->config->samplerate;
	  	return A2_OK;
	  case A2_PBUFFER:
		*v = st->config->buffer;
	  	return A2_OK;
	  case A2_PCHANNELS:
		*v = st->config->channels;
	  	return A2_OK;
	  case A2_PACTIVEVOICES:
		*v = st->activevoices;
	  	return A2_OK;
	  case A2_PFREEVOICES:
		*v = st->totalvoices - st->activevoices;
	  	return A2_OK;
	  case A2_PTOTALVOICES:
		*v = st->totalvoices;
	  	return A2_OK;
	  case A2_PCPULOADAVG:
		*v = st->cpuloadavg;
		st->statreset = 1;
	  	return A2_OK;
	  case A2_PCPULOADMAX:
		*v = st->cpuloadmax;
	  	return A2_OK;
	  case A2_PCPUTIMEAVG:
		*v = st->cputimeavg;
		st->statreset = 1;
	  	return A2_OK;
	  case A2_PCPUTIMEMAX:
		*v = st->cputimemax;
	  	return A2_OK;
	  case A2_PINSTRUCTIONS:
		*v = st->instructions;
	  	return A2_OK;
	  case A2_PEXPORTALL:
		*v = (st->config->flags & A2_EXPORTALL) == A2_EXPORTALL;
	  	return A2_OK;
	  case A2_PTABSIZE:
		*v = st->ss->tabsize;
	  	return A2_OK;
	  case A2_POFFLINEBUFFER:
		*v = st->ss->offlinebuffer;
	  	return A2_OK;
	  case A2_PSILENCELEVEL:
		*v = st->ss->silencelevel;
	  	return A2_OK;
	  case A2_PSILENCEWINDOW:
		*v = st->ss->silencewindow;
	  	return A2_OK;
	  case A2_PSILENCEGRACE:
		*v = st->ss->silencegrace;
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
	  default:
		return A2_NOTFOUND;
	}
}


A2_errors a2_SetProperty(A2_state *st, A2_handle h, A2_properties p, int v)
{
	switch(p)
	{
	  case A2_PSIZE:
	  case A2_PAVAILABLE:
	  case A2_PSPACE:
		return A2_READONLY;
	  case A2_PPOSITION:
	  	return a2_SetPosition(st, h, v);

	  case A2_PCPULOADAVG:
	  case A2_PCPUTIMEAVG:
		st->statreset = 1;
		return A2_OK;
	  case A2_PCPULOADMAX:
		st->cpuloadmax = v;
		return A2_OK;
	  case A2_PCPUTIMEMAX:
		st->cputimemax = v;
		return A2_OK;
	  case A2_PINSTRUCTIONS:
		st->instructions = v;
		return A2_OK;
	  case A2_PEXPORTALL:
		if(v)
			st->config->flags |= A2_EXPORTALL;
		else
			st->config->flags &= ~A2_EXPORTALL;
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
	  default:
		return A2_NOTFOUND;
	}
}


A2_errors a2_SetProperties(A2_state *st, A2_handle h, A2_property *props)
{
	int p;
	for(p = 0; props[p].property; ++p)
	{
		A2_errors res = a2_SetProperty(st, h, props[p].property,
				props[p].value);
		if(res)
			return res;
	}
	return A2_OK;
}
