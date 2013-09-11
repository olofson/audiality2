/*
 * gui.c - Tracker style GUI
 *
 * Copyright (C) 2006, 2011-2012 David Olofson <david@olofson.net>
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
#include <string.h>
#include "gui.h"

#define	MAXACTIVITY	400

#define	BANKINFO_Y	(156 + FONT_CH * 8 + 12 + 6)

static int dirtyrects = 0;
static SDL_Rect dirtytab[MAXRECTS];

static SDL_Surface *screen = NULL;
static SDL_Surface *font = NULL;
static SDL_Surface *logo = NULL;
static Uint32 fwc;

static char *message_text = NULL;
//static int activity[SSEQ_TRACKS];


void gui_dirty(SDL_Rect *r)
{
	if(dirtyrects < 0)
		return;
	if((dirtyrects >= MAXRECTS) || !r)
		dirtyrects = -1;
	else
	{
		dirtytab[dirtyrects] = *r;
		++dirtyrects;
	}
}


void gui_refresh(void)
{
	if(dirtyrects < 0)
	{
		SDL_UpdateRect(screen, 0, 0, 0, 0);
		dirtyrects = 0;
	}
	else
		SDL_UpdateRects(screen, dirtyrects, dirtytab);
	dirtyrects = 0;
}


void gui_box(int x, int y, int w, int h, Uint32 c, SDL_Surface *dst)
{
	SDL_Rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = 1;
	SDL_FillRect(dst, &r, c);

	r.x = x;
	r.y = y + h - 1;
	r.w = w;
	r.h = 1;
	SDL_FillRect(dst, &r, c);

	r.x = x;
	r.y = y + 1;
	r.w = 1;
	r.h = h - 2;
	SDL_FillRect(dst, &r, c);

	r.x = x + w - 1;
	r.y = y + 1;
	r.w = 1;
	r.h = h - 2;
	SDL_FillRect(dst, &r, c);

	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	gui_dirty(&r);
}


void gui_bar(int x, int y, int w, int h, Uint32 c, SDL_Surface *dst)
{
	SDL_Rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	SDL_FillRect(dst, &r, SDL_MapRGB(dst->format, 0, 0, 0));
	gui_box(x, y, w, h, c, dst);
}


void gui_oscilloscope(Sint32 *buf, int bufsize,
		int start, int x, int y, int w, int h,
		SDL_Surface *dst)
{
	int i;
	Uint32 green, red;
	SDL_Rect r;
	int xscale = bufsize / w;
	if(xscale < 1)
		xscale = 1;
	else if(xscale > 8)
		xscale = 8;

	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	SDL_FillRect(dst, &r, SDL_MapRGB(dst->format, 0, 0, 0));
	gui_dirty(&r);

	green = SDL_MapRGB(dst->format, 0, 200, 0);
	red = SDL_MapRGB(dst->format, 255, 0, 0);
	r.w = 1;
	for(i = 0; i < w; ++i)
	{
		Uint32 c = green;
		int s = -buf[(start + i * xscale) % bufsize] >> 8;
		s *= h;
		s >>= 16;
		r.x = x + i;
		if(s < 0)
		{
			if(s <= -h / 2)
			{
				s = -h / 2;
				c = red;
			}
			r.y = y + h / 2 + s;
			r.h = -s;
		}
		else
		{
			++s;
			if(s >= h / 2)
			{
				s = h / 2;
				c = red;
			}
			r.y = y + h / 2;
			r.h = s;
		}
		SDL_FillRect(dst, &r, c);
	}

	r.x = x;
	r.y = y + h / 2;
	r.w = w;
	r.h = 1;
	SDL_FillRect(dst, &r, SDL_MapRGB(dst->format, 128, 128, 255));
}



SDL_Surface *gui_load_image(const char *fn)
{
	SDL_Surface *cvt;
	SDL_Surface *img = SDL_LoadBMP(fn);
	if(!img)
		return NULL;
	cvt = SDL_DisplayFormat(img);
	SDL_FreeSurface(img);
	return cvt;
}


void gui_text(int x, int y, const char *txt, SDL_Surface *dst)
{
	int sx = x;
	int sy = y;
	const char *stxt = txt;
	int highlights = 0;
	SDL_Rect sr;
	sr.w = FONT_CW;
	sr.h = FONT_CH;
	while(*txt)
	{
		int c = *txt++;
		switch(c)
		{
		  case 0:	/* terminator */
			break;
		  case '\n':	/* newline */
			x = sx;
			y += FONT_CH;
			break;
		  case '\t':	/* tab */
			x -= sx;
			x += 8 * FONT_CW;
			x %= 8 * FONT_CW;
			x += sx;
			break;
		  case '\001':	/* red highlight */
		  case '\002':	/* green highlight */
		  case '\003':	/* yellow highlight */
		  case '\004':	/* blue highlight */
		  case '\005':	/* purple highlight */
		  case '\006':	/* cyan highlight */
		  case '\007':	/* white highlight */
			highlights = 1;
			if(*txt == '\001')
				txt += 2;
			break;
		  case '\021':	/* red bullet */
		  case '\022':	/* green bullet */
		  case '\023':	/* yellow bullet */
		  case '\024':	/* blue bullet */
		  case '\025':	/* purple bullet */
		  case '\026':	/* cyan bullet */
		  case '\027':	/* white bullet */
		  {
			SDL_Rect r;
			int hlr = c & 1 ? 255 : 0;
			int hlg = c & 2 ? 255 : 0;
			int hlb = c & 4 ? 255 : 0;
			Uint32 hlc = SDL_MapRGB(dst->format, hlr, hlg, hlb);
			r.x = x;
			r.y = y;
			r.w = FONT_CW;
			r.h = FONT_CH;
			SDL_FillRect(dst, &r,
					SDL_MapRGB(dst->format, 0, 0, 0));
			gui_dirty(&r);
			r.x = x + 2;
			r.y = y + 2;
			r.w = FONT_CW - 6;
			r.h = FONT_CH - 6;
			SDL_FillRect(dst, &r, hlc);
			x += FONT_CW;
			break;
		  }
		  default:	/* printables */
		  {
			SDL_Rect dr;
			if(c < ' ' || c > 127)
				c = 127;
			c -= 32;
			sr.x = (c % (font->w / FONT_CW)) * FONT_CW;
			sr.y = (c / (font->w / FONT_CW)) * FONT_CH;
			dr.x = x;
			dr.y = y;
			SDL_BlitSurface(font, &sr, dst, &dr);
			gui_dirty(&dr);
			x += FONT_CW;
			break;
		  }
		}
	}
	if(!highlights)
		return;
	x = sx;
	y = sy;
	txt = stxt;
	while(*txt)
	{
		int c = *txt++;
		switch(c)
		{
		  case 0:	/* terminator */
			break;
		  case '\n':	/* newline */
			x = sx;
			y += FONT_CH;
			break;
		  case '\t':	/* tab */
			x -= sx;
			x += 8 * FONT_CW;
			x %= 8 * FONT_CW;
			x += sx;
			break;
		  case '\001':	/* red highlight */
		  case '\002':	/* green highlight */
		  case '\003':	/* yellow highlight */
		  case '\004':	/* blue highlight */
		  case '\005':	/* purple highlight */
		  case '\006':	/* cyan highlight */
		  case '\007':	/* white highlight */
		  {
			int hlr = c & 1 ? 255 : 0;
			int hlg = c & 2 ? 255 : 0;
			int hlb = c & 4 ? 255 : 0;
			Uint32 hlc = SDL_MapRGB(screen->format, hlr, hlg, hlb);
			int hlw = 1;
			if(*txt == '\001')
			{
				hlw = txt[1];
				txt += 2;
			}
			gui_box(x - 2, y - 2,
					FONT_CW * hlw + 2, FONT_CH + 2,
					hlc, dst);
			break;
		  }
		  default:	/* printables */
			x += FONT_CW;
			break;
		}
	}
}


void gui_cpuload(int v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "CPULoad: %3.1d%%", v);
	gui_text(12, 60, buf, screen);
}


void gui_voices(int v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), " Voices: %4.1d", v);
	gui_text(12, 60 + FONT_CH, buf, screen);
}


void gui_instructions(int v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "I/s: %8.1d", v);
	gui_text(12, 60 + FONT_CH * 2, buf, screen);
}


void gui_bankinfo(int row, const char *label, const char *text)
{
	int y0 = BANKINFO_Y;
	if(!row && !label && !text)
	{
		gui_bar(6, BANKINFO_Y, screen->w - 12, FONT_CH * 7 + 12, fwc,
				screen);
		return;
	}
	gui_text(12, y0 + 6 + FONT_CH * row, label, screen);
	gui_text(12 + 12 * FONT_CW, y0 + 6 + FONT_CH * row, text, screen);
}


void gui_message(const char *message, int curspos)
{
	int y0 = screen->h - FONT_CH - 12;
	SDL_Rect r;
	r.x = 10;
	r.y = y0 - 2;
	r.w = screen->w - 20;
	r.h = FONT_CH + 4;
	SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0, 0, 0));
	gui_dirty(&r);
	if(message)
	{
		free(message_text);
		message_text = strdup(message);
	}
	if(message_text)
		gui_text(12, y0, message_text, screen);
	if(curspos >= 0)
		gui_text(12 + FONT_CW * curspos, y0, "\007", screen);
}


static void gui_logo(Uint32 c)
{
	gui_bar(6, 6, 258, 44, c, screen);
	if(logo)
	{
		SDL_Rect r;
		r.x = 7;
		r.y = 7;
		r.w = logo->w;
		SDL_BlitSurface(logo, NULL, screen, &r);
		gui_dirty(&r);
	}
	else
	{
		gui_text(18, 17, "  Audiality 2", screen);
		gui_box(6 + 3, 6 + 3, 258 - 6, 44 - 6, c, screen);
	}
}


void gui_draw_screen(void)
{
	int w;
	fwc = SDL_MapRGB(screen->format, 0, 128, 0);

	/* Clear */
	SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 48, 0));
	gui_dirty(NULL);

	gui_logo(fwc);

	/* Oscilloscope frames */
	w = (screen->w - 270) / 2 - 8;
	gui_bar(270 - 2, 8 - 2, w + 4, 128 + 4, fwc, screen);
	gui_bar(270 + w + 8 - 2, 8 - 2, w + 4, 128 + 4, fwc, screen);

	/* Song info panel */
	gui_bar(6, 54, 258, 4 * FONT_CH + 12 + 8, fwc, screen);
	gui_cpuload(0);
	gui_voices(0);
	gui_instructions(0);

	/* Instructions */
	gui_bar(6, 150, screen->w - 12, FONT_CH * 8 + 12, fwc, screen);
	gui_text(12, 156,
			"\005\001\003ESC to quit       "
					"\005R to reload sounds\n"
			"\005\001\002F1..\005\001\003F12 for chromatic "
					"playing\n"
			"\005\001\005Shift or \005\001\004Ctrl for "
					"legato mode on the F keys\n"
			"\005*/\005/ to increase/decrease modulation\n"
			"\005\001\004PgUp/\005\001\004PgDn to "
					"shift one octave up/down\n"
			"\005P/\005Y to start/stop sound   "
					"\005K to kill all sounds\n"
			"\005\001\003End/\005\001\004Home or "
					"\005\001\003Del/\005\001\003Ins to "
					"select next/previous bank\n"
			"\005+/\005- to select next/previous bank object\n",
			screen);

	gui_bankinfo(0, NULL, NULL);

	/* Message bar */
	gui_bar(6, screen->h - FONT_CH - 12 - 6,
			screen->w - 12, FONT_CH + 12, fwc, screen);
	gui_message(NULL, -1);
}


int gui_open(SDL_Surface *scrn)
{
	screen = scrn;
	font = gui_load_image("data/font.bmp");
	if(!font)
	{
		fprintf(stderr, "Couldn't load font!\n");
		return -1;
	}
	logo = gui_load_image("data/Audiality2-256x42.bmp");
	if(!logo)
	{
		fprintf(stderr, "Couldn't load logo!\n");
#if 0
		return -1;
#endif
	}
//	SDL_EnableKeyRepeat(250, 25);
//	memset(activity, 0, sizeof(activity));
	return 0;
}


void gui_close(void)
{
	SDL_FreeSurface(font);
	font = NULL;
}
