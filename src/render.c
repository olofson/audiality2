/*
 * render.c - Audiality 2 off-line and asynchronous rendering
 *
 * Copyright 2013-2016, 2022 David Olofson <david@olofson.net>
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

#include "audiality2.h"

/*
 * Run 'program' off-line with the specified arguments, rendering at
 * 'samplerate', writing the output to 'stream'.
 * 
 * Rendering will stop after 'length' sample frames have been rendered, or if
 * 'length' is 0, when the output is silent.
 *
 * Returns number of sample frames rendered, or a negated A2_errors error code.
 */
int a2_Render(A2_interface *i,
		A2_handle stream,
		unsigned samplerate, unsigned length, A2_property *props,
		A2_handle program, unsigned argc, float *argv)
{
	int res;
	A2_handle h;
	A2_driver *drv;
	A2_config *cfg;
	A2_interface *ssi;
	int frames = 0;
	unsigned lastpeak = 0; /* Frames since last peak > abs(silencelevel) */
	int offlinebuffer, silencewindow, silencegrace;
	float silencelevel;

	a2_GetStateProperty(i, A2_POFFLINEBUFFER, &offlinebuffer);
	a2_GetStatePropertyf(i, A2_PSILENCELEVEL, &silencelevel);
	a2_GetStateProperty(i, A2_PSILENCEWINDOW, &silencewindow);
	a2_GetStateProperty(i, A2_PSILENCEGRACE, &silencegrace);

	/* Open off-line substate for rendering */
	if(!(drv = a2_NewDriver(A2_AUDIODRIVER, "buffer")))
		return -a2_LastError();
	if(!(cfg = a2_OpenConfig(samplerate, offlinebuffer, 1, A2_AUTOCLOSE)))
		return -a2_LastError();
	if(drv && a2_AddDriver(cfg, drv))
		return -a2_LastError();
	if(!(ssi = a2_SubState(i, cfg)))
		return -a2_LastError();

	/* Parse the property table, if one was provided */
	if(props)
		a2_SetStateProperties(ssi, props);

	/* Start program! */
	if((h = a2_Starta(ssi, a2_RootVoice(ssi), program, argc, argv)) < 0)
		return h;

	/* Render... */
	while(1)
	{
		int j;
		float *buf = ((A2_audiodriver *)drv)->buffers[0];
		unsigned frag = cfg->buffer;
		if(length && (frag > length - frames))
			frag = length - frames;
		if(!frag)
			break;
		if((res = a2_Run(ssi, frag)) < 0)
		{
			a2_Close(ssi);
			return res;
		}
		if(!length)
		{
			lastpeak += frag;
			for(j = 0; j < frag; ++j)
				if((buf[j] > silencelevel) ||
						(-buf[j] > silencelevel))
					lastpeak = frag - j;
		}
		if((res = a2_Write(i, stream, A2_F32, buf,
				frag * sizeof(float))))
		{
			a2_Close(ssi);
			return -res;
		}
		frames += frag;
		if(length)
		{
			if(frames >= length)
				break;
		}
		else
		{
			if((frames >= silencegrace) &&
					(lastpeak >= silencewindow))
				break;
		}
	}

	res = a2_LastRTError(ssi);

	a2_TimestampReset(ssi);
	a2_Send(ssi, h, 1);
	a2_Release(ssi, h);

	/* Close substate */
	a2_Close(ssi);

	if(res)
		return -res;
	else
		return frames;
}


/*
 * Create a wave as specified by 'wt', 'period' and 'flags', then run 'program'
 * off-line with the specified arguments, writing the output into the wave.
 *
 * If 'period' is 0, wave tuning will be configured so that a pitch of 0.0
 * plays the wave back at 'samplerate'.
 *
 * Rendering will stop after 'length' sample frames have been rendered, or if
 * 'length' is 0, when the output is silent.
 * 
 * The wave will be returned prepared and ready for use.
 *
 * Returns the handle of the rendered wave, or a negated A2_errors error code.
 */
A2_handle a2_RenderWave(A2_interface *i,
		A2_wavetypes wt, unsigned period, int flags,
		unsigned samplerate, unsigned length, A2_property *props,
		A2_handle program, unsigned argc, float *argv)
{
	int res;
	A2_handle wh, sh;
	if(!period)
		period = samplerate / A2_MIDDLEC;
	if((wh = a2_NewWave(i, wt, period, flags)) < 0)
		return wh;
	if((sh = a2_OpenStream(i, wh, 0, 0, 0)) < 0)
	{
		a2_Release(i, wh);
		return sh;
	}

	res = a2_Render(i, sh, samplerate, length, props,
			program, argc, argv);
	if(res < 0)
	{
		a2_Release(i, sh);
		a2_Release(i, wh);
		return res;
	}

	if((res = a2_Release(i, sh)))
	{
		a2_Release(i, wh);
		return -res;
	}

	return wh;
}
