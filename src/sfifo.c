/*
------------------------------------------------------------
   SFIFO 2.0 - Simple portable lock-free FIFO
------------------------------------------------------------
 * Copyright (C) 2000-2009, 2012 David Olofson
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

#include "sfifo.h"

#include <string.h>
#include <stdlib.h>

#ifdef _SFIFO_TEST_
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#define DBG(x)	/*(x)*/
#define TEST_BUFSIZE	10
#else
#define DBG(x)
#endif


SFIFO *sfifo_Open(unsigned size)
{
	SFIFO *f;
	unsigned bsize;
	if(size > SFIFO_MAX_BUFFER_SIZE)
		return NULL;	/* Too large buffer! */

	/*
	 * Set sufficient power-of-2 size.
	 *
	 * No, there's no bug. If you need
	 * room for N bytes, the buffer must
	 * be at least N+1 bytes. (The fifo
	 * can't tell 'empty' from 'full'
	 * without unsafe index manipulations
	 * otherwise.)
	 */
	for(bsize = 1; bsize <= size; bsize <<= 1)
		;

	f = (SFIFO *)malloc(sizeof(SFIFO) + bsize);
	if(!f)
		return NULL;
	f->size = bsize;
	f->readpos = f->writepos = 0;
	f->flags = SFIFO_IS_OPEN | SFIFO_FREE_MEMORY;
	return f;
}


SFIFO *sfifo_Init(void *mem, unsigned memsize)
{
	SFIFO *f = (SFIFO *)mem;
	unsigned bsize;
	memsize -= sizeof(SFIFO);
	if(memsize > SFIFO_MAX_BUFFER_SIZE)
		memsize = SFIFO_MAX_BUFFER_SIZE;
	for(bsize = 1; bsize <= memsize; bsize <<= 1)
		;
	bsize >>= 1;
	f->size = bsize;
	f->readpos = f->writepos = 0;
	f->flags = SFIFO_IS_OPEN;
	return f;
}


void sfifo_Close(SFIFO *f)
{
	if(!(f->flags & SFIFO_IS_OPEN))
		return;	/* Already closed! */
	if(f->flags & SFIFO_FREE_MEMORY)
	{
		memset(f, 0, sizeof(SFIFO));
		free(f);
	}
	else
		memset(f, 0, sizeof(SFIFO));
}


void sfifo_Flush(SFIFO *f)
{
	f->readpos = 0;
	f->writepos = 0;
}


int sfifo_Write(SFIFO *f, const void *_buf, unsigned len)
{
	unsigned total;
	int i;
	const char *buf = (const char *)_buf;
	char *buffer = (char *)(f + 1);

	if(!(f->flags & SFIFO_IS_OPEN))
		return SFIFO_CLOSED;

	/* total = len = min(space, len) */
	total = sfifo_Space(f);
	if(len > total)
		len = total;
	else
		total = len;

	i = f->writepos;
	if(i + len > f->size)
	{
		memcpy(buffer + i, buf, f->size - i);
		buf += f->size - i;
		len -= f->size - i;
		i = 0;
	}
	memcpy(buffer + i, buf, len);
	f->writepos = i + len;

	return (int)total;
}


int sfifo_Read(SFIFO *f, void *_buf, unsigned len)
{
	unsigned total;
	int i;
	char *buf = (char *)_buf;
	char *buffer = (char *)(f + 1);

	if(!(f->flags & SFIFO_IS_OPEN))
		return SFIFO_CLOSED;

	/* total = len = min(used, len) */
	total = sfifo_Used(f);
	if(len > total)
		len = total;
	else
		total = len;

	i = f->readpos;
	if(i + len > f->size)
	{
		memcpy(buf, buffer + i, f->size - i);
		buf += f->size - i;
		len -= f->size - i;
		i = 0;
	}
	memcpy(buf, buffer + i, len);
	f->readpos = i + len;

	return (int)total;
}


int sfifo_WriteSpin(SFIFO *f, const void *buf, unsigned len)
{
	while(sfifo_Space(f) < len)
		;
	return sfifo_Write(f, buf, len);
}


int sfifo_ReadSpin(SFIFO *f, void *buf, unsigned len)
{
	while(sfifo_Used(f) < len)
		;
	return sfifo_Read(f, buf, len);
}


#ifdef _SFIFO_TEST_
#include <stdio.h>
void *sender(void *arg)
{
	char buf[TEST_BUFSIZE*2];
	int i,j;
	int cnt = 0;
	int res;
	SFIFO *sf = (SFIFO *)arg;
	while(1)
	{
		j = sfifo_Space(sf);
		for(i = 0; i < j; ++i)
		{
			++cnt;
			buf[i] = cnt;
		}
		res = sfifo_Write(sf, &buf, j);
		if(res != j)
		{
			printf("Write failed!\n");
			sleep(1);
		} else if(res)
			printf("Wrote %d\n", res);
	}
}
int main()
{
	SFIFO *sf;
	char last = 0;
	pthread_t thread;
	char buf[100] = "---------------------------------------";
#if 0
	char mem[1000];
	sf = sfifo_Init(&mem, sizeof(mem));
	printf("sfifo_Init(%p, %lu) = %p\n", &mem, sizeof(mem), sf);
#else
	sf = sfifo_Open(TEST_BUFSIZE);
	printf("sfifo_Open(%d) = %p\n", TEST_BUFSIZE, sf);
#endif

#if 0
	printf("sfifo_Write(sf, \"0123456789\", 7) = %d\n",
		sfifo_Write(sf, "0123456789", 7) );

	printf("sfifo_Write(sf, \"abcdefghij\", 7) = %d\n",
		sfifo_Write(sf, "abcdefghij", 7) );

	printf("sfifo_Read(sf, buf, 8) = %d\n",
		sfifo_Read(&sf, buf, 8) );

	buf[20] = 0;
	printf("buf =\"%s\"\n", buf);

	printf("sfifo_Write(sf, \"0123456789\", 7) = %d\n",
		sfifo_Write(sf, "0123456789", 7) );

	printf("sfifo_Read(sf, buf, 10) = %d\n",
		sfifo_Read(sf, buf, 10) );

	buf[20] = 0;
	printf("buf =\"%s\"\n", buf);
#else
	pthread_create(&thread, NULL, sender, sf);

	while(1)
	{
		static int c = 0;
		++last;
		while(sfifo_Read(sf, buf, 1) != 1)
			/*sleep(1)*/;
		if(last != buf[0])
		{
			printf("Error %d!\n", buf[0] - last);
			last = buf[0];
		}
		else
			printf("Ok. (%d)\n", ++c);
	}
#endif

	sfifo_Close(sf);
	printf("sfifo_Close(sf)\n");

	return 0;
}

#endif
