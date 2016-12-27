/*
 * streamtest.c - Test of asynchronous streaming via xsink and xsource
 *
 * Copyright 2014-2016 David Olofson <david@olofson.net>
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
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include "audiality2.h"

/* Fragment size for wave rendering/uploading */
#define	FRAGSIZE	256

/* Stream poll/write period (ms) */
#define	POLLPERIOD	100

/* Stream buffer size (ms) */
#define	STREAMBUFFER	500

/* Capture buffer size (ms) */
#define	CAPTUREBUFFER	5000

/* Configuration */
const char *audiodriver = "default";
int samplerate = 48000;
int channels = 2;
int audiobuf = 4096;

static int do_exit = 0;


static void usage(const char *exename)
{
	fprintf(stderr,	"\n\nUsage: %s [switches] <file>\n\n", exename);
	fprintf(stderr, "Switches:  -d<name>[,opt[,opt[,...]]]\n"
			"                       Audio driver + options\n"
			"           -b<n>       Audio buffer size (frames)\n"
			"           -r<n>       Audio sample rate (Hz)\n"
			"           -c<n>       Number of audio channels\n\n"
			"           -h          Help\n\n");
}


static void parse_args(int argc, const char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		if(argv[i][0] != '-')
			continue;
		if(strncmp(argv[i], "-d", 2) == 0)
		{
			audiodriver = &argv[i][2];
			printf("[Driver: %s]\n", audiodriver);
		}
		else if(strncmp(argv[i], "-b", 2) == 0)
		{
			audiobuf = atoi(&argv[i][2]);
			printf("[Buffer: %d]\n", audiobuf);
		}
		else if(strncmp(argv[i], "-r", 2) == 0)
		{
			samplerate = atoi(&argv[i][2]);
			printf("[Sample rate: %d]\n", samplerate);
		}
		else if(strncmp(argv[i], "-c", 2) == 0)
		{
			channels = atoi(&argv[i][2]);
			printf("[Channels %d]\n", channels);
		}
		else if(strncmp(argv[i], "-h", 2) == 0)
		{
			usage(argv[0]);
			exit(0);
		}
		else
		{
			fprintf(stderr, "Unknown switch '%s'!\n", argv[i]);
			exit(1);
		}
	}
}


static void breakhandler(int a)
{
	fprintf(stderr, "Stopping...\n");
	do_exit = 1;
}


static void fail(unsigned where, A2_errors err)
{
	fprintf(stderr, "ERROR at %d: %s\n", where, a2_ErrorString(err));
	exit(100);
}


int main(int argc, const char *argv[])
{
	A2_errors res;
	A2_handle h, songh, streamh;
	A2_handle captureprogram;
	A2_handle streamprogram;
	A2_driver *drv;
	A2_config *cfg;
	A2_interface *iface;
	int32_t *buffer;
	unsigned length, position;
	int n;

	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Command line switches */
	parse_args(argc, argv);

	/* Configure and open master state */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, audiodriver)))
		fail(1, a2_LastError());
	if(!(cfg = a2_OpenConfig(samplerate, audiobuf, channels,
			A2_TIMESTAMP | A2_AUTOCLOSE)))
		fail(2, a2_LastError());
	if(drv && a2_AddDriver(cfg, drv))
		fail(3, a2_LastError());
	if(!(iface = a2_Open(cfg)))
		fail(4, a2_LastError());
	if(samplerate != cfg->samplerate)
		printf("Actual master state sample rate: %d (requested %d)\n",
				cfg->samplerate, samplerate);

	/* Load jingle */
	if((h = a2_Load(iface, "data/a2jingle.a2s", 0)) < 0)
		fail(5, -h);
	if((songh = a2_Get(iface, h, "Song")) < 0)
		fail(6, -songh);

	/* Load test programs */
	if((h = a2_Load(iface, "data/testprograms.a2s", 0)) < 0)
		fail(7, -h);
	if((captureprogram = a2_Get(iface, h, "CaptureVoice")) < 0)
		fail(8, -captureprogram);
	if((streamprogram = a2_Get(iface, h, "StreamVoice")) < 0)
		fail(9, -streamprogram);

	/* Allocate capture buffer */
	length = samplerate * CAPTUREBUFFER / 1000;
	buffer = malloc(length * sizeof(int32_t));
	if(!buffer)
		fail(10, A2_OOMEMORY);

	/* Record some audio from a CaptureVoice */
	fprintf(stderr, "Capturing %d sample frames...\n", length);
	position = 0;
	a2_TimestampReset(iface);
	if((h = a2_Start(iface, a2_RootVoice(iface), captureprogram)) < 0)
		fail(11, -h);
	if((streamh = a2_OpenSink(iface, h, 0,
			 samplerate * STREAMBUFFER / 1000, 0)) < 0)
		fail(12, -streamh);
	a2_Play(iface, h, songh);
	while(!do_exit && (position < length))
	{
		a2_TimestampReset(iface);
		while((n = a2_Available(iface, streamh)) > 0)
		{
			if(n > length - position)
			{
				n = length - position;
				if(!n)
					break;
			}
			if((res = a2_Read(iface, streamh, A2_I24,
					buffer + position,
					n * sizeof(int32_t))))
				fail(13, res);
			position += n;
		}
		if(n < 0)
			fail(14, -n);
		fprintf(stderr, "[%d]\n", position);
		a2_Sleep(POLLPERIOD);
		a2_PumpMessages(iface);
	}
	a2_TimestampReset(iface);
	a2_Kill(iface, h);

	/* Play back through a StreamVoice */
	fprintf(stderr, "Playing...\n");
	position = 0;
	a2_TimestampReset(iface);
	if((h = a2_Start(iface, a2_RootVoice(iface), streamprogram)) < 0)
		fail(15, -h);
	if((streamh = a2_OpenSource(iface, h,
			0, samplerate * STREAMBUFFER / 1000, 0)) < 0)
		fail(16, -streamh);
	while(!do_exit && (position < length))
	{
		a2_TimestampReset(iface);
		while((n = a2_Space(iface, streamh)) > 0)
		{
			if(n > length - position)
			{
				n = length - position;
				if(!n)
					break;
			}
			if((res = a2_Write(iface, streamh, A2_I24,
					buffer + position,
					n * sizeof(int32_t))))
				fail(17, res);
			position += n;
		}
		if(n < 0)
			fail(18, -n);
		fprintf(stderr, "[%d]\n", position);
		a2_Sleep(POLLPERIOD);
		a2_PumpMessages(iface);
	}

	fprintf(stderr, "Waiting for stream buffer to drain...\n");
	while((n = a2_Available(iface, streamh)))
	{
		fprintf(stderr, "[%d]\n", n);
		a2_Sleep(POLLPERIOD);
		a2_PumpMessages(iface);
	}

	fprintf(stderr, "Done!\n");

	free(buffer);
	a2_Close(iface);
	return 0;
}
