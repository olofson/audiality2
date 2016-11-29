/*
 * pitch.c - Audiality 2 linear pitch conversion tools
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

#include <math.h>
#include <stdlib.h>
#include "pitch.h"
#include "internals.h"
#if PITCHDEBUG(1)+0
# include <stdio.h>
#endif


/* Linear pitch to phase increment translation table */
#define	A2_PITCH_TABLE_BITS	6
#define	A2_PITCH_TABLE_SIZE	(1 << A2_PITCH_TABLE_BITS)

typedef struct A2_ptentry
{
	unsigned	base;
	unsigned	coeff;
} A2_ptentry;

static A2_ptentry *a2_pitchtab = NULL;


float a2_F2Pf(float f, float reference)
{
	return log2(f / reference);
}


float a2_P2If(float pitch)
{
	return powf(2.0f, pitch);
}


unsigned a2_P2I(int pitch)
{
	/* (double)0x10000 * powf(2.0f, pitch * (1.0f / 65536.0f)) */
	int n = pitch & 0xffff;
	int oct = pitch >> 16;
	A2_ptentry *pe = &a2_pitchtab[n >> (16 - A2_PITCH_TABLE_BITS)];
	unsigned dph = pe->coeff * (n & (0xffff >> A2_PITCH_TABLE_BITS));
	dph >>= 8 - A2_PITCH_TABLE_BITS;
	dph += pe->base;
	return dph >> (7 - oct);
}


A2_errors a2_pitch_open(void)
{
	unsigned i, b;
#if PITCHDEBUG(1)+0
	int maxc = 0;
	float minerr = 0.0f;
	float maxerr = 0.0f;
#endif
	a2_pitchtab = (A2_ptentry *)malloc(A2_PITCH_TABLE_SIZE *
			sizeof(A2_ptentry));
	if(!a2_pitchtab)
		return A2_OOMEMORY;

	PITCHDEBUG(printf("Pitch LUT; %d segments:\n", A2_PITCH_TABLE_SIZE);)
	b = 0x80000000;
	for(i = 0; i < A2_PITCH_TABLE_SIZE; ++i)
	{
		unsigned b2 = (double)0x80000000 * powf(2.0f,
				(i + 1) * (1.0f / A2_PITCH_TABLE_SIZE)) + 0.5f;
		a2_pitchtab[i].base = b;
		a2_pitchtab[i].coeff = (b2 - b + 128) >> 8;
		PITCHDEBUG(if(a2_pitchtab[i].coeff > maxc)
			maxc = a2_pitchtab[i].coeff;)
		b = b2;
		PITCHDEBUG(printf("%u+%u\t", a2_pitchtab[i].base,
				a2_pitchtab[i].coeff);)
	}
#if PITCHDEBUG(1)+0
	/* Overall error */
	printf("\nmaxc: %u\n", maxc);
	for(i = 0; i < 65536; ++i)
	{
		float f = 16777216.0f * a2_P2If(i / 65536.0f);
		float f2 = a2_P2I(i);
		float err = f2 - f;
		if(err > maxerr)
			maxerr = err;
		if(err < minerr)
			minerr = err;
	}
	printf("minerr: %f\n", minerr);
	printf("maxerr: %f\n", maxerr);

	/* Error per table segment */
	for(i = 0; i < A2_PITCH_TABLE_SIZE; ++i)
	{
		int j;
		minerr =  maxerr = 0.0f;
		for(j = 0; j < 65536 / A2_PITCH_TABLE_SIZE; ++j)
		{
			int ij = i * 65536 / A2_PITCH_TABLE_SIZE + j;
			float f = 16777216.0f * a2_P2If(ij / 65536.0f);
			float f2 = a2_P2I(ij);
			float err = f2 - f;
			if(err > maxerr)
				maxerr = err;
			if(err < minerr)
				minerr = err;
		}
		printf("%.2f/%.2f\t", minerr, maxerr);
	}
	printf("\n");
#endif
	return A2_OK;
}


void a2_pitch_close(void)
{
	free(a2_pitchtab);
	a2_pitchtab = NULL;
}
