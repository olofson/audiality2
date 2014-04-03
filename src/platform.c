/*
 * platform.c - Audiality 2 platform interface
 *
 * Copyright 2013-2014 David Olofson <david@olofson.net>
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
#include "platform.h"

#ifdef _WIN32
char *strndup(const char *s, size_t size)
{
	char *r;
	char *end = memchr(s, 0, size);
	if(end)	/* Length + 1 */
		size = end - s + 1;
	r = malloc(size);
	if(size)
	{
		memcpy(r, s, size - 1);
		r[size - 1] = '\0';
	}
	return r;
}
#endif


/*---------------------------------------------------------
	Timing
---------------------------------------------------------*/

/* Static data for a2_GetTicks() and a2_GetMicros() */
#ifdef _WIN32
DWORD a2_start_time;
LARGE_INTEGER a2_perfc_frequency = { 0 };
#else
struct timeval a2_start_time;
#endif


void a2_time_open(void)
{
#ifdef _WIN32
	timeBeginPeriod(1);
	a2_start_time = timeGetTime();
	if(!QueryPerformanceFrequency(&a2_perfc_frequency))
		a2_perfc_frequency.QuadPart = 0;
#else
	gettimeofday(&a2_start_time, NULL);
#endif
}


void a2_time_close(void)
{
#ifdef _WIN32
	timeEndPeriod(1);
#endif
}


unsigned a2_GetTicks(void)
{
#ifdef _WIN32
	DWORD now;
	now = timeGetTime();
	if(now < a2_start_time)
		return ((~(DWORD)0) - a2_start_time) + now;
	else
		return now - a2_start_time;
#else
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec - a2_start_time.tv_sec) * 1000 +
			(now.tv_usec - a2_start_time.tv_usec) / 1000;
#endif
}


uint64_t a2_GetMicros(void)
{
#ifdef _WIN32
	LARGE_INTEGER now;
	if(!a2_perfc_frequency.QuadPart || !QueryPerformanceCounter(&now))
		return (uint64_t)a2_GetTicks() * 1000;
	return now.QuadPart * 1000000 / a2_perfc_frequency.QuadPart;
#else
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec - a2_start_time.tv_sec) * 1000000.0f +
			(now.tv_usec - a2_start_time.tv_usec);
#endif
}


unsigned a2_Sleep(unsigned milliseconds)
{
#ifdef _WIN32
	DWORD t1 = timeGetTime();
	Sleep(milliseconds);
	return timeGetTime() - t1;
#else
	struct timeval tv;
	unsigned long t1, then, now, elapsed;
	t1 = then = a2_GetTicks();
	if(!milliseconds)
	{
		sched_yield();
		now = a2_GetTicks();
	}
	else
		while(1)
		{
			now = a2_GetTicks();
			elapsed = now - then;
			then = now;
			if(elapsed >= milliseconds)
				break;
			milliseconds -= elapsed;
			tv.tv_sec = milliseconds / 1000;
			tv.tv_usec = (milliseconds % 1000) * 1000;
			select(0, NULL, NULL, NULL, &tv);
		}
	return now - t1;
#endif
}
