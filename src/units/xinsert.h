/*
 * xinsert.h - Audiality 2 External Sink/Source/Insert unit set
 *
 *	This unit implements the realtime side of the callback and stream
 *	sink/source/insert APIs; a2_SinkCallback(), a2_SourceCallback(),
 *	a2_InsertCallback(), a2_OpenSink() and a2_OpenSource().
 *
 * Copyright 2012-2014 David Olofson <david@olofson.net>
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

#ifndef A2_XINSERT_H
#define A2_XINSERT_H

#include "units.h"
#include "sfifo.h"

typedef struct A2_xinsert A2_xinsert;
typedef struct A2_xinsert_client A2_xinsert_client;

typedef enum A2_xiflags
{
	A2_XI_READ =	0x00000100,	/* Client reads from unit inputs */
	A2_XI_WRITE =	0x00000200,	/* Client writes to unit outputs */
	A2_XI_STREAM =	0x00000400,	/* 0: callback, 1: stream */
	A2_XI_SILENT =	0x00000800	/* Don't report realtime xflows */
} A2_xiflags;

struct A2_xinsert_client
{
	A2_xinsert_client	*next;
	A2_xinsert		*unit;		/* Owner */
	A2_xinsert_cb		callback;
	void			*userdata;
	SFIFO			*fifo;
	int			channel;
	A2_handle		handle;		/* Client handle */
	A2_handle		stream;		/* Stream handle, or 0 */
	unsigned		flags;		/* A2_xiflags */
	int			xflow;		/* 1 if in an xflow state */
};

struct A2_xinsert
{
	A2_unit			header;
	A2_state		*state;
	A2_xinsert_client	*clients;
	void (*SetProcess)(A2_unit *u);
	A2_handle		voice;
	unsigned		flags;		/* A2_unitflags */
};

static inline A2_xinsert *a2_xinsert_cast(A2_unit *u)
{
	return (A2_xinsert *)u;
}

extern const A2_unitdesc a2_xsink_unitdesc;
extern const A2_unitdesc a2_xsource_unitdesc;
extern const A2_unitdesc a2_xinsert_unitdesc;

#endif /* A2_XINSERT_H */
