/*
------------------------------------------------------------
   SFIFO 2.1 - Simple portable lock-free FIFO
------------------------------------------------------------
 * Copyright 2000-2009, 2012, 2014 David Olofson
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held
 * liable for any damages arising from the use of this
 * software.
 *
 * Permission is granted to anyone to use this software for
 * any purpose, including commercial applications, and to
 * alter it and redistribute it freely, subject to the
 * following restrictions:
 *
 * 1. The origin of this software must not be misrepresented;
 *    you must not claim that you wrote the original
 *    software. If you use this software in a product, an
 *    acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such,
 *    and must not be misrepresented as being the original
 *    software.
 * 3. This notice may not be removed or altered from any
 *    source distribution.
 */

#ifndef SFIFO_H
#define	SFIFO_H

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------
	"Private" stuff
------------------------------------------------*/
/*
 * Porting note:
 *	Reads and writes of a variable of this type in memory
 *	must be *atomic*! 'int' is *not* atomic on all platforms.
 *	A safe type should be used, and  sfifo should limit the
 *	maximum buffer size accordingly.
 */
typedef volatile int SFIFO_ATOMIC;

/* Kludge: Assume 32 bit platform */
#define	SFIFO_MAX_BUFFER_SIZE	0x7fffffff

/* (flags) Set if SFIFO is initialized and open */
#define	SFIFO_IS_OPEN		0x00000001

/* (flags) Set if SFIFO was allocated using malloc() */
#define	SFIFO_FREE_MEMORY	0x00000002

typedef struct SFIFO
{
	unsigned	size;		/* Number of bytes */
	unsigned	flags;
	SFIFO_ATOMIC	readpos;	/* Read position */
	SFIFO_ATOMIC	writepos;	/* Write position */
	/* (Buffer follows this structure!) */
} SFIFO;

#define SFIFO_SIZEMASK(x)	((x)->size - 1)


/*------------------------------------------------
	API
------------------------------------------------*/

typedef enum
{
	SFIFO_MEMORY =	-1,
	SFIFO_CLOSED =	-2
} SFIFO_errors;

/* Allocate and initialize FIFO that fits at least 'size' bytes of data. */
SFIFO *sfifo_Open(unsigned size);

/* Create the largest possible FIFO in the specified memory area. */
SFIFO *sfifo_Init(void *mem, unsigned memsize);

/* Close and (where applicable) deallocate the specified FIFO. */
void sfifo_Close(SFIFO *f);

/* Returns the number of bytes in use (available for reading) in 'f'. */
static inline int sfifo_Used(SFIFO *f)
{
	return (f->writepos - f->readpos) & SFIFO_SIZEMASK(f);
}

/* Returns the number of unused bytes (available for writing) in 'f'. */
static inline int sfifo_Space(SFIFO *f)
{
	return f->size - 1 - sfifo_Used(f);
}

/*
 * Write up to 'len' bytes to FIFO 'f'. Returns the number of bytes written,
 * or a negative error code.
 */
int sfifo_Write(SFIFO *f, const void *buf, unsigned len);

/*
 * Read up to 'len' bytes from FIFO 'f'. Returns the number of bytes actually
 * read, or a negative error code.
 */
int sfifo_Read(SFIFO *f, void *buf, unsigned len);

/*
 * Skip up to 'len' bytes from FIFO 'f' without actually reading it. Returns
 * the number of bytes discarded, or a negative error code.
 *
 * NOTE: This is thread safe when called from the sfifo_Read() context.
 */
int sfifo_Skip(SFIFO *f, unsigned len);

/*
 * Clear the FIFO, discarding all data currently available for reading. Returns
 * the number of bytes discarded, or a negative error code.
 *
 * NOTE: As of version 2.1, this is actually thread safe - but only when called
 *       from the sfifo_Read() context!
 */
static inline int sfifo_Flush(SFIFO *f)
{
	return sfifo_Skip(f, sfifo_Used(f));
}

/*
 * Spinning versions of sfifo_Read() and sfifo_Write().
 *
 * NOTE:
 *	These are BUSY-WAITING versions that should ONLY be used on
 *	multiprocessor systems, in applications where one CPU will
 *	occasionally wait for data from another for brief moments!
 */
int sfifo_WriteSpin(SFIFO *f, const void *buf, unsigned len);
int sfifo_ReadSpin(SFIFO *f, void *buf, unsigned len);

#ifdef __cplusplus
};
#endif

#endif /* SFIFO_H */
