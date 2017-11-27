/*
 * core.c - Audiality 2 realtime core and scripting VM
 *
 * Copyright 2010-2017 David Olofson <david@olofson.net>
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
#include "inline.h"
#include "xinsert.h"


/*---------------------------------------------------------
	Event handling
---------------------------------------------------------*/

/*
 * Discard events (sort of) without processing them. This is to make sure we
 * don't leak objects when killing voices prematurely, including when closing
 * the engine.
 */
static inline void a2_flush_event(A2_state *st, A2_event *e, A2_handle h)
{
	switch(e->b.common.action)
	{
	  case A2MT_ADDXIC:
	  {
		/*
		 * The logic here is that we discard any incoming XICs right
		 * here, whether or not this queue belongs to a real voice or a
		 * NEWVOICE. (A handle serving as a temporary event queue.)
		 * A2MT_REMOVEXIC events can just be ignored, since if there's
		 * no voice, any XICs would already have been discarded, and if
		 * there is a voice, the xinsert unit will take care of XICs in
		 * Deinitialize().
		 */
		if(st->config->flags & A2_REALTIME)
		{
			A2_apimessage am;
			am.b.common.action = A2MT_XICREMOVED;
			am.b.common.timestamp = st->now_ticks;
			am.b.xic.client = e->b.xic.client;
			a2_writemsg(st->toapi, &am, A2_MSIZE(b.xic));
		}
		else
			free(e->b.xic.client);
		break;
	  }
	  case A2MT_RELEASE:
		if(h >= 0)
			a2r_DetachHandle(st, h);
		break;
	  default:
		break;
	}
}

void a2_FlushEventQueue(A2_state *st, A2_event **eq, A2_handle h)
{
	while(*eq)
	{
		A2_event *e = *eq;
		*eq = e->next;
		a2_flush_event(st, e, h);
		a2_FreeEvent(st, e);
	}
}


/*---------------------------------------------------------
	Voice and audio processing
---------------------------------------------------------*/

static inline A2_errors a2_VoicePush(A2_state *st, A2_voice *v, int firstreg,
		int topreg, int interrupt)
{
	A2_stackentry *se = &a2_AllocBlock(st)->stackentry;
	int saveregs = topreg - firstreg + 1;
	if(!se)
		return A2_OOMEMORY;
	se->prev = v->stack;
	v->stack = se;
	se->state = v->s.state;
	se->func = v->s.func;
	se->pc = v->s.pc;
	se->interrupt = interrupt;
	se->waketime = v->s.waketime;
	se->firstreg = firstreg;
	se->topreg = topreg;
#ifdef DEBUG
	if(saveregs > A2_MAXSAVEREGS)
	{
		a2_FreeBlock(st, se);
		A2_LOG_INT("A2S compiler bug: Too large stack frame! "
				"%d (max: %d)", saveregs, A2_MAXSAVEREGS);
		return A2_INTERNAL + 401;
	}
#endif
	memcpy(se->r, v->s.r + firstreg, sizeof(int) * saveregs);
	return A2_OK;
}


static inline int a2_VoicePop(A2_state *st, A2_voice *v)
{
	A2_stackentry *se = v->stack;
	int inter = se->interrupt;
	int saveregs = se->topreg - se->firstreg + 1;
	v->s.state = se->state;
	v->s.func = se->func;
	if(inter)
	{
		v->s.pc = se->pc;
		v->s.waketime = se->waketime;
	}
	else
		v->s.pc = se->pc + 1;
	memcpy(v->s.r + se->firstreg, se->r, sizeof(int) * saveregs);
	v->stack = se->prev;
	a2_FreeBlock(st, se);
	return inter;
}


static inline void a2_VoiceControl(A2_state *st, A2_voice *v, unsigned reg,
		unsigned start, unsigned duration)
{
	A2_cport *cp = &v->cregs[reg];
	if(cp->write)
		cp->write(cp->unit, v->s.r[reg], start & 255, duration);
}


/*
 * Instantiate, initialize and wire a unit as described by descriptor 'ud',
 * and add it at the end of the chain in voice 'v'.
 *
 * NOTE:
 *	We're not keeping a pointer to the last unit in A2_voice (voice
 *	structures are "hardwired" by the compiler anyway, so they're never
 *	rebuilt on the fly), we keep it as a temporary variable when
 *	constructing the voice, and pass it via the 'lastunit' argument
 *	instead.
 */
static inline A2_unit *a2_AddUnit(A2_state *st, const A2_structitem *si,
		A2_voice *v, A2_unit *lastunit, int32_t **scratch,
		unsigned noutputs, int32_t **outputs)
{
	DBG(A2_interface *i = &st->interfaces->interface;)
	A2_errors res;
	int minoutputs, maxoutputs, ninputs;
	A2_unit *u;
	const A2_unitdesc *ud = st->ss->units[si->kind];
	A2_unitstate *us = st->unitstate + si->kind;

	if(us->status)
	{
		/* This module failed in shared state init. We can't use it! */
		a2r_Error(st, us->status, "a2_AddUnit()[1]");
		return NULL;
	}

	if(!(u = &a2_AllocBlock(st)->unit))
	{
		a2r_Error(st, A2_OOMEMORY, "a2_AddUnit()[2]");
		return NULL;
	}

	DUMPSTRUCTRT(A2_DLOG("Wiring %s... ", ud->name);)

	/* Input wiring */
	switch(si->p.unit.ninputs)
	{
	  case A2_IO_MATCHOUT:
		ninputs = noutputs;
		if(ninputs < ud->mininputs)
		{
			a2_FreeBlock(st, u);
			A2_LOG_DBG(i, "Voice %p has too few channels for "
					"unit '%s'!", v, ud->name);
			a2r_Error(st, A2_FEWCHANNELS, "a2_AddUnit()[3]");
			return NULL;
		}
		else if(ninputs > ud->maxinputs)
			ninputs = ud->maxinputs;
		break;
	  default:
		ninputs = si->p.unit.ninputs;
		break;
	}

	/* Some units require ninputs == noutputs... */
	if(ud->flags & A2_MATCHIO)
		minoutputs = maxoutputs = ninputs;
	else
	{
		minoutputs = ud->minoutputs;
		maxoutputs = ud->maxoutputs;
	}

	/* Output wiring */
	switch(si->p.unit.noutputs)
	{
	  case A2_IO_WIREOUT:
	  case A2_IO_MATCHOUT:
		u->noutputs = noutputs;
		if(u->noutputs < minoutputs)
		{
			a2_FreeBlock(st, u);
			A2_LOG_DBG(i, "Voice %p has too few channels for "
					"unit '%s'!", v, ud->name);
			a2r_Error(st, A2_FEWCHANNELS, "a2_AddUnit()[4]");
			return NULL;
		}
		else if(u->noutputs > maxoutputs)
			u->noutputs = maxoutputs;
		break;
	  default:
		u->noutputs = si->p.unit.noutputs;
		break;
	}
	if(si->p.unit.noutputs == A2_IO_WIREOUT)
		u->outputs = outputs;
	else
		u->outputs = scratch;

	/* Initialize instance struct and wire any control registers */
	u->descriptor = ud;
	u->registers = v->s.r + v->ncregs;
	if(ud->registers)
	{
		int j;
		for(j = 0; ud->registers[j].name; ++j)
		{
			v->cregs[v->ncregs].write = ud->registers[j].write;
			v->cregs[v->ncregs].unit = u;
			++v->ncregs;
		}
	}
	u->ninputs = ninputs;
	u->inputs = scratch;
	DUMPSTRUCTRT(A2_DLOG("in: %d\tout:%d", u->ninputs, u->noutputs);)

	/* Initialize control outputs, if any */
	if(ud->coutputs)
	{
		int j;
/*HACK*/
		if(!(u->coutputs = (A2_cport *)a2_AllocBlock(st)))
		{
			a2_FreeBlock(st, u);
			a2r_Error(st, A2_OOMEMORY, "a2_AddUnit()[5]");
			return NULL;
		}
/*/HACK*/
		for(j = 0; ud->coutputs[j].name; ++j)
			u->coutputs[j].write = NULL;
	}
	else
		u->coutputs = NULL;

	if((ud->flags & A2_MATCHIO) && (u->ninputs != u->noutputs))
	{
		a2_FreeBlock(st, u);
		A2_LOG_DBG(i, "Unit '%s' needs to have matching input/output "
				"counts!", ud->name);
		a2r_Error(st, A2_IODONTMATCH, "a2_AddUnit()[6]");
		return NULL;
	}

	/* Initialize the unit instance itself! */
	if((res = ud->Initialize(u, &v->s, us->statedata, si->p.unit.flags)))
	{
		a2_FreeBlock(st, u);
		A2_LOG_DBG(i, "Unit '%s' on voice %p failed to initialize! "
				"(%s)", ud->name, v, a2_ErrorString(res));
		a2r_Error(st, res, "a2_AddUnit()[7]");
		return NULL;
	}
	DUMPSTRUCTRT(A2_DLOG("\n");)

	/* Add to list! */
	if(lastunit)
		lastunit->next = u;
	else
		v->units = u;
	u->next = NULL;

	return u;
}


/*
 * Deinitialize and destroy unit 'u'.
 *
 * NOTE:
 *	This does NOT remove the unit from the voice unit chain! The unit must
 *	be detached from the list before destroyed with this function.
 */
static inline void a2_DestroyUnit(A2_state *st, A2_unit *u)
{
	if(u->descriptor->Deinitialize)
		u->descriptor->Deinitialize(u);
/*HACK*/
	if(u->coutputs)
		a2_FreeBlock(st, u->coutputs);
/*/HACK*/
	a2_FreeBlock(st, u);
}


static inline A2_errors a2_ControlWire(A2_state *st, const A2_structitem *si,
		A2_voice *v)
{
	/* Find the unit with the control output */
	int i;
	A2_cport *co, *cr;
	A2_unit *u = v->units;
	for(i = 0; i < si->p.wire.from_unit; ++i)
		u = u->next;
	co = &u->coutputs[si->p.wire.from_output];
	cr = &v->cregs[si->p.wire.to_register];
	co->unit = cr->unit;
	co->write = cr->write;
	return A2_OK;
}


/*
 * Populate voice 'v' with units as described by program 'p'.
 */
static inline A2_errors a2_PopulateVoice(A2_state *st, const A2_program *p,
		A2_voice *v)
{
	A2_structitem *si;
	A2_unit *lastu = NULL;
	int32_t **scratch = NULL;

	/* The 'inline' unit changes these! */
	unsigned noutputs = v->noutputs;
	int32_t **outputs = v->outputs;

	if(!p->units)
		return A2_OK;	/* No units - all done! */

	/* Make sure we have enough scratch buffers, if any are needed */
	if(p->buffers)
	{
		A2_bus **b = st->scratch + v->nestlevel;
		int bmin = p->buffers;
		if(bmin < 0)
		{
			/*
			 * We have units using scratch buffers while adapting
			 * to the voice output channel count! Make sure we have
			 * enough buffers to match the bus, to safely handle
			 * autowiring.
			 */
			bmin = -bmin;
			if(bmin < noutputs)
				bmin = noutputs;
		}
		DUMPSTRUCTRT(A2_DLOG("%sllocating %d channel bus for voice %p,"
				" nestlevel %d\n", *b ? "Rea" : "A",
				bmin, v, v->nestlevel);)
		if(!*b)
		{
			if(!(*b = a2_AllocBus(st, bmin)))
				return A2_OOMEMORY;
		}
		else if((*b)->channels < bmin)
		{
			if(!a2_ReallocBus(st, *b, bmin))
				return A2_OOMEMORY;
		}
		scratch = (*b)->buffers;
	}

	/* Add and wire the voice units! */
	for(si = p->units; si; si = si->next)
		if(!(lastu = a2_AddUnit(st, si, v, lastu, scratch,
				noutputs, outputs)))
			return A2_VOICEINIT;

	for(si = p->wires; si; si = si->next)
		switch(si->kind)
		{
		  case A2_SI_CONTROL_WIRE:
		  {
			A2_errors res = a2_ControlWire(st, si, v);
			if(res)
				return res;
			break;
		  }
		  case A2_SI_AUDIO_WIRE:
			return A2_NOTIMPLEMENTED;
		  default:
			return A2_INTERNAL + 402;
		}

	return A2_OK;
}


/*===========================================================================
 * WARNING: These are tuned for minimal init/cleanup overhead! Be careful...
 *===========================================================================*/

A2_voice *a2_VoiceAlloc(A2_state *st)
{
	A2_voice *v = (A2_voice *)st->sys->RTAlloc(st->sys, sizeof(A2_voice));
	if(!v)
	{
		a2r_Error(st, A2_OOMEMORY, "a2_VoiceAlloc()");
		return NULL;
	}
	v->sub = NULL;
	v->stack = NULL;
	v->program = NULL;
	v->events = NULL;
	v->units = NULL;
	v->ncregs = A2_FIXEDREGS;	/* Start at the first free register */
	v->handle = -1;
#if A2_SV_LUT_SIZE
	memset(v->sv, 0, sizeof(v->sv));
#endif
	memset(v->cregs, 0, sizeof(v->cregs));
	++st->totalvoices;
#ifdef DEBUG
	if(st->audio && st->audio->Process &&
			(st->config->flags & A2_REALTIME))
		A2_LOG_DBG(&st->interfaces->interface, "Voice pool exhausted! "
				"Allocated new voice %p.", v);
#endif
	return v;
}


A2_voice *a2_VoiceNew(A2_state *st, A2_voice *parent, unsigned when)
{
	A2_voice *v = st->voicepool;
	if(parent->nestlevel >= A2_NESTLIMIT - 1)
	{
		/* FIXME: Can we get the program name here instead? */
		a2r_Error(st, A2_VOICENEST, "a2_VoiceNew()");
		return NULL;
	}
	if(v)
		st->voicepool = v->next;
	else if(!(v = a2_VoiceAlloc(st)))
		return NULL;
	++st->activevoices;
	if(st->activevoices > st->activevoicesmax)
		st->activevoicesmax = st->activevoices;
	v->nestlevel = parent->nestlevel + 1;
	v->next = parent->sub;
	parent->sub = v;
	v->s.waketime = when;
	v->s.r[R_TICK] = parent->s.r[R_TICK];
	v->s.r[R_TRANSPOSE] = parent->s.r[R_TRANSPOSE];
	v->noutputs = parent->noutputs;
	v->outputs = parent->outputs;
	return v;
}


/* Special constructor for the root voice, which doesn't have a parent. */
A2_errors a2_init_root_voice(A2_state *st)
{
	int j, res;
	A2_voice *v;
	A2_program *rootdriver;
	A2_interface *i = &st->interfaces->interface;
	/*
	 * FIXME: We can't handle arbitrary channel counts very well at this
	 * FIXME: point, so we actually mix stereo internally at all times.
	 */
	const char *rd = "a2_rootdriver";
	if(st->config->channels < 2)
		rd = "a2_rootdriver_mono";
	rootdriver = a2_GetProgram(st, a2_Get(i, A2_ROOTBANK, rd));
	if(!rootdriver)
		return A2_INTERNAL + 400;
	if(!(v = a2_VoiceAlloc(st)))
		return A2_OOMEMORY;
	st->rootvoice = rchm_NewEx(&st->ss->hm, v, A2_TVOICE, A2_LOCKED, 1);
	if(st->rootvoice < 0)
		return -st->rootvoice;
	v->handle = st->rootvoice;
	++st->activevoices;
	if(st->activevoices > st->activevoicesmax)
		st->activevoicesmax = st->activevoices;
	v->nestlevel = 0;
	v->flags = A2_ATTACHED | A2_APIHANDLE;
	v->s.waketime = st->now_fragstart;
	v->next = NULL;
	v->s.r[R_TICK] = A2_DEFAULTTICK;
	v->s.r[R_TRANSPOSE] = 0;
	v->noutputs = st->master->channels;
	v->outputs = st->master->buffers;
	for(j = A2_FIRSTCONTROLREG; j < v->ncregs; ++j)
		a2_VoiceControl(st, v, j, 0, 0);
	if((res = a2_VoiceStart(st, v, rootdriver, 0, NULL)))
	{
		a2_VoiceFree(st, &v);
		rchm_Free(&st->ss->hm, st->rootvoice);
		return res;
	}
	return A2_OK;
}


/* Instantly kill and free voice and any subvoices recursively. */
void a2_VoiceFree(A2_state *st, A2_voice **head)
{
	unsigned i;
	A2_voice *v = *head;
	*head = v->next;
	v->next = st->voicepool;
	st->voicepool = v;
	--st->activevoices;

	if(v->flags & A2_APIHANDLE)
	{
		a2r_DetachHandle(st, v->handle);
		v->handle = -1;
		v->flags &= ~A2_APIHANDLE;
	}

	/* NOTE: -1 because we deal with the voice handles here! */
	if(v->events)
		a2_FlushEventQueue(st, &v->events, -1);

	while(v->sub)
		a2_VoiceFree(st, &v->sub);
#if A2_SV_LUT_SIZE
	memset(v->sv, 0, sizeof(v->sv));
#endif

	while(v->units)
	{
		A2_unit *u = v->units;
		v->units = u->next;
		a2_DestroyUnit(st, u);
	}

	while(v->stack)
		a2_VoicePop(st, v);

	v->program = st->ss->terminator;
	v->s.func = 0;
	v->s.pc = 0;
	v->s.state = A2_RUNNING;
	v->flags = 0;
	v->program = NULL;
	for(i = A2_FIXEDREGS; i < v->ncregs; ++i)
		v->cregs[i].write = NULL;
	v->ncregs = A2_FIXEDREGS;
}


/*===========================================================================
 * /WARNING
 *===========================================================================*/


/*
 * Start program 'p' on voice 'v'.
 *
 * NOTE:
 *	As of 1.9.1, voices are NOT populated at this point! This is instead
 *	performed by the INITV VM instruction, in order to have the voice units
 *	initialized and started at the exact start time of the program.
 */
A2_errors a2_VoiceStart(A2_state *st, A2_voice *v,
		A2_program *p, int argc, int *argv)
{
	int i;
	v->program = p;
	v->flags |= p->vflags;	/* A2_SUBINLINE etc */
	v->s.func = 0;
	v->s.pc = 0;
	v->s.state = A2_RUNNING;

	/* Grab the arguments! */
	if(argc > p->funcs[0].argc)
		argc = p->funcs[0].argc;
	memcpy(v->s.r + p->funcs[0].argv, argv, argc * sizeof(int));

	/* Get the defaults for any unspecified arguments */
	for(i = argc; i < p->funcs[0].argc; ++i)
		v->s.r[i + p->funcs[0].argv] = p->funcs[0].argdefs[i];

	/* Unit control registers start after the main program arguments! */
	v->ncregs = p->funcs->argv + p->funcs->argc;

	return A2_OK;
}


/*
 * Make a function or interrupt call to function 'func' of the current program.
 */
A2_errors a2_VoiceCall(A2_state *st, A2_voice *v, unsigned func,
		int argc, int *argv, int interrupt)
{
	int i;
	A2_function *fn = v->program->funcs + func;
	A2_errors res;
	if((res = a2_VoicePush(st, v, fn->argv, fn->topreg, interrupt)))
		return res;
	v->s.func = func;
	v->s.pc = 0;
	if(interrupt)
		v->s.state = A2_INTERRUPT;
	if(argc > fn->argc)
	{
		/* TODO: Figure out what program and entry point this is! */
		A2_LOG_DBG(&st->interfaces->interface, "Too many arguments to "
				"function %d! (%d/%d)",
				func, argc, fn->argc);
		argc = fn->argc;
	}
	memcpy(v->s.r + fn->argv, argv, argc * sizeof(int));
	for(i = argc; i < fn->argc; ++i)
		v->s.r[i + fn->argv] = fn->argdefs[i];
	return A2_OK;
}


/* Enqueue a timestamped event for entry point 'ep' of the specified voice. */
static inline A2_errors a2_VoiceSend(A2_state *st, A2_voice *v, unsigned when,
		unsigned ep, int argc, int *argv)
{
	A2_event *e;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	MSGTRACK(e->source = "a2_VoiceSend()";)
	e->b.common.action = A2MT_SEND;
	e->b.common.timestamp = when;
	e->b.play.program = ep;
	e->b.common.argc = argc;
	memcpy(e->b.play.a, argv, argc * sizeof(int));
	a2_SendEvent(&v->events, e);
	return A2_OK;
}


static inline A2_errors a2_VoiceKill(A2_state *st, A2_voice *v, unsigned when)
{
	A2_event *e;
	if(!(e = a2_AllocEvent(st)))
		return A2_OOMEMORY;
	MSGTRACK(e->source = "a2_VoiceKill()";)
	e->b.common.action = A2MT_KILL;
	e->b.common.timestamp = when;
	a2_SendEvent(&v->events, e);
	return A2_OK;
}


static inline A2_voice *a2_FindSubvoice(A2_voice *v, int vid)
{
	A2_voice *sv;
	if(vid < 0)
		return NULL;
#if A2_SV_LUT_SIZE
	if(vid < A2_SV_LUT_SIZE)
		return v->sv[vid];
#endif
	for(sv = v->sub; sv; sv = sv->next)
		if((sv->handle == vid) && (sv->flags & A2_ATTACHED) &&
				!(sv->flags & A2_APIHANDLE))
			return sv;
	return NULL;
}


static inline void a2_AttachSubvoice(A2_voice *v, A2_voice *sv, int vid)
{
#ifdef DEBUG
	if(sv->flags & A2_APIHANDLE)
	{
		A2_LOG_DBG(NULL, "a2_AttachSubvoice(): %p already attached to "
				" API handle %d!", sv, sv->handle);
		return;
	}
	else if(sv->flags & A2_ATTACHED)
	{
		A2_LOG_DBG(NULL, "a2_AttachSubvoice(): %p already attached; "
				"VID %d!", sv, sv->handle);
		return;
	}
#endif
	if(vid < 0)
	{
		if(vid == -2)
		{
			sv->flags |= A2_ATTACHED;
			sv->handle = -1;
		}
		return;
	}
#if A2_SV_LUT_SIZE
	if(vid < A2_SV_LUT_SIZE)
		v->sv[vid] = sv;
#endif
	sv->flags |= A2_ATTACHED;
	sv->handle = vid;
}


static inline void a2_DetachSubvoice(A2_voice *v, int vid)
{
	A2_voice *sv;
	if(vid < 0)
		return;
#if A2_SV_LUT_SIZE
	if((vid < A2_SV_LUT_SIZE) && v->sv[vid])
	{
		a2_VoiceDetach(v->sv[vid], v->s.waketime);
		v->sv[vid] = NULL;
		return;
	}
#endif
	for(sv = v->sub; sv; sv = sv->next)
		if((sv->handle == vid) && (sv->flags & A2_ATTACHED) &&
				!(sv->flags & A2_APIHANDLE))
		{
			a2_VoiceDetach(sv, v->s.waketime);
			break;
		}
}


static inline void a2_KillSubvoice(A2_state *st, A2_voice *v, int vid)
{
	A2_voice *sv;
	if(vid < 0)
		return;
#if A2_SV_LUT_SIZE
	if((vid < A2_SV_LUT_SIZE) && v->sv[vid])
	{
		a2_VoiceKill(st, v->sv[vid], v->s.waketime);
		v->sv[vid] = NULL;
		return;
	}
#endif
	for(sv = v->sub; sv; sv = sv->next)
		if((sv->handle == vid) && (sv->flags & A2_ATTACHED) &&
				!(sv->flags & A2_APIHANDLE))
		{
			a2_VoiceKill(st, sv, v->s.waketime);
			break;
		}
}


/*
 * Start a new voice, at the specified time, with the specified program.
 *
 * If 'vid' >= 0, any former voice with that ID will be detached, and the new
 * voice will be attached to that ID instead.
 *
 * If 'vid' is -1, the new voice will start in detached mode.
 *
 * If 'vid' is -2, the new voice will start in attached mode, but will be
 * anonymous.
 */
static A2_errors a2_VoiceSpawn(A2_state *st, A2_voice *v, int vid,
		A2_handle program, int argc, int *argv)
{
	int res;
	A2_voice *nv;
	A2_program *p = a2_GetProgram(st, program);
	a2_DetachSubvoice(v, vid);
	if(!p)
		return A2_BADPROGRAM;
	if(!(nv = a2_VoiceNew(st, v, v->s.waketime)))
		return v->nestlevel < A2_NESTLIMIT ?
				A2_VOICEALLOC : A2_VOICENEST;
	nv->flags = 0;
	a2_AttachSubvoice(v, nv, vid);
	if(!nv)
		return v->nestlevel < A2_NESTLIMIT ?
				A2_VOICEALLOC : A2_VOICENEST;
	if((res = a2_VoiceStart(st, nv, p, argc, argv)))
		a2_VoiceFree(st, &v->sub);
	return res;
}


DUMPMSGS(static void printargs(int argc, int *argv)
{
	int i;
	for(i = 0; i < argc; ++i)
		if(i < argc - 1)
			A2_DLOG("%f, ", argv[i] / 65536.0f);
		else
			A2_DLOG("%f", argv[i] / 65536.0f);
})

/* Start a detached new voice as specified by 'eb' under 'parent'. */
static inline A2_errors a2_event_play(A2_state *st, A2_voice *parent,
		A2_eventbody *eb)
{
	A2_voice *v;
	A2_program *p = a2_GetProgram(st, eb->play.program);
	if(!p)
		return A2_BADPROGRAM;
	if(!(v = a2_VoiceNew(st, parent, eb->common.timestamp)))
		return parent->nestlevel < A2_NESTLIMIT ?
				A2_VOICEALLOC : A2_VOICENEST;
	v->flags = 0;
	return a2_VoiceStart(st, v, p, eb->common.argc, eb->play.a);
}

/*
 * Start a new voice as specified by 'eb' under 'parent', and if specified,
 * attach it to 'handle', which would typically by pointed at the d.data
 * pointer in the RCHM_handleinfo struct.
 */
static inline A2_errors a2_event_start(A2_state *st, A2_voice *parent,
		A2_eventbody *eb, RCHM_handleinfo *hi)
{
	A2_voice *v;
	A2_program *p = a2_GetProgram(st, eb->start.program);
	if(!p)
		return A2_BADPROGRAM;
	if(!(v = a2_VoiceNew(st, parent, eb->common.timestamp)))
		return parent->nestlevel < A2_NESTLIMIT ?
				A2_VOICEALLOC : A2_VOICENEST;
	/*
	 * At this point, the handle type is A2_TNEWVOICE! The handle itself
	 * may have events enqueued, so we need to grab those here.
	 */
	v->events = (A2_event *)hi->d.data;
	hi->d.data = (void *)v;
	hi->typecode = A2_TVOICE;
	v->flags = A2_ATTACHED | A2_APIHANDLE;
	return a2_VoiceStart(st, v, p, eb->common.argc, eb->start.a);
}

/*
 * Forward event 'e' to the first subvoice of 'parent', then send copies of 'e'
 * to any further subvoices.
 *
 * NOTE: This is for KILL and SEND events only!
 */
static inline void a2_event_subforward(A2_state *st, A2_voice *parent,
		A2_event *e)
{
	int esize;
	A2_voice *sv = parent->sub;
#ifdef DEBUG
	A2_interface *i = &st->interfaces->interface;
	switch(e->b.common.action)
	{
	  case A2MT_SEND:
	  case A2MT_KILL:
	  	break;
	  default:
		A2_LOG_DBG(i, "a2_event_subforward() used on unsupported "
				"action %d!", e->b.common.action);
		return;
	}
	if(!sv)
	{
		A2_LOG_DBG(i, "a2_event_subforward() called with no "
				"subvoices!");
		return;
	}
#endif
	a2_SendEvent(&sv->events, e);
	if(!sv->next)
		return;
	if(e->b.common.argc)
		esize = offsetof(A2_eventbody, play.a) +
				sizeof(int) * (e->b.common.argc);
	else
		esize = offsetof(A2_eventbody, play.program);
	while(sv->next)
	{
		A2_event *ne = a2_AllocEvent(st);
		if(!ne)
		{
			a2r_Error(st, A2_OOMEMORY, "a2_event_subforward()");
			return;
		}
		sv = sv->next;
		memcpy(&ne->b, &e->b, esize);
		MSGTRACK(ne->source = "a2_event_subforward()";)
		a2_SendEvent(&sv->events, ne);
	}
}

/*
 * Process events. Will return A2_OK until the voice is killed, or there is an
 * error.
 *
 * NOTE: This function now stops as soon as timestamps aren't EXACTLY equal!
 *       (Previous versions would keep going until the next sample frame.)
 */
static inline A2_errors a2_VoiceProcessEvents(A2_state *st, A2_voice *v)
{
	unsigned current = v->events->b.common.timestamp;
	while(v->events)
	{
		int res;
		A2_event *e = v->events;
		if(e->b.common.timestamp != current)
			return A2_OK;
		DUMPMSGS(A2_DLOG("%f:\th (%p) ",
				e->b.common.timestamp / 256.0f, v);)
		NUMMSGS(A2_DLOG("[ %u ] ", e->number);)
#ifdef DEBUG
		if(a2_TSDiff(e->b.common.timestamp, st->now_fragstart) < 0)
		{
			/* NOTE: Can only happen if there's a bug somewhere! */
			A2_interface *i = &st->interfaces->interface;
			A2_LOG_DBG(i, "Incorrect timestamp for voice %p! "
					"(%f frames late.)", v,
					(st->now_fragstart -
					e->b.common.timestamp) / 256.0f);
			MSGTRACK(A2_LOG_DBG(i, "  Tracking: ev %p from %s",
					e, e->source);)
			e->b.common.timestamp = st->now_fragstart;
		}
#endif
		switch(e->b.common.action)
		{
		  case A2MT_PLAY:
			DUMPMSGS(
				A2_DLOG("PLAY(");
				printargs(e->b.common.argc, e->b.play.a);
				A2_DLOG(")\n");
			)
			if((res = a2_event_play(st, v, &e->b)))
				a2r_Error(st, res, "A2MT_PLAY");
			break;
		  case A2MT_START:
		  {
			RCHM_handleinfo *hi = rchm_Get(&st->ss->hm,
					e->b.start.voice);
#ifdef DEBUG
			if(!hi)
			{
				a2r_Error(st, A2_BADVOICE, "A2MT_START[1]");
				break;
			}
#endif
			DUMPMSGS(
				A2_DLOG("START(");
				printargs(e->b.start.argc, e->b.start.a);
				A2_DLOG(")\n");
			)
			if((res = a2_event_start(st, v, &e->b, hi)))
			{
				a2r_Error(st, res, "A2MT_START[2]");
				a2_FlushEventQueue(st,
						(A2_event **)&hi->d.data, -1);
				a2r_DetachHandle(st, e->b.start.voice);
			}
			break;
		  }
		  case A2MT_SEND:
		  {
			int ep;
			DUMPMSGS(
				A2_DLOG("SEND(%u: ", e->b.play.program);
				printargs(e->b.common.argc, e->b.play.a);
				A2_DLOG(")\n");
			)
			if((ep = v->program->eps[e->b.play.program]) < 0)
			{
#ifdef DEBUG
				a2r_Error(st, A2_BADENTRY, "A2MT_SEND[1]");
#endif
				break;
			}
			if((res = a2_VoiceCall(st, v, ep, e->b.common.argc,
					e->b.play.a, 1)))
			{
				a2r_Error(st, res, "A2MT_SEND[2]");
				break;
			}
			v->s.waketime = e->b.common.timestamp;
			v->events = e->next;
			a2_FreeEvent(st, e);
			return A2_OK;	/* Spin the VM to process message! */
		  }
		  case A2MT_SENDSUB:
		  case A2MT_KILLSUB:
			DUMPMSGS(
				if(e->b.common.action == A2MT_SENDSUB)
				{
					A2_DLOG("SENDSUB(%u: ",
							e->b.play.program);
					printargs(e->b.common.argc,
							e->b.play.a);
					A2_DLOG(")\n");
				}
				else
					A2_DLOG("KILLSUB\n");
			)
			if(v->sub)
			{
				/* Turn into non-SUB event! */
				--e->b.common.action;
				v->events = e->next;
				a2_event_subforward(st, v, e);
				continue;	/* The event is reused! */
			}
			break;
		  case A2MT_KILL:
			DUMPMSGS(A2_DLOG("KILL\n");)
			return A2_END;
		  case A2MT_ADDXIC:
			DUMPMSGS(A2_DLOG("ADDXIC\n");)
			if((res = a2_XinsertAddClient(st, v, e->b.xic.client)))
				a2r_Error(st, res, "A2MT_ADDXIC");
			break;
		  case A2MT_REMOVEXIC:
			DUMPMSGS(A2_DLOG("REMOVEXIC\n");)
			if((res = a2_XinsertRemoveClient(e->b.xic.client)))
				a2r_Error(st, res, "A2MT_REMOVEXIC");
			break;
		  case A2MT_RELEASE:
			DUMPMSGS(A2_DLOG("RELEASE\n");)
			a2r_DetachHandle(st, v->handle);
			v->handle = -1;
			v->flags &= ~A2_APIHANDLE;
			a2_VoiceDetach(v, e->b.common.timestamp);
			break;
		}
		v->events = e->next;
		a2_FreeEvent(st, e);
	}
	return A2_OK;
}


/*
 * Register write tracker
 */
typedef struct A2_regtracker
{
	uint32_t	mask;			/* One bit/register */
	uint32_t	position;		/* Current index in regs[] */
	uint8_t		regs[A2_REGISTERS];	/* Indices of written regs */
} A2_regtracker;

static inline void a2_RTInit(A2_regtracker *rt)
{
	rt->mask = rt->position = 0;
}

static inline void a2_RTMark(A2_regtracker *rt, unsigned r)
{
	uint32_t b = 1 << r;
	if(b & rt->mask)
		return;		/* Already marked! --> */
	rt->mask |= b;
	rt->regs[rt->position++] = r;
}

static inline void a2_RTUnmark(A2_regtracker *rt, unsigned r)
{
	uint32_t b = 1 << r;
	if(b & rt->mask)
	{
		int i;
		rt->mask &= ~b;
		for(i = 0; i < rt->position; ++i)
			if(rt->regs[i] == r)
			{
				rt->regs[i] = rt->regs[--rt->position];
				break;
			}
	}
}

static inline void a2_RTApply(A2_regtracker *rt, A2_state *st, A2_voice *v,
		unsigned start, unsigned duration)
{
	int i;
	for(i = 0; i < rt->position; ++i)
		a2_VoiceControl(st, v, rt->regs[i], start, duration);
}

static inline void a2_RTSetAll(A2_regtracker *rt, A2_state *st, A2_voice *v,
		unsigned start)
{
	int i;
	for(i = 0; i < rt->position; ++i)
		a2_VoiceControl(st, v, rt->regs[i], start, 0);
	a2_RTInit(rt);
}


/* Convert musical tick duration to audio frame delta time */
static inline unsigned a2_ticks2t(A2_state *st, A2_voice *v, int d)
{
	return ((((uint64_t)d * v->s.r[R_TICK] + 127) >> 8) *
			st->msdur + 0x7fffffff) >> 32;
}


/* Convert milliseconds to audio frame delta time */
static inline unsigned a2_ms2t(A2_state *st, int d)
{
	return ((int64_t)d * st->msdur + 0x7fffff) >> 24;
}


/* Get the size (number of elements) of an indexable object */
static int a2_sizeof_object(A2_state *st, int handle)
{
	A2_wave *w;
	A2_interface *i = &st->interfaces->interface;
	if(handle < 0)
		return -A2_INVALIDHANDLE << 16;
	if(!(w = a2_GetWave(i, handle)))
		return -A2_WRONGTYPE << 16;
	switch(w->type)
	{
	  case A2_WWAVE:
	  case A2_WMIPWAVE:
		break;
	  default:
		return -A2_WRONGTYPE << 16;
	}
	return ((int64_t)(w->d.wave.size[0]) << 16) / w->period;
}

/*
 * Execute VM instructions until a timing instruction is executed, or the
 * program ends. Returns A2_OK as long as the VM program wants to keep running.
 *
 * NOTE: 'limit' is the number of 256th frames to process.
 */
#define	A2_VMABORT(e, m)					\
	{							\
		st->instructions += A2_INSLIMIT - inscount;	\
		a2r_Error(st, e, m);				\
		return e;					\
	}
static inline A2_errors a2_VoiceProcessVM(A2_state *st, A2_voice *v)
{
	int res;
	int cargc = 0, cargv[A2_MAXARGS];	/* run/spawn argument stack */
	unsigned *code = v->program->funcs[v->s.func].code;
	int *r = v->s.r;
	unsigned inscount = A2_INSLIMIT;
	A2_regtracker rt;
	if(v->s.state == A2_WAITING)
		v->s.state = A2_RUNNING;
	a2_RTInit(&rt);
	while(1)
	{
		unsigned dt;
		A2_instruction *ins = (A2_instruction *)(code + v->s.pc);
		DUMPCODERT(
			A2_DLOG("%p: ", v);
			a2_DumpIns(code, v->s.pc);
		)
		if(!--inscount)
			A2_VMABORT(A2_OVERLOAD, "VM");
		switch((A2_opcodes)ins->opcode)
		{

		/* Program flow control */
		  case OP_END:
		  {
		  	unsigned now = v->s.waketime;
			a2_RTApply(&rt, st, v, v->s.waketime, 0);
			v->s.waketime += 1000000;
			if(v->s.state == A2_FINALIZING)
			{
				/* Wait for subvoices to terminate */
				st->instructions += A2_INSLIMIT - inscount;
				DUMPCODERT(
				  if(v->sub)
				    A2_DLOG("%p: [still waiting for "
				        "subvoices]\n", v);
				  else
				    A2_DLOG("%p: [end]\n", v);
				)
				return v->sub ? A2_OK : A2_END;
			}
			v->s.state = A2_ENDING;
			if((v->flags & A2_ATTACHED) || v->events)
			{
				/* Hang around until detached! */
				st->instructions += A2_INSLIMIT - inscount;
				DUMPCODERT(A2_DLOG("%p: [waiting for "
						"detach]\n", v);)
				return A2_OK;
			}
			v->s.state = A2_FINALIZING;
			if(!v->sub)
			{
				/* That's it - all done! */
				st->instructions += A2_INSLIMIT - inscount;
				DUMPCODERT(A2_DLOG("%p: [end]\n", v);)
				return A2_END;
			}
			/* Detach subvoices, then wait for them to terminate */
#if A2_SV_LUT_SIZE
			memset(v->sv, 0, sizeof(v->sv));
#endif
			for(v = v->sub; v; v = v->next)
				a2_VoiceDetach(v, now);
			st->instructions += A2_INSLIMIT - inscount;
			DUMPCODERT(A2_DLOG("%p: [waiting for subvoices]\n",
					v);)
			return A2_OK;
		  }
		  case OP_RETURN:
		  {
			unsigned now = v->s.waketime;
			if(a2_VoicePop(st, v))
			{
				/* Return from interrupt */
				code = v->program->funcs[v->s.func].code;
				if(v->s.state >= A2_ENDING)
					continue;
				dt = v->s.waketime - now;
				v->s.waketime = now;
				goto timing_interrupt;
			}
			else
			{
				/* Return from local function */
				code = v->program->funcs[v->s.func].code;
				continue;
			}
		  }
		  case OP_CALL:
		  {
#ifdef DEBUG
			A2_interface *i = &st->interfaces->interface;
			if(!ins->a2)
				A2_LOG_DBG(i, "CALL to function 0!");
			if(ins->a2 >= v->program->nfuncs)
				A2_LOG_DBG(i, "Function index %d out of "
						"range!", ins->a2);
#endif
			if((res = a2_VoiceCall(st, v, ins->a2, cargc, cargv,
					0)))
				A2_VMABORT(res, "VM:CALL");
			code = v->program->funcs[v->s.func].code;
			cargc = 0;
			continue;
		  }

		/* Local flow control */
		  case OP_JUMP:
			v->s.pc = ins->a2;
			continue;
		  case OP_LOOP:
			r[ins->a1] -= 65536;
			if(r[ins->a1] <= 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JZ:
			if(r[ins->a1])
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JNZ:
			if(!r[ins->a1])
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JG:
			if(r[ins->a1] <= 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JL:
			if(r[ins->a1] >= 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JGE:
			if(r[ins->a1] < 0)
				break;
			v->s.pc = ins->a2;
			continue;
		  case OP_JLE:
			if(r[ins->a1] > 0)
				break;
			v->s.pc = ins->a2;
			continue;

		/* Timing */
		  case OP_DELAY:
			dt = a2_ms2t(st, ins->a3);
			++v->s.pc;
			goto timing;
		  case OP_DELAYR:
			dt = a2_ms2t(st, r[ins->a1]);
			goto timing;
		  case OP_TDELAY:
			dt = a2_ticks2t(st, v, ins->a3);
			++v->s.pc;
			goto timing;
		  case OP_TDELAYR:
			dt = a2_ticks2t(st, v, r[ins->a1]);
			goto timing;

		/* Arithmetics */
		  case OP_SUBR:
			r[ins->a1] -= r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_DIVR:
			if(!r[ins->a2])
				A2_VMABORT(A2_DIVBYZERO, "VM:DIVR");
			r[ins->a1] = ((int64_t)r[ins->a1] << 16) / r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_P2DR:
			r[ins->a1] = A2_1K_DIV_MIDDLEC / a2_P2I(r[ins->a2]);
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_NEGR:
			r[ins->a1] = -r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_LOAD:
			r[ins->a1] = ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_LOADR:
			r[ins->a1] = r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_ADD:
			r[ins->a1] += ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_ADDR:
			r[ins->a1] += r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_MUL:
			r[ins->a1] = (int64_t)r[ins->a1] * ins->a3 >> 16;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_MULR:
			r[ins->a1] = (int64_t)r[ins->a1] * r[ins->a2] >> 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_MOD:
			r[ins->a1] %= ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_MODR:
			if(!r[ins->a2])
				A2_VMABORT(A2_DIVBYZERO, "VM:MODR");
			r[ins->a1] %= r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_QUANT:
			r[ins->a1] = r[ins->a1] / ins->a3 * ins->a3;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_QUANTR:
			if(!r[ins->a2])
				A2_VMABORT(A2_DIVBYZERO, "VM:QUANTR");
			r[ins->a1] = r[ins->a1] / r[ins->a2] * r[ins->a2];
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_RAND:
			r[ins->a1] = (int64_t)a2_Noise(&st->noisestate) *
					ins->a3 >> 16;
			a2_RTMark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_RANDR:
			r[ins->a1] = (int64_t)a2_Noise(&st->noisestate) *
					r[ins->a2] >> 16;
			a2_RTMark(&rt, ins->a1);
			break;

		/* Comparison operators */
/*TODO: Versions with an immediate second operand! */
		  case OP_GR:
			r[ins->a1] = (r[ins->a1] > r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_LR:
			r[ins->a1] = (r[ins->a1] < r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_GER:
			r[ins->a1] = (r[ins->a1] >= r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_LER:
			r[ins->a1] = (r[ins->a1] <= r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_EQR:
			r[ins->a1] = (r[ins->a1] == r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_NER:
			r[ins->a1] = (r[ins->a1] != r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;

		/* Boolean operators */
		  case OP_ANDR:
			r[ins->a1] = (r[ins->a1] && r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_ORR:
			r[ins->a1] = (r[ins->a1] || r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_XORR:
			r[ins->a1] = (!r[ins->a1] != !r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_NOTR:
			r[ins->a1] = (!r[ins->a2]) << 16;
			a2_RTMark(&rt, ins->a1);
			break;

		/* Unit control */
		  case OP_SET:
			a2_VoiceControl(st, v, ins->a1, v->s.waketime, 0);
			a2_RTUnmark(&rt, ins->a1);
			break;

		  case OP_SETALL:
			a2_RTSetAll(&rt, st, v, v->s.waketime);
			break;

		  case OP_RAMP:
			a2_VoiceControl(st, v, ins->a1, v->s.waketime,
					a2_ms2t(st, ins->a3));
			a2_RTUnmark(&rt, ins->a1);
			++v->s.pc;
			break;
		  case OP_RAMPR:
			a2_VoiceControl(st, v, ins->a1, v->s.waketime,
					a2_ms2t(st, r[ins->a2]));
			a2_RTUnmark(&rt, ins->a1);
			break;

		  case OP_RAMPALL:
			a2_RTApply(&rt, st, v, v->s.waketime,
					a2_ms2t(st, ins->a3));
			a2_RTInit(&rt);
			++v->s.pc;
			break;
		  case OP_RAMPALLR:
			a2_RTApply(&rt, st, v, v->s.waketime,
					a2_ms2t(st, r[ins->a1]));
			a2_RTInit(&rt);
			break;

		/* Subvoice control */
		  case OP_PUSH:
			if(cargc >= A2_MAXARGS)
				A2_VMABORT(A2_MANYARGS, "VM:PUSH");
			cargv[cargc++] = ins->a3;
			++v->s.pc;
			break;
		  case OP_PUSHR:
			if(cargc >= A2_MAXARGS)
				A2_VMABORT(A2_MANYARGS, "VM:PUSHR");
			cargv[cargc++] = r[ins->a1];
			break;
		  case OP_SPAWNVR:
			a2_VoiceSpawn(st, v, r[ins->a1] >> 16,
					r[ins->a2] >> 16, cargc, cargv);
			cargc = 0;
			break;
		  case OP_SPAWNV:
			a2_VoiceSpawn(st, v, r[ins->a1] >> 16,
					ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  case OP_SPAWNR:
			a2_VoiceSpawn(st, v, ins->a1, r[ins->a2] >> 16,
					cargc, cargv);
			cargc = 0;
			break;
		  case OP_SPAWN:
			a2_VoiceSpawn(st, v, ins->a1, ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  case OP_SPAWNDR:
			a2_VoiceSpawn(st, v, -1, r[ins->a1] >> 16, cargc,
					cargv);
			cargc = 0;
			break;
		  case OP_SPAWND:
			a2_VoiceSpawn(st, v, -1, ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  case OP_SPAWNAR:
			a2_VoiceSpawn(st, v, -2, r[ins->a1] >> 16, cargc,
					cargv);
			cargc = 0;
			break;
		  case OP_SPAWNA:
			a2_VoiceSpawn(st, v, -2, ins->a2, cargc, cargv);
			cargc = 0;
			break;
		  case OP_SENDR:
		  {
			A2_voice *sv;
			if((sv = a2_FindSubvoice(v, r[ins->a1] >> 16)))
				a2_VoiceSend(st, sv, v->s.waketime, ins->a2,
						cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SEND:
		  {
			A2_voice *sv;
#ifdef DEBUG
			A2_interface *i = &st->interfaces->interface;
			if(!ins->a2)
				A2_LOG_DBG(i, "Weird...! SEND to EP0...");
#endif
			if((sv = a2_FindSubvoice(v, ins->a1)))
				a2_VoiceSend(st, sv, v->s.waketime, ins->a2,
						cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SENDA:
		  {
			A2_voice *sv;
			for(sv = v->sub; sv; sv = sv->next)
				a2_VoiceSend(st, sv, v->s.waketime, ins->a2,
						cargc, cargv);
			cargc = 0;
			break;
		  }
		  case OP_SENDS:
		  {
			int ep = v->program->eps[ins->a2];
			if(ep < 0)
				A2_VMABORT(A2_BADENTRY, "VM:SENDS");
			if((res = a2_VoiceCall(st, v, ep, cargc, cargv, 1)))
				A2_VMABORT(res, "VM:SENDS");
			code = v->program->funcs[v->s.func].code;
			cargc = 0;
			break;
		  }
		  case OP_WAIT:
		  {
			A2_voice *sv = a2_FindSubvoice(v, ins->a1);
			if(!sv)
				break;	/* No voice to wait for! */
			/* NOTE: This only waits with fragment granularity! */
			if(sv->s.state >= A2_ENDING)
				break;	/* Done! */
			a2_RTApply(&rt, st, v, v->s.waketime, 0);
			v->s.waketime = st->now_fragstart + (A2_MAXFRAG << 8);
			v->s.state = A2_WAITING;
			st->instructions += A2_INSLIMIT - inscount;
			DUMPCODERT(A2_DLOG("%p: [waiting]\n", v);)
			return A2_OK;
		  }
		  case OP_KILLR:
		  {
			unsigned vid = r[ins->a1] >> 16;
			a2_KillSubvoice(st, v, vid);
			break;
		  }
		  case OP_KILL:
			a2_KillSubvoice(st, v, ins->a1);
			break;
		  case OP_KILLA:
		  {
			A2_voice *sv;
			for(sv = v->sub; sv; sv = sv->next)
				a2_VoiceKill(st, sv, v->s.waketime);
#if A2_SV_LUT_SIZE
			memset(v->sv, 0, sizeof(v->sv));
#endif
			break;
		  }
		  case OP_DETACHR:
		  {
			unsigned vid = r[ins->a1] >> 16;
			a2_DetachSubvoice(v, vid);
			break;
		  }
		  case OP_DETACH:
			a2_DetachSubvoice(v, ins->a1);
			break;
		  case OP_DETACHA:
		  {
			A2_voice *sv;
			for(sv = v->sub; sv; sv = sv->next)
				a2_VoiceDetach(sv, v->s.waketime);
#if A2_SV_LUT_SIZE
			memset(v->sv, 0, sizeof(v->sv));
#endif
			break;
		  }

		/* Message handling */
		  case OP_SLEEP:
			a2_RTApply(&rt, st, v, v->s.waketime, 0);
			v->s.state = A2_ENDING;
			st->instructions += A2_INSLIMIT - inscount;
			v->s.waketime += 1000000;
			return A2_OK;
#if 0
TODO:
		  case OP_SLEEP:
			...
			v->s.state = A2_SLEEPING;
			...

		  case OP_WAKE:
		  {
			A2_stackentry *se = v->stack;
			while(se->prev && (se->state == A2_INTERRUPT))
				se = se->prev;
			if(se->state != A2_SLEEPING)
				break;
			++se->pc;	/* Skip out of the SLEEP instruction */
			se->timer = 0;	/* Wake the VM up */
			se->state = A2_RUNNING;
			break;
		  }
#endif
		  case OP_WAKE:
		  {
			A2_stackentry *se = v->stack;
			while(se->prev && (se->state == A2_INTERRUPT))
				se = se->prev;
			if(se->state < A2_ENDING)
				break;
			se->pc = ins->a2;
			se->state = A2_RUNNING;
			se->waketime = v->s.waketime;
			break;
		  }
		  case OP_FORCE:
		  {
			A2_stackentry *se = v->stack;
			while(se->prev && (se->state == A2_INTERRUPT))
				se = se->prev;
			se->pc = ins->a2;
			se->state = A2_RUNNING;
			se->waketime = v->s.waketime;
			break;
		  }

		/* Debugging */
		  case OP_DEBUGR:
		  {
			A2_interface *i = &st->interfaces->interface;
			A2_LOG_MSG(i, "debug R%d=%f\t(%p)", ins->a1,
					r[ins->a1] * (1.0f / 65536.0f), v);
			break;
		  }
		  case OP_DEBUG:
		  {
			A2_interface *i = &st->interfaces->interface;
			A2_LOG_MSG(i, "debug %f\t(%p)",
					ins->a3 * (1.0f / 65536.0f), v);
			++v->s.pc;
			break;
		  }

		/* Special instructions */
		  case OP_INITV:
			if((res = a2_PopulateVoice(st, v->program, v)))
			{
				st->instructions += A2_INSLIMIT - inscount;
				return res;
			}
			break;
		  case OP_SIZEOF:
			if((res = a2_sizeof_object(st, ins->a2) < 0))
				A2_VMABORT(-res >> 16, "VM:SIZEOF");
			r[ins->a1] = res;
			a2_RTMark(&rt, ins->a1);
			break;
		  case OP_SIZEOFR:
			if((res = a2_sizeof_object(st, r[ins->a2] >> 16)) < 0)
				A2_VMABORT(-res >> 16, "VM:SIZEOFR");
			r[ins->a1] = res;
			a2_RTMark(&rt, ins->a1);
			break;

		  case A2_OPCODES:
		  default:
			A2_VMABORT(A2_ILLEGALOP, "VM:ILLEGALOP");
		}
		++v->s.pc;
		continue;
	  timing:
		++v->s.pc;
	  timing_interrupt:
		a2_RTApply(&rt, st, v, v->s.waketime, dt);
		if(!dt)
			continue;
		DUMPCODERT(A2_DLOG("%p: [reschedule; dt=%f]\n",
				v, dt / 256.0f);)
		v->s.state = A2_WAITING;
		st->instructions += A2_INSLIMIT - inscount;
		v->s.waketime += dt;
		return A2_OK;
	}
}
#undef	A2_VMABORT


/* Wrapper for recursive calls to a2_ProcessVoices() */
static inline void a2_ProcessSubvoices(A2_state *st, A2_voice *v,
		unsigned offset, unsigned frames)
{
	if(!v->sub)
		return;
	a2_ProcessVoices(st, &v->sub, offset, frames);
	if(!v->sub)
		if(v->s.state >= A2_ENDING)
			/* Notify parent that subs are done! */
			v->s.waketime = st->now_fragstart + (frames << 8);
}


/* Adding and replacing Process() implementations for the 'inline' unit */
void a2_inline_ProcessAdd(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_inline *il = a2_inline_cast(u);
	a2_ProcessSubvoices(il->state, il->voice, offset, frames);
}

void a2_inline_Process(A2_unit *u, unsigned offset, unsigned frames)
{
	A2_inline *il = a2_inline_cast(u);
	int i;
	for(i = 0; i < u->noutputs; ++i)
		memset(u->outputs[i] + offset, 0, frames * sizeof(int));
	a2_ProcessSubvoices(il->state, il->voice, offset, frames);
}


/*
 * Process all events and instructions for the current sample frame. Returns
 * the number of sample frames to the next instruction or event, or a negative
 * VM return code.
 */
static inline int a2_VoiceProcessVMEv(A2_state *st, A2_voice *v, unsigned now)
{
	/* Events + VM loop */
	while(v->events)
	{
		int res;
		int nextvm = a2_TSDiff(v->s.waketime, now);
		int nextev = a2_TSDiff(v->events->b.common.timestamp, now);
		if((nextvm > 255) && (nextev > 255))
		{
			if(nextvm < nextev)
				return nextvm >> 8;
			else
				return nextev >> 8;
		}
		if(nextvm <= nextev)
			res = a2_VoiceProcessVM(st, v);
		else
			res = a2_VoiceProcessEvents(st, v);
		if(res)
		{
#ifdef DEBUG
			A2_interface *i = &st->interfaces->interface;
			if(res != A2_END)
				A2_LOG_DBG(i, "a2_VoiceProcess%s(): %s!",
						nextvm <= nextev ?
						"Events" : "VMP",
						a2_ErrorString(res));
#endif
			return -res;
		}
	}

	/* VM only loop */
	while(1)
	{
		int res;
		int nextvm = a2_TSDiff(v->s.waketime, now);
		if(nextvm > 255)
			return nextvm >> 8;
		if((res = a2_VoiceProcessVM(st, v)))
		{
#ifdef DEBUG
			A2_interface *i = &st->interfaces->interface;
			if(res != A2_END)
				A2_LOG_DBG(i, "a2_VoiceProcessVM(): %s!",
						a2_ErrorString(res));
#endif
			return -res;
		}
	}
}


/*
 * Process a single voice, alternating between the VM and units (if any) as
 * needed. If the fragment is cut short by program termination, or an error,
 * the 'frames' argument is adjusted to the number of frames processed before
 * this event occurred.
 *
 * NOTE: 'inline' units also run here, so this may recursively do subvoice
 *       processing as well!
 */
static inline A2_errors a2_VoiceProcess(A2_state *st, A2_voice *v,
		unsigned offset, unsigned *frames)
{
	int s = offset;
	int s_stop = offset + *frames;	/* End of fragment */
	while(s < s_stop)
	{
		A2_unit *u;
		unsigned now = st->now_fragstart + (s << 8);
		int res = a2_VoiceProcessVMEv(st, v, now);
		if(res < 0)
		{
#if 0
			/*
			 * FIXME: This is what *should* happen, theoretically,
			 *        but it causes clicks in some situations. Why?
			 */
			*frames = s - offset;	/* Cut fragment short! */
#endif
			return -res;
		}
#ifdef DEBUG
		if(!res)
			A2_LOG_DBG(&st->interfaces->interface,
					"a2_VoiceProcessVMEv() returned 0!");
#endif
		if(s + res > s_stop)
			res = s_stop - s;
		for(u = v->units; u; u = u->next)
			u->Process(u, s, res);
		s += res;
	}
	return A2_OK;
}


void a2_ProcessVoices(A2_state *st, A2_voice **head, unsigned offset,
		unsigned frames)
{
	while(*head)
	{
		A2_errors res = a2_VoiceProcess(st, *head, offset, &frames);
		if(!((*head)->flags & A2_SUBINLINE))
			a2_ProcessSubvoices(st, *head, offset, frames);
		if(res)
			a2_VoiceFree(st, head);
		else
			head = &(*head)->next;
	}
}


/* Pack the fragments from the master bus into the driver output buffers! */
static void a2_ProcessMaster(A2_state *st, unsigned offset, unsigned frames)
{
	int c;
	int32_t **in = st->master->buffers;
	int32_t **bufs = st->audio->buffers;
	for(c = 0; c < st->config->channels; ++c)
		memcpy(bufs[c] + offset, in[c], frames * sizeof(int32_t));
}


/* Poll any open MIDI drivers */
static void a2_PollMIDI(A2_state *st, unsigned frames)
{
	A2_driver *d;
	for(d = st->config->drivers; d; d = d->next)
	{
		A2_mididriver *md;
		if(d->type != A2_MIDIDRIVER)
			continue;
		if(!(d->flags & A2_ISOPEN))
			continue;
		md = (A2_mididriver *)d;
		md->Poll(md, frames);
	}
}


void a2_AudioCallback(A2_audiodriver *driver, unsigned frames)
{
	A2_state *st = (A2_state *)driver->state;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, st->rootvoice);
	A2_voice *rootvoice = (A2_voice *)hi->d.data;
	unsigned offset = 0;
	unsigned remain = frames;
	unsigned latelimit = st->now_frames;
	uint64_t t1u = a2_GetMicros();	/* Monitoring: pre DSP timestamp */
	unsigned dur;

	/* Clear API message stats, if requested */
	if(st->tsstatreset)
	{
		st->tsstatreset = 0;
		st->tssamples = 0;
		st->tssum = 0;
		st->tsmin = INT32_MAX;
		st->tsmax = INT32_MIN;
	}

	/* Update API message timestamping time reference */
	st->now_frames = st->now_fragstart + (frames << 8);
	st->now_ticks = a2_GetTicks();	/* Event timing reference */
	st->now_guard = st->now_frames;

	/* API message processing */
	a2r_PumpEngineMessages(st, latelimit);

	/* Update API message stats */
	if(st->tssamples)
		st->tsavg = ((int64_t)st->tssum << 8) / st->tssamples;

	/* MIDI input processing */
	a2_PollMIDI(st, frames);

	/* Audio processing */
	while(remain)
	{
		unsigned frag = remain > A2_MAXFRAG ? A2_MAXFRAG : remain;
		a2_ClearBus(st->master, 0, frag);
		a2_ProcessVoices(st, &rootvoice, 0, frag);
		a2_ProcessMaster(st, offset, frag);
		offset += frag;
		remain -= frag;
		st->now_fragstart += frag << 8;
	}
	dur = a2_GetMicros() - t1u;	/* Monitoring: DSP processing time */

	/* Update CPU stats */
	if(st->statreset)
	{
		st->statreset = 0;
		st->cputimesum = st->cputimecount = 0;
		st->avgstart = t1u;
		st->cpuloadmax = 0;
	}
	if(dur > st->cputimemax)
		st->cputimemax = dur;
	st->cputimesum += dur;
	++st->cputimecount;
	if(t1u != st->now_micros)
	{
		unsigned ld = dur * 100 / (t1u - st->now_micros);
		if(ld > st->cpuloadmax)
			st->cpuloadmax = ld;
		st->now_micros = t1u;
	}
	st->cputimeavg = st->cputimesum / st->cputimecount;
	if(t1u != st->avgstart)
		st->cpuloadavg = st->cputimesum * 100 / (t1u - st->avgstart);

	/* Process end-of-cycle messages */
	a2r_ProcessEOCEvents(st, frames);
}


int a2_Run(A2_interface *i, unsigned frames)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	A2_state *st = ii->state;
	if(!st->audio->Run)
		return -A2_NOTIMPLEMENTED;
	return st->audio->Run(st->audio, frames);
}


static void a2_kill_subvoices_using_program(A2_state *st, A2_voice *v,
		A2_program *p)
{
	A2_voice **head = &v->sub;
	while(*head)
	{
		A2_voice *sv = *head;
		if(sv->program == p)
		{
#if A2_SV_LUT_SIZE
			int i;
			for(i = 0; i < A2_SV_LUT_SIZE; ++i)
				if(v->sv[i] == sv)
				{
					v->sv[i] = NULL;
					break;
				}
#endif
			a2_VoiceFree(st, head);
			/*
			 * Since we may be killing voices started by scripts,
			 * we need to make sure those are detached properly!
			 */
		}
		else
		{
			a2_kill_subvoices_using_program(st, sv, p);
			head = &sv->next;
		}
	}

}

void a2_KillVoicesUsingProgram(A2_state *st, A2_handle program)
{
	/* Can't use a2_GetProgram() because we may see zero refcounts here! */
	A2_program *p;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, program);
	if(!hi || (hi->typecode != A2_TPROGRAM))
		return;
	p = (A2_program *)hi->d.data;
	if(st->parent)
		st = st->parent;
	for( ; st; st = st->next)
	{
		hi = rchm_Get(&st->ss->hm, st->rootvoice);
		if(!hi || (hi->typecode != A2_TVOICE) || (!hi->d.data))
			continue;	/* Wut? Root voice died...? */
		st->audio->Lock(st->audio);
		a2_kill_subvoices_using_program(st, (A2_voice *)hi->d.data, p);
		st->audio->Unlock(st->audio);
	}
}


int a2_LockAllStates(A2_state *st)
{
	int count = 0;
	if(st->parent)
		st = st->parent;
	for( ; st; st = st->next, ++count)
		st->audio->Lock(st->audio);
	return count;
}


int a2_UnlockAllStates(A2_state *st)
{
	int count = 0;
	if(st->parent)
		st = st->parent;
	for( ; st; st = st->next, ++count)
		st->audio->Unlock(st->audio);
	return count;
}
