/*
 * renderwave.c - Audiality 2 render-to-wave via low level API
 *
 *	This test sets up a realtime iface with an off-line substate, uses the
 *	latter to render sound into a wave, and then plays that on the realtime
 *	iface.
 *
 * Copyright 2013-2016 David Olofson <david@olofson.net>
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
#include "audiality2.h"

/* Configuration */
typedef struct TEST_settings {
	const char	*audiodriver;
	int		samplerate;
	int		channels;
	int		audiobuf;
} TEST_settings;

static TEST_settings settings[2] = {
	{
		"default", 48000, 2, 4096
	},
	{
		"buffer", 48000, 1, 1024
	}
};

static int do_exit = 0;


static void usage(const char *exename)
{
	fprintf(stderr,	"\n\nUsage: %s [switches] <file>\n\n", exename);
	fprintf(stderr, "Switches:  -d[s]<name>[,opt[,opt[,...]]]\n"
			"                       Audio driver + options\n"
			"           -b[s]<n>    Audio buffer size (frames)\n"
			"           -r[s]<n>    Audio sample rate (Hz)\n"
			"           -c[s]<n>    Number of audio channels\n\n"
			"                       's' is 1 or unpsecified for\n"
			"                       the master iface, and 2 for\n"
			"                       the substate.\n\n"
			"           -h          Help\n\n");
}


/* Parse driver selection and configuration switches */
static void parse_args(int argc, const char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		TEST_settings *s;
		int si = 0;
		int skip = 0;
		if(argv[i][0] != '-')
			continue;
		if((strlen(argv[i]) >= 3) &&
				(argv[i][2] >= '1') && (argv[i][2] <= '2'))
		{
			skip = 1;
			si = argv[i][2] - '1';
		}
		s = settings + si;
		if(strncmp(argv[i], "-d", 2) == 0)
		{
			s->audiodriver = &argv[i][2 + skip];
			printf("[Driver %d: %s]\n", si + 1, s->audiodriver);
		}
		else if(strncmp(argv[i], "-b", 2) == 0)
		{
			s->audiobuf = atoi(&argv[i][2 + skip]);
			printf("[Buffer %d: %d]\n", si + 1, s->audiobuf);
		}
		else if(strncmp(argv[i], "-r", 2) == 0)
		{
			s->samplerate = atoi(&argv[i][2 + skip]);
			printf("[Sample rate %d: %d]\n", si + 1, s->samplerate);
		}
		else if(strncmp(argv[i], "-c", 2) == 0)
		{
			s->channels = atoi(&argv[i][2 + skip]);
			printf("[Channels %d: %d]\n", si + 1, s->channels);
		}
		else if(strncmp(argv[i], "-h", 2) == 0)
		{
			usage(argv[0]);
			exit(0);
		}
		else
		{
			fprintf(stderr, "Unknown switch '%s'!\n", argv[i]);
			usage(argv[0]);
			exit(1);
		}
	}
}


static void breakhandler(int a)
{
	printf("Stopping...\n");
	do_exit = 1;
}


static void fail(unsigned where, A2_errors err)
{
	fprintf(stderr, "ERROR at %d: %s\n", where, a2_ErrorString(err));
	exit(100);
}


static A2_handle render_wave(A2_interface *iface, A2_handle h)
{
	int res;
	A2_handle wh, sh;
	A2_driver *drv;
	A2_config *cfg;
	A2_interface *ssiface;
	int frames = 0;

	/* Configure and open substate */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, settings[1].audiodriver)))
		return -a2_LastError();
	if(!(cfg = a2_OpenConfig(settings[1].samplerate, settings[1].audiobuf,
			settings[1].channels, A2_AUTOCLOSE)))
		return -a2_LastError();
	if(drv && a2_AddDriver(cfg, drv))
		return -a2_LastError();
	if(!(ssiface = a2_SubState(iface, cfg)))
		return -a2_LastError();
	if(settings[1].samplerate != cfg->samplerate)
		printf("Actual substate sample rate: %d (requested %d)\n",
				cfg->samplerate, settings[1].samplerate);

	/* Create target wave */
	wh = a2_NewWave(iface, A2_WWAVE, cfg->samplerate / A2_MIDDLEC, 0);
	if(wh < 0)
	{
		fprintf(stderr, "a2_WaveNew() failed!\n");
		a2_Close(ssiface);
		return wh;
	}

	/* Open stream to write to the target wave */
	if((sh = a2_OpenStream(iface, wh, 0, 0, 0)) < 0)
	{
		a2_Close(ssiface);
		a2_Release(iface, wh);
		return sh;
	}

	/* Start sound! */
	a2_Play(ssiface, a2_RootVoice(ssiface), h);

	/* Render... */
	while(1)
	{
		int i;
		int32_t *buf = ((A2_audiodriver *)drv)->buffers[0];
		int32_t max = 0x80000000;
		if((res = a2_Run(ssiface, cfg->buffer)) < 0)
		{
			fprintf(stderr, "a2_Run() failed!\n");
			a2_Close(ssiface);
			a2_Release(iface, wh);
			return res;
		}
		for(i = 0; i < cfg->buffer; ++i)
			if(buf[i] > max)
				max = buf[i];
			else if(-buf[i] > max)
				max = -buf[i];
		if((frames > 1000) && (max < 256))
			break;
		frames += cfg->buffer;
		if((res = a2_Write(iface, sh, A2_I24, buf,
				cfg->buffer * sizeof(int32_t))))
		{
			fprintf(stderr, "a2_Write() failed!\n");
			a2_Close(ssiface);
			a2_Release(iface, sh);
			a2_Release(iface, wh);
			return -res;
		}
	}

	/* Close substate */
	a2_Close(ssiface);

	/*
	 * Prepare and return wave.
	 *
	 * NOTE: This isn't needed here, as flushing is done automatically
	 *       as a stream is closed. a2_Flush() is really for when you
	 *       want to keep the stream open to modify the wave later.
	 */
	if((res = a2_Flush(iface, sh)))
	{
		fprintf(stderr, "a2_Flush() failed!\n");
		a2_Release(iface, sh);
		a2_Release(iface, wh);
		return -res;
	}	

	a2_Release(iface, sh);
	return wh;
}


int main(int argc, const char *argv[])
{
	A2_handle h, songh, ph, vh;
	A2_driver *drv;
	A2_config *cfg;
	A2_interface *iface;
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Command line switches */
	parse_args(argc, argv);

	/* Configure and open master state */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, settings[0].audiodriver)))
		fail(1, a2_LastError());
	if(!(cfg = a2_OpenConfig(settings[0].samplerate, settings[0].audiobuf,
			settings[0].channels,
			A2_TIMESTAMP | A2_AUTOCLOSE)))
		fail(2, a2_LastError());
	if(drv && a2_AddDriver(cfg, drv))
		fail(3, a2_LastError());
	if(!(iface = a2_Open(cfg)))
		fail(4, a2_LastError());
	if(settings[0].samplerate != cfg->samplerate)
		printf("Actual master iface sample rate: %d (requested %d)\n",
				cfg->samplerate, settings[0].samplerate);

	printf("Loading...\n");
	
	/* Load jingle */
	if((h = a2_Load(iface, "data/a2jingle.a2s", 0)) < 0)
		fail(5, -h);
	if((songh = a2_Get(iface, h, "Song")) < 0)
		fail(6, -songh);

	/* Load wave player program */
	if((h = a2_Load(iface, "data/testprograms.a2s", 0)) < 0)
		fail(7, -h);
	if((ph = a2_Get(iface, h, "PlayTestWave")) < 0)
		fail(8, -ph);

	/* Render */
	printf("Rendering...\n");
	if((h = render_wave(iface, songh)) < 0)
		fail(9, -h);

	/* Start playing! */
	printf("Playing...\n");
	a2_TimestampReset(iface);
	vh = a2_Start(iface, a2_RootVoice(iface), ph, 0.0f, 1.0f, h);
	if(vh < 0)
		fail(10, -vh);

	/* Wait for abort */
	while(!do_exit)
	{
		a2_Sleep(100);
		a2_PumpMessages(iface);
	}

	a2_TimestampReset(iface);
	a2_Send(iface, vh, 1);
	a2_Release(iface, vh);
	a2_Sleep(1000);

	/*
	 * Not very nice at all - just butcher everything! But this is supposed
	 * to work without memory leaks or anything, so we may as well test it.
	 */
	a2_Close(iface);
	return 0;
}
