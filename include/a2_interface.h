/*
 * a2_interface.h - Audiality 2 Interface
 *
 * Copyright 2016 David Olofson <david@olofson.net>
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

#ifndef A2_INTERFACE_H
#define A2_INTERFACE_H

#include "a2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Function pointers to frequently used calls with multiple implementations.
 *
 * Also note that this is actually the only safe way for dynamically loaded
 * code (such as "plugin" units and drivers) to call back into the host
 * application's instance of Audiality 2 on some platforms!
 */
struct A2_interface
{
	/* Handle management */
	A2_errors (*Release)(A2_interface *i, A2_handle handle);

	/* Timestamping */
	A2_timestamp (*TimestampNow)(A2_interface *i);
	A2_timestamp (*TimestampGet)(A2_interface *i);
	A2_timestamp (*TimestampSet)(A2_interface *i, A2_timestamp ts);
	int (*ms2Timestamp)(A2_interface *i, double t);
	double (*Timestamp2ms)(A2_interface *i, int ts);
	A2_timestamp (*TimestampBump)(A2_interface *i, int dt);
	int (*TimestampNudge)(A2_interface *i, int offset, float amount);

	/* Playing and controlling */
	A2_handle (*NewGroup)(A2_interface *i, A2_handle parent);
	A2_handle (*Starta)(A2_interface *i, A2_handle parent,
			A2_handle program, unsigned argc, int *argv);
	A2_errors (*Playa)(A2_interface *i, A2_handle parent,
			A2_handle program, unsigned argc, int *argv);
	A2_errors (*Senda)(A2_interface *i, A2_handle voice, unsigned ep,
			unsigned argc, int *argv);
	A2_errors (*SendSuba)(A2_interface *i, A2_handle voice,
			unsigned ep, unsigned argc, int *argv);
	A2_errors (*Kill)(A2_interface *i, A2_handle voice);
	A2_errors (*KillSub)(A2_interface *i, A2_handle voice);

	/* (Implementation specific data may follow) */
};


/*---------------------------------------------------------
	Timestamping
---------------------------------------------------------*/

/*
 * Compare two timestamps. Returns (a - b) with correct handling of timestamp
 * wrapping.
 *
 * NOTE:
 *	This will assume that b is BEFORE a if the difference is more than half
 *	of the "wrap period"! This is to allow proper handling of messages that
 *	arrive late.
 */
static inline int a2_TSDiff(A2_timestamp a, A2_timestamp b)
{
	return (int)(a - b);
}

/*
 * Calculates a timestamp that would have commands sent right away applied as
 * soon as possible with constant latency.
 */
static inline A2_timestamp a2_TimestampNow(A2_interface *i)
{
	return i->TimestampNow(i);
}

/*
 * Get the current API timestamp.
 */
static inline A2_timestamp a2_TimestampGet(A2_interface *i)
{
	return i->TimestampGet(i);
}

/*
 * Set new API timestamp for subsequent commands. Returns the previous
 * timestamp.
 */
static inline A2_timestamp a2_TimestampSet(A2_interface *i, A2_timestamp ts)
{
	return i->TimestampSet(i, ts);
}

/*
 * Convert from milliseconds to engine timestamp (fixed point fractional audio
 * frames).
 */
static inline int a2_ms2Timestamp(A2_interface *i, double t)
{
	return i->ms2Timestamp(i, t);
}

/*
 * Convert from engine timestamp (fixed point fractional audio frames) to
 * milliseconds.
 */
static inline double a2_Timestamp2ms(A2_interface *i, int ts)
{
	return i->Timestamp2ms(i, ts);
}

/*
 * Set the API timestamp so that subsequent commands are executed as soon as
 * possible with constant latency. Returns the previous value of the API
 * timestamp.
 */
static inline A2_timestamp a2_TimestampReset(A2_interface *i)
{
	return i->TimestampSet(i, i->TimestampNow(i));
}

/*
 * Bump the timestamp for subsequent commands by the specified amount, adjusted
 * by any nudge amount calculated by the last a2_TimestampNudge() call. The
 * nudge amount is clamped so that the API timestamp is never moved backwards,
 * and any remainder is pushed to the next a2_TimestampBump() call.
 *
 * Returns the previous timestamp.
 */
static inline A2_timestamp a2_TimestampBump(A2_interface *i, int dt)
{
	return i->TimestampBump(i, dt);
}

/*
 * Calculate a full ('amount' == 1.0f) or partial ('amount' < 1.0f) adjustment
 * that would bring the API timestamp towards (a2_TimestampNow() - offset).
 *
 * Returns the calculated value, which is also stored internally, and applied
 * by the next call to, or next few calls to, a2_TimestampBump().
 *
 * NOTE:
 *	This call does NOT change the API timestamp. Any necessary adjustments
 *	are handled by subsequent a2_TimestampBump() calls.
 */
static inline int a2_TimestampNudge(A2_interface *i, int offset, float amount)
{
	return i->TimestampNudge(i, offset, amount);
}


/*---------------------------------------------------------
	Playing and controlling
---------------------------------------------------------*/

/*
 * Create a new voice under voice 'parent', running the default group program,
 * "a2_groupdriver", featuring volume and pan controls, and support for
 * a2_Tap() and a2_Insert().
 */
static inline A2_handle a2_NewGroup(A2_interface *i, A2_handle parent)
{
	return i->NewGroup(i, parent);
}

/*
 * Start 'program' on a new subvoice of 'parent'.
 *
 * The a2_Starta() versions expect arrays of 16:16 fixed point values.
 *
 * The a2_Start() macro converts and passes any number of floating point
 * arguments.
 *
 * Returns a handle, or a negative error code.
 */
static inline A2_handle a2_Starta(A2_interface *i, A2_handle parent,
		A2_handle program, unsigned argc, int *argv)
{
	return i->Starta(i, parent, program, argc, argv);
}

#define a2_Start(i, p, prg, args...)					\
	({								\
		float fa[] = { args };					\
		int j, ia[sizeof(fa) / sizeof(float)];			\
		for(j = 0; j < (int)(sizeof(ia) / sizeof(int)); ++j)	\
			ia[j] = fa[j] * 65536.0f;			\
		a2_Starta(i, p, prg, sizeof(ia) / sizeof(int), ia);	\
	})

/*
 * Start 'program' on a new subvoice of 'parent', without attaching the new
 * voice to a handle.
 *
 * NOTE:
 *	Although detached voices cannot be addressed directly, they still
 *	recieve messages sent to all voices in the group!
 */
static inline A2_errors a2_Playa(A2_interface *i, A2_handle parent,
		A2_handle program, unsigned argc, int *argv)
{
	return i->Playa(i, parent, program, argc, argv);
}

#define a2_Play(i, p, prg, args...)					\
	({								\
		float fa[] = { args };					\
		int j, ia[sizeof(fa) / sizeof(float)];			\
		for(j = 0; j < (int)(sizeof(ia) / sizeof(int)); ++j)	\
			ia[j] = fa[j] * 65536.0f;			\
		a2_Playa(i, p, prg, sizeof(ia) / sizeof(int), ia);	\
	})

/* Send a message to entry point 'ep' of the program running on 'voice'. */
static inline A2_errors a2_Senda(A2_interface *i, A2_handle voice, unsigned ep,
		unsigned argc, int *argv)
{
	return i->Senda(i, voice, ep, argc, argv);
}

#define a2_Send(i, v, ep, args...)					\
	({								\
		float fa[] = { args };					\
		int j, ia[sizeof(fa) / sizeof(float)];			\
		for(j = 0; j < (int)(sizeof(ia) / sizeof(int)); ++j)	\
			ia[j] = fa[j] * 65536.0f;			\
		a2_Senda(i, v, ep, sizeof(ia) / sizeof(int), ia);	\
	})

/* Send a message to entry point 'ep' of all subvoices of 'voice'. */
static inline A2_errors a2_SendSuba(A2_interface *i, A2_handle voice,
		unsigned ep, unsigned argc, int *argv)
{
	return i->SendSuba(i, voice, ep, argc, argv);
}

#define a2_SendSub(i, v, ep, args...)					\
	({								\
		float fa[] = { args };					\
		int j, ia[sizeof(fa) / sizeof(float)];			\
		for(j = 0; j < (int)(sizeof(ia) / sizeof(int)); ++j)	\
			ia[j] = fa[j] * 65536.0f;			\
		a2_SendSuba(i, v, ep, sizeof(ia) / sizeof(int), ia);	\
	})

/*
 * Instantly stop 'voice' and any subvoices running under it. The handles of
 * the voice and any subvoices running under it will be released.
 *
 * NOTE:
 *	Care should be taken to not use the 'voice' handle, or any subvoice
 *	handles after this call, as they're all released! (Using invalid
 *	handles is technically safe, but may have "interesting" effects...)
 *
 * WARNING:
 *	There is no fadeout or anything, so this will most definitely result in
 *	a nasty click or pop if done to voices that are still audible!
 */
static inline A2_errors a2_Kill(A2_interface *i, A2_handle voice)
{
	return i->Kill(i, voice);
}

/* Kill all subvoices of 'voice', but not 'voice' itself */
static inline A2_errors a2_KillSub(A2_interface *i, A2_handle voice)
{
	return i->KillSub(i, voice);
}

#ifdef __cplusplus
};
#endif

#endif /* A2_INTERFACE_H */
