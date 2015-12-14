/*
 * coreaudiodrv.c - Audiality 2 Core Audio driver
 *
 * Copyright 2015 Jonathan Howard <j@hovverd.com>
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

#ifdef __APPLE__

#include <CoreAudio/CoreAudioTypes.h>
#include <AudioToolbox/AudioQueue.h>

#include "coreaudiodrv.h"
#include "platform.h"

/* Extended A2_audiodriver struct */
static const int kNumBuffers = 2;
typedef struct CoreAudio_audiodriver
{
	A2_audiodriver              ad;
	AudioQueueRef               queue;
	AudioQueueBufferRef         buffer[kNumBuffers];
	AudioStreamBasicDescription desc;
} CoreAudio_audiodriver;

/* CoreAudio render thread callback */
static void coreaudio_process(void * inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
{
	A2_audiodriver *driver = (A2_audiodriver *)inUserData;
	CoreAudio_audiodriver * cad = (CoreAudio_audiodriver *)inUserData;
	A2_config * config = driver->driver.config;
	int c, i;

	int frames = config->buffer;

	if( driver->Process )
	{
		driver->Process(driver, config->buffer);
	}
	else
	{
		for ( c = 0; c < config->channels; c++ )
		{
			memset(driver->buffers[c], 0, sizeof(int32_t) * config->buffer);
		}
	}

	for ( i = 0; i < frames; i++ )
	{
		for ( c = 0; c < config->channels; c++ )
		{
			((int32_t *)inBuffer->mAudioData)[i + c] = driver->buffers[c][i];
		}
	}
	inBuffer->mAudioDataByteSize = inBuffer->mAudioDataBytesCapacity;

	AudioQueueEnqueueBuffer(cad->queue, inBuffer, 0, NULL);
}

static void coreaudiod_Close(A2_driver *driver)
{
	CoreAudio_audiodriver * cad = (CoreAudio_audiodriver *)driver;

	AudioQueueStop(cad->queue, true);

	if ( cad->ad.buffers )
	{
		int c;
		for ( c = 0; c < cad->ad.driver.config->channels; c++ )
			free( cad->ad.buffers[c] );
		free( cad->ad.buffers );
	}

	AudioQueueDispose(cad->queue, true);

	cad->ad.Run = NULL;
	cad->ad.Lock = NULL;
	cad->ad.Unlock = NULL;
}

static void coreaudio_Lock(A2_audiodriver * driver)
{
}

static void coreaudio_Unlock(A2_audiodriver * driver)
{
}

static A2_errors coreaudiod_Open(A2_driver *driver)
{
	CoreAudio_audiodriver * drv         = (CoreAudio_audiodriver *)driver;
	A2_config * config                  = drv->ad.driver.config;
	AudioStreamBasicDescription * desc  = &drv->desc;
	OSStatus err;
	int c, i;

	/* allocate Audiality2 buffers */
	if ( !(drv->ad.buffers = calloc(config->channels, sizeof(int32_t *))))
		return A2_OOMEMORY;
	for ( c = 0; c < config->channels; c++ )
	{
		if ( !(drv->ad.buffers[c] = calloc(config->buffer, sizeof(int32_t))))
			return A2_OOMEMORY;
	}

	/* a2 callbacks */
	drv->ad.Run = NULL;
	drv->ad.Lock = coreaudio_Lock;
	drv->ad.Unlock = coreaudio_Unlock;

	config->buffer = 4096;

	/* set up stream description */
	desc->mSampleRate        = (Float64)config->samplerate;
	desc->mFormatID          = kAudioFormatLinearPCM;
	desc->mFormatFlags       = kAudioFormatFlagIsSignedInteger;

	desc->mFramesPerPacket   = 1;
	desc->mChannelsPerFrame  = config->channels;

	/* packet -> frame -> channel -> data */
	desc->mBytesPerFrame     = desc->mChannelsPerFrame * sizeof(int32_t);
	desc->mBytesPerPacket    = desc->mBytesPerFrame * desc->mFramesPerPacket;
	desc->mBitsPerChannel    = 24; /* 8:24 PCM */

	/* set up queue */
	err = AudioQueueNewOutput(
						desc,               // data format
						coreaudio_process,  // callback
						driver,             // data passed to callback
						NULL,               // internal run loop
						NULL,               // kCFRunLoopCommonMode
						0,                  // reserved by Apple
						&drv->queue         // queue output
					);
	if ( err ) goto error;

	err = AudioQueueStart(drv->queue, NULL);

	for ( i = 0; i < kNumBuffers; i++ )
	{
		/* internal buffer */
		err = AudioQueueAllocateBuffer(
							drv->queue,
							desc->mBytesPerPacket * config->buffer,
							&drv->buffer[i]
						);

		printf("allocating buffer %i: ------\n"
			"\t        mBytesPerPacket: %i\n"
			"\tmAudioDataBytesCapacity: %i\n"
			,
			i,
			desc->mBytesPerPacket,
			drv->buffer[i]->mAudioDataBytesCapacity);

		if (err) goto error;

		/* start callback polling */
		drv->buffer[i]->mAudioDataByteSize = drv->buffer[i]->mAudioDataBytesCapacity;
		err = AudioQueueEnqueueBuffer(drv->queue, drv->buffer[i], 0, NULL);
		if (err) goto error;
	}
	return A2_OK;

error:
	fprintf(stderr, "Audiality 2: Cannot activate CoreAudio driver!\n");
    printf("CoreAudio driver.\n");
	coreaudiod_Close(&drv->ad.driver);
	return A2_DEVICEOPEN;
}

A2_driver *a2_coreaudio_audiodriver(A2_drivertypes type, const char *name)
{
	CoreAudio_audiodriver * cad = calloc(1, sizeof(CoreAudio_audiodriver));
	A2_driver * d = &cad->ad.driver;

	if (!d)
		return NULL;

	d->type  = A2_AUDIODRIVER;
	d->name  = "coreaudio";
	d->Open  = coreaudiod_Open;
	d->Close = coreaudiod_Close;
	d->flags = A2_REALTIME;

	return d;
}

#endif // __APPLE__
