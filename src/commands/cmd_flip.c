/* ASE - Allegro Sprite Editor
 * Copyright (C) 2007  David A. Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#ifndef USE_PRECOMPILED_HEADER

#include "modules/editors.h"
#include "modules/gui.h"
#include "modules/sprites.h"
#include "raster/image.h"
#include "raster/mask.h"
#include "raster/sprite.h"
#include "raster/undo.h"
#include "util/misc.h"

#endif

static void do_flip(int horz);

/* ======================== */
/* flip_horizontal          */
/* ======================== */

bool command_enabled_flip_horizontal(const char *argument)
{
  return current_sprite != NULL;
}

void command_execute_flip_horizontal(const char *argument)
{
  do_flip(TRUE);
}

/* ======================== */
/* flip_vertical            */
/* ======================== */

bool command_enabled_flip_vertical(const char *argument)
{
  return current_sprite != NULL;
}

void command_execute_flip_vertical(const char *argument)
{
  do_flip(FALSE);
}

/************************************************************/
/* do_flip */

static void do_flip(int horz)
{
  Sprite *sprite = current_sprite;
  Image *image, *area;
  int x1, y1, x2, y2;
  int x, y;

  image = GetImage2(sprite, &x, &y, NULL);
  if (!image)
    return;

  if (mask_is_empty(sprite->mask)) {
    x1 = 0;
    y1 = 0;
    x2 = image->w-1;
    y2 = image->h-1;
  }
  else {
    /* apply the frame offset */
    x1 = sprite->mask->x - x;
    y1 = sprite->mask->y - y;
    x2 = sprite->mask->x + sprite->mask->w - 1 - x;
    y2 = sprite->mask->y + sprite->mask->h - 1 - y;

    /* clip */
    x1 = MID (0, x1, image->w-1);
    y1 = MID (0, y1, image->h-1);
    x2 = MID (0, x2, image->w-1);
    y2 = MID (0, y2, image->h-1);
  }

  /* insert the undo operation */
  if (undo_is_enabled(sprite->undo))
    undo_flip(sprite->undo, image, x1, y1, x2, y2, horz);

  /* flip the portion of the bitmap */
  area = image_crop(image, x1, y1, x2-x1+1, y2-y1+1);
  for (y=0; y<(y2-y1+1); y++)
    for (x=0; x<(x2-x1+1); x++)
      image_putpixel(image,
		     horz ? x2-x: x1+x,
		     !horz? y2-y: y1+y,
		     image_getpixel (area, x, y));
  image_free(area);
  GUI_Refresh(current_sprite);
}