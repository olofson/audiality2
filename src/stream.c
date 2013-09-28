/*
 * stream.c - Audiality 2 stream interface
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

#include <stdlib.h>
#include "stream.h"
#include "internals.h"


A2_errors a2_StreamOpen(A2_state *st, A2_handle handle, unsigned flags)
{
	A2_errors res;
	A2_stream *str;
	A2_typeinfo *ti;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return A2_INVALIDHANDLE;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return A2_BADTYPE;
#endif
	if(!ti->OpenStream)
		return A2_NOTIMPLEMENTED;
	if(!(str = (A2_stream *)calloc(1, sizeof(A2_stream))))
		return A2_OOMEMORY;
	str->state = st;
	str->handle = handle;
	str->flags = flags;
	*(A2_stream **)hi->d.data = str;
	str->object = hi->d.data;
	if((res = ti->OpenStream(str)))
	{
		*(A2_stream **)hi->d.data = NULL;
		free(str);
		return res;
	}
	return A2_OK;
}


A2_errors a2_StreamClose(A2_state *st, A2_handle handle)
{
	A2_errors res = A2_OK;
	A2_typeinfo *ti;
	A2_stream **str;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return A2_INVALIDHANDLE;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return A2_BADTYPE;
#endif
	if(!ti->OpenStream)
		return A2_NOTIMPLEMENTED;	/* No stream interface! */
	str = (A2_stream **)hi->d.data;
	if((*str)->Close)
		res = (*str)->Close(*str);
	else if((*str)->Flush)
		res = (*str)->Flush(*str);
	free(*str);
	*str = NULL;
	return res;
}


A2_errors a2_SetPos(A2_state *st, A2_handle handle, unsigned offset)
{
	A2_stream *str;
	A2_typeinfo *ti;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return A2_INVALIDHANDLE;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return A2_BADTYPE;
#endif
	if(!ti->OpenStream)
		return A2_NOTIMPLEMENTED;	/* No stream interface! */
	str = *(A2_stream **)hi->d.data;
	if(str->SetPos)
		return str->SetPos(str, offset);
	else
	{
		str->position = offset;
		return A2_OK;
	}
}


unsigned a2_GetPos(A2_state *st, A2_handle handle)
{
	A2_stream *str;
	A2_typeinfo *ti;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return 0;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return 0;
#endif
	if(!ti->OpenStream)
		return 0;
	str = *(A2_stream **)hi->d.data;
	if(str->GetPos)
		return str->GetPos(str);
	else
		return str->position;
}


A2_errors a2_Read(A2_state *st, A2_handle handle,
		A2_sampleformats fmt, void *buffer, unsigned size)
{
	A2_stream *str;
	A2_typeinfo *ti;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return A2_INVALIDHANDLE;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return A2_BADTYPE;
#endif
	if(!ti->OpenStream)
		return A2_NOTIMPLEMENTED;	/* No stream interface! */
	str = *(A2_stream **)hi->d.data;
	if(!str->Read)
		return A2_NOTIMPLEMENTED;
	return str->Read(str, fmt, buffer, size);
}


A2_errors a2_Write(A2_state *st, A2_handle handle,
		A2_sampleformats fmt, const void *data, unsigned size)
{
	A2_stream *str;
	A2_typeinfo *ti;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return A2_INVALIDHANDLE;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return A2_BADTYPE;
#endif
	if(!ti->OpenStream)
		return A2_NOTIMPLEMENTED;	/* No stream interface! */
	str = *(A2_stream **)hi->d.data;
	if(!str->Write)
		return A2_NOTIMPLEMENTED;
	return str->Write(str, fmt, data, size);
}


A2_errors a2_Flush(A2_state *st, A2_handle handle)
{
	A2_stream *str;
	A2_typeinfo *ti;
	RCHM_handleinfo *hi = rchm_Get(&st->ss->hm, handle);
	if(!hi)
		return A2_INVALIDHANDLE;
	ti = (A2_typeinfo *)rchm_TypeUserdata(&st->ss->hm, hi->typecode);
#ifdef DEBUG
	if(!ti)
		return A2_BADTYPE;
#endif
	if(!ti->OpenStream)
		return A2_NOTIMPLEMENTED;	/* No stream interface! */
	str = *(A2_stream **)hi->d.data;
	if(!str->Flush)
		return A2_OK;
	return str->Flush(str);
}
