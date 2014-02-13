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


int a2_GetProperty(A2_state *st, A2_handle h, A2_properties p)
{
	int res;
#if 0
/*TODO:*/
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(hi)
	{
		A2_typeinfo *ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm,
				hi->typecode);
		
	}
#endif
	switch(p)
	{
	  case A2_PSIZE:
	  	return a2_Size(st, h);
	  case A2_PPOSITION:
	  	return a2_GetPosition(st, h);
	  case A2_PAVAILABLE:
	  	return a2_Available(st, h);
	  case A2_PSPACE:
	  	return a2_Space(st, h);

	  case A2_PSAMPLERATE:
		return st->config->samplerate;
	  case A2_PBUFFER:
		return st->config->buffer;
	  case A2_PCHANNELS:
		return st->config->channels;
	  case A2_PACTIVEVOICES:
		return st->activevoices;
	  case A2_PFREEVOICES:
		return st->totalvoices - st->activevoices;
	  case A2_PTOTALVOICES:
		return st->totalvoices;
	  case A2_PCPULOADAVG:
		res = st->cpuloadavg;
		st->statreset = 1;
		return res;
	  case A2_PCPULOADMAX:
		return st->cpuloadmax;
	  case A2_PCPUTIMEAVG:
		res = st->cputimeavg;
		st->statreset = 1;
		return res;
	  case A2_PCPUTIMEMAX:
		return st->cputimemax;
	  case A2_PINSTRUCTIONS:
		return st->instructions;
	  case A2_PEXPORTALL:
		return st->ss->c->exportall;
	  case A2_PTABSIZE:
		return st->ss->c->tabsize;
	  case A2_POFFLINEBUFFER:
		return st->ss->offlinebuffer;
	  case A2_PSILENCELEVEL:
		return st->ss->silencelevel;
	  case A2_PSILENCEWINDOW:
		return st->ss->silencewindow;
	  case A2_PSILENCEGRACE:
		return st->ss->silencegrace;
	/*
	 * FIXME:
	 *	This might be confusing: These two are actually returning RNG
	 * 	*states*, as opposed to the initial seeds that were once set!
	 */
	  case A2_PRANDSEED:
		return st->randstate;
	  case A2_PNOISESEED:
		return st->noisestate;
	  default:
		return 0;
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
		st->ss->c->exportall = v;
		return A2_OK;
	  case A2_PTABSIZE:
		if(v < 1)
			v = 8;
		st->ss->c->tabsize = v;
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
