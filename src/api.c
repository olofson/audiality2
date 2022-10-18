/*
 * api.c - Audiality 2 asynchronous API implementation
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
#include "xinsert.h"
#include "pitch.h"


/*---------------------------------------------------------
	Global API resource management
---------------------------------------------------------*/

static A2_atomic a2_api_users = 0;
static A2_atomic a2_api_up = 0;
static A2_errors a2_api_error = A2_OK;


A2_errors a2_add_api_user(void)
{
	if(a2_AtomicAdd(&a2_api_users, 1) == 0)
	{
		A2_errors e;
		/* We could arrive here right when the API is being closed! */
		while(a2_AtomicAdd(&a2_api_up, 0))
			a2_Yield();
		a2_api_error = A2_OK;
		if((e = a2_time_open()) ||
				(e = a2_drivers_open()) ||
				(e = a2_units_open()) ||
				(e = a2_pitch_open()))
		{
			a2_api_error = e;
			a2_AtomicAdd(&a2_api_users, -1);
			return e;
		}
		a2_AtomicAdd(&a2_api_up, 1);
	}
	else
	{
		/* Someone beat us to it. Wait until the API is actually up! */
		while(!a2_AtomicAdd(&a2_api_up, 0))
		{
			if(a2_api_error)
			{
				/*
				 * Oh... The thread opening the API failed.
				 * We're not likely going to succeed either,
				 * so we return the same error code.
				 */
				a2_AtomicAdd(&a2_api_users, -1);
				return a2_api_error;
			}
			a2_Yield();
		}
	}
	return A2_OK;
}


void a2_remove_api_user(void)
{
	int users = a2_AtomicAdd(&a2_api_users, -1);
	if(users == 1)
	{
		/*
		 * If someone tries to reopen now, a2_add_api_user() will wait
		 * until we're done closing, before opening again.
		 */
		a2_pitch_close();
		a2_units_close();
		a2_drivers_close();
		a2_time_close();
		a2_AtomicAdd(&a2_api_up, -1);
	}
	else if(!users)
	{
		a2_AtomicAdd(&a2_api_users, 1);
		A2_LOG_INT("a2_remove_api_user() called while a2_api_users == "
				"0!");
	}
}


/*---------------------------------------------------------
	Handle management
---------------------------------------------------------*/

A2_handle a2_RootVoice(A2_interface *i)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return st->rootvoice;
}


A2_otypes a2_TypeOf(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return -A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return -A2_DEADHANDLE;
	return (A2_otypes)hi->typecode;
}


const char *a2_TypeName(A2_interface *i, A2_otypes type)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return rchm_TypeName(&st->ss->hm, type);
}


double a2_Value(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return 0.0f;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return 0.0f;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TCONSTANT:
		return ((A2_constant *)hi->d.data)->value;
	  case A2_TBANK:
	  case A2_TWAVE:
	  case A2_TPROGRAM:
	  case A2_TSTRING:
	  case A2_TSTREAM:
		return a2_Size(i, handle);
	  case A2_TUNIT:
	  case A2_TXICLIENT:
	  case A2_TDETACHED:
	  case A2_TNEWVOICE:
	  case A2_TVOICE:
		break;
	}
	return 0.0f;
}


const char *a2_String(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi;
	char *sb = st->ss->strbuf;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return NULL;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
	  {
		A2_bank *b = (A2_bank *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<bank \"%s\" %p>", b->name, b);
		return sb;
	  }
	  case A2_TWAVE:
	  {
		A2_wave *w = (A2_wave *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<wave %p>", w);
		return sb;
	  }
	  case A2_TUNIT:
	  {
		const A2_unitdesc *ud = a2_GetUnitDescriptor(i, handle);
		snprintf(sb, A2_TMPSTRINGSIZE, "<unit '%s' %p>", ud->name, ud);
		return sb;
	  }
	  case A2_TPROGRAM:
	  {
		A2_program *p = (A2_program *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<program %p>", p);
		return sb;
	  }
	  case A2_TCONSTANT:
	  {
		A2_constant *c = (A2_constant *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<constant value %f>",
				c->value);
		return sb;
	  }
	  case A2_TSTRING:
		return ((A2_string *)hi->d.data)->buffer;
	  case A2_TSTREAM:
	  {
		A2_stream *p = (A2_stream *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<stream %p>", p);
		return sb;
	  }
	  case A2_TXICLIENT:
	  {
		snprintf(sb, A2_TMPSTRINGSIZE, "<xinsert client %p>",
				hi->d.data);
		return sb;
	  }
	  case A2_TDETACHED:
		snprintf(sb, A2_TMPSTRINGSIZE, "<detached handle %d>", handle);
		return sb;
	  case A2_TNEWVOICE:
	  {
		snprintf(sb, A2_TMPSTRINGSIZE, "<new voice>");
		return sb;
	  }
	  case A2_TVOICE:
	  {
		A2_voice *v = (A2_voice *)hi->d.data;
		if(!v)
			return "<detached voice handle>";
		snprintf(sb, A2_TMPSTRINGSIZE, "<voice %p>", v);
		return sb;
	  }
	}
	return "<object of unknown type>";
}


const char *a2_Name(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return NULL;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
		return ((A2_bank *)hi->d.data)->name;
	  case A2_TUNIT:
	  {
		const A2_unitdesc *ud = a2_GetUnitDescriptor(i, handle);
		if(!ud)
			return NULL;
		return ud->name;
	  }
	  case A2_TWAVE:
	  case A2_TPROGRAM:
	  case A2_TCONSTANT:
	  case A2_TSTRING:
	  case A2_TSTREAM:
	  case A2_TXICLIENT:
	  case A2_TDETACHED:
	  case A2_TNEWVOICE:
	  case A2_TVOICE:
		return NULL;
	}
	return NULL;
}


int a2_Size(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return -A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return -A2_DEADHANDLE;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
		/* NOTE: Exports only! Private symbols are excluded here. */
		return ((A2_bank *)hi->d.data)->exports.nitems;
	  case A2_TWAVE:
	  {
		A2_wave *w = (A2_wave *)hi->d.data;
		switch(w->type)
		{
		  case A2_WOFF:
		  case A2_WNOISE:
			return -A2_NOTIMPLEMENTED;
		  case A2_WWAVE:
		  case A2_WMIPWAVE:
			return w->d.wave.size[0];
		}
		return -(A2_INTERNAL + 30);
	  }
	  case A2_TSTRING:
		return ((A2_string *)hi->d.data)->length;
	  case A2_TPROGRAM:
	  {
	  	/* Calculate total code size ("words") of program */
		A2_program *p = (A2_program *)hi->d.data;
		int size = 0;
		int j;
		for(j = 0; j < p->nfuncs; ++j)
			size += p->funcs[j].size;
		return size;
	  }
	  case A2_TSTREAM:
	  {
		A2_stream *str;
		A2_errors res = a2_GetStream(st, handle, &str);
		if(res)
			return -res;
		if(str->Size)
			return str->Size(str);
		else
			return str->size;
	  }
	  case A2_TUNIT:
	  case A2_TXICLIENT:
	  case A2_TDETACHED:
	  case A2_TNEWVOICE:
	  case A2_TVOICE:
	  case A2_TCONSTANT:
		return -A2_NOTIMPLEMENTED;
	}
	return -(A2_INTERNAL + 31);
}


A2_errors a2_Retain(A2_interface *i, A2_handle handle)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return (A2_errors)rchm_Retain(&st->ss->hm, handle);
}


/*---------------------------------------------------------
	Utilities
---------------------------------------------------------*/

float a2_Rand(A2_interface *i, float max)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	return a2_Random(&st->noisestate) * max;
}


/*---------------------------------------------------------
	Handle types for API objects
---------------------------------------------------------*/

static RCHM_errors a2_VoiceDestructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	return RCHM_REFUSE;
}

A2_errors a2_RegisterAPITypes(A2_state *st)
{
	A2_errors res = a2_RegisterType(st, A2_TNEWVOICE, "newvoice",
			a2_VoiceDestructor, NULL);
	if(!res)
		res = a2_RegisterType(st, A2_TVOICE, "voice",
			a2_VoiceDestructor, NULL);
	if(!res)
		res = a2_RegisterType(st, A2_TDETACHED, "detached",
			NULL, NULL);
	return res;
}
