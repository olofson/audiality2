/*
 * streamdrv.c - Audiality 2 streaming audio driver
 *
 * Copyright 2012-2013 David Olofson <david@olofson.net>
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

#include <stdlib.h>
#include <string.h>
#include "streamdrv.h"


static A2_errors streamd_Run(A2_audiodriver *driver, unsigned frames)
{
	A2_config *cfg = driver->driver.config;
	if(driver->Process)
		driver->Process(driver, frames);
	else
	{
		int c;
		for(c = 0; c < cfg->channels; ++c)
			memset(driver->buffers[c], 0, sizeof(int32_t) * frames);
	}
	return A2_OK;
}


static void streamd_Lock(A2_audiodriver *driver)
{
}


static void streamd_Unlock(A2_audiodriver *driver)
{
}


static void streamd_Close(A2_driver *driver)
{
	A2_audiodriver *ad = (A2_audiodriver *)&driver;
	A2_config *cfg = driver->config;
	if(ad->buffers)
	{
		int c;
		for(c = 0; c < cfg->channels; ++c)
			free(ad->buffers[c]);
		free(ad->buffers);
		memset(ad->buffers, 0, sizeof(ad->buffers));
	}
	ad->Run = NULL;
	ad->Lock = NULL;
	ad->Unlock = NULL;
}


static A2_errors streamd_Open(A2_driver *driver)
{
	A2_audiodriver *ad = (A2_audiodriver *)&driver;
	A2_config *cfg = driver->config;
	int c;
	ad->Run = streamd_Run;
	ad->Lock = streamd_Lock;
	ad->Unlock = streamd_Unlock;
	if(!(ad->buffers = calloc(cfg->channels, sizeof(int32_t *))))
	{
		streamd_Close(driver);
		return A2_OOMEMORY;
	}
	for(c = 0; c < cfg->channels; ++c)
		if(!(ad->buffers[c] = calloc(cfg->buffer, sizeof(int32_t))))
			{
				streamd_Close(driver);
				return A2_OOMEMORY;
			}
	return A2_OK;
}


A2_driver *a2_stream_audiodriver(A2_drivertypes type, const char *name)
{
	A2_audiodriver *d = calloc(1, sizeof(A2_audiodriver));
	if(!d)
		return NULL;
	d->driver.type = A2_AUDIODRIVER;
	d->driver.name = "stream";
	d->driver.Open = streamd_Open;
	d->driver.Close = streamd_Close;
	return &d->driver;
}
