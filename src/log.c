/*
 * log.c - Audiality 2 logging facilities
 *
 * Copyright 2017 David Olofson <david@olofson.net>
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

#include <stdarg.h>
#include "internals.h"

void a2_Log(A2_interface *i, A2_loglevels ll, const char *fmt, ...)
{
	A2_interface_i *ii = (A2_interface_i *)i;
	va_list args;
	FILE *f;
	const char *pre;

	/* Print only if no interface, or if loglevel enabled on interface! */
	if(ii && !(ii->loglevels & ll))
		return;		/* Disabled by filter! --> */

	switch(ll)
	{
	  case A2_LOG_INTERNAL:
		f = stderr;
		pre = "Audiality 2 INTERNAL ERROR: ";
		break;
	  case A2_LOG_CRITICAL:
		f = stderr;
		pre = "Audiality 2 CRITICAL ERROR: ";
		break;
	  case A2_LOG_ERROR:
		f = stderr;
		pre = "Audiality 2 ERROR: ";
		break;
	  case A2_LOG_WARNING:
		f = stdout;
		pre = "Audiality 2 WARNING: ";
		break;
	  case A2_LOG_INFO:
	  case A2_LOG_MESSAGE:
		f = stdout;
		pre = "Audiality 2: ";
		break;
	  case A2_LOG_DEBUG:
		f = stdout;
		pre = "Audiality 2 DEBUG: ";
		break;
	  case A2_LOG_DEVELOPER:
		f = stdout;
		pre = NULL;
		break;
	  default:
		f = stderr;
		pre = "<unknown loglevel>: ";
		break;
	}
	if(pre)
		fputs(pre, f);
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	if(ll != A2_LOG_DEVELOPER)
		fputc('\n', f);
}
