/*
 * stream.c - Audiality 2 stream interface
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
#include "stream.h"
#include "internals.h"


A2_handle a2_OpenStream(A2_state *st, A2_handle handle,
		int channel, int size, unsigned flags)
{
	A2_handle h;
	A2_errors res;
	A2_stream *str;
	A2_typeinfo *ti;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return -A2_INVALIDHANDLE;
	if(!hi->refcount && !(hi->userbits & A2_LOCKED))
		return -A2_DEADHANDLE;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return -A2_BADTYPE;
#endif
	if(!ti->OpenStream)
		return -A2_NOTIMPLEMENTED;
	if(!(str = (A2_stream *)calloc(1, sizeof(A2_stream))))
		return -A2_OOMEMORY;
	if((h = rchm_NewEx(&st->ss->hm, str, A2_TSTREAM, flags, 1)) < 0)
	{
		free(str);
		return h;
	}
	str->state = st;
	str->channel = channel;
	str->size = size;
	str->flags = flags;
	str->targetobject = hi->d.data;
	str->targethandle = handle;
	if((res = ti->OpenStream(str, h)))
	{
		rchm_Free(&st->ss->hm, h);
		free(str);
		return res;
	}
	rchm_Retain(&st->ss->hm, str->targethandle);
	return h;
}


static RCHM_errors a2_StreamDestructor(RCHM_handleinfo *hi, void *ti, RCHM_handle h)
{
	A2_errors res = A2_OK;
	A2_stream *str = (A2_stream *)hi->d.data;
	if(hi->userbits & A2_LOCKED)
		return RCHM_REFUSE;
	if(str->Close)
		res = str->Close(str);
	else if(str->Flush)
		res = str->Flush(str);
	rchm_Release(&((A2_typeinfo *)ti)->state->ss->hm, str->targethandle);
	free(str);
	return (RCHM_errors)res;
}


A2_errors a2_SetPosition(A2_state *st, A2_handle stream, unsigned offset)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return res;
	if(str->SetPosition)
		return str->SetPosition(str, offset);
	else
	{
		str->position = offset;
		return A2_OK;
	}
}


unsigned a2_GetPosition(A2_state *st, A2_handle stream)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return -res;
	if(str->GetPosition)
		return str->GetPosition(str);
	else
		return str->position;
}


int a2_Available(A2_state *st, A2_handle stream)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return -res;
	if(str->Available)
		return str->Available(str);
	else
		return -A2_NOTIMPLEMENTED;
}


int a2_Space(A2_state *st, A2_handle stream)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return -res;
	if(str->Space)
		return str->Space(str);
	else
		return -A2_NOTIMPLEMENTED;
}


A2_errors a2_Read(A2_state *st, A2_handle stream,
		A2_sampleformats fmt, void *buffer, unsigned size)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return res;
	if(!str->Read)
		return A2_NOTIMPLEMENTED;
	return str->Read(str, fmt, buffer, size);
}


A2_errors a2_Write(A2_state *st, A2_handle stream,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return res;
	if(!str->Write)
		return A2_NOTIMPLEMENTED;
	return str->Write(str, fmt, data, size);
}


A2_errors a2_Flush(A2_state *st, A2_handle stream)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return res;
	if(!str->Flush)
		return A2_OK;
	return str->Flush(str);
}


A2_errors a2_RegisterStreamTypes(A2_state *st)
{
	return a2_RegisterType(st, A2_TSTREAM, "stream",
			a2_StreamDestructor, NULL);
}


static A2_errors closed_Read(A2_stream *str,
		A2_sampleformats fmt, void *buffer, unsigned size)
{
	return A2_STREAMCLOSED;
}

static A2_errors closed_Write(A2_stream *str,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	return A2_STREAMCLOSED;
}

static A2_errors closed_SetPosition(A2_stream *str, unsigned offset)
{
	return A2_STREAMCLOSED;
}

static unsigned closed_GetPosition(A2_stream *str)
{
	return 0;
}

static int closed_Size(A2_stream *str)
{
	return -A2_STREAMCLOSED;
}

static A2_errors closed_Flush(A2_stream *str)
{
	return A2_STREAMCLOSED;
}

A2_errors a2_DetachStream(A2_state *st, A2_handle stream)
{
	A2_stream *str;
	A2_errors res = a2_GetStream(st, stream, &str);
	if(res)
		return res;
	str->Read = closed_Read;
	str->Write = closed_Write;
	str->SetPosition = closed_SetPosition;
	str->GetPosition = closed_GetPosition;
	str->Size = str->Available = str->Space = closed_Size;
	str->Flush = closed_Flush;
	str->Close = NULL;
	return A2_OK;
}
