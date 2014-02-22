/*
 * platform.h - Audiality 2 platform interface
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

#ifndef A2_PLATFORM_H
#define A2_PLATFORM_H

#include "audiality2.h"

#ifdef _WIN32
# if defined(_MSC_VER) && (_MSC_VER >= 1500)
#  include <intrin.h>
#  define HAVE_MSC_ATOMICS 1
# endif
# define WIN32_LEAN_AND_MEAN
# define STRICT
# include <windows.h>
# include <mmsystem.h>
#elif defined(__MACOSX__)
# include <libkern/OSAtomic.h>
#else
# include <sched.h>
# include <sys/time.h>
# include <sys/wait.h>
# include <pthread.h>
# include <errno.h>
#endif

#ifdef _WIN32
char *strndup(const char *s, size_t size);
#endif


/*---------------------------------------------------------
	Atomics
---------------------------------------------------------*/

typedef int A2_atomic;

static inline int a2_AtomicCAS(A2_atomic *a, int ov, int nv)
{
#ifdef HAVE_MSC_ATOMICS
	return (_InterlockedCompareExchange((long*)a, nv, ov) == ov);
#elif defined(_WIN32)
	return (InterlockedCompareExchange((long*)a, nv, ov) == ov);
#elif defined(__MACOSX__)
	return OSAtomicCompareAndSwap32Barrier(ov, nv, a);
#else
	/* Let's hope we have GCC atomics, then! */
	return __sync_bool_compare_and_swap(a, ov, nv);
#endif
}

static inline int a2_AtomicAdd(A2_atomic *a, int v)
{
	while(1)
	{
		int ov = *a;
		if(a2_AtomicCAS(a, ov, (ov + v)))
			return ov;
	}
}


/*---------------------------------------------------------
	Mutex
---------------------------------------------------------*/

typedef struct A2_mutex
{
#ifdef _WIN32
	CRITICAL_SECTION	cs;
#else
	pthread_mutex_t		mutex;
#endif
} A2_mutex;


/*
 * WIN32 implementation
 */
#ifdef _WIN32
static inline A2_errors a2_MutexOpen(A2_mutex *mtx)
{
	InitializeCriticalSectionAndSpinCount(&mtx->cs, 2000);
	return A2_OK;
}

static inline int a2_MutexTryLock(A2_mutex *mtx)
{
	return TryEnterCriticalSection(&mtx->cs);
}

static inline void a2_MutexLock(A2_mutex *mtx)
{
	EnterCriticalSection(&mtx->cs);
}

static inline void a2_MutexUnlock(A2_mutex *mtx)
{
	LeaveCriticalSection(&mtx->cs);
}

static inline void a2_MutexClose(A2_mutex *mtx)
{
	DeleteCriticalSection(&mtx->cs);
}


/*
 * pthreads implementation
 */
#else	/* _WIN32 */
static inline A2_errors a2_MutexOpen(A2_mutex *mtx)
{
	switch(pthread_mutex_init(&mtx->mutex, NULL))
	{
	  case 0:
		return A2_OK;
	  case EBUSY:
		return A2_ALREADYOPEN;
	  default:
		return A2_DEVICEOPEN;
	}
}

static inline int a2_MutexTryLock(A2_mutex *mtx)
{
	return (pthread_mutex_trylock(&mtx->mutex) == 0);
}

static inline void a2_MutexLock(A2_mutex *mtx)
{
	pthread_mutex_lock(&mtx->mutex);
}

static inline void a2_MutexUnlock(A2_mutex *mtx)
{
	pthread_mutex_unlock(&mtx->mutex);
}

static inline void a2_MutexClose(A2_mutex *mtx)
{
	pthread_mutex_destroy(&mtx->mutex);
}
#endif	/* _WIN32 */


/*---------------------------------------------------------
	Timing
---------------------------------------------------------*/

#ifdef _WIN32
extern DWORD a2_start_time;
extern LARGE_INTEGER a2_perfc_frequency;
#else
extern struct timeval a2_start_time;
#endif

static inline uint32_t a2_GetTicks(void)
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

static inline uint64_t a2_GetMicros(void)
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


void a2_time_open(void);
void a2_time_close(void);

#endif /* A2_PLATFORM_H */
