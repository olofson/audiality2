/*
 * alsamididrv.c - Audiality 2 ALSA sequencer MIDI driver
 *
 * Copyright 2016-2017, 2022 David Olofson <david@olofson.net>
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

#ifdef A2_HAVE_ALSA

#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include "alsamididrv.h"
#include "a2_log.h"
#include "a2_dsp.h"

#define	A2_MIDI_CHANNELS	16
#define	A2_7BIT2UNITY		(1.0f / 127.0f)

/*
 * MIDI event types/program entry points
 *
 * NOTE: These do NOT match MIDI event types!
 */
typedef enum
{
	ME_NOTEOFF = 0,
	ME_NOTEON,
	ME_AFTERTOUCH,
	ME_CONTROLCHANGE,
	ME_PROGRAMCHANGE,
	ME_CHANNELPRESSURE,
	ME_PITCHBEND,
	ME_SYSTEM,
	ME_RPN,
	ME_NRPN
} ALSAMD_midi_events;

/* RPN/NRPN state */
typedef struct ALSAMD_chstate
{
	A2_handle	voice;	/* Target voice */

	/* RPN/NRPN parsing */
	int		rpn;	/* 0 ==> NRPN; 1 == RPN */
	unsigned	index;
	unsigned	data;
} ALSAMD_chstate;

/* Extended A2_mididriver struct */
typedef struct ALSA_mididriver
{
	A2_mididriver	md;

	/* Interface for messages */
	A2_interface	*interface;

	/* ALSA sequencer */
	int		port_id;
	snd_seq_t	*seq_handle;

	ALSAMD_chstate	channels[A2_MIDI_CHANNELS];
} ALSA_mididriver;


static inline void alsamd_send3(ALSA_mididriver *amd, int type,
		int ch, float arg)
{
	float args[3];
	args[0] = type;
	args[1] = ch;
	args[2] = arg;
	a2_Senda(amd->interface, amd->channels[ch].voice, 7, 3, args);
}

static inline void alsamd_send4(ALSA_mididriver *amd, int type,
		int ch, float arg1, float arg2)
{
	float args[4];
	args[0] = type;
	args[1] = ch;
	args[2] = arg1;
	args[3] = arg2;
	a2_Senda(amd->interface, amd->channels[ch].voice, 7, 4, args);
}

static inline void alsamd_off(ALSA_mididriver *amd,
		unsigned ch, unsigned pitch, unsigned vel)
{
	alsamd_send4(amd, ME_NOTEOFF, ch, pitch, vel * A2_7BIT2UNITY);
}

static inline void alsamd_on(ALSA_mididriver *amd,
		unsigned ch, unsigned pitch, unsigned vel)
{
	alsamd_send4(amd, ME_NOTEON, ch, pitch, vel * A2_7BIT2UNITY);
}

static inline void alsamd_aftertouch(ALSA_mididriver *amd,
		unsigned ch, unsigned pitch, unsigned press)
{
	alsamd_send4(amd, ME_AFTERTOUCH, ch, pitch, press * A2_7BIT2UNITY);
}

static inline void alsamd_control(ALSA_mididriver *amd,
		unsigned ch, unsigned ctrl, unsigned amt)
{
	alsamd_send4(amd, ME_CONTROLCHANGE, ch, ctrl, amt * A2_7BIT2UNITY);
}

static inline void alsamd_rpn(ALSA_mididriver *amd,
		unsigned ch, unsigned ctrl, unsigned amt)
{
	alsamd_send4(amd, ME_RPN, ch, ctrl, amt * A2_7BIT2UNITY);
}

static inline void alsamd_nrpn(ALSA_mididriver *amd,
		unsigned ch, unsigned ctrl, unsigned amt)
{
	alsamd_send4(amd, ME_NRPN, ch, ctrl, amt * A2_7BIT2UNITY);
}

static inline void alsamd_do_rpn(ALSA_mididriver *amd, unsigned ch)
{
	ALSAMD_chstate *chs = &amd->channels[ch];
	if(chs->index == 16383)
		return;
	if(chs->rpn)
		alsamd_rpn(amd, ch, chs->index, chs->data);
	else
		alsamd_nrpn(amd, ch, chs->index, chs->data);
}

static inline void alsamd_program(ALSA_mididriver *amd,
		unsigned ch, unsigned prog)
{
	alsamd_send3(amd, ME_PROGRAMCHANGE, ch, prog);
}

static inline void alsamd_pressure(ALSA_mididriver *amd,
		unsigned ch, unsigned press)
{
	alsamd_send3(amd, ME_CHANNELPRESSURE, ch, press * A2_7BIT2UNITY);
}

static inline void alsamd_bend(ALSA_mididriver *amd,
		unsigned ch, int amt)
{
	if(amt == 8191)
		amt = 8192;
	alsamd_send3(amd, ME_PITCHBEND, ch, amt * A2_ONEDIV8K);
}

static void alsamd_handle_event(ALSA_mididriver *amd, snd_seq_event_t *ev)
{
	switch (ev->type)
	{
	  case SND_SEQ_EVENT_CONTROLLER:
	  {
		ALSAMD_chstate *chs;
		if(ev->data.control.channel >= A2_MIDI_CHANNELS)
		{
			alsamd_control(amd, ev->data.control.channel,
					ev->data.control.param,
					ev->data.control.value);
			break;
		}
		chs = &amd->channels[ev->data.control.channel];
		switch(ev->data.control.param)
		{
		  case 6:	/* Data Entry (coarse) */
			chs->data = ev->data.control.value << 7;
			break;
		  case 38:	/* Data Entry (fine) */
			chs->data |= ev->data.control.value;
			alsamd_do_rpn(amd, ev->data.control.channel);
			break;
		  case 98:	/* NRPN (fine) */
			chs->rpn = 0;
			chs->index |= ev->data.control.value;
			break;
		  case 99:	/* NRPN (coarse) */
			chs->rpn = 0;
			chs->index = ev->data.control.value << 7;
			break;
		  case 100:	/* RPN (fine) */
			chs->rpn = 1;
			chs->index |= ev->data.control.value;
			break;
		  case 101:	/* RPN (coarse) */
			chs->rpn = 1;
			chs->index = ev->data.control.value << 7;
			break;
		  default:
			alsamd_control(amd, ev->data.control.channel,
					ev->data.control.param,
					ev->data.control.value);
			break;
		}
		break;
	  }
	  case SND_SEQ_EVENT_NONREGPARAM:
		alsamd_nrpn(amd, ev->data.control.channel,
				ev->data.control.param,
				ev->data.control.value);
		break;
	  case SND_SEQ_EVENT_REGPARAM:
		alsamd_rpn(amd, ev->data.control.channel,
				ev->data.control.param,
				ev->data.control.value);
		break;
	  case SND_SEQ_EVENT_PGMCHANGE:
		alsamd_program(amd, ev->data.control.channel,
				ev->data.control.value);
		break;
	  case SND_SEQ_EVENT_NOTEON:
		alsamd_on(amd, ev->data.note.channel,
				ev->data.note.note,
				ev->data.note.velocity);
		break;
	  case SND_SEQ_EVENT_NOTEOFF:
		alsamd_off(amd, ev->data.note.channel,
				ev->data.note.note,
				ev->data.note.velocity);
		break;
	  case SND_SEQ_EVENT_KEYPRESS:
		alsamd_aftertouch(amd, ev->data.note.channel,
				ev->data.note.note,
				ev->data.note.velocity);
		break;
	  case SND_SEQ_EVENT_CHANPRESS:
		alsamd_pressure(amd, ev->data.control.channel,
				ev->data.control.value);
		break;
	  case SND_SEQ_EVENT_PITCHBEND:
		alsamd_bend(amd, ev->data.control.channel,
				ev->data.control.value);
		break;
	  default:
		/* Unknown MIDI event! Just drop it. */
		break;
	}
}


static A2_errors alsamd_Poll(A2_mididriver *driver, unsigned frames)
{
	ALSA_mididriver *amd = (ALSA_mididriver *)driver;
	snd_seq_event_t *ev;
	int res;
	while((res = snd_seq_event_input(amd->seq_handle, &ev)) > 0)
	{
		alsamd_handle_event(amd, ev);
		snd_seq_free_event(ev);
	}
	switch(res)
	{
	  case -EAGAIN:
		return A2_OK;
	  case -ENOSPC:
		/* TODO: Events lost! Should probably do "all notes off." */
		return A2_OK;
	}
	return A2_OK;
}


static A2_errors alsamd_Connect(A2_mididriver *driver, int channel,
			A2_handle voice)
{
	ALSA_mididriver *amd = (ALSA_mididriver *)driver;
	if(channel == -1)
	{
		int i;
		for(i = 0; i < A2_MIDI_CHANNELS; ++i)
			amd->channels[i].voice = voice;
	}
	else if((channel < 0) || (channel >= A2_MIDI_CHANNELS))
		return A2_INDEXRANGE;
	else
		amd->channels[channel].voice = voice;
	return A2_OK;
}


static A2_errors alsamd_Open(A2_driver *driver)
{
	ALSA_mididriver *amd = (ALSA_mididriver *)driver;
	A2_interface *i = driver->config->interface;
	const char *label = "Audiality 2";
	/*
	 * We use A2_NOREF instead of A2_AUTOCLOSE here, because we can't tell
	 * if the application closes this driver explicitly, or relies on the
	 * state cleanup to do it. In the latter case, A2_AUTOCLOSE would have
	 * us trying to close a stale pointer.
	 */
	if(!(amd->interface = a2_Interface(driver->config->interface,
			A2_REALTIME | A2_NOREF)))
	{
		A2_LOG_ERR(i, "Could not create realtime interface!");
		return A2_DEVICEOPEN;
	}
	if(snd_seq_open(&amd->seq_handle, "default", SND_SEQ_OPEN_DUPLEX,
			SND_SEQ_NONBLOCK) < 0)
	{
		A2_LOG_ERR(i, "alsaseq_open(): Error opening sequencer!");
		a2_Close(amd->interface);
		return A2_DEVICEOPEN;
	}
	snd_seq_set_client_name(amd->seq_handle, label);
	amd->port_id = snd_seq_create_simple_port(amd->seq_handle, label,
			SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
			SND_SEQ_PORT_TYPE_SYNTH);
	if(amd->port_id < 0)
	{
		A2_LOG_ERR(i, "alsaseq_open(): Error creating port!");
		a2_Close(amd->interface);
		snd_seq_close(amd->seq_handle);
		amd->seq_handle = NULL;
		return A2_DEVICEOPEN;
	}
	amd->md.Poll = alsamd_Poll;
	amd->md.Connect = alsamd_Connect;
	return A2_OK;
}


static void alsamd_Close(A2_driver *driver)
{
	ALSA_mididriver *amd = (ALSA_mididriver *)driver;
	if(amd->interface)
		a2_Close(amd->interface);
	if(amd->seq_handle)
	{
		snd_seq_close(amd->seq_handle);
		amd->seq_handle = NULL;
	}
}


A2_driver *a2_alsa_mididriver(A2_drivertypes type, const char *name)
{
	ALSA_mididriver *amd = calloc(1, sizeof(ALSA_mididriver));
	A2_driver *d = &amd->md.driver;
	if(!amd)
		return NULL;
	d->type = A2_MIDIDRIVER;
	d->name = "alsa";
	d->Open = alsamd_Open;
	d->Close = alsamd_Close;
	d->flags = A2_REALTIME;
	return d;
}

#endif /* A2_HAVE_ALSA */
