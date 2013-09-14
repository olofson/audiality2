/*
 * inline.c - Audiality 2 inline subvoice processing unit
 *
 * Copyright 2012 David Olofson <david@olofson.net>
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

#include "inline.h"


static A2_errors a2i_Initialize(A2_unit *u, A2_vmstate *vms, A2_config *cfg,
		unsigned flags)
{
	A2_inline *il = (A2_inline *)u;
	il->state = cfg->state;
	il->voice = a2_voice_from_vms(vms);
	il->voice->noutputs = u->noutputs;
	il->voice->outputs = u->outputs;
	if(flags & A2_PROCADD)
		u->Process = a2i_ProcessAdd;
	else
		u->Process = a2i_Process;
	return A2_OK;
}


static const A2_crdesc regs[] =
{
	{ NULL, NULL }
};

const A2_unitdesc a2_inline_unitdesc =
{
	"inline",		/* name */

	regs,			/* registers */

	0, 0,			/* [min,max]inputs */
	1, A2_MAXCHANNELS,	/* [min,max]outputs */

	sizeof(A2_inline),	/* instancesize */
	a2i_Initialize,		/* Initialize */
	NULL			/* Deinitialize */
};
