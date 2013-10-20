/*
 * a2play.c - Audiality 2 command line player
 *
 * Copyright 2013 David Olofson <david@olofson.net>
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
#include "waves.h"
#include "units.h"


/* Configuration */
static char *audiodriver = NULL;
static int samplerate = 44100;
static int channels = 2;
static int audiobuf = 4096;
static int a2flags = A2_REALTIME | A2_EXPORTALL | A2_RTERRORS | A2_TIMESTAMP;

/* State and control */
static A2_state *state = NULL;	/* Engine state*/
static A2_handle module = -1;	/* Handle of last loaded module */

static double stoptime = 0.0f;	/* (Need final sample rate for stopframes!) */
static int stopframes = 0;
static int playedframes = 0;
static int stoplevel = -1;

static int do_exit = 0;


/*-------------------------------------------------------------------
	Audio processing
-------------------------------------------------------------------*/

/* Callback for automatic stop logic */
static A2_errors tap_process(int **buffers, unsigned nbuffers, unsigned frames,
		void *userdata)
{
	int i, j;
	int smin = 0x7fffffff;
	int smax = 0x80000000;

	/* Count sample frames processed */
	playedframes += frames;

	/* Find absolute peak level */
	for(j = 0; j < nbuffers; ++j)
		for(i = 0; i < frames; ++i)
		{
			int s = buffers[j][i];
			if(s < smin)
				smin = s;
			if(s > smax)
				smax = s;
		}
	if(-smin > smax)
		smax = -smin;

	/* Stop conditions */
	if(stopframes && (stoplevel >= 0))
	{
		if((playedframes >= stopframes) && (smax <= stoplevel))
			do_exit = 1;
	}
	else if(stopframes && (playedframes >= stopframes))
		do_exit = 1;
	else if((stoplevel >= 0) && (smax <= stoplevel))
		do_exit = 1;

	return A2_OK;
}


/*-------------------------------------------------------------------
	Object info printouts
-------------------------------------------------------------------*/

static void print_info(int indent, const char *xname, A2_handle h)
{
	int i;
	A2_handle x;
	A2_otypes t = a2_TypeOf(state, h);
	const char *name = a2_Name(state, h);
	int has_exports = a2_GetExport(state, h, 0) >= 1;
	if(has_exports)
	{
		for(i = 0; i < indent; ++i)
			printf("    ");
		printf("---------------------------------------------\n");
	}
	for(i = 0; i < indent; ++i)
		printf("    ");
	if(xname)
		printf("%-16s", xname);
	else if(name)
		printf("%-16s", name);
	else
		printf("%-16d", h);
	printf("%-12s", a2_TypeName(state, t));
	switch(t)
	{
	  case A2_TBANK:
		break;
	  case A2_TWAVE:
	  {
		A2_wave *w = a2_GetWave(state, h);
		switch(w->type)
		{
		  case A2_WOFF:
			printf("OFF     ");
			break;
		  case A2_WNOISE:
			printf("NOISE   ");
			break;
		  case A2_WWAVE:
			printf("WAVE    ");
			break;
		  case A2_WMIPWAVE:
			printf("MIPWAVE ");
			break;
		}
		switch(w->type)
		{
		  case A2_WOFF:
			break;
		  case A2_WNOISE:
			printf(" per: %-8d", w->period);
			break;
		  case A2_WWAVE:
		  case A2_WMIPWAVE:
			printf(" per: %-8d size: %-8d", w->period,
					w->d.wave.size[0]);
			if(w->flags & A2_LOOPED)
				printf(" LOOPED");
			break;
		}
		break;
	  }
	  case A2_TPROGRAM:
		/* TODO: New properties for argument count, defaults etc! */
		break;
	  case A2_TUNIT:
	  {
		const A2_crdesc *crd;
		const A2_unitdesc *ud = a2_GetUnitDescriptor(state, h);

		/* Inputs and outputs */
		if(ud->maxinputs)
		{
			if(ud->mininputs == ud->maxinputs)
				printf("i: %d     ", ud->mininputs);
			else
				printf("i: %d..%d  ", ud->mininputs,
						ud->maxinputs);
		}
		else
			printf("i: ----  ");
		if(ud->maxoutputs)
		{
			if(ud->minoutputs == ud->maxoutputs)
				printf("o: %d     ", ud->minoutputs);
			else
				printf("o: %d..%d  ", ud->minoutputs,
						ud->maxoutputs);
		}
		else
			printf("o: ----  ");

		/* Control registers */
		for(crd = ud->registers; crd->name; crd++)
			printf(" %s", crd->name);
		break;
	  }
	  case A2_TSTRING:
		printf("%s", a2_String(state, h));
		break;
	  case A2_TDETACHED:
	  case A2_TVOICE:
		break;
	}
	printf("\n");
	if(has_exports)
	{
		for(i = 0; i < indent; ++i)
			printf("    ");
		printf("---------------------------------------------\n");
		for(i = 0; (x = a2_GetExport(state, h, i)) >= 0; ++i)
			print_info(indent + 1, a2_GetExportName(state, h, i), x);
		for(i = 0; i < indent; ++i)
			printf("    ");
	}
}


/*-------------------------------------------------------------------
	Loading
-------------------------------------------------------------------*/

/* Try to load each non-option argument as an .a2s file. */
static void load_sounds(int argc, const char *argv[])
{
	int i;
	for(i = 1; i < argc; ++i)
	{
		A2_handle h;
		if(argv[i][0] == '-')
			continue;
		if((h = a2_Load(state, argv[i])) < 0)
		{
			fprintf(stderr, "Could not load \"%s\"! (%s)\n",
					argv[i], a2_ErrorString(-h));
			continue;
		}
		a2_Export(state, A2_ROOTBANK, h, NULL);
		module = h;
		fprintf(stderr, "Loaded \"%s\" - %s - %s\n",
				a2_Name(state, h),
				a2_String(state, a2_Get(state, h, "author")),
				a2_String(state, a2_Get(state, h, "title")));
	}
}


/*-------------------------------------------------------------------
	Playing
-------------------------------------------------------------------*/

/* Parse the body of a -p switch; <name>[,pitch[,vel[,mod[,...]]]] */
static int play_sound(const char *cmd)
{
	A2_handle h;
	A2_errors res;
	int i;
	char program[256];
	float a[A2_MAXARGS];
	int ia[A2_MAXARGS];
	int cnt;
	printf("Playing %s/%s...\n", a2_Name(state, module), cmd);
	cnt = sscanf(cmd, "%[A-Za-z0-9_],%f,%f,%f,%f,%f,%f,%f,%f", program,
			a, a + 1, a + 2, a + 3, a + 4, a + 5, a + 6, a + 7);
	if(cnt <= 0)
	{
		fprintf(stderr, "a2play: -p switch with no arguments!\n");
		return -1;
	}
	if((h = a2_Get(state, module, program)) < 0)
	{
		fprintf(stderr, "a2play: a2_Get(\"%s\"): %s\n", program,
				a2_ErrorString(-h));
		return -2;
	}
	for(i = 0; i < cnt - 1; ++i)
		ia[i] = a[i] * 65536.0f;
	if((res = a2_Playa(state, a2_RootVoice(state), h, cnt - 1, ia)))
	{
		fprintf(stderr, "a2play: a2_Starta(): %s\n",
				a2_ErrorString(res));
		return -3;
	}
	return 1;
}


/*
 * Handle any -p switches. If no -p switches are found, try the default entry
 * point "Song".
 *
 * Returns 1 if one or more programs were started, a negative value if there
 * was an error, otherwise 0.
 */
static int play_sounds(int argc, const char *argv[])
{
	int i;
	int res = 0;
	for(i = 1; i < argc; ++i)
	{
		if(argv[i][0] != '-')
			continue;
		if(strncmp(argv[i], "-p", 2) == 0)
		{
			if(play_sound(&argv[i][2]) < 0)
				return -1;
			res = 1;
		}
		else if(strncmp(argv[i], "-xr", 3) == 0)
			print_info(0, NULL, A2_ROOTBANK);
		else if(strncmp(argv[i], "-x", 2) == 0)
			print_info(0, NULL, module);
	}
	if(!res && (a2_Get(state, module, "Song") >= 0))
		res = play_sound("Song");
	return res;
}


/*-------------------------------------------------------------------
	User interface
-------------------------------------------------------------------*/

static void usage(const char *exename)
{
	unsigned v = a2_LinkedVersion();
	fprintf(stderr, "\nAudiality 2 v%d.%d.%d.%d\n",
			A2_MAJOR(v),
			A2_MINOR(v),
			A2_MICRO(v),
			A2_BUILD(v));
	fprintf(stderr, "Copyright 2010-2013 David Olofson\n\n"
			"Usage: %s [switches] <file>\n\n", exename);
	fprintf(stderr, "Switches:  -d<name>[,opt[,opt[,...]]]\n"
			"                       Audio driver + options\n"
			"           -b<n>       Audio buffer size (frames)\n"
			"           -r<n>       Audio sample rate (Hz)\n"
			"           -c<n>       Number of audio channels\n"
			"           -p<name>[,arg[,arg[,...]]]\n"
			"                       Run program <name> with the "
			"specified arguments\n"
			"           -st<n>      Stop time (seconds)\n"
			"           -sl<n>      Stop level (1.0 <==> clip)\n"
			"           -x          Print module exports\n"
			"           -xr         Print engine root exports\n"
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
			free(audiodriver);
			audiodriver = strdup(&argv[i][2]);
			printf("[Audio driver: %s]\n", audiodriver);
		}
		else if(strncmp(argv[i], "-b", 2) == 0)
		{
			audiobuf = atoi(&argv[i][2]);
			printf("[Audio buffer: %d]\n", audiobuf);
		}
		else if(strncmp(argv[i], "-r", 2) == 0)
		{
			samplerate = atoi(&argv[i][2]);
			printf("[Audio sample rate: %d]\n", samplerate);
		}
		else if(strncmp(argv[i], "-c", 2) == 0)
		{
			samplerate = atoi(&argv[i][2]);
			printf("[Audio channels: %d]\n", channels);
		}
		else if(strncmp(argv[i], "-p", 2) == 0)
			continue;
		else if(strncmp(argv[i], "-st", 3) == 0)
		{
			stoptime = atof(&argv[i][3]);
			printf("[Stop after: %f s]\n", stoptime);
		}
		else if(strncmp(argv[i], "-sl", 3) == 0)
		{
			double sl = atof(&argv[i][3]);
			stoplevel = sl * 65536.0f;
			printf("[Stop below: %f]\n", sl);
		}
		else if(strncmp(argv[i], "-xr", 3) == 0)
			continue;
		else if(strncmp(argv[i], "-x", 2) == 0)
			continue;
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
	fprintf(stderr, "a2play: Stopping...\n");
	do_exit = 1;
}


static void fail(A2_errors err)
{
	fprintf(stderr, "ERROR: %s\n", a2_ErrorString(err));
	exit(100);
}


/*-------------------------------------------------------------------
	main()
-------------------------------------------------------------------*/

int main(int argc, const char *argv[])
{
	A2_config *cfg;
	float a;
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	/* Command line switches */
	if(argc <= 1)
	{
		fprintf(stderr, "No arguments specified!\n");
		usage(argv[0]);
		exit(1);
	}
	parse_args(argc, argv);

	/* Configure and open engine */
	if(!(cfg = a2_OpenConfig(samplerate, audiobuf, 2,
			a2flags | A2_STATECLOSE)))
		fail(a2_LastError());
	if(audiodriver)
		if(a2_AddDriver(cfg, a2_NewDriver(A2_AUDIODRIVER, audiodriver)))
			fail(a2_LastError());
	if(!(state = a2_Open(cfg)))
		fail(a2_LastError());
	if(samplerate != cfg->samplerate)
		printf("a2play: Actual sample rate: %d (requested %d)\n",
				cfg->samplerate, samplerate);
	stopframes = stoptime * cfg->samplerate;

	/* Load sounds */
	load_sounds(argc, argv);

	/* Start playing! */
	a2_Now(state);
	a2_SetTapCallback(state, a2_RootVoice(state), tap_process, NULL);
	if(play_sounds(argc, argv) == 1)
	{
		/* Wait for completion or abort */
		while(!do_exit)
		{
			a2_Now(state);
			sleep(1);
		}

		/* Fade out */
		a2_Now(state);
		a = 1.0f;
		while(a > 0.01f)
		{
			a2_Send(state, a2_RootVoice(state), 2, a);
			a2_Wait(state, 100.0f);
			a *= 0.5f;
		}
		a2_Send(state, a2_RootVoice(state), 2, 0.0f);
		sleep(1);
	}

	/* Close and clean up */
	a2_Close(state);
	free(audiodriver);
	return 0;
}
