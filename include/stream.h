/*
 * stream.h - Audiality 2 stream interface
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

#ifndef A2_STREAM_H
#define A2_STREAM_H

#include "audiality2.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open a stream on object 'handle'. The meaning (if any) of the additional
 * arguments depend on the object identified with 'handle'.
 *
 * Returns the stream handle, or a negated error code.
 */
A2_handle a2_OpenStream(A2_state *st, A2_handle handle,
		int channel, int size, unsigned flags);


/*
 * Change read/write position of 'stream' to 'offset'.
 *
 * Returns A2_OK, or an error code.
 */
A2_errors a2_SetPosition(A2_state *st, A2_handle stream, unsigned offset);

/* Read the current read/write position of 'stream'. */
unsigned a2_GetPosition(A2_state *st, A2_handle stream);

/*
 * Returns number of items currently available for reading from 'stream', or
 * -A2_NOTIMPLEMENTED if this feature is not implemented on this object.
 */
int a2_Available(A2_state *st, A2_handle stream);

/*
 * Returns the available space for writing to 'stream', or
 * -A2_NOTIMPLEMENTED if this feature is not implemented on this object.
 */
int a2_Space(A2_state *st, A2_handle stream);

/*
 * Read 'size' bytes of audio from 'stream', converting it into the format
 * specified by 'fmt' and writing it into 'buffer'.
 *
 * This call is non-blocking, and will return A2_BUFUNDERFLOW if the requested
 * amount of data is not available.
 *
FIXME: Do we want support for blocking I/O in this API?
 */
A2_errors a2_Read(A2_state *st, A2_handle stream,
		A2_sampleformats fmt, void *buffer, unsigned size);

/*
 * Write raw audio to 'stream'.
 *
 * 'fmt' is the sample format code for the 'data'.
 *
 * 'data' points to the raw waveform data to convert and upload. If NULL, the
 * resulting waveform will be allocated, and the contents left undefined. If
 * the A2_CLEAR flag is set, 'data' is ignored, and the waveform is cleared.
 * 
 * 'size' is the size in BYTES of 'data'. (This is still used to calculate the
 * waveform size when 'data' is NULL and/or A2_CLEAR is used.)
 *
 * This call is non-blocking, and will return A2_BUFOVERFLOW if it is not
 * possible to write the provided amount of data at the time the call is made.
 *
FIXME: Do we want support for blocking I/O in this API?
 */
A2_errors a2_Write(A2_state *st, A2_handle stream,
		A2_sampleformats fmt, const void *data, unsigned size);

/*
 * For write streams: Ensure that writes until this point are applied to the
 * target object. For read streams: Discard any remaining data in the buffer.
 *
 * Returns A2_OK, or an error code.
 */
A2_errors a2_Flush(A2_state *st, A2_handle stream);

#ifdef __cplusplus
};
#endif

#endif /* A2_STREAM_H */
