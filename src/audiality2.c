/*
 * audiality2.c - Audiality 2 main file - configuration, open/close etc
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

#include <stdio.h>
#include <stdlib.h>
#include "internals.h"
#include "compiler.h"
#include "wtosc.h"
#include "inline.h"
#include "panmix.h"
#include "xinsert.h"
#include "dbgunit.h"
#include "limiter.h"
#include "fbdelay.h"
#include "filter12.h"

/* Static data for a2_GetTicks() and a2_GetMicros() */
static int time_initialized = 0;
#ifdef _WIN32
DWORD a2_start_time;
extern LARGE_INTEGER a2_perfc_frequency = 0;
#else
struct timeval a2_start_time;
#endif


/*---------------------------------------------------------
	Error handling
---------------------------------------------------------*/

A2_errors a2_last_error = A2_OK;

A2_errors a2_LastError(void)
{
	return a2_last_error;
}


/*---------------------------------------------------------
	Versioning
---------------------------------------------------------*/

unsigned a2_LinkedVersion(void)
{
	return A2_VERSION;
}


/*---------------------------------------------------------
	Engine type registry
---------------------------------------------------------*/

A2_errors a2_RegisterType(A2_state *st, A2_otypes otype, const char *name,
		RCHM_destructor_cb destroy, A2_stropen_cb stropen)
{
	A2_errors res;
	RCHM_manager *m = &st->ss->hm;
	A2_typeinfo *ti = (A2_typeinfo *)calloc(1, sizeof(A2_typeinfo));
	if(!ti)
		return A2_OOMEMORY;
	ti->state = st;
	ti->OpenStream = stropen;
	res = rchm_RegisterType(m, otype, name, destroy, ti);
	if(res)
		free(ti);
	return res;
}


static void type_registry_cleanup(A2_state *st)
{
	int i;
	RCHM_manager *m = &st->ss->hm;
	for(i = 0; i < m->ntypes; ++i)
	{
		A2_typeinfo *ti = (A2_typeinfo *)rchm_TypeUserdata(m, i);
		if(!ti)
			continue;
		free(ti);
	}
}


/*---------------------------------------------------------
	API owned and locked objects
---------------------------------------------------------*/

int a2_UnloadAll(A2_state *st)
{
	int count = 0;
	RCHM_manager *hm = &st->ss->hm;
	RCHM_handle h;
	for(h = 0; h < hm->nexthandle; ++h)
	{
		RCHM_handleinfo *hi = rchm_Get(hm, h);
		if(hi && (hi->userbits & A2_APIOWNED))
		{
			DBG(const char *s = a2_String(st, h);)
			hi->userbits &= ~A2_APIOWNED;
			if(rchm_Release(hm, h) == 0)
			{
				++count;
				DBG(fprintf(stderr, "a2_UnloadAll(): Unloaded "
						"object %d %s\n", h, s);)
			}
		}
	}
	return count;
}


/* Removed all A2_LOCKED flags and release any objects with refcount 0 */
static int a2_unlock_all(A2_state *st)
{
	int count = 0;
	RCHM_manager *hm = &st->ss->hm;
	RCHM_handle h;
	DBG(fprintf(stderr, "=== a2_unlock_all() ===\n");)
	for(h = 0; h < hm->nexthandle; ++h)
	{
		DBG(const char *s = a2_String(st, h);)
		RCHM_handleinfo *hi = rchm_Get(hm, h);
		if(hi && (hi->userbits & A2_LOCKED))
		{
			hi->userbits &= ~A2_LOCKED;
			if(hi->refcount == 0)
				if(rchm_Release(hm, h) == 0)
				{
					++count;
					DBG(fprintf(stderr, "   %d %s\n", h, s);)
				}
		}
	}
	DBG(fprintf(stderr, "=======================\n");)
	return count;
}


/*---------------------------------------------------------
	Open/close
---------------------------------------------------------*/

/* Array of builtin units to register */
static const A2_unitdesc *a2_core_units[] = {
	&a2_inline_unitdesc,
	&a2_wtosc_unitdesc,
	&a2_panmix_unitdesc,
	&a2_xinsert_unitdesc,
	&a2_dbgunit_unitdesc,
	&a2_limiter_unitdesc,
	&a2_fbdelay_unitdesc,
	&a2_filter12_unitdesc,
	NULL
};

static A2_errors a2_OpenSharedState(A2_state *st)
{
	A2_errors res;
	int i;
	st->ss = (A2_sharedstate *)calloc(1, sizeof(A2_sharedstate));
	if(!st->ss)
		return A2_OOMEMORY;

	/* Init handle manager */
	if((res = rchm_Init(&st->ss->hm, A2_INITHANDLES)))
		return res;

	/* Register handle types */
	if((res = a2_RegisterBankTypes(st)))
		return res;
	if((res = a2_RegisterWaveTypes(st)))
		return res;
	if((res = a2_RegisterAPITypes(st)))
		return res;

	/* Set up the root bank (MUST get handle 0!) */
	res = a2_NewBank(st, "root", A2_LOCKED);
	if(res != A2_ROOTBANK)
		return A2_INTERNAL + 3;	/* Houston, we have a problem...! */

	/* Render builtin waves */
	if((res = a2_InitWaves(st, A2_ROOTBANK)))
		return res;

	/* Initialize the A2S compiler */
	if((res = a2_OpenCompiler(st, st->config->flags & A2_INITFLAGS)))
		return res;

	/* Register the builtin voice units */
	for(i = 0; a2_core_units[i]; ++i)
	{
		A2_handle h = a2_RegisterUnit(st, a2_core_units[i]);
		if(h < 0)
			return -h;
		if((res = a2_Export(st, A2_ROOTBANK, h, NULL)))
			return res;
	}

	/* Compile builtin programs */
	if((res = a2_CompileString(st->ss->c, A2_ROOTBANK,
			"def square pulse50\n"
			"\n"
			"a2_rootdriver()\n"
			"{\n"
			"	struct {\n"
			"		inline 0 *\n"
			"		panmix * *\n"
			"		xinsert * >\n"
			"	}\n"
			".restart\n"
			"	d 100\n"
			"	2(V) { vol V, force restart }\n"
			"	3(PX PY PZ) { pan PX, force restart }\n"
			"}\n"
			"\n"
			"a2_groupdriver()\n"
			"{\n"
			"	struct {\n"
			"		inline 0 *\n"
			"		panmix * *\n"
			"		xinsert * >\n"
			"	}\n"
			".restart\n"
			"	d 100\n"
			"	2(V) { vol V, force restart }\n"
			"	3(PX PY PZ) { pan PX, force restart }\n"
			"}\n"
			"\n"
			"a2_terminator() {}\n", "rootbank")))
		return res;

	/* Grab frequently used objects, so we don't have to do that "live" */
	if(!(st->ss->terminator = a2_GetProgram(st,
			a2_Get(st, A2_ROOTBANK, "a2_terminator"))))
		return A2_INTERNAL + 5;

	return A2_OK;
}

static void a2_CloseSharedState(A2_state *st)
{
/*	if(!st->ss)
		return;*/
	type_registry_cleanup(st);
	rchm_Cleanup(&st->ss->hm);
	if(st->ss->c)
		a2_CloseCompiler(st->ss->c);
	free(st->ss);
	st->ss = NULL;
}


/* Create an A2_state struct, instantiate drivers as needed, and open them. */
static A2_state *a2_Open0(A2_config *config)
{
	A2_errors res;
	A2_state *st = calloc(1, sizeof(A2_state));
	if(!st)
	{
		a2_last_error = A2_OOMEMORY;
		return NULL;
	}
	st->rootvoice = -1;

	if(!config)
	{
		/* No config! Create a default one. */
		if(!(config = a2_OpenConfig(-1, -1, -1, -1)))
			return NULL;
		config->flags |= A2_STATECLOSE;
	}

	st->config = config;
	config->state = st;

	/* Get required drivers */
	st->sys = (A2_sysdriver *)a2_GetDriver(config, A2_SYSDRIVER);
	st->audio = (A2_audiodriver *)a2_GetDriver(config, A2_AUDIODRIVER);
	if(!st->sys || !st->audio)
	{
		a2_Close(st);
		return NULL;
	}

	if(!(st->config->flags & A2_SUBSTATE) && !time_initialized)
	{
/*FIXME: Move this into the sysdriver? */
#ifdef _WIN32
		timeBeginPeriod(1);
		a2_start_time = timeGetTime();
		if(!QueryPerformanceFrequency(&a2_perfc_frequency))
			a2_perfc_frequency = 0;
#else
		gettimeofday(&a2_start_time, NULL);
#endif
		++time_initialized;
	}

	/* Open drivers */
	if((res = a2_OpenDrivers(st->config, A2_STATECLOSE)))
	{
		a2_Close(st);
		a2_last_error = res;
		return NULL;
	}

	return st;
}


/* Initialize and start up the actual engine of state 'st'! */
static A2_errors a2_Open2(A2_state *st)
{
	A2_errors res;
	int i;

	/* Prepare memory block pool */
	for(i = 0; i < st->config->blockpool; ++i)
	{
		A2_block *b = st->sys->RTAlloc(st->sys, sizeof(A2_block));
		if(!b)
			return A2_OOMEMORY;
		b->next = st->blockpool;
		st->blockpool = b;
	}

	/*
	 * For master states, initialize the bank, builtin waves and a2s
	 * compiler.
	 */
	if(!(st->config->flags & A2_SUBSTATE))
	{
		if((res = a2_OpenSharedState(st)))
			return res;
	}
	else
		st->ss = st->parent->ss;

	/* Set up master audio bus */
	if(!(st->master = a2_AllocBus(st, st->config->channels)))
		return A2_OOMEMORY;

	/* Prepare initial voice pool */
	for(i = 0; i < st->config->voicepool; ++i)
	{
		A2_voice *v = a2_VoiceAlloc(st);
		if(!v)
			return A2_OOMEMORY;
		v->next = st->voicepool;
		st->voicepool = v;
	}

	/* Start the root voice! */
	st->msdur = st->config->samplerate * 65.536f + .5f;
	if((res = a2_init_root_voice(st)))
		return res;

	/* Initialize the realtime control API */
	if((res = a2_OpenAPI(st)))
		return res;
	st->now_ticks = a2_GetTicks();
	st->now_micros = st->avgstart = a2_GetMicros();
	st->noisestate = A2_NOISESEED;

	/* Install the master process callback! */
	st->audio->Lock(st->audio);
	st->audio->state = st;
	st->audio->Process = a2_AudioCallback;
	st->audio->Unlock(st->audio);

	return A2_OK;
}


A2_state *a2_Open(A2_config *config)
{
	A2_errors res;
	A2_state *st;
	a2_last_error = A2_OK;
	DUMPSIZES(
		printf("A2_wave:\t%d\n", sizeof(A2_wave));
		printf("A2_bank:\t%d\n", sizeof(A2_bank));
		printf("A2_program:\t%d\n", sizeof(A2_program));
		printf("A2_string:\t%d\n", sizeof(A2_string));
		printf("A2_stackentry:\t%d\n", sizeof(A2_stackentry));
		printf("A2_event:\t%d\n", sizeof(A2_event));
		printf("A2_voice:\t%d\n", sizeof(A2_voice));
		printf("A2_block:\t%d\n", sizeof(A2_block));
		printf("A2_unit:\t%d\n", sizeof(A2_unit));
		printf("A2_unitdesc:\t%d\n", sizeof(A2_unitdesc));
		printf("A2_stream:\t%d\n", sizeof(A2_stream));
	)
#ifdef DEBUG
	if(config)
	{
		printf("a2_Open(%p) ------\n", config);
		a2_DumpConfig(config);
		printf("------\n");
	}
	else
		printf("a2_Open(NULL)\n");
#endif
	if(!(st = a2_Open0(config)))
		return NULL;
	if((res = a2_Open2(st)))
	{
		a2_Close(st);
		fprintf(stderr, "Audiality 2: Initialization failed; %s!\n",
				a2_ErrorString(res));
		a2_last_error = res;
		return NULL;
	}
#ifdef DEBUG
	printf("a2_Open(), resulting config: ------\n", st->config);
	a2_DumpConfig(st->config);
	printf("------\n");
#endif
	a2_Now(st);
	return st;
}


A2_state *a2_SubState(A2_state *parent, A2_config *config)
{
	A2_errors res;
	A2_state *st;
	a2_last_error = A2_OK;

	/*
	 * Substate of substate is "ok", but becomes another substate of the
	 * same master state.
	 */
	if(parent->parent)
		parent = parent->parent;

	/*
	 * If no config is provided, create a typical offline streaming setup
	 * based on the master state configuration!
	 */
	if(!config)
	{
		config = a2_OpenConfig(parent->config->samplerate, 256,
				parent->config->channels, 0);
		if(!config)
			return NULL;	/* Most likely not going to work! --> */
		if((res = a2_AddDriver(config,
				a2_NewDriver(A2_AUDIODRIVER, "stream"))))
		{
			a2_CloseConfig(config);
			a2_last_error = res;
			return NULL;
		}
		config->flags |= A2_STATECLOSE;	/* We close what we open! */
	}
	if(!config)
		return NULL;	/* Most likely not going to work! --> */

	/*
	 * This flag wires some stuff to the master state instead of
	 * initializing local instances.
	 */
	config->flags |= A2_SUBSTATE;

	if(!(st = a2_Open0(config)))
		return NULL;

	/* Link the substate to the master state */
	st->parent = parent;
	st->next = parent->next;
	parent->next = st;

	if((res = a2_Open2(st)))
	{
		a2_Close(st);
		fprintf(stderr, "Audiality 2: Initialization failed; %s!\n",
				a2_ErrorString(res));
		a2_last_error = res;
		return NULL;
	}
	return st;
}


void a2_Close(A2_state *st)
{
	int i;

	/* Unload any "forgotten" API created objects... */
	a2_UnloadAll(st);

	/* First, close all substates! */
	if(st->next)
		a2_Close(st->next);

	/* Detach the audio callack */
	if(st->audio)
	{
		A2_audiodriver *d = st->audio;
		if(d->driver.flags & A2_ISOPEN)
		{
			d->Lock(d);
			d->state = NULL;
			d->Process = NULL;
			d->Unlock(d);
		}
	}

	/* Close the realtime context of the engine */
	a2_CloseAPI(st);
	if(st->rootvoice >= 0)
	{
		RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, st->rootvoice);
		if(hi && hi->d.data)
			a2_VoiceFree(st, (A2_voice **)&hi->d.data);
		rchm_Free(&st->ss->hm, st->rootvoice);
	}
	for(i = 0; i < A2_NESTLIMIT; ++i)
		if(st->scratch[i])
			a2_FreeBus(st, st->scratch[i]);
	if(st->master)
		a2_FreeBus(st, st->master);
	while(st->voicepool)
	{
		A2_voice *v = st->voicepool;
		st->voicepool = v->next;
		st->sys->RTFree(st->sys, v);
	}
	while(st->blockpool)
	{
		A2_block *b = st->blockpool;
		st->blockpool = b->next;
		st->sys->RTFree(st->sys, b);
	}

	/* If this is a master state... */
	if(!st->parent)
	{
		/* Unload the root bank and any other A2_LOCKed objects! */
		a2_unlock_all(st);
		a2_Release(st, A2_ROOTBANK);

		/* Close the bank and non realtime parts of the state */
		a2_CloseSharedState(st);
		--time_initialized;
#ifdef _WIN32
		if(!time_initialized)
			timeEndPeriod(1);
#endif
	}

	/* Close the A2_config, if we created it! */
	if(st->config)
	{
		/* Close any drivers that we're supposed to close */
		a2_CloseDrivers(st->config, A2_STATECLOSE);

		/* Close if we're supposed to, otherwise, detach from state! */
		if(st->config->flags & A2_STATECLOSE)
			a2_CloseConfig(st->config);
		else
		{
			/* Just detach it from the state! */
			st->config->state = NULL;
			st->config = NULL;
		}
	}

	/* Detach from master state, if any */
	if(st->parent)
	{
		A2_state *s = st->parent->next;
		A2_state *ps = NULL;
		while(s)
		{
			if(s == st)
			{
				if(ps)
					ps->next = st->next;
				else
					st->parent->next = st->next;
				break;
			}
			ps = s;
			s = s->next;
		}
	}

	free(st);
}
