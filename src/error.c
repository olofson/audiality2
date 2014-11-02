/*
 * error.c - Audiality 2 error codes
 *
 * Copyright 2010-2014 David Olofson <david@olofson.net>
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
#include "audiality2.h"


#define	A2_DEFERR(x, y)	y,
static const char *a2_errdescs[] = {
	"Ok - no error!",
	A2_ALLERRORS
};
#undef	A2_DEFERR


#define	A2_DEFERR(x, y)	#x,
static const char *a2_errnames[] = {
	"OK",
	A2_ALLERRORS
};
#undef	A2_DEFERR


static char a2_errbuf[128];

const char *a2_ErrorString(unsigned errorcode)
{
	if((errorcode >= 0) && (errorcode < A2_INTERNAL))
		return a2_errdescs[errorcode];
	else
	{
		a2_errbuf[sizeof(a2_errbuf) - 1] = 0;
		snprintf(a2_errbuf, sizeof(a2_errbuf) - 1,
				"INTERNAL ERROR #%d; please report to "
				"<david@olofson.net>",
				errorcode - A2_INTERNAL);
		return a2_errbuf;
	}
}


const char *a2_ErrorName(A2_errors errorcode)
{
	if((errorcode >= 0) && (errorcode < A2_INTERNAL))
		return a2_errnames[errorcode];
	else
		return NULL;
}


const char *a2_ErrorDescription(A2_errors errorcode)
{
	if((errorcode >= 0) && (errorcode < A2_INTERNAL))
		return a2_errdescs[errorcode];
	else
		return NULL;
}
