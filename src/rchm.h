/*----------------------------------------------------------------------------.
        rchm.h - Reference Counting Handle Manager 0.2                        |
 .----------------------------------------------------------------------------'
 | Copyright (C) 2012-2013 David Olofson <david@olofson.net>
 |
 | This software is provided 'as-is', without any express or implied warranty.
 | In no event will the authors be held liable for any damages arising from the
 | use of this software.
 |
 | Permission is granted to anyone to use this software for any purpose,
 | including commercial applications, and to alter it and redistribute it
 | freely, subject to the following restrictions:
 |
 | 1. The origin of this software must not be misrepresented; you must not
 |    claim that you wrote the original software. If you use this software
 |    in a product, an acknowledgment in the product documentation would be
 |    appreciated but is not required.
 | 2. Altered source versions must be plainly marked as such, and must not be
 |    misrepresented as being the original software.
 | 3. This notice may not be removed or altered from any source distribution.
 |
 | - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 |
 |    Features:
 |	* Maps integer handles to pointers + type codes.
 |	* Thread safe and lock-free!
 |	* Up to 255 object types.
 |		* Type registry with application provided data for each type;
 |			* Type name	(C string)
 |			* Destructor	(callback)
 |			* User data	(void * passed to the destructor)
 |	* 8 bits for flags and the like.
 |	* Up to 1048576 handles. (Compile time configurable up to 2G.)
 |
 |    Restrictions:
 |	1) The handle registry can never shrink - only grow.
 |	2) Only one API thread at a time can safely add or remove handles.
 |	3) A handle can be freed only after it has been ensured that no other
 |	   thread will try to look up or use the handle.
 |	4) If a handle is used as a virtual reference (data pointer managed by
 |	   a different thread), synchronization is required to safely retrieve
 |	   that pointer, or (as per 3) to free the handle.
 '---------------------------------------------------------------------------*/

#ifndef RCHM_H
#define RCHM_H

#include <stdint.h>

#ifndef NULL
#	if defined __GNUG__ &&					\
			(__GNUC__ > 2 ||			\
			(__GNUC__ == 2 && __GNUC_MINOR__ >= 8))
#		define NULL (__null)
#	else
#		if !defined(__cplusplus)
#			define NULL ((void*)0)
#		else
#			define NULL (0)
#		endif
#	endif
#endif

#define	RCHM_MAXBLOCKS		4096
#define	RCHM_BLOCKSIZE_POW2	8	/* 256 handles per block */

#define	RCHM_BLOCKSIZE		(1 << (RCHM_BLOCKSIZE_POW2))
#define	RCHM_BLOCKSIZE_MASK	((RCHM_BLOCKSIZE) - 1)

typedef int32_t RCHM_handle;
typedef uint8_t RCHM_typecode;
typedef uint8_t RCHM_userbits;
typedef uint16_t RCHM_refcount;

typedef enum RCHM_errors {
	RCHM_OK = 0,		/* Everything's fine! */
	RCHM_REFUSE,		/* Destructor wants handle to remain in place */
	RCHM_OOMEMORY,		/* Out of memory */
	RCHM_OOHANDLES,		/* Out of handle address space */
	RCHM_INVALIDHANDLE,	/* Handle has no physical representation */
	RCHM_FREEHANDLE		/* Handle exists, but has been freed */
} RCHM_errors;

/* "Internal" handle implementation */
typedef struct RCHM_handleinfo
{
	union {
		void		*data;	/* Pointer to target object */
		RCHM_handle	prev;	/* Previous handle in free pool */
	} d;
	RCHM_refcount	refcount;
	RCHM_typecode	typecode;
	RCHM_userbits	userbits;
} RCHM_handleinfo;

typedef RCHM_errors (*RCHM_destructor_cb)(RCHM_handleinfo *handleinfo,
		void *typedata, RCHM_handle handle);

typedef struct RCHM_typeinfo
{
	char			*name;
	RCHM_destructor_cb	destructor;
	void			*userdata;
} RCHM_typeinfo;

/* Handle manager instance with an array of RCHM_HM_MAXBLOCKS block pointers */
typedef struct RCHM_manager
{
	RCHM_handleinfo	*blocktab[RCHM_MAXBLOCKS];
	RCHM_handle	pool;		/* LIFO stack of free handles */
	RCHM_handle	nexthandle;	/* Next handle to try if pool empty */

	/* Table of info about registered types */
	int		ntypes;		/* Size of arrays */
	RCHM_typeinfo	*types;		/* Callbacks */
} RCHM_manager;


/* Init/cleanup */
RCHM_errors rchm_Init(RCHM_manager *m, int inithandles);
void rchm_Cleanup(RCHM_manager *m);

/* Register an object type with a destructor */
RCHM_errors rchm_RegisterType(RCHM_manager *m, RCHM_typecode tc,
		const char *name, RCHM_destructor_cb destroy, void *userdata);

/*
 * Get the name or userdata of a type.
 * 
 * Returns NULL if the type has not been registered, or the requested
 * information was not provided.
 */
const char *rchm_TypeName(RCHM_manager *m, RCHM_typecode tc);
void *rchm_TypeUserdata(RCHM_manager *m, RCHM_typecode tc);

/* Add a new block of handles, adding the handles to the pool */
RCHM_errors rchm_AddBlock(RCHM_manager *m, int bi);


/*
 * Locate handle 'h', returning the internal handle struct. This function WILL
 * return handles that are in the free pool!
 *
 * Returns NULL if the handle is not valid.
 */
static inline RCHM_handleinfo *rchm_Locate(RCHM_manager *m, RCHM_handle h)
{
	unsigned bi = h >> RCHM_BLOCKSIZE_POW2;
	int hi = h & RCHM_BLOCKSIZE_MASK;
	if((unsigned)bi >= RCHM_MAXBLOCKS)
		return NULL;	/* Handle out of range! --> */
	if(!m->blocktab[bi])
		return NULL;	/* No block for this range! --> */
	return &m->blocktab[bi][hi];
}


/*
 * Create a new handle, setting its type, data pointer, userbits and initial
 * refcount.
 *
 * Returns a negative error code in case of failure.
 *
 * NOTE: 0 is not a valid type code for 'type'!
 */
static inline RCHM_handle rchm_NewEx(RCHM_manager *m, void *data,
		RCHM_typecode tc, RCHM_userbits ub, RCHM_refcount initrc)
{
	RCHM_handle h;
	RCHM_handleinfo *hi;
	if(m->pool >= 0)
	{
		/* Recycle one from the pool! */
		hi = rchm_Locate(m, m->pool);
		h = m->pool;
		m->pool = hi->d.prev;
	}
	else
	{
		/* Grab a new one off the end of the last block! */
		int bi = m->nexthandle >> RCHM_BLOCKSIZE_POW2;
		if(bi >= RCHM_MAXBLOCKS)
			return -RCHM_OOHANDLES;	/* Can't add more blocks! --> */
		if(!m->blocktab[bi])
		{
			/* Try to add a new block... */
			RCHM_errors res = rchm_AddBlock(m, bi);
			if(res)
				return -res;
		}
		h = m->nexthandle++;
		hi = &m->blocktab[bi][h & RCHM_BLOCKSIZE_MASK];
	}
	hi->d.data = data;
	hi->typecode = tc;
	hi->userbits = ub;
	hi->refcount = initrc;
	return h;
}

/*
 * Create a new handle, setting its type and data pointer. refcount is set to 1
 * and userbits to 0.
 *
 * Returns a negative error code in case of failure.
 *
 * NOTE: 0 is not a valid type code for 'type'!
 */
static inline RCHM_handle rchm_New(RCHM_manager *m, void *d, RCHM_typecode tc)
{
	return rchm_NewEx(m, d, tc, 0, 0);
}


/*
 * Get handle 'h'.
 *
 * Returns NULL if the handle is not valid or not allocated.
 */
static inline RCHM_handleinfo *rchm_Get(RCHM_manager *m, RCHM_handle h)
{
	RCHM_handleinfo *hi = rchm_Locate(m, h);
	if(!hi)
		return NULL;
	if(!hi->typecode)
		return NULL;	/* Handle is free! --> */
	return hi;
}


/*
 * Grab handle 'h', increase the reference count by one, and return the data
 * pointer, provided its typecode matches 'tc'.
 *
 * Returns NULL if the handle is invalid, free or of the wrong type.
 */
static inline void *rchm_Grab(RCHM_manager *m, RCHM_handle h, RCHM_typecode tc)
{
	RCHM_handleinfo *hi = rchm_Locate(m, h);
	if(!hi)
		return NULL;
	if(hi->typecode != tc)
		return NULL;	/* Handle is free or wrong type! --> */
	++hi->refcount;
	return hi->d.data;
}


/*
 * Increase the reference count of handle 'h'.
 *
 * Returns RCHM_INVALIDHANDLE if the handle is invalid, or RCHM_FREEHANDLE in
 * the handle is in the free pool.
 */
static inline RCHM_errors rchm_Retain(RCHM_manager *m, RCHM_handle h)
{
	RCHM_handleinfo *hi = rchm_Locate(m, h);
	if(!hi)
		return RCHM_INVALIDHANDLE;	/* Doesn't exist! */
	if(!hi->typecode)
		return RCHM_FREEHANDLE;		/* Too late? */
	++hi->refcount;
	return RCHM_OK;
}


/*
 * Free a handle, disregarding reference count and destructors.
 *
 * Returns the resulting reference count, or a negative error code;
 *	RCHM_INVALIDHANDLE if the handle physically does not exist.
 *	RCHM_FREEHANDLE if the handle exists, but is already in the free pool.
 */
static inline RCHM_errors rchm_Free(RCHM_manager *m, RCHM_handle h)
{
	RCHM_handleinfo *hi = rchm_Locate(m, h);
	if(!hi)
		return RCHM_INVALIDHANDLE;	/* Doesn't exist! */
	if(!hi->typecode)
		return RCHM_FREEHANDLE;		/* Already gone! */
	hi->typecode = 0;
	hi->d.prev = m->pool;
	m->pool = h;
	return RCHM_OK;
}


/*
 * Release handle 'h'. If the reference count reaches zero, the registered
 * destructor callback for the handle type is called, and, if allowed by the
 * destructor, the handle is then returned to the free pool.
 *
 * Returns the resulting reference count, or a negative error code;
 *	RCHM_REFUSE if the destructor wants the handler to remain allocated.
 *	RCHM_INVALIDHANDLE if the handle physically does not exist.
 *	RCHM_FREEHANDLE if the handle exists, but is already in the free pool.
 *
 * WARNING:
 *	When using this, it is important that no other threads are trying to
 *	use the handle it has been freed! If the handle is only ever looked up
 *	by the API thread, which then passes the returned pointer to the engine
 *	context instead, this is not an issue.
 */
static inline int rchm_Release(RCHM_manager *m, RCHM_handle h)
{
	RCHM_handleinfo *hi = rchm_Locate(m, h);
	if(!hi)
		return -RCHM_INVALIDHANDLE;	/* Doesn't exist! */
	if(!hi->typecode)
		return -RCHM_FREEHANDLE;	/* Already gone! */
	if(hi->refcount && --hi->refcount)	/* Don't wrap...! */
		return hi->refcount;	/* There are still references! */
	if(hi->typecode < m->ntypes)
	{
		/* Try to call a destructor, and see what that says... */
		RCHM_typeinfo *ti = &m->types[hi->typecode];
		if(ti->destructor)
		{
			RCHM_errors res = ti->destructor(hi, ti->userdata, h);
			if(res)
			{
				/* Nope, leave this alone...! */
				hi->refcount = 0;
				return -res;
			}
		}
	}
	hi->typecode = 0;
	hi->d.prev = m->pool;
	m->pool = h;
	return 0;	/* Done! */
}

#endif	/* RCHM_H */
