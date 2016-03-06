/*
 * Audiality 2 shared API state stress test
 *
 * This code is in the public domain. Do what you like with it. NO WARRANTY!
 *
 * 2014, 2016 David Olofson
 */

#include <signal.h>
#include "audiality2.h"
#include "SDL.h"
#include "SDL_thread.h"

#define	NTHREADS	10

/* Delay between open/close cycles, to stress the API state open/close logic */
#define	SPARSE

static int do_exit = 0;

static void breakhandler(int a)
{
	do_exit = 1;
}


static void fail(A2_errors err)
{
	fprintf(stderr, "ERROR, Audiality 2 result: %s\n",
			a2_ErrorString(err));
	exit(100);
}


static int testthread(void *data)
{
	int *count = (int *)data;
	while(!do_exit)
	{
		A2_config *cfg;
		A2_state *st;
		if(!(cfg = a2_OpenConfig(44100, 1024, 2, A2_STATECLOSE)))
			fail(a2_LastError());
		if(a2_AddDriver(cfg, a2_NewDriver(A2_AUDIODRIVER, "dummy")))
			fail(a2_LastError());
		if(!(st = a2_Open(cfg)))
			fail(a2_LastError());
		a2_Close(st);
#ifdef SPARSE
		SDL_Delay(10);
#endif
		++*count;
	}
	return 0;
}


int main(int argc, char *argv[])
{
	int i, total, t;
	SDL_Thread *threads[NTHREADS];
	int count[NTHREADS];

	memset(count, 0, sizeof(count));
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	t = SDL_GetTicks();
	for(i = 0; i < NTHREADS; ++i)
	{
#if (SDL_MAJOR_VERSION >= 2)
		threads[i] = SDL_CreateThread(testthread, NULL, count + i);
#else
		threads[i] = SDL_CreateThread(testthread, count + i);
#endif
		if(!threads[i])
		{
			fprintf(stderr, "Could not create thread! (%s)\n",
					SDL_GetError());
			exit(200);
		}
	}

	while(!do_exit)
		SDL_Delay(10);

	t = SDL_GetTicks() - t;

	total = 0;
	for(i = 0; i < NTHREADS; ++i)
	{
		SDL_WaitThread(threads[i], NULL);
		printf("Thread %d opened and closed %d A2 states.\n",
				i, count[i]);
		total += count[i];
	}
	printf("Total: %d A2 states in %f s.\n", total, t * .001f);

	return 0;
}
