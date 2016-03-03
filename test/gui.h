/*
 * gui.h - Tracker style GUI
 *
 * Copyright 2006, 2011-2012, 2016 David Olofson <david@olofson.net>
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

#ifndef	GUI_H
#define	GUI_H

#include "SDL.h"

#define MAXRECTS	1024
#define	FONT_CW		16
#define	FONT_CH		16

/*
 * Low level GUI stuff
 */

/* Load and convert image */
SDL_Surface *gui_load_image(const char *fn);

/* Add a dirtyrect */
void gui_dirty(SDL_Rect *r);

/* Update all dirty areas */
void gui_refresh(void);

/* Draw a hollow box */
void gui_box(int x, int y, int w, int h, Uint32 c);

/* Draw a black box with a colored outline */
void gui_bar(int x, int y, int w, int h, Uint32 c);

/* Render text */
void gui_text(int x, int y, const char *txt);

/* Render an oscilloscope */
void gui_oscilloscope(Sint32 *buf, int bufsize, int start,
		int x, int y, int w, int h);

/*
 * High level GUI stuff
 */
#if (SDL_MAJOR_VERSION >= 2)
int gui_open(SDL_Renderer *screen);
#else
int gui_open(SDL_Surface *screen);
#endif
void gui_close(void);

void gui_cpuload(int v);
void gui_voices(int v);
void gui_instructions(int v);
void gui_bankinfo(int row, const char *label, const char *text);
void gui_message(const char *message, int curspos);

void gui_draw_screen(void);

#endif	/* GUI_H */
