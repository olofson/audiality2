/*
 * mallocdrv.c - Audiality 2 malloc system driver
 *
 * Copyright 2012-2013, 2017 David Olofson <david@olofson.net>
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
#include "mallocdrv.h"
#include "a2_log.h"


static void *mallocsd_RTAlloc(A2_sysdriver *driver, unsigned size)
{
#ifdef DEBUG
	A2_sysdriver **b = malloc(size + sizeof(A2_sysdriver *));
	if(!b)
		return NULL;
	b[0] = driver;
	return b + 1;
#else
	return (void *)malloc(size);
#endif
}

static void mallocsd_RTFree(A2_sysdriver *driver, void *block)
{
#ifdef DEBUG
	A2_interface *i = driver->driver.config->interface;
	A2_sysdriver **b = ((A2_sysdriver **)block) - 1;
	if(b[0] == driver)
		free(b);
	else
		A2_LOG_DBG(i, "defaultsd_RTFree(): Attempted to free block "
				"%p with driver %p, but the block belongs to "
				"driver %p!", block, driver, b[0]);
#else
	free(block);
#endif
}

static A2_errors mallocsd_Open(A2_driver *driver)
{
	A2_sysdriver *sd = (A2_sysdriver *)driver;
	sd->RTAlloc = mallocsd_RTAlloc;
	sd->RTFree = mallocsd_RTFree;
	return A2_OK;
}

static void mallocsd_Close(A2_driver *driver)
{
	A2_sysdriver *sd = (A2_sysdriver *)driver;
	sd->RTAlloc = NULL;
	sd->RTFree = NULL;
}

A2_driver *a2_malloc_sysdriver(A2_drivertypes type, const char *name)
{
	A2_sysdriver *sd = calloc(1, sizeof(A2_sysdriver));
	A2_driver *d = &sd->driver;
	if(!sd)
		return NULL;
	d->type = A2_SYSDRIVER;
	d->name = "malloc";
	d->Open = mallocsd_Open;
	d->Close = mallocsd_Close;
	return d;
}
