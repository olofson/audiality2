/*
 * api.c - Audiality 2 asynchronous API implementation
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
#include "xinsert.h"


/*---------------------------------------------------------
	Global API resource management
---------------------------------------------------------*/

/* FIXME: This should be atomic. */
static A2_atomic a2_api_users = 0;


void a2_add_api_user(void)
{
	if(!a2_api_users)
	{
		a2_time_open();
		a2_drivers_open();
	}
	++a2_api_users;
}


void a2_remove_api_user(void)
{
	if(!a2_api_users)
	{
		fprintf(stderr, "Audiality 2 INTERNAL ERROR: "
				"a2_remove_api_user() called while "
				"a2_api_users = 0!\n");
		return;
	}
	--a2_api_users;
	if(!a2_api_users)
	{
		a2_drivers_close();
		a2_time_close();
	}
}


/*---------------------------------------------------------
	Handle management
---------------------------------------------------------*/

A2_handle a2_RootVoice(A2_state *st)
{
	return st->rootvoice;
}


A2_otypes a2_TypeOf(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return -A2_INVALIDHANDLE;
	if(!hi->refcount)
		return -A2_DEADHANDLE;
	return (A2_otypes)hi->typecode;
}


const char *a2_TypeName(A2_state *st, A2_otypes type)
{
	return rchm_TypeName(&st->ss->hm, type);
}


const char *a2_String(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi;
	char *sb = st->ss->strbuf;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	if(!hi->refcount)
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
		A2_unitdesc *ud = (A2_unitdesc *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<unit '%s' %p>", ud->name, ud);
		return sb;
	  }
	  case A2_TPROGRAM:
	  {
		A2_program *p = (A2_program *)hi->d.data;
		snprintf(sb, A2_TMPSTRINGSIZE, "<program %p>", p);
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


const char *a2_Name(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return NULL;
	if(!hi->refcount)
		return NULL;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
		return ((A2_bank *)hi->d.data)->name;
	  case A2_TUNIT:
		return ((A2_unitdesc *)hi->d.data)->name;
	  case A2_TWAVE:
	  case A2_TPROGRAM:
	  case A2_TSTRING:
	  case A2_TSTREAM:
	  case A2_TXICLIENT:
	  case A2_TDETACHED:
	  case A2_TVOICE:
		return NULL;
	}
	return NULL;
}


int a2_Size(A2_state *st, A2_handle handle)
{
	RCHM_handleinfo *hi;
	if(!(hi = rchm_Get(&st->ss->hm, handle)))
		return -A2_INVALIDHANDLE;
	if(!hi->refcount)
		return -A2_DEADHANDLE;
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TBANK:
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
		int i;
		for(i = 0; i < p->nfuncs; ++i)
			size += p->funcs[i].size;
		return size;
	  }
	  case A2_TSTREAM:
	  {
		A2_stream *str = a2_GetStream(st, handle);
		if(str->Size)
			return str->Size(str);
		else
			return str->size;
	  }
	  case A2_TUNIT:
	  case A2_TXICLIENT:
	  case A2_TDETACHED:
	  case A2_TVOICE:
		return -A2_NOTIMPLEMENTED;
	}
	return -(A2_INTERNAL + 31);
}


A2_errors a2_Retain(A2_state *st, A2_handle handle)
{
	return rchm_Retain(&st->ss->hm, handle);
}


/*---------------------------------------------------------
	Async API message gateway
---------------------------------------------------------*/

A2_errors a2_OpenAPI(A2_state *st)
{
	int i;

	/* Initialize FIFOs for the API */
	float buffer = (float)st->config->buffer / st->config->samplerate;
	int nmessages = A2_MINMESSAGES + buffer * A2_TIMEMESSAGES;
	st->fromapi = sfifo_Open(nmessages * sizeof(A2_apimessage));
	st->toapi = sfifo_Open(nmessages * sizeof(A2_apimessage));
	if(!st->fromapi || !st->toapi)
	{
		fprintf(stderr, "Audiality 2: Could not open async API!\n");
		return A2_OOMEMORY;
	}

	/* Initialize event pool for internal realtime communication */
	if(st->config->eventpool >= 0)
		nmessages = st->config->eventpool;
	else
		nmessages = A2_MINEVENTS + buffer * A2_TIMEEVENTS;
	for(i = 0; i < nmessages; ++i)
	{
		A2_event *e = a2_NewEvent(st);
		if(!e)
		{
			fprintf(stderr, "Audiality 2: Could not initialize "
					"internal event pool!\n");
			return A2_OOMEMORY;
		}
		e->next = st->eventpool;
		st->eventpool = e;
	}
	EVLEAKTRACK(fprintf(stderr, "Audiality 2: Allocated %d events.\n",
			st->numevents);)
	return A2_OK;
}


void a2_CloseAPI(A2_state *st)
{
	if(st->fromapi)
	{
		sfifo_Close(st->fromapi);
		st->fromapi = NULL;
	}
	if(st->toapi)
	{
		sfifo_Close(st->toapi);
		st->toapi = NULL;
	}
	while(st->eventpool)
	{
		A2_event *e = st->eventpool;
		st->eventpool = e->next;
		st->sys->RTFree(st->sys, e);
		EVLEAKTRACK(--st->numevents;)
	}
	EVLEAKTRACK(if(st->numevents)
		fprintf(stderr, "Audiality 2: %d events leaked!\n",
				st->numevents);)
}


/*
 * Engine side message pump
 */

static inline void a2r_em_start(A2_state *st, A2_apimessage *am)
{
	A2_event *e;
	A2_voice *tv = a2_GetVoice(st, am->target);
	if(!(tv))
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_start()[1]");
		return;
	}
	if(!(e = a2_AllocEvent(st)))
	{
		a2r_Error(st, A2_OOMEMORY, "a2r_em_start()[2]");
		return;
	}
	e->b.action = am->b.action;
	/* Adjust late messages, to avoid "bad" timestamps internally! */
	if(a2_TSDiff(am->b.timestamp, st->now_frames) < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n", (st->now_frames -
				am->b.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2r_em_start()[3]");
		am->b.timestamp = st->now_frames;
	}
	e->b.timestamp = am->b.timestamp;
	e->b.a1 = am->b.a1;	/* program handle */
	e->b.a2 = am->b.a2;	/* handle for new voice */
	if(am->size >= A2_MSIZE(b.argc))
	{
		e->b.argc = am->b.argc;
		memcpy(&e->b.a, &am->b.a, sizeof(int) * am->b.argc);
	}
	else
		e->b.argc = 0;
	MSGTRACK(e->source = "a2r_em_start()";)
	a2_SendEvent(tv, e);
}

static inline void a2r_em_send(A2_state *st, A2_apimessage *am)
{
	A2_event *e;
	A2_voice *tv = a2_GetVoice(st, am->target);
	if(!(tv))
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_send()[1]");
		return;
	}
	if(!(e = a2_AllocEvent(st)))
	{
		a2r_Error(st, A2_OOMEMORY, "a2r_em_send()[2]");
		return;
	}
	e->b.action = am->b.action;
	if(a2_TSDiff(am->b.timestamp, st->now_frames) < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n", (st->now_frames -
				am->b.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2r_em_send()[3]");
		am->b.timestamp = st->now_frames;
	}
	e->b.timestamp = am->b.timestamp;
	e->b.a1 = am->b.a1;	/* entry point index */
	if(am->size >= A2_MSIZE(b.argc))
	{
		e->b.argc = am->b.argc;
		memcpy(&e->b.a, &am->b.a, sizeof(int) * am->b.argc);
	}
	else
		e->b.argc = 0;
	MSGTRACK(e->source = "a2r_em_send()";)
	a2_SendEvent(tv, e);
}

static inline void a2r_em_sendsub(A2_state *st, A2_apimessage *am)
{
	A2_voice *v = a2_GetVoice(st, am->target);
	if(!v)
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_sendsub()[1]");
		return;
	}
	if(a2_TSDiff(am->b.timestamp, st->now_frames) < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n", (st->now_frames -
				am->b.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2r_em_sendsub()[2]");
		am->b.timestamp = st->now_frames;
	}
	for(v = v->sub; v; v = v->next)
	{
		A2_event *e = a2_AllocEvent(st);
		if(!e)
		{
			a2r_Error(st, A2_OOMEMORY, "a2r_em_sendsub()[3]");
			return;
		}
		e->b.action = am->b.action;
		e->b.timestamp = am->b.timestamp;
		e->b.a1 = am->b.a1;	/* entry point index */
		if(am->size >= A2_MSIZE(b.argc))
		{
			e->b.argc = am->b.argc;
			memcpy(&e->b.a, &am->b.a, sizeof(int) * am->b.argc);
		}
		else
			e->b.argc = 0;
		MSGTRACK(e->source = "a2r_em_sendsub()";)
		a2_SendEvent(v, e);
	}
}

static inline void a2r_em_release(A2_state *st, A2_apimessage *am)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, am->target);
	if(!hi)
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_release()[1]");
		return;
	}
#if 0
	switch((A2_otypes)hi->typecode)
	{
	  case A2_TVOICE:
		/*
		 * Tell the voice (if any!?) that it's been detached.
		 * NOTE: The voice handle is already in am->target!
		 */
		if(hi->d.data)
		{
			A2_voice *v = (A2_voice *)hi->d.data;
			v->handle = -1;
			a2_VoiceDetach(v);
		}
		break;
	  case A2_TXICLIENT:
	  	/* Get the xinsert client handle */
		am->target = ;
		break;
	  default:
		a2r_Error(st, A2_WRONGTYPE, "a2r_em_release()[2]");
		return;
	}
#else
	if(hi->typecode != A2_TVOICE)
	{
		a2r_Error(st, A2_WRONGTYPE, "a2r_em_release()[2]");
		return;
	}
	/*
	 * Tell the voice (if any!?) that it's been detached.
	 * NOTE: The voice handle is already in am->target!
	 */
	if(hi->d.data)
	{
		A2_voice *v = (A2_voice *)hi->d.data;
		v->handle = -1;
		a2_VoiceDetach(v);
	}
#endif
	/* Respond back to the API: "Clear to free the handle!" */
	am->b.action = A2MT_DETACH;
	a2_writemsg(st->toapi, am, A2_MSIZE(b.action));
}

static inline void a2r_em_kill(A2_state *st, A2_apimessage *am)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, am->target);
	if(!hi)
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_kill()[1]");
		return;
	}
	if(hi->typecode != A2_TVOICE)
	{
		a2r_Error(st, A2_WRONGTYPE, "a2r_em_kill()[2]");
		return;
	}
	if(!hi->d.data)
	{
		a2r_Error(st, A2_NOOBJECT, "a2r_em_kill()[3]");
		return;
	}
	a2_VoiceKill(st, (A2_voice *)hi->d.data);
	am->b.action = A2MT_DETACH;
	a2_writemsg(st->toapi, am, A2_MSIZE(b.action));
}

static inline void a2r_em_killsub(A2_state *st, A2_apimessage *am)
{
	A2_voice *v, *sv;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, am->target);
	if(!hi)
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_killsub()[1]");
		return;
	}
	if(hi->typecode != A2_TVOICE)
	{
		a2r_Error(st, A2_WRONGTYPE, "a2r_em_killsub()[2]");
		return;
	}
	if(!hi->d.data)
	{
		a2r_Error(st, A2_NOOBJECT, "a2r_em_killsub()[3]");
		return;
	}
	v = (A2_voice *)hi->d.data;
	for(sv = v->sub; sv; sv = sv->next)
		a2_VoiceKill(st, sv);
	memset(v->sv, 0, sizeof(v->sv));
}

static inline void a2r_em_xic(A2_state *st, A2_apimessage *am)
{
	A2_event *e;
	A2_voice *tv = a2_GetVoice(st, am->target);
	if(!tv)
	{
		a2r_Error(st, A2_BADVOICE, "a2r_em_xic()[1]");
		return;
	}
	if(!(e = a2_AllocEvent(st)))
	{
		a2r_Error(st, A2_OOMEMORY, "a2r_em_xic()[2]");
		return;
	}
	memcpy(&e->b, &am->b, am->size - offsetof(A2_apimessage, b));
	if(a2_TSDiff(e->b.timestamp, st->now_frames) < 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Audiality 2: API message deliverad "
				"%f frames late!\n", (st->now_frames -
				e->b.timestamp) / 256.0f);
#endif
		a2r_Error(st, A2_LATEMESSAGE, "a2r_em_xic()[3]");
		e->b.timestamp = st->now_frames;
	}
	MSGTRACK(e->source = "a2r_em_xic()";)
	a2_SendEvent(tv, e);
}

static inline void a2r_em_eocevent(A2_state *st, A2_apimessage *am)
{
	A2_event *e = a2_AllocEvent(st);
	if(!e)
	{
		a2r_Error(st, A2_OOMEMORY, "a2r_em_eocevent()[1]");
		return;
	}
	memcpy(&e->b, &am->b, am->size - offsetof(A2_apimessage, b));
	MSGTRACK(e->source = "a2r_em_eocevent()";)
	/* FIXME: Events are queued in reverse order here... */
	e->next = st->eocevents;
	st->eocevents = e;
}

void a2r_PumpEngineMessages(A2_state *st)
{
	while(sfifo_Used(st->fromapi) >= A2_APIREADSIZE)
	{
		A2_apimessage am;
		if(sfifo_Read(st->fromapi, &am, (unsigned)A2_APIREADSIZE) < 0)
		{
			a2r_Error(st, A2_INTERNAL + 25,
					"Engine side FIFO read error");
			return;
		}
		if(am.size > A2_APIREADSIZE)
			if(sfifo_Read(st->fromapi,
					((char *)&am) + A2_APIREADSIZE,
					am.size - A2_APIREADSIZE) < 0)
			{
				a2r_Error(st, A2_INTERNAL + 26,
						"Engine side FIFO read error");
				return;
			}
		switch(am.b.action)
		{
		  case A2MT_PLAY:
		  case A2MT_START:
			a2r_em_start(st, &am);
			break;
		  case A2MT_SEND:
			a2r_em_send(st, &am);
			break;
		  case A2MT_SENDSUB:
			a2r_em_sendsub(st, &am);
			break;
		  case A2MT_RELEASE:
			a2r_em_release(st, &am);
			break;
		  case A2MT_KILL:
			a2r_em_kill(st, &am);
			break;
		  case A2MT_KILLSUB:
			a2r_em_killsub(st, &am);
			break;
		  case A2MT_ADDXIC:
		  case A2MT_REMOVEXIC:
			a2r_em_xic(st, &am);
			break;
		  case A2MT_WAHP:
			a2r_em_eocevent(st, &am);
			break;
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unknown API message "
					"%d!\n", am.b.action);
			break;
#endif
		}
	}
}


static inline void a2_detach_or_free_handle(A2_state *st, A2_handle h)
{
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, h);
	if(hi)
	{
		if(hi->refcount)
			hi->typecode = A2_TDETACHED;
		else
			rchm_Free(&st->ss->hm, h);
	}
}


/*
 * API side message pump
 */

void a2_PumpAPIMessages(A2_state *st)
{
	while(sfifo_Used(st->toapi) >= A2_APIREADSIZE)
	{
		A2_apimessage am;
		if(sfifo_Read(st->toapi, &am, (unsigned)A2_APIREADSIZE) < 0)
		{
			fprintf(stderr, "Audiality 2: API side FIFO read"
					" error! (27)\n");
			return;
		}
		if(am.size > A2_APIREADSIZE)
			if(sfifo_Read(st->toapi, (char *)&am + A2_APIREADSIZE,
					am.size - A2_APIREADSIZE) < 0)
			{
				fprintf(stderr, "Audiality 2: API side FIFO"
						" read error! (28)\n");
				return;
			}
		if(am.size < A2_MSIZE(b.argc))
			am.b.argc = 0;
		switch(am.b.action)
		{
		  case A2MT_DETACH:
			a2_detach_or_free_handle(st, am.target);
			break;
		  case A2MT_XICREMOVED:
		  {
			A2_xinsert_client *c = *(A2_xinsert_client **)&am.b.a1;
			if(c->stream)
				a2_detach_or_free_handle(st, c->stream);
			a2_detach_or_free_handle(st, c->handle);
			if(c->fifo)
				sfifo_Close(c->fifo);
		  	free(c);
			break;
		  }
		  case A2MT_ERROR:
		  {
			const char *s = *(const char **)&am.b.a2;
			fprintf(stderr, "Audiality 2: [RT] %s (%s)\n",
					a2_ErrorString(am.b.a1), s);
			break;
		  }
		  case A2MT_WAHP:
		  {
			A2_wahp_entry *we = *((A2_wahp_entry **)&am.b.a1);
			--we->count;
			if(!we->count)
			{
				/* We're last. Let's make the callback! */
				we->callback(we->state, we->userdata);
				free(we);
			}
			break;
		  }
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unknown engine message "
					"%d!\n", am.b.action);
			break;
#endif
		}
	}
}


void a2r_ProcessEOCEvents(A2_state *st, unsigned frames)
{
	/*
	 * We don't count it as a cycle unless samples were processed!
	 *
	 * NOTE: This is to make sure A2MT_WAHP works as intended. We may have
	 *       have to change this if we add other EOC events later.
	 */
	if(!frames)
		return;

	while(st->eocevents)
	{
		A2_event *e = st->eocevents;
		switch(e->b.action)
		{
		  case A2MT_WAHP:
		  {
		  	/* Just send it back as is to the API context! */
		  	A2_apimessage am;
		  	int msize = A2_MSIZE(b.a1) + sizeof(void *);
			memcpy(&am.b, &e->b,
					msize - offsetof(A2_apimessage, b));
			a2_writemsg(st->toapi, &am, msize);
			break;
		  }
#ifdef DEBUG
		  default:
			fprintf(stderr, "Audiality 2: Unexpected message "
					"%d in a2r_ProcessEOCEvents()!\n",
					e->b.action);
			break;
#endif
		}
		st->eocevents = e->next;
		a2_FreeEvent(st, e);
	}
}


A2_errors a2_WhenAllHaveProcessed(A2_state *st, A2_generic_cb cb,
		void *userdata)
{
	A2_apimessage am;
	A2_wahp_entry **d = (A2_wahp_entry **)&am.b.a1;
	A2_state *pstate = st->parent ? st->parent : st;
	A2_wahp_entry *we = (A2_wahp_entry *)malloc(sizeof(A2_wahp_entry));
	if(!we)
		return A2_OOMEMORY;
	we->state = st;
	we->callback = cb;
	we->userdata = userdata;
	we->count = 0;
	for(st = pstate; st; st = st->next)
		++we->count;
	am.b.action = A2MT_WAHP;
	/* NOTE: Overwrites a2 on platforms with 64 bit pointers! */
	*d = we;
	for(st = pstate; st; st = st->next)
		a2_writemsg(st->toapi, &am, A2_MSIZE(b.a1) - sizeof(am.b.a1) +
				sizeof(void *));
	return A2_OK;
}


/* Post error message to the API from engine context */
A2_errors a2r_Error(A2_state *st, A2_errors e, const char *info)
{
	st->last_rt_error = e;
	if(st->config->flags & A2_RTSILENT)
		return A2_OK;
	if(st->config->flags & A2_REALTIME)
	{
		A2_apimessage am;
		const char **d = (const char **)&am.b.a2;
		am.b.action = A2MT_ERROR;
		am.b.timestamp = st->now_ticks;
		am.b.a1 = e;
		/* NOTE: Overwrites a[0] on platforms with 64 bit pointers! */
		*d = info;
		return a2_writemsg(st->toapi, &am,
				A2_MSIZE(b.a2) - sizeof(am.b.a2) +
				sizeof(void *));
	}
	else
	{
		fprintf(stderr, "Audiality 2: [engine] %s (%s)\n",
				a2_ErrorString(e), info);
		return A2_OK;
	}
}


/*
 * Send a message to the API context regarding handle 'h', telling it to either
 * free it immediately (refcount == 0), or to change its type to A2_TDETACHED,
 * so it can be released later.
FIXME: This is only used in one place...
 */
void a2r_DetachHandle(A2_state *st, A2_handle h)
{
	A2_apimessage am;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, h);
	if(!hi)
		return;
	if(!hi->typecode)
		return;
	if(!st->toapi)
		return;
	/* Respond back to the API: "Clear to free the handle!" */
	am.b.action = A2MT_DETACH;
	am.target = h;
	a2_writemsg(st->toapi, &am, A2_MSIZE(b.action));
}


A2_errors a2_Release(A2_state *st, A2_handle handle)
{
	A2_errors res = -rchm_Release(&st->ss->hm, handle);
	if(res == A2_REFUSE)
	{
		/*
		 * Special hack to deal with objects that need an engine
		 * round trip for cleanup. Destructors only have access to the
		 * master state - not the actual owner state provided through
		 * 'st' here, which we need to get the correct message FIFO!
		 */
		RCHM_handleinfo *hi = rchm_Locate(&st->ss->hm, handle);
		switch((A2_otypes)hi->typecode)
		{
		  case A2_TVOICE:
		  {
			A2_apimessage am;
			am.target = handle;
			am.b.action = A2MT_RELEASE;
			a2_writemsg(st->fromapi, &am, A2_MSIZE(b.action));
			break;
		  }
		  case A2_TXICLIENT:
		  {
			A2_apimessage am;
			am.target = handle;
			am.b.action = A2MT_REMOVEXIC;
			a2_writemsg(st->fromapi, &am, A2_MSIZE(b.action));
			break;
		  }
		  case A2_TBANK:
		  case A2_TUNIT:
		  case A2_TWAVE:
		  case A2_TPROGRAM:
		  case A2_TSTRING:
		  case A2_TSTREAM:
		  case A2_TDETACHED:
			break;
		}
	}
	return res;
}


/*---------------------------------------------------------
	Utilities
---------------------------------------------------------*/

float a2_F2P(float f)
{
	return log2(f / A2_MIDDLEC);
}


float a2_Rand(A2_state *st, float max)
{
	return a2_Noise(&st->noisestate) * max / 65536.0f;
}


/*---------------------------------------------------------
	Playing and controlling
---------------------------------------------------------*/

void a2_Now(A2_state *st)
{
	unsigned nt;
	a2_poll_api(st);
	if(st->config->flags & A2_REALTIME)
	{
		unsigned nf;
		int dt;
		do {
			nf = st->now_frames;
			nt = nf + (st->config->buffer << 8);
			dt = a2_GetTicks() - st->now_ticks;
		} while(nf != st->now_guard);
		if(dt < 0)
			dt = 0;	/* Audio has been off for a looooong time... */
		nt += (int64_t)st->msdur * dt >> 8;
	}
	else
		nt = st->now_frames;
	if(a2_TSDiff(nt, st->timestamp) >= 0)
		st->timestamp = nt;
}


void a2_Wait(A2_state *st, float dt)
{
	st->timestamp += (unsigned)(st->msdur * dt / 256.0f);
}


A2_handle a2_NewGroup(A2_state *st, A2_handle parent)
{
	return a2_Starta(st, parent, a2_Get(st, A2_ROOTBANK, "a2_groupdriver"),
			0, NULL);
}


A2_handle a2_Starta(A2_state *st, A2_handle parent, A2_handle program,
		unsigned argc, int *argv)
{
	A2_errors res;
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = parent;
	am.b.action = A2MT_START;
	am.b.timestamp = st->timestamp;
	am.b.a1 = program;
	if((am.b.a2 = rchm_New(&st->ss->hm, NULL, A2_TVOICE)) < 0)
		return am.b.a2;
	if((res = a2_writemsgargs(st->fromapi, &am, argc, argv)))
		return res;
	return am.b.a2;
}


A2_errors a2_Playa(A2_state *st, A2_handle parent, A2_handle program,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = parent;
	am.b.action = A2MT_PLAY;
	am.b.timestamp = st->timestamp;
	am.b.a1 = program;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv);
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.a1));
}


A2_errors a2_Senda(A2_state *st, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if((ep < 0) || (ep >= A2_MAXEPS))
		return A2_INDEXRANGE;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_SEND;
	am.b.timestamp = st->timestamp;
	am.b.a1 = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv);
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.a1));
}


A2_errors a2_SendSuba(A2_state *st, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	A2_apimessage am;
	if((ep < 0) || (ep >= A2_MAXEPS))
		return A2_INDEXRANGE;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_SENDSUB;
	am.b.timestamp = st->timestamp;
	am.b.a1 = ep;
	if(argc)
		return a2_writemsgargs(st->fromapi, &am, argc, argv);
	else
		return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.a1));
}


A2_errors a2_Kill(A2_state *st, A2_handle voice)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_KILL;
	am.b.timestamp = st->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.timestamp));
}


A2_errors a2_KillSub(A2_state *st, A2_handle voice)
{
	A2_apimessage am;
	if(!(st->config->flags & A2_TIMESTAMP))
		a2_Now(st);
	else
		a2_poll_api(st);
	am.target = voice;
	am.b.action = A2MT_KILLSUB;
	am.b.timestamp = st->timestamp;
	return a2_writemsg(st->fromapi, &am, A2_MSIZE(b.timestamp));
}


static RCHM_errors a2_VoiceDestructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	return RCHM_REFUSE;
}

A2_errors a2_RegisterAPITypes(A2_state *st)
{
	A2_errors res = a2_RegisterType(st, A2_TVOICE, "voice",
			a2_VoiceDestructor, NULL);
	if(!res)
		res = a2_RegisterType(st, A2_TDETACHED, "detached",
			NULL, NULL);
	return res;
}
