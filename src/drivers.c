/*
 * drivers.c - Audiality 2 device driver and configuration interfaces
 *
 * Copyright 2012 David Olofson <david@olofson.net>
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

#ifdef DEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "internals.h"

/* Builtin system drivers */
#include "mallocdrv.h"

/* Builtin audio drivers */
#include "sdldrv.h"
#include "jackdrv.h"
#include "dummydrv.h"
#include "streamdrv.h"

#ifdef A2_HAVE_SDL
# define A2_DEFAULT_AUDIODRIVER	a2_sdl_audiodriver
#elif defined(A2_HAVE_JACK)
# define A2_DEFAULT_AUDIODRIVER	a2_jack_audiodriver
#else
# define A2_DEFAULT_AUDIODRIVER	a2_dummy_audiodriver
#endif


/*---------------------------------------------------------
	Configuration
---------------------------------------------------------*/

void a2_DumpConfig(A2_config *c)
{
#ifdef DEBUG
	A2_driver *d = c->drivers;
	printf("    samplerate: %d\n", c->samplerate);
	printf("        buffer: %d\n", c->buffer);
	printf("      channels: %d\n", c->channels);
	printf("         flags:");
	if(c->flags & A2_EXPORTALL) printf(" EXPORTALL");
	if(c->flags & A2_TIMESTAMP) printf(" TIMESTAMP");
	if(c->flags & A2_NOAUTOCNX) printf(" NOAUTOCNX");
	if(c->flags & A2_REALTIME) printf(" REALTIME");
	if(c->flags & A2_SUBSTATE) printf(" SUBSTATE");
	if(c->flags & A2_ISOPEN) printf(" ISOPEN");
	if(c->flags & A2_STATECLOSE) printf(" STATECLOSE");
	if(c->flags & A2_CFGCLOSE) printf(" CFGCLOSE");
	printf("\n");
	printf("      poolsize: %d\n", c->poolsize);
	printf("     blockpool: %d\n", c->blockpool);
	printf("     voicepool: %d\n", c->voicepool);
	printf("   eventpool: %d\n", c->eventpool);
	printf("       drivers:\n");
	while(d)
	{
		const char *dt;
		switch(d->type)
		{
		  case A2_SYSDRIVER:	dt = "SYS"; break;
		  case A2_AUDIODRIVER:	dt = "AUDIO"; break;
		  default:		dt = "<unknown>"; break;
		}
		printf("           \"%s\" (%s)\n", d->name, dt);
		d = d->next;
	}
#endif
}


A2_config *a2_OpenConfig(int samplerate, int buffer, int channels, int flags)
{
	A2_config *c = (A2_config *)calloc(1, sizeof(A2_config));
	a2_last_error = A2_OK;
	if(!c)
	{
		a2_last_error = A2_OOMEMORY;
		return NULL;
	}

	/* Flags first, as they affect the defaults for other parameters! */
	if(flags >= 0)
		c->flags = flags;
	else
		c->flags = A2_TIMESTAMP | A2_REALTIME;

	if(samplerate >= 0)
		c->samplerate = samplerate;
	else
		c->samplerate = 48000;
	if(buffer >= 0)
		c->buffer = buffer;
	else
		c->buffer = 1024;
	if(channels >= 0)
		c->channels = channels;
	else
		c->channels = 2;

	/* We set up initial pools only for realtime states! */
	if(c->flags & A2_REALTIME)
	{
		c->blockpool = A2_INITBLOCKS;
		c->voicepool = A2_INITVOICES;
		c->eventpool = -1;
	}

#ifdef DEBUG
	printf("Created A2_config %p: ------\n", c);
	a2_DumpConfig(c);
	printf("------\n");
#endif
	return c;
}


A2_errors a2_AddDriver(A2_config *config, A2_driver *driver)
{
	a2_last_error = A2_OK;
	/* Just so we can safely nest a non-safe a2_GetBuiltinDriver() call */
	if(!driver)
		return (a2_last_error =  A2_NODRIVER);
	driver->next = config->drivers;
	config->drivers = driver;
	driver->config = config;
	return A2_OK;
}


A2_driver *a2_GetDriver(A2_config *config, A2_drivertypes type)
{
	A2_errors res;
	A2_driver *d;
	a2_last_error = A2_OK;
	for(d = config->drivers; d; d = d->next)
		if(d->type == type)
			return d;
	if(!(d = a2_NewDriver(type, NULL)))
		return NULL;
	if((res = a2_AddDriver(config, d)))
	{
		a2_last_error = res;
		d->Destroy(d);
		return NULL;
	}
	return d;
}


A2_errors a2_OpenDrivers(A2_config *config, int flags)
{
	A2_errors res;
	A2_driver *d;
	a2_last_error = A2_OK;
	for(d = config->drivers; d; d = d->next)
		if((res = a2_OpenDriver(d, flags)))
			return res;
	return A2_OK;
}


A2_errors a2_CloseDrivers(A2_config *config, int mask)
{
	A2_driver *d;
	for(d = config->drivers; d; d = d->next)
	{
		if(mask & !(mask & d->flags))
			continue;	/* Not a match! */
		a2_CloseDriver(d);
	}
	return A2_OK;
}


static void a2_destroy_drivers(A2_config *config)
{
	while(config->drivers)
	{
		A2_driver *d = config->drivers;
		config->drivers = d->next;
		d->Destroy(d);
	}
}


void a2_CloseConfig(A2_config *config)
{
	a2_CloseDrivers(config, 0);
	a2_destroy_drivers(config);
	if(config->state)
		config->state->config = NULL;	/* Detach from state! */
	free(config);
}


/*---------------------------------------------------------
	Drivers
---------------------------------------------------------*/

A2_errors a2_OpenDriver(A2_driver *driver, int flags)
{
	A2_errors res;
	if(driver->flags & A2_ISOPEN)
		return A2_ALREADYOPEN;
	driver->flags |= driver->config->flags & A2_INITFLAGS;
	if((res = driver->Open(driver)))
		return res;
	driver->flags |= A2_ISOPEN | flags;
	return A2_OK;
}

void a2_CloseDriver(A2_driver *driver)
{
	if(driver->flags & A2_ISOPEN)
	{
		driver->Close(driver);
		driver->flags &= ~A2_ISOPEN;
	}
}


/*---------------------------------------------------------
	Driver registry
---------------------------------------------------------*/

typedef struct A2_regdriver A2_regdriver;
struct A2_regdriver
{
	A2_regdriver	*next;
	A2_drivertypes	type;
	const char	*name;
	A2_newdriver_cb	create;
};

A2_regdriver *a2_driver_registry = NULL;


static void a2_register_builtin_drivers(void)
{
	a2_RegisterDriver(A2_SYSDRIVER, "default", a2_malloc_sysdriver);
	a2_RegisterDriver(A2_SYSDRIVER, "malloc", a2_malloc_sysdriver);
/*FIXME*/a2_RegisterDriver(A2_SYSDRIVER, "realtime", a2_malloc_sysdriver);

	a2_RegisterDriver(A2_AUDIODRIVER, "default", A2_DEFAULT_AUDIODRIVER);
#ifdef A2_HAVE_SDL
	a2_RegisterDriver(A2_AUDIODRIVER, "sdl", a2_sdl_audiodriver);
#endif
#ifdef A2_HAVE_JACK
	a2_RegisterDriver(A2_AUDIODRIVER, "jack", a2_jack_audiodriver);
#endif
	a2_RegisterDriver(A2_AUDIODRIVER, "dummy", a2_dummy_audiodriver);
	a2_RegisterDriver(A2_AUDIODRIVER, "stream", a2_stream_audiodriver);
}


A2_errors a2_RegisterDriver(A2_drivertypes type, const char *name,
		A2_newdriver_cb create)
{
	A2_regdriver *rd = (A2_regdriver *)calloc(1, sizeof(A2_regdriver));
	if(!rd)
		return A2_OOMEMORY;
	rd->type = type;
	rd->name = strdup(name);
	if(!rd->name)
	{
		free(rd);
		return A2_OOMEMORY;
	}
	rd->create = create;
	rd->next = a2_driver_registry;
	a2_driver_registry = rd;
	return A2_OK;
}


A2_driver *a2_NewDriver(A2_drivertypes type, const char *name)
{
	A2_regdriver *rd;
	a2_last_error = A2_OK;
	if(!a2_driver_registry)
		a2_register_builtin_drivers();
	if(!name)
		name = "default";
	for(rd = a2_driver_registry; rd; rd = rd->next)
		if((rd->type == type) && !strcmp(rd->name, name))
			return rd->create(type, name);
	a2_last_error = A2_DRIVERNOTFOUND;
	return NULL;
}
