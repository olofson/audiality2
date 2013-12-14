/*
 * platform.c - Audiality 2 platform interface
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

#include "platform.h"


/*---------------------------------------------------------
	Timing
---------------------------------------------------------*/

/* Static data for a2_GetTicks() and a2_GetMicros() */
#ifdef _WIN32
DWORD a2_start_time;
extern LARGE_INTEGER a2_perfc_frequency = 0;
#else
struct timeval a2_start_time;
#endif


void a2_time_open(void)
{
#ifdef _WIN32
	timeBeginPeriod(1);
	a2_start_time = timeGetTime();
	if(!QueryPerformanceFrequency(&a2_perfc_frequency))
		a2_perfc_frequency = 0;
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
