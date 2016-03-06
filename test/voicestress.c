/*
 * voicestress.c - Audiality 2 voice management stress test
 *
 *	This test starts, communicates with, and releases voices at an extreme
 *	rate, in order to stress test the engine voice management logic.
 *	  The expected result is a rapid stream of random notes, with a clearly
 *	audible buzz at the 200 Hz rate at which the notes are played. The
 *	printed "nudge" corrections are supposed to remain close to zero.
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


/* Number of voices hold on to at any one time */
#define	VOICES	50

/* Delay between notes (ms) */
#define	DELAY	5

/* Timestamp nudge correction coefficient [0, 1] */
#define	CORRECTION	0.01f


/* Configuration */
const char *audiodriver = "default";
int samplerate = 44100;
int channels = 2;
int audiobuf = 4096;
int waverate = 0;

static int do_exit = 0;


static void usage(const char *exename)
{
	fprintf(stderr,	"\n\nUsage: %s [switches] <file>\n\n", exename);
	fprintf(stderr, "Switches:  -d<name>[,opt[,opt[,...]]]\n"
			"                       Audio driver + options\n"
			"           -b<n>       Audio buffer size (frames)\n"
			"           -r<n>       Audio sample rate (Hz)\n"
			"           -c<n>       Number of audio channels\n\n"
			"           -wr<n>      Wave sample rate (Hz)\n"
			"           -h          Help\n\n");
}


/* Parse driver selection and configuration switches */
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
		else if(strncmp(argv[i], "-wr", 3) == 0)
		{
			waverate = atoi(&argv[i][3]);
			printf("[Wave sample rate: %d]\n", waverate);
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


#ifdef _WIN32
int main(int argc, char *argv[])
#else
int main(int argc, const char *argv[])
#endif
{
	int vhi, t;
	A2_handle h, ph, vh[VOICES];
	A2_driver *drv;
	A2_config *cfg;
	A2_state *state;

	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Command line switches */
	parse_args(argc, argv);

	/* Configure and open master state */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, audiodriver)))
		fail(1, a2_LastError());
	if(!(cfg = a2_OpenConfig(samplerate, audiobuf, channels,
			A2_TIMESTAMP | A2_STATECLOSE)))
		fail(2, a2_LastError());
	if(drv && a2_AddDriver(cfg, drv))
		fail(3, a2_LastError());
	if(!(state = a2_Open(cfg)))
		fail(4, a2_LastError());
	if(samplerate != cfg->samplerate)
		printf("Actual master state sample rate: %d (requested %d)\n",
				cfg->samplerate, samplerate);

	/* Load wave player program */
	if((h = a2_Load(state, "data/testprograms.a2s", 0)) < 0)
		fail(5, -h);
	if((ph = a2_Get(state, h, "PlayTestNote")) < 0)
		fail(6, -ph);

	/* Abuse! */
	memset(vh, 0, sizeof(vh));
	vhi = 0;
	t = a2_GetTicks();
	fprintf(stderr, "Starting!\n");
	a2_TimestampReset(state);
	while(!do_exit)
	{
		/* Stop and detach! */
		if(vh[vhi])
		{
			a2_Send(state, vh[vhi], 1);
			a2_Release(state, vh[vhi]);
		}

		/* Play! */
		vh[vhi] = a2_Start(state, a2_RootVoice(state), ph,
				a2_Rand(state, 2.0f), 0.5f);
		if(vh[vhi] < 0)
			fail(10, -vh[vhi]);

		/* Timing... */
		a2_TimestampBump(state, a2_ms2Timestamp(state, DELAY));
		vhi = (vhi + 1) % VOICES;
		if(vhi == 0)
		{
			int corr;
			t += DELAY * VOICES;
			while((t - (int)a2_GetTicks() > 0) && !do_exit)
				a2_Sleep(1);
			corr = a2_TimestampNudge(state, 0, CORRECTION);
			fprintf(stderr, "(nudge %f)\n", corr / 256.0f);
			a2_PumpMessages(state);
		}
	}

	a2_Close(state);
	return 0;
}
