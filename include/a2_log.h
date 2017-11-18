/*
 * a2_log.h - Audiality 2 logging facilities
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

#ifndef	A2_LOG_H
#define	A2_LOG_H

#include "audiality2.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The actual log call, which checks enabled log levels, and if the indicated
 * level is enabled, formats and prints the log message via the configured log
 * facility. (Currently hardwired to stdout and stderr, depending on level.)
 *
 * The A2 interface, 'ii' can be NULL, in which case 'll' is ignored, and the
 * message is always printed, as there is (currently) no global log level mask.
 *
 * 'll' should be exactly one value from A2_loglevels, denoting the intended
 * log level for the message. If the log level is disabled, nothing is done.
 */
void a2_Log(A2_interface *i, A2_loglevels ll, const char *fmt, ...);


/* Internal/critical log levels. No interface - always logged! */
#define	A2_LOG_INT(args...)		a2_Log(NULL, A2_LOG_INTERNAL, args)
#define	A2_LOG_CRIT(args...)		a2_Log(NULL, A2_LOG_CRITICAL, args)

/* Normal log levels. Can be enabled/disabled per interface. */
#define	A2_LOG_ERR(i, args...)		a2_Log(i, A2_LOG_ERROR, args)
#define	A2_LOG_WARN(i, args...)		a2_Log(i, A2_LOG_WARNING, args)
#define	A2_LOG_INFO(i, args...)		a2_Log(i, A2_LOG_INFO, args)
#define	A2_LOG_MSG(i, args...)		a2_Log(i, A2_LOG_MESSAGE, args)

/* Debug log levels. */
#ifdef DEBUG
# define	A2_LOG_DBG(i, args...)	a2_Log(i, A2_LOG_DEBUG, args)
#else
# define	A2_LOG_DBG(i, args...)
#endif

/*
 * Development debug output. Normally unused in all builds.
 * NOTE: Does not add newlines automatically!
 */
#define	A2_DLOG(args...)		a2_Log(NULL, A2_LOG_DEVELOPER, args)

#ifdef __cplusplus
};
#endif

#endif	/* A2_LOG_H */
