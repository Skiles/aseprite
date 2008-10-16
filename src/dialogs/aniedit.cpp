/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2008  David A. Capello
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

#include <allegro.h>
#include <assert.h>

#include "jinete/jinete.h"
/* #include "jinete/jintern.h" */

#include "commands/commands.h"
/* #include "core/app.h" */
/* #include "core/cfg.h" */
/* #include "core/core.h" */
#include "modules/gfx.h"
#include "modules/gui.h"
/* #include "modules/palettes.h" */
#include "modules/rootmenu.h"
#include "modules/sprites.h"
#include "raster/raster.h"
#include "raster/undoable.h"
#include "util/celmove.h"
#include "util/functions.h"
#include "util/thmbnail.h"

/*
   Animator Editor

                        Frames ...
                        
   --------------------+-----+-----+-----+---
            Layers     |msecs|msecs|msecs|...
   --------------------+-----+-----+-----+---
    [1] [2] Layer 1    | Cel | Cel | Cel |...
   --------------------+-----+-----+-----+---
    [1] [2] Background | Cel | Cel | Cel |...
   --------------------+-----+-----+-----+---

   [1] Eye-icon
   [2] Padlock-icon
 */

/* size of the thumbnail in the screen (width x height), the really
   size of the thumbnail bitmap is specified in the
   'generate_thumbnail' routine */
#define THUMBSIZE	(32*guiscale())

/* height of the headers */
#define HDRSIZE		(3 + text_height(widget->text_font)*2 + 3 + 3)

/* width of the frames */
#define FRMSIZE		(3 + THUMBSIZE + 3)

/* height of the layers */
#define LAYSIZE		(3 + MAX(text_height(widget->text_font), THUMBSIZE) + 4)

/* space between icons and other information in the layer */
#define ICONSEP		(2*guiscale())

/* space between the icon-bitmap and the edge of the surrounding button */
#define ICONBORDER	(4*guiscale())

enum {
  STATE_STANDBY,
  STATE_SCROLLING,
  STATE_MOVING_SEPARATOR,
  STATE_MOVING_LAYER,
  STATE_MOVING_CEL,
  STATE_MOVING_FRAME,
};

enum {
  PART_NOTHING,
  PART_SEPARATOR,
  PART_HEADER_LAYER,
  PART_HEADER_FRAME,
  PART_LAYER,
  PART_LAYER_EYE_ICON,
  PART_LAYER_LOCK_ICON,
  PART_CEL
};

typedef struct AniEditor
{
  Sprite* sprite;
  int state;
  Layer* selected_layer;
  Layer** layers;
  int nlayers;
  int scroll_x;
  int scroll_y;
  int separator_x;
  int separator_w;
  /* the 'hot' part is where the mouse is on top of */
  int hot_part;
  int hot_layer;
  int hot_frame;
  /* the 'clk' part is where the mouse's button was pressed (maybe for
     a drag & drop operation) */
  int clk_part;
  int clk_layer;
  int clk_frame;
  /* keys */
  bool space_pressed;
} AniEditor;

static JWidget current_anieditor = NULL;

static JWidget anieditor_new(Sprite* sprite);
static int anieditor_type();
static AniEditor* anieditor_data(JWidget widget);
static bool anieditor_msg_proc(JWidget widget, JMessage msg);
static void anieditor_setcursor(JWidget widget, int x, int y);
static void anieditor_get_drawable_layers(JWidget widget, JRect clip, int* first_layer, int* last_layer);
static void anieditor_get_drawable_frames(JWidget widget, JRect clip, int* first_frame, int* last_frame);
static void anieditor_draw_header(JWidget widget, JRect clip);
static void anieditor_draw_header_frame(JWidget widget, JRect clip, int frame);
static void anieditor_draw_header_part(JWidget widget, JRect clip, int x1, int y1, int x2, int y2,
				       bool is_hot, bool is_clk,
				       const char* line1, int align1,
				       const char* line2, int align2);
static void anieditor_draw_separator(JWidget widget, JRect clip);
static void anieditor_draw_layer(JWidget widget, JRect clip, int layer_index);
static void anieditor_draw_layer_padding(JWidget widget);
static void anieditor_draw_cel(JWidget widget, JRect clip, int layer_index, int frame);
static bool anieditor_draw_part(JWidget widget, int part, int layer, int frame);
static void anieditor_regenerate_layers(JWidget widget);
static void anieditor_hot_this(JWidget widget, int hot_part, int hot_layer, int hot_frame);
static void anieditor_center_cel(JWidget widget, int layer, int frame);
static void anieditor_show_cel(JWidget widget, int layer, int frame);
static void anieditor_show_current_cel(JWidget widget);
static void anieditor_clean_clk(JWidget widget);
static void anieditor_set_scroll(JWidget widget, int x, int y, bool use_refresh_region);
static int anieditor_get_layer_index(JWidget widget, Layer* layer);

static void icon_rect(BITMAP* icon, int x1, int y1, int x2, int y2, bool is_selected, bool is_hot, bool is_clk);

bool animation_editor_is_movingcel()
{
  return
    current_anieditor != NULL &&
    anieditor_data(current_anieditor)->state == STATE_MOVING_CEL;
}

/**
 * Shows the animation editor for the current sprite.
 */
void switch_between_animation_and_sprite_editor()
{
  Sprite* sprite = current_sprite;
  JWidget window;
  JWidget anieditor;
  int layer;

  /* create the window & the animation-editor */
  window = jwindow_new_desktop();
  anieditor = anieditor_new(sprite);
  current_anieditor = anieditor;

  jwidget_add_child(window, anieditor);
  jwindow_remap(window);

  /* show the current cel */
  layer = anieditor_get_layer_index(anieditor, sprite->layer);
  if (layer >= 0)
    anieditor_center_cel(anieditor, layer, sprite->frame);

  /* show the window */
  jwindow_open_fg(window);

  /* destroy the window */
  jwidget_free(window);
  current_anieditor = NULL;

  /* destroy thumbnails */
  destroy_thumbnails();

  update_screen_for_sprite(sprite);
}

/*********************************************************************
   The Animation Editor
 *********************************************************************/

static JWidget anieditor_new(Sprite* sprite)
{
  JWidget widget = jwidget_new(anieditor_type());
  AniEditor* anieditor = jnew0(AniEditor, 1);

  anieditor->sprite = sprite;
  anieditor->state = STATE_STANDBY;
  anieditor->layers = NULL;
  anieditor->nlayers = 0;
  anieditor->scroll_x = 0;
  anieditor->scroll_y = 0;
  anieditor->separator_x = 100 * guiscale();
  anieditor->separator_w = 1;
  anieditor->hot_part = PART_NOTHING;
  anieditor->clk_part = PART_NOTHING;
  anieditor->space_pressed = FALSE;

  jwidget_add_hook(widget, anieditor_type(), anieditor_msg_proc, anieditor);
  jwidget_focusrest(widget, TRUE);

  anieditor_regenerate_layers(widget);

  return widget;
}

static int anieditor_type()
{
  static int type = 0;
  if (!type)
    type = ji_register_widget_type();
  return type;
}

static AniEditor* anieditor_data(JWidget widget)
{
  return reinterpret_cast<AniEditor*>(jwidget_get_data(widget, anieditor_type()));
}

static bool anieditor_msg_proc(JWidget widget, JMessage msg)
{
  AniEditor* anieditor = anieditor_data(widget);

  switch (msg->type) {

    case JM_DESTROY:
      if (anieditor->layers)
	jfree(anieditor->layers);
      jfree(anieditor);
      break;

    case JM_REQSIZE:
      /* this doesn't matter, the AniEditor'll use the entire screen
	 anyway */
      msg->reqsize.w = 32;
      msg->reqsize.h = 32;
      return TRUE;

    /* case JM_CLOSE: */
    /*   break; */

    case JM_DRAW: {
      JRect clip = &msg->draw.rect;
      int layer, first_layer, last_layer;
      int frame, first_frame, last_frame;

      anieditor_get_drawable_layers(widget, clip, &first_layer, &last_layer);
      anieditor_get_drawable_frames(widget, clip, &first_frame, &last_frame);

      /* draw the header for layers */
      anieditor_draw_header(widget, clip);

      /* draw the header for each visible frame */
      for (frame=first_frame; frame<=last_frame; frame++)
	anieditor_draw_header_frame(widget, clip, frame);

      /* draw the separator */
      anieditor_draw_separator(widget, clip);

      /* draw each visible layer */
      for (layer=first_layer; layer<=last_layer; layer++) {
	anieditor_draw_layer(widget, clip, layer);

	/* draw every visible cel for each layer */
	for (frame=first_frame; frame<=last_frame; frame++)
	  anieditor_draw_cel(widget, clip, layer, frame);
      }

      anieditor_draw_layer_padding(widget);

      return TRUE;
    }

    case JM_TIMER:
      break;

    case JM_MOUSEENTER:
      if (key[KEY_SPACE]) anieditor->space_pressed = TRUE;
      break;

    case JM_MOUSELEAVE:
      if (anieditor->space_pressed) anieditor->space_pressed = FALSE;
      break;

    case JM_BUTTONPRESSED:
      if (msg->mouse.middle || anieditor->space_pressed) {
	jwidget_hard_capture_mouse(widget);
	anieditor->state = STATE_SCROLLING;
	return TRUE;
      }

      /* clicked-part = hot-part */
      anieditor->clk_part = anieditor->hot_part;
      anieditor->clk_layer = anieditor->hot_layer;
      anieditor->clk_frame = anieditor->hot_frame;
	
      switch (anieditor->hot_part) {
	case PART_NOTHING:
	  /* do nothing */
	  break;
	case PART_SEPARATOR:
	  jwidget_hard_capture_mouse(widget);
	  anieditor->state = STATE_MOVING_SEPARATOR;
	  break;
	case PART_HEADER_LAYER:
	  /* do nothing */
	  break;
	case PART_HEADER_FRAME:
	  anieditor->sprite->frame = anieditor->clk_frame;
	  jwidget_dirty(widget); /* TODO replace this by redrawing old current frame and new current frame */
	  jwidget_hard_capture_mouse(widget);
	  anieditor->state = STATE_MOVING_FRAME;
	  break;
	case PART_LAYER: {
	  int old_layer = anieditor_get_layer_index(widget, anieditor->sprite->layer);
	  int frame = anieditor->sprite->frame;

	  /* did the user select another layer? */
	  if (old_layer != anieditor->clk_layer) {
	    anieditor->sprite->layer = anieditor->layers[anieditor->clk_layer];

	    jmouse_hide();
	    /* redraw the old & new selected cel */
	    anieditor_draw_part(widget, PART_CEL, old_layer, frame);
	    anieditor_draw_part(widget, PART_CEL, anieditor->clk_layer, frame);
	    /* redraw the old selected layer */
	    anieditor_draw_part(widget, PART_LAYER, old_layer, frame);
	    jmouse_show();
	  }

	  /* change the scroll to show the new selected cel */
	  anieditor_show_cel(widget,
			     anieditor->clk_layer,
			     anieditor->sprite->frame);
	  jwidget_hard_capture_mouse(widget);
	  anieditor->state = STATE_MOVING_LAYER;
	  break;
	}
	case PART_LAYER_EYE_ICON:
	  jwidget_hard_capture_mouse(widget);
	  break;
	case PART_LAYER_LOCK_ICON:
	  jwidget_hard_capture_mouse(widget);
	  break;
	case PART_CEL: {
	  int old_layer = anieditor_get_layer_index(widget, anieditor->sprite->layer);
	  int old_frame = anieditor->sprite->frame;

	  /* select the new clicked-part */
	  if (old_layer != anieditor->clk_layer ||
	      old_frame != anieditor->clk_frame) {
	    anieditor->sprite->layer = anieditor->layers[anieditor->clk_layer];
	    anieditor->sprite->frame = anieditor->clk_frame;

	    jmouse_hide();
	    /* redraw the old & new selected layer */
	    if (old_layer != anieditor->clk_layer) {
	      anieditor_draw_part(widget, PART_LAYER, old_layer, old_frame);
	      anieditor_draw_part(widget, PART_LAYER,
				  anieditor->clk_layer,
				  anieditor->clk_frame);
	    }
	    /* redraw the old selected cel */
	    anieditor_draw_part(widget, PART_CEL, old_layer, old_frame);
	    jmouse_show();
	  }

	  /* change the scroll to show the new selected cel */
	  anieditor_show_cel(widget,
			     anieditor->clk_layer,
			     anieditor->sprite->frame);

	  /* capture the mouse (to move the cel) */
	  jwidget_hard_capture_mouse(widget);
	  anieditor->state = STATE_MOVING_CEL;
	  break;
	}
      }

      /* redraw the new selected part (header, layer or cel) */
      jmouse_hide();
      anieditor_draw_part(widget,
			  anieditor->clk_part,
			  anieditor->clk_layer,
			  anieditor->clk_frame);
      jmouse_show();
      break;
      
    case JM_MOTION: {
      int hot_part = PART_NOTHING;
      int hot_layer = -1;
      int hot_frame = -1;
      int mx = msg->mouse.x - widget->rc->x1;
      int my = msg->mouse.y - widget->rc->y1;

      if (jwidget_has_capture(widget)) {
	if (anieditor->state == STATE_SCROLLING) {
	  anieditor_set_scroll(widget,
			       anieditor->scroll_x+jmouse_x(1)-jmouse_x(0),
			       anieditor->scroll_y+jmouse_y(1)-jmouse_y(0), TRUE);

	  jmouse_control_infinite_scroll(widget->rc);
	  return TRUE;
	}
	/* if the mouse pressed the mouse's button in the separator, we
	   shouldn't change the hot (so the separator can be tracked to
	   the mouse's released) */
	else if (anieditor->clk_part == PART_SEPARATOR) {
	  hot_part = anieditor->clk_part;
	  anieditor->separator_x = mx;
	  jwidget_dirty(widget);
	  return TRUE;
	}
      }

      /* is the mouse on the separator= */
      if (mx > anieditor->separator_x-4 && mx < anieditor->separator_x+4)  {
	hot_part = PART_SEPARATOR;
      }
      /* is the mouse on the headers? */
      else if (my < HDRSIZE) {
	/* is on the layers' header? */
	if (mx < anieditor->separator_x)
	  hot_part = PART_HEADER_LAYER;
	/* is on a frame header? */
	else {
	  hot_part = PART_HEADER_FRAME;
	  hot_frame = (mx
		       - anieditor->separator_x
		       - anieditor->separator_w
		       + anieditor->scroll_x) / FRMSIZE;
	}
      }
      else {
	hot_layer = (my
		     - HDRSIZE
		     + anieditor->scroll_y) / LAYSIZE;
	
	/* is the mouse on a layer's label? */
	if (mx < anieditor->separator_x) {
	  BITMAP* icon1 = get_gfx(GFX_BOX_SHOW);
	  BITMAP* icon2 = get_gfx(GFX_BOX_UNLOCK);
	  int x1, y1, x2, y2, y_mid;

	  x1 = 0;
	  y1 = HDRSIZE + LAYSIZE*hot_layer - anieditor->scroll_y;
	  x2 = x1 + anieditor->separator_x - 1;
	  y2 = y1 + LAYSIZE - 1;
	  y_mid = (y1+y2) / 2;

	  if (mx >= x1+2 &&
	      mx <= x1+ICONSEP+ICONBORDER+icon1->w+ICONBORDER-1 &&
	      my >= y_mid-icon1->h/2-ICONBORDER &&
	      my <= y_mid+icon1->h/2+ICONBORDER) {
	    hot_part = PART_LAYER_EYE_ICON;
	  }
	  else if (mx >= x1+ICONSEP+ICONBORDER+icon1->w+ICONBORDER &&
		   mx <= x1+ICONSEP+ICONBORDER+icon1->w+ICONBORDER+ICONBORDER+icon2->w+ICONBORDER-1 &&
		   my >= y_mid-icon2->h/2-ICONBORDER &&
		   my <= y_mid+icon2->h/2+ICONBORDER) {
	    hot_part = PART_LAYER_LOCK_ICON;
	  }
	  else
	    hot_part = PART_LAYER;
	}
	else {
	  hot_part = PART_CEL;
	  hot_frame = (mx
		       - anieditor->separator_x
		       - anieditor->separator_w
		       + anieditor->scroll_x) / FRMSIZE;
	}
      }

      /* set the new 'hot' thing */
      anieditor_hot_this(widget, hot_part, hot_layer, hot_frame);
      return TRUE;
    }

    case JM_BUTTONRELEASED:
      if (jwidget_has_capture(widget)) {
	jwidget_release_mouse(widget);

	if (anieditor->state == STATE_SCROLLING) {
	  anieditor->state = STATE_STANDBY;
	  return TRUE;
	}

	switch (anieditor->hot_part) {
	  case PART_NOTHING:
	    /* do nothing */
	    break;
	  case PART_SEPARATOR:
	    /* do nothing */
	    break;
	  case PART_HEADER_LAYER:
	    /* do nothing */
	    break;
	  case PART_HEADER_FRAME:
	    /* show the frame pop-up menu */
	    if (msg->mouse.right) {
	      if (anieditor->clk_frame == anieditor->hot_frame) {
		JWidget popup_menu = get_frame_popup_menu();
		if (popup_menu != NULL) {
		  jmenu_popup(popup_menu, msg->mouse.x, msg->mouse.y);

		  destroy_thumbnails();
		  jwidget_dirty(widget);
		}
	      }
	    }
	    /* show the frame's properties dialog */
	    else if (msg->mouse.left) {
	      if (anieditor->clk_frame == anieditor->hot_frame)
		command_execute(command_get_by_name(CMD_FRAME_PROPERTIES), NULL);
	      else {
		if (anieditor->hot_frame >= 0 &&
		    anieditor->hot_frame < anieditor->sprite->frames &&
		    anieditor->hot_frame != anieditor->clk_frame+1) {
		  {
		    Undoable undoable(anieditor->sprite, "Move Frame");
		    undoable.move_frame_before(anieditor->clk_frame, anieditor->hot_frame);
		    undoable.commit();
		  }
		  jwidget_dirty(widget);
		}
	      }
	    }
	    break;
	  case PART_LAYER:
	    /* show the layer pop-up menu */
	    if (msg->mouse.right) {
	      if (anieditor->clk_layer == anieditor->hot_layer) {
	      	JWidget popup_menu = get_layer_popup_menu();
	      	if (popup_menu != NULL) {
	      	  jmenu_popup(popup_menu, msg->mouse.x, msg->mouse.y);

		  destroy_thumbnails();
		  jwidget_dirty(widget);
		  anieditor_regenerate_layers(widget);
	      	}
	      }
	    }
	    /* move a layer */
	    else if (msg->mouse.left) {
	      if (anieditor->hot_layer >= 0 &&
		  anieditor->hot_layer < anieditor->nlayers &&
		  anieditor->hot_layer != anieditor->clk_layer &&
		  anieditor->hot_layer != anieditor->clk_layer+1) {
		if (!layer_is_background(anieditor->layers[anieditor->clk_layer])) {
		  // move the clicked-layer after the hot-layer
		  {
		    Undoable undoable(anieditor->sprite, "Move Layer");
		    undoable.move_layer_after(anieditor->layers[anieditor->clk_layer],
					      anieditor->layers[anieditor->hot_layer]);
		    undoable.commit();
		  }

		  /* select the new layer */
		  anieditor->sprite->layer = anieditor->layers[anieditor->clk_layer];

		  jwidget_dirty(widget);
		  anieditor_regenerate_layers(widget);
		}
		else {
		  jalert(PACKAGE "<<You can't move the `Background' layer.||&OK");
		}
	      }
	    }
	    break;
	  case PART_LAYER_EYE_ICON:
	    /* hide/show layer */
	    if (anieditor->hot_layer == anieditor->clk_layer &&
		anieditor->hot_layer >= 0 &&
		anieditor->hot_layer < anieditor->nlayers) {
	      Layer *layer = anieditor->layers[anieditor->clk_layer];
	      assert(layer != NULL);
	      layer->flags ^= LAYER_IS_READABLE;
	    }
	    break;
	  case PART_LAYER_LOCK_ICON:
	    /* lock/unlock layer */
	    if (anieditor->hot_layer == anieditor->clk_layer &&
		anieditor->hot_layer >= 0 &&
		anieditor->hot_layer < anieditor->nlayers) {
	      Layer *layer = anieditor->layers[anieditor->clk_layer];
	      assert(layer != NULL);
	      layer->flags ^= LAYER_IS_WRITABLE;
	    }
	    break;
	  case PART_CEL: {
	    bool movement =
	      anieditor->clk_part == PART_CEL &&
	      anieditor->hot_part == PART_CEL &&
	      (anieditor->clk_layer != anieditor->hot_layer ||
	       anieditor->clk_frame != anieditor->hot_frame);

	    if (movement) {
	      set_frame_to_handle
		(/* source cel */
		 anieditor->layers[anieditor->clk_layer],
		 anieditor->clk_frame,
		 /* destination cel */
		 anieditor->layers[anieditor->hot_layer],
		 anieditor->hot_frame);
	    }

	    /* show the cel pop-up menu */
	    if (msg->mouse.right) {
	      JWidget popup_menu = movement ? get_cel_movement_popup_menu():
					      get_cel_popup_menu();
	      if (popup_menu != NULL) {
		jmenu_popup(popup_menu, msg->mouse.x, msg->mouse.y);

		destroy_thumbnails();
		anieditor_regenerate_layers(widget);
		jwidget_dirty(widget);
	      }
	    }
	    /* move the cel */
	    else if (msg->mouse.left) {
	      if (movement) {
		move_cel();

		destroy_thumbnails();
		anieditor_regenerate_layers(widget);
		jwidget_dirty(widget);
	      }
	    }
	    break;
	  }
	}

	/* clean the clicked-part & redraw the hot-part */
	jmouse_hide();
	anieditor_clean_clk(widget);
	anieditor_draw_part(widget,
			    anieditor->hot_part,
			    anieditor->hot_layer,
			    anieditor->hot_frame);
	jmouse_show();

	/* restore the cursor */
	anieditor->state = STATE_STANDBY;
	anieditor_setcursor(widget, msg->mouse.x, msg->mouse.y);
	return TRUE;
      }
      break;

    case JM_KEYPRESSED: {
      Command *command = command_get_by_key(msg);
      Sprite *sprite = anieditor->sprite;

      /* close animation editor */
      if ((command && (strcmp(command->name, CMD_FILM_EDITOR) == 0)) ||
	  (msg->key.scancode == KEY_ESC)) {
	jwidget_close_window(widget);
	return TRUE;
      }

      /* undo */
      if (command && strcmp(command->name, CMD_UNDO) == 0) {
	if (undo_can_undo(sprite->undo)) {
	  undo_do_undo(sprite->undo);

	  destroy_thumbnails();
	  anieditor_regenerate_layers(widget);
	  anieditor_show_current_cel(widget);
	  jwidget_dirty(widget);
	}
	return TRUE;
      }

      /* redo */
      if (command && strcmp(command->name, CMD_REDO) == 0) {
	if (undo_can_redo(sprite->undo)) {
	  undo_do_redo(sprite->undo);

	  destroy_thumbnails();
	  anieditor_regenerate_layers(widget);
	  anieditor_show_current_cel(widget);
	  jwidget_dirty(widget);
	}
	return TRUE;
      }

      /* new_frame, remove_frame, new_cel, remove_cel */
      if (command != NULL) {
	if (strcmp(command->name, CMD_NEW_FRAME) == 0 ||
	    strcmp(command->name, CMD_REMOVE_CEL) == 0 ||
	    strcmp(command->name, CMD_REMOVE_FRAME) == 0 ||
	    strcmp(command->name, CMD_GOTO_FIRST_FRAME) == 0 ||
	    strcmp(command->name, CMD_GOTO_PREVIOUS_FRAME) == 0 ||
	    strcmp(command->name, CMD_GOTO_PREVIOUS_LAYER) == 0 ||
	    strcmp(command->name, CMD_GOTO_NEXT_FRAME) == 0 ||
	    strcmp(command->name, CMD_GOTO_NEXT_LAYER) == 0 ||
	    strcmp(command->name, CMD_GOTO_LAST_FRAME) == 0) {
	  command_execute(command, NULL);

	  anieditor_show_current_cel(widget);
	  jwidget_dirty(widget);
	  return TRUE;
	}

	if (strcmp(command->name, CMD_NEW_LAYER) == 0 ||
	    strcmp(command->name, CMD_REMOVE_LAYER) == 0) {
	  command_execute(command, NULL);

	  anieditor_regenerate_layers(widget);
	  anieditor_show_current_cel(widget);
	  jwidget_dirty(widget);
	  return TRUE;
	}
      }

      switch (msg->key.scancode) {
	case KEY_SPACE:
	  anieditor->space_pressed = TRUE;
	  anieditor_setcursor(widget, jmouse_x(0), jmouse_y(0));
	  return TRUE;
      }

      break;
    }

    case JM_KEYRELEASED:
      switch (msg->key.scancode) {

	case KEY_SPACE:
	  if (anieditor->space_pressed) {
	    /* we have to clear all the KEY_SPACE in buffer */
	    clear_keybuf();

	    anieditor->space_pressed = FALSE;
	    anieditor_setcursor(widget, jmouse_x(0), jmouse_y(0));
	    return TRUE;
	  }
	  break;
      }
      break;

    case JM_WHEEL: {
      int dz = jmouse_z(1) - jmouse_z(0);
      int dx = 0;
      int dy = 0;

      if ((msg->any.shifts & KB_CTRL_FLAG) == KB_CTRL_FLAG)
	dx = dz * FRMSIZE;
      else
	dy = dz * LAYSIZE;

      if ((msg->any.shifts & KB_SHIFT_FLAG) == KB_SHIFT_FLAG) {
      	dx *= 3;
      	dy *= 3;
      }

      anieditor_set_scroll(widget,
			   anieditor->scroll_x+dx,
			   anieditor->scroll_y+dy, TRUE);
      break;
    }

    case JM_SETCURSOR:
      anieditor_setcursor(widget, msg->mouse.x, msg->mouse.y);
      return TRUE;

  }

  return FALSE;
}

static void anieditor_setcursor(JWidget widget, int x, int y)
{
  AniEditor* anieditor = anieditor_data(widget);
  int mx = x - widget->rc->x1;
  int my = y - widget->rc->y1;

  /* is the mouse in the separator */
  if (mx > anieditor->separator_x-2 && mx < anieditor->separator_x+2)  {
    jmouse_set_cursor(JI_CURSOR_SIZE_L);
  }
  /* scrolling */
  else if (anieditor->state == STATE_SCROLLING ||
	   anieditor->space_pressed) {
    jmouse_set_cursor(JI_CURSOR_SCROLL);
  }
  /* moving a frame */
  else if (anieditor->state == STATE_MOVING_FRAME &&
	   anieditor->clk_part == PART_HEADER_FRAME &&
	   anieditor->hot_part == PART_HEADER_FRAME &&
	   anieditor->clk_frame != anieditor->hot_frame) {
    jmouse_set_cursor(JI_CURSOR_MOVE);
  }
  /* moving a layer */
  else if (anieditor->state == STATE_MOVING_LAYER &&
	   anieditor->clk_part == PART_LAYER &&
	   anieditor->hot_part == PART_LAYER &&
	   anieditor->clk_layer != anieditor->hot_layer) {
    if (layer_is_background(anieditor->layers[anieditor->clk_layer]))
      jmouse_set_cursor(JI_CURSOR_FORBIDDEN);
    else
      jmouse_set_cursor(JI_CURSOR_MOVE);
  }
  /* moving a cel */
  else if (anieditor->state == STATE_MOVING_CEL &&
	   anieditor->clk_part == PART_CEL &&
	   anieditor->hot_part == PART_CEL &&
	   (anieditor->clk_frame != anieditor->hot_frame ||
	    anieditor->clk_layer != anieditor->hot_layer)) {
    jmouse_set_cursor(JI_CURSOR_MOVE);
  }
  /* normal state */
  else {
    jmouse_set_cursor(JI_CURSOR_NORMAL);
  }
}

static void anieditor_get_drawable_layers(JWidget widget, JRect clip, int *first_layer, int *last_layer)
{
  AniEditor* anieditor = anieditor_data(widget);

  *first_layer = 0;
  *last_layer = anieditor->nlayers-1;
}

static void anieditor_get_drawable_frames(JWidget widget, JRect clip, int *first_frame, int *last_frame)
{
  AniEditor* anieditor = anieditor_data(widget);

  *first_frame = 0;
  *last_frame = anieditor->sprite->frames-1;
}

static void anieditor_draw_header(JWidget widget, JRect clip)
{
  AniEditor* anieditor = anieditor_data(widget);
  /* bool is_hot = (anieditor->hot_part == PART_HEADER_LAYER); */
  /* bool is_clk = (anieditor->clk_part == PART_HEADER_LAYER); */
  int x1, y1, x2, y2;

  x1 = widget->rc->x1;
  y1 = widget->rc->y1;
  x2 = x1 + anieditor->separator_x - 1;
  y2 = y1 + HDRSIZE - 1;

  /* draw the header for the layers */
  anieditor_draw_header_part(widget, clip, x1, y1, x2, y2,
			     /* is_hot, is_clk, */
			     FALSE, FALSE,
			     "Frames >>", 1,
			     "Layers", -1);
}

static void anieditor_draw_header_frame(JWidget widget, JRect clip, int frame)
{
  AniEditor* anieditor = anieditor_data(widget);
  bool is_hot = (anieditor->hot_part == PART_HEADER_FRAME &&
		 anieditor->hot_frame == frame);
  bool is_clk = (anieditor->clk_part == PART_HEADER_FRAME &&
		 anieditor->clk_frame == frame);
  int x1, y1, x2, y2;
  int cx1, cy1, cx2, cy2;
  char buf1[256];
  char buf2[256];

  get_clip_rect(ji_screen, &cx1, &cy1, &cx2, &cy2);

  x1 = widget->rc->x1 + anieditor->separator_x + anieditor->separator_w
    + FRMSIZE*frame - anieditor->scroll_x;
  y1 = widget->rc->y1;
  x2 = x1 + FRMSIZE - 1;
  y2 = y1 + HDRSIZE - 1;

  add_clip_rect(ji_screen,
		widget->rc->x1 + anieditor->separator_x + anieditor->separator_w,
		y1, widget->rc->x2-1, y2);

  /* draw the header for the layers */
  usprintf(buf1, "%d", frame+1);
  usprintf(buf2, "%d", sprite_get_frlen(anieditor->sprite, frame));
  anieditor_draw_header_part(widget, clip, x1, y1, x2, y2,
			     is_hot, is_clk,
			     buf1, 0,
			     buf2, 0);

  /* if this header wasn't clicked but there are another frame's
     header clicked, we have to draw some indicators to show that the
     user can move frames */
  if (is_hot && !is_clk &&
      anieditor->clk_part == PART_HEADER_FRAME) {
    rectfill(ji_screen, x1+1, y1+1, x1+4, y2-1, ji_color_selected());
  }

  /* padding in the right side */
  if (frame == anieditor->sprite->frames-1) {
    if (x2+1 <= widget->rc->x2-1) {
      /* right side */
      vline(ji_screen, x2+1, y1, y2, ji_color_foreground());
      if (x2+2 <= widget->rc->x2-1)
	rectfill(ji_screen, x2+2, y1, widget->rc->x2-1, y2, ji_color_face());
    }
  }

  set_clip_rect(ji_screen, cx1, cy1, cx2, cy2);
}

static void anieditor_draw_header_part(JWidget widget, JRect clip, int x1, int y1, int x2, int y2,
				       bool is_hot, bool is_clk,
				       const char *line1, int align1,
				       const char *line2, int align2)
{
  int x, fg, face, facelight, faceshadow;

  if ((x2 < clip->x1) || (x1 >= clip->x2) ||
      (y2 < clip->y1) || (y1 >= clip->y2))
    return;

  fg = !is_hot && is_clk ? ji_color_background(): ji_color_foreground();
  face = is_hot ? ji_color_hotface(): (is_clk ? ji_color_selected():
						ji_color_face());
  facelight = is_hot && is_clk ? ji_color_faceshadow(): ji_color_facelight();
  faceshadow = is_hot && is_clk ? ji_color_facelight(): ji_color_faceshadow();

  /* draw the border of this text */
  jrectedge(ji_screen, x1, y1, x2, y2, facelight, faceshadow);

  /* fill the background of the part */
  rectfill(ji_screen, x1+1, y1+1, x2-1, y2-1, face);

  /* draw the text inside this header */
  if (line1 != NULL) {
    if (align1 < 0)
      x = x1+3;
    else if (align1 == 0)
      x = (x1+x2)/2 - text_length(widget->text_font, line1)/2;
    else
      x = x2 - 3 - text_length(widget->text_font, line1);
      
    jdraw_text(widget->text_font, line1,
	       x, y1+3,
	       fg, face, TRUE);
  }

  if (line2 != NULL) {
    if (align2 < 0)
      x = x1+3;
    else if (align2 == 0)
      x = (x1+x2)/2 - text_length(widget->text_font, line2)/2;
    else
      x = x2 - 3 - text_length(widget->text_font, line2);
    
    jdraw_text(widget->text_font, line2,
	       x, y1+3+ji_font_get_size(widget->text_font)+3,
	       fg, face, TRUE);
  }
}

static void anieditor_draw_separator(JWidget widget, JRect clip)
{
  AniEditor* anieditor = anieditor_data(widget);
  bool is_hot = (anieditor->hot_part == PART_SEPARATOR);
  int x1, y1, x2, y2;

  x1 = widget->rc->x1 + anieditor->separator_x;
  y1 = widget->rc->y1;
  x2 = widget->rc->x1 + anieditor->separator_x + anieditor->separator_w - 1;
  y2 = widget->rc->y2 - 1;

  if ((x2 < clip->x1) || (x1 >= clip->x2) ||
      (y2 < clip->y1) || (y1 >= clip->y2))
    return;

  vline(ji_screen, x1, y1, y2, is_hot ? ji_color_selected():
					ji_color_foreground());
}

static void anieditor_draw_layer(JWidget widget, JRect clip, int layer_index)
{
  AniEditor* anieditor = anieditor_data(widget);
  Layer *layer = anieditor->layers[layer_index];
  BITMAP *icon1 = get_gfx(layer_is_readable(layer) ? GFX_BOX_SHOW: GFX_BOX_HIDE);
  BITMAP *icon2 = get_gfx(layer_is_writable(layer) ? GFX_BOX_UNLOCK: GFX_BOX_LOCK);
  bool selected_layer = (layer == anieditor->sprite->layer);
  bool is_hot = (anieditor->hot_part == PART_LAYER && anieditor->hot_layer == layer_index);
  bool is_clk = (anieditor->clk_part == PART_LAYER && anieditor->clk_layer == layer_index);
  int bg = selected_layer ?
    ji_color_selected(): (is_hot ? ji_color_hotface():
			  (is_clk ? ji_color_selected():
				    ji_color_face()));
  int fg = selected_layer ? ji_color_background(): ji_color_foreground();
  int x1, y1, x2, y2, y_mid;
  int cx1, cy1, cx2, cy2;
  int u;

  get_clip_rect(ji_screen, &cx1, &cy1, &cx2, &cy2);
  
  x1 = widget->rc->x1;
  y1 = widget->rc->y1 + HDRSIZE + LAYSIZE*layer_index - anieditor->scroll_y;
  x2 = x1 + anieditor->separator_x - 1;
  y2 = y1 + LAYSIZE - 1;
  y_mid = (y1+y2) / 2;

  add_clip_rect(ji_screen,
		widget->rc->x1,
		widget->rc->y1 + HDRSIZE,
		widget->rc->x1 + anieditor->separator_x - 1,
		widget->rc->y2-1);

  if (is_hot) {
    jrectedge(ji_screen, x1, y1, x2, y2-1, ji_color_facelight(), ji_color_faceshadow());
    rectfill(ji_screen, x1+1, y1+1, x2-1, y2-2, bg);
  }
  else {
    rectfill(ji_screen, x1, y1, x2, y2-1, bg);
  }
  hline(ji_screen, x1, y2, x2, ji_color_foreground());

  /* if this layer wasn't clicked but there are another layer clicked,
     we have to draw some indicators to show that the user can move
     layers */
  if (is_hot && !is_clk &&
      anieditor->clk_part == PART_LAYER) {
    rectfill(ji_screen, x1+1, y1+1, x2-1, y1+5, ji_color_selected());
  }

  /* u = the position where to put the next element (like eye-icon, lock-icon, layer-name) */
  u = x1+ICONSEP;
  
  /* draw the eye (readable flag) */
  icon_rect(icon1,
	    u,
	    y_mid-icon1->h/2-ICONBORDER,
	    u+ICONBORDER+icon1->w+ICONBORDER-1,
	    y_mid-icon1->h/2+icon1->h+ICONBORDER-1,
	    selected_layer,
	    (anieditor->hot_part == PART_LAYER_EYE_ICON &&
	     anieditor->hot_layer == layer_index),
	    (anieditor->clk_part == PART_LAYER_EYE_ICON &&
	     anieditor->clk_layer == layer_index));

  u += u+ICONBORDER+icon1->w+ICONBORDER;

  /* draw the padlock (writable flag) */
  icon_rect(icon2,
	    u,
	    y_mid-icon1->h/2-ICONBORDER,
	    u+ICONBORDER+icon2->w+ICONBORDER-1,
	    y_mid-icon1->h/2+icon1->h-1+ICONBORDER,
	    selected_layer,
	    (anieditor->hot_part == PART_LAYER_LOCK_ICON &&
	     anieditor->hot_layer == layer_index),
	    (anieditor->clk_part == PART_LAYER_LOCK_ICON &&
	     anieditor->clk_layer == layer_index));

  u += ICONBORDER+icon2->w+ICONBORDER+ICONSEP;

  /* draw the layer's name */
  jdraw_text(widget->text_font, layer->name,
	     u, y_mid - ji_font_get_size(widget->text_font)/2,
	     fg, bg, TRUE);

  /* the background should be underlined */
  if (layer_is_background(layer)) {
    hline(ji_screen,
	  u,
	  y_mid - ji_font_get_size(widget->text_font)/2 + ji_font_get_size(widget->text_font) + 1,
	  u + text_length(widget->text_font, layer->name),
	  fg);
  }

  set_clip_rect(ji_screen, cx1, cy1, cx2, cy2);
}

static void anieditor_draw_layer_padding(JWidget widget)
{
  AniEditor* anieditor = anieditor_data(widget);
  int layer_index = anieditor->nlayers-1;
  int x1, y1, x2, y2;
  
  x1 = widget->rc->x1;
  y1 = widget->rc->y1 + HDRSIZE + LAYSIZE*layer_index - anieditor->scroll_y;
  x2 = x1 + anieditor->separator_x - 1;
  y2 = y1 + LAYSIZE - 1;

  /* padding in the bottom side */
  if (y2+1 <= widget->rc->y2-1) {
    rectfill(ji_screen, x1, y2+1, x2, widget->rc->y2-1, ji_color_disabled());
    rectfill(ji_screen,
	     x2+1+anieditor->separator_w, y2+1,
	     widget->rc->x2-1, widget->rc->y2-1,
	     ji_color_disabled());
  }
}

static void anieditor_draw_cel(JWidget widget, JRect clip, int layer_index, int frame)
{
  AniEditor* anieditor = anieditor_data(widget);
  Layer *layer = anieditor->layers[layer_index];
  bool selected_layer = (layer == anieditor->sprite->layer);
  bool is_hot = (anieditor->hot_part == PART_CEL &&
		 anieditor->hot_layer == layer_index &&
		 anieditor->hot_frame == frame);
  bool is_clk = (anieditor->clk_part == PART_CEL &&
		 anieditor->clk_layer == layer_index &&
		 anieditor->clk_frame == frame);
  int bg = is_hot ? ji_color_hotface():
		    ji_color_face();
  int x1, y1, x2, y2;
  int cx1, cy1, cx2, cy2;
  JRect thumbnail_rect;
  BITMAP *thumbnail;
  Cel *cel;

  get_clip_rect(ji_screen, &cx1, &cy1, &cx2, &cy2);

  x1 = widget->rc->x1 + anieditor->separator_x + anieditor->separator_w
    + FRMSIZE*frame - anieditor->scroll_x;
  y1 = widget->rc->y1 + HDRSIZE
    + LAYSIZE*layer_index - anieditor->scroll_y;
  x2 = x1 + FRMSIZE - 1;
  y2 = y1 + LAYSIZE - 1;

  add_clip_rect(ji_screen,
		widget->rc->x1 + anieditor->separator_x + anieditor->separator_w,
		widget->rc->y1 + HDRSIZE,
		widget->rc->x2-1,
		widget->rc->y2-1);

  thumbnail_rect = jrect_new(x1+3, y1+3, x2-2, y2-2);

  /* draw the box for the cel */
  if (selected_layer && frame == anieditor->sprite->frame) {
    /* current cel */
    if (is_hot)
      jrectedge(ji_screen, x1, y1, x2, y2-1,
		ji_color_facelight(), ji_color_faceshadow());
    else
      rect(ji_screen, x1, y1, x2, y2-1, ji_color_selected());
    rect(ji_screen, x1+1, y1+1, x2-1, y2-2, ji_color_selected());
    rect(ji_screen, x1+2, y1+2, x2-2, y2-3, bg);
  }
  else {
    if (is_hot) {
      jrectedge(ji_screen, x1, y1, x2, y2-1,
		ji_color_facelight(), ji_color_faceshadow());
      rectfill(ji_screen, x1+1, y1+1, x2-1, y2-2, bg);
    }
    else {
      rectfill(ji_screen, x1, y1, x2, y2-1, bg);
    }
  }
  hline(ji_screen, x1, y2, x2, ji_color_foreground());

  /* get the cel of this layer in this frame */
  cel = layer_get_cel(layer, frame);

  /* empty cel? */
  if (cel == NULL ||
      stock_get_image(anieditor->sprite->stock,
		      cel->image) == NULL) { /* TODO why a cel can't have an associated image? */
    jdraw_rectfill(thumbnail_rect, bg);
    draw_emptyset_symbol(thumbnail_rect, ji_color_disabled());
  }
  else {
    thumbnail = generate_thumbnail(layer, cel, anieditor->sprite);
    if (thumbnail != NULL) {
      stretch_blit(thumbnail, ji_screen,
		   0, 0, thumbnail->w, thumbnail->h,
		   thumbnail_rect->x1, thumbnail_rect->y1,
		   jrect_w(thumbnail_rect),
		   jrect_h(thumbnail_rect));
    }
  }

  jrect_free(thumbnail_rect);

  /* if this cel is hot and other cel was clicked, we have to draw
     some indicators to show that the user can move cels */
  if (is_hot && !is_clk &&
      anieditor->clk_part == PART_CEL) {
    rectfill(ji_screen, x1+1, y1+1, x1+FRMSIZE/3, y1+4, ji_color_selected());
    rectfill(ji_screen, x1+1, y1+5, x1+4, y1+FRMSIZE/3, ji_color_selected());

    rectfill(ji_screen, x2-FRMSIZE/3, y1+1, x2-1, y1+4, ji_color_selected());
    rectfill(ji_screen, x2-4, y1+5, x2-1, y1+FRMSIZE/3, ji_color_selected());

    rectfill(ji_screen, x1+1, y2-4, x1+FRMSIZE/3, y2-1, ji_color_selected());
    rectfill(ji_screen, x1+1, y2-FRMSIZE/3, x1+4, y2-5, ji_color_selected());

    rectfill(ji_screen, x2-FRMSIZE/3, y2-4, x2-1, y2-1, ji_color_selected());
    rectfill(ji_screen, x2-4, y2-FRMSIZE/3, x2-1, y2-5, ji_color_selected());
  }

  /* padding in the right side */
  if (frame == anieditor->sprite->frames-1) {
    if (x2+1 <= widget->rc->x2-1) {
      /* right side */
      vline(ji_screen, x2+1, y1, y2, ji_color_foreground());
      if (x2+2 <= widget->rc->x2-1)
	rectfill(ji_screen, x2+2, y1, widget->rc->x2-1, y2, ji_color_disabled());
    }
  }

  set_clip_rect(ji_screen, cx1, cy1, cx2, cy2);
}

static bool anieditor_draw_part(JWidget widget, int part, int layer, int frame)
{
  AniEditor* anieditor = anieditor_data(widget);

  switch (part) {
    case PART_NOTHING:
      /* do nothing */
      return TRUE;
    case PART_SEPARATOR:
      anieditor_draw_separator(widget, widget->rc);
      return TRUE;
    case PART_HEADER_LAYER:
      anieditor_draw_header(widget, widget->rc);
      return TRUE;
    case PART_HEADER_FRAME:
      if (frame >= 0 && frame < anieditor->sprite->frames) {
	anieditor_draw_header_frame(widget, widget->rc, frame);
	return TRUE;
      }
      break;
    case PART_LAYER:
    case PART_LAYER_EYE_ICON:
    case PART_LAYER_LOCK_ICON:
      if (layer >= 0 && layer < anieditor->nlayers) {
	anieditor_draw_layer(widget, widget->rc, layer);
	return TRUE;
      }
      break;
    case PART_CEL:
      if (layer >= 0 && layer < anieditor->nlayers &&
	  frame >= 0 && frame < anieditor->sprite->frames) {
	anieditor_draw_cel(widget, widget->rc, layer, frame);
	return TRUE;
      }
      break;
  }

  return FALSE;
}

static void anieditor_regenerate_layers(JWidget widget)
{
  AniEditor* anieditor = anieditor_data(widget);
  int c;

  if (anieditor->layers != NULL) {
    jfree(anieditor->layers);
    anieditor->layers = NULL;
  }

  anieditor->nlayers = sprite_count_layers(anieditor->sprite);

  /* here we build an array with all the layers */
  if (anieditor->nlayers > 0) {
    anieditor->layers = (Layer**)jmalloc(sizeof(Layer*) * anieditor->nlayers);

    for (c=0; c<anieditor->nlayers; c++)
      anieditor->layers[c] = sprite_index2layer(anieditor->sprite,
						anieditor->nlayers-c-1);
  }
}

static void anieditor_hot_this(JWidget widget, int hot_part, int hot_layer, int hot_frame)
{
  AniEditor* anieditor = anieditor_data(widget);
  int old_hot_part;

  /* if the part, layer or frame change */
  if (hot_part != anieditor->hot_part ||
      hot_layer != anieditor->hot_layer ||
      hot_frame != anieditor->hot_frame) {
    jmouse_hide();

    /* clean the old 'hot' thing */
    old_hot_part = anieditor->hot_part;
    anieditor->hot_part = PART_NOTHING;

    anieditor_draw_part(widget,
			old_hot_part,
			anieditor->hot_layer,
			anieditor->hot_frame);

    /* draw the new 'hot' thing */
    anieditor->hot_part = hot_part;
    anieditor->hot_layer = hot_layer;
    anieditor->hot_frame = hot_frame;

    if (!anieditor_draw_part(widget, hot_part, hot_layer, hot_frame)) {
      anieditor->hot_part = PART_NOTHING;
    }

    jmouse_show();
  }
}

static void anieditor_center_cel(JWidget widget, int layer, int frame)
{
  AniEditor* anieditor = anieditor_data(widget);
  int target_x = (widget->rc->x1 + anieditor->separator_x + anieditor->separator_w + widget->rc->x2)/2 - FRMSIZE/2;
  int target_y = (widget->rc->y1 + HDRSIZE + widget->rc->y2)/2 - LAYSIZE/2;
  int scroll_x = widget->rc->x1 + anieditor->separator_x + anieditor->separator_w + FRMSIZE*frame - target_x;
  int scroll_y = widget->rc->y1 + HDRSIZE + LAYSIZE*layer - target_y;

  anieditor_set_scroll(widget, scroll_x, scroll_y, FALSE);
}

static void anieditor_show_cel(JWidget widget, int layer, int frame)
{
  AniEditor* anieditor = anieditor_data(widget);
  int scroll_x, scroll_y;
  int x1, y1, x2, y2;

  x1 = widget->rc->x1 + anieditor->separator_x + anieditor->separator_w + FRMSIZE*frame - anieditor->scroll_x;
  y1 = widget->rc->y1 + HDRSIZE + LAYSIZE*layer - anieditor->scroll_y;
  x2 = x1 + FRMSIZE - 1;
  y2 = y1 + LAYSIZE - 1;

  scroll_x = anieditor->scroll_x;
  scroll_y = anieditor->scroll_y;

  if (x1 < widget->rc->x1 + anieditor->separator_x + anieditor->separator_w) {
    scroll_x -= (widget->rc->x1 + anieditor->separator_x + anieditor->separator_w) - (x1);
  }
  else if (x2 > widget->rc->x2-1) {
    scroll_x += (x2) - (widget->rc->x2-1);
  }

  if (y1 < widget->rc->y1 + HDRSIZE) {
    scroll_y -= (widget->rc->y1 + HDRSIZE) - (y1);
  }
  else if (y2 > widget->rc->y2-1) {
    scroll_y += (y2) - (widget->rc->y2-1);
  }

  if (scroll_x != anieditor->scroll_x ||
      scroll_y != anieditor->scroll_y)
    anieditor_set_scroll(widget, scroll_x, scroll_y, TRUE);
}

static void anieditor_show_current_cel(JWidget widget)
{
  AniEditor* anieditor = anieditor_data(widget);
  int layer = anieditor_get_layer_index(widget, anieditor->sprite->layer);
  if (layer >= 0)
    anieditor_show_cel(widget, layer, anieditor->sprite->frame);
}

static void anieditor_clean_clk(JWidget widget)
{
  AniEditor* anieditor = anieditor_data(widget);
  int clk_part = anieditor->clk_part;
  anieditor->clk_part = PART_NOTHING;
  
  anieditor_draw_part(widget,
		      clk_part,
		      anieditor->clk_layer,
		      anieditor->clk_frame);
}

static void anieditor_set_scroll(JWidget widget, int x, int y, bool use_refresh_region)
{
  AniEditor* anieditor = anieditor_data(widget);
  int old_scroll_x = 0;
  int old_scroll_y = 0;
  int max_scroll_x;
  int max_scroll_y;
  JRegion region = NULL;

  if (use_refresh_region) {
    region = jwidget_get_drawable_region(widget, JI_GDR_CUTTOPWINDOWS);
    old_scroll_x = anieditor->scroll_x;
    old_scroll_y = anieditor->scroll_y;
  }

  max_scroll_x = anieditor->sprite->frames * FRMSIZE - jrect_w(widget->rc)/2;
  max_scroll_y = anieditor->nlayers * LAYSIZE - jrect_h(widget->rc)/2;
  max_scroll_x = MAX(0, max_scroll_x);
  max_scroll_y = MAX(0, max_scroll_y);

  anieditor->scroll_x = MID(0, x, max_scroll_x);
  anieditor->scroll_y = MID(0, y, max_scroll_y);

  if (use_refresh_region) {
    int new_scroll_x = anieditor->scroll_x;
    int new_scroll_y = anieditor->scroll_y;
    int dx = old_scroll_x - new_scroll_x;
    int dy = old_scroll_y - new_scroll_y;
    JRegion reg1 = jregion_new(NULL, 0);
    JRegion reg2 = jregion_new(NULL, 0);
    JRect rect2 = jrect_new(0, 0, 0, 0);

    jmouse_hide();

    /* scroll layers */
    jrect_replace(rect2,
		  widget->rc->x1,
		  widget->rc->y1 + HDRSIZE,
		  widget->rc->x1 + anieditor->separator_x,
		  widget->rc->y2);
    jregion_reset(reg2, rect2);
    jregion_copy(reg1, region);
    jregion_intersect(reg1, reg1, reg2);
    jwidget_scroll(widget, reg1, 0, dy);

    /* scroll header-frame */
    jrect_replace(rect2,
		  widget->rc->x1 + anieditor->separator_x + anieditor->separator_w,
		  widget->rc->y1,
		  widget->rc->x2,
		  widget->rc->y1 + HDRSIZE);
    jregion_reset(reg2, rect2);
    jregion_copy(reg1, region);
    jregion_intersect(reg1, reg1, reg2);
    jwidget_scroll(widget, reg1, dx, 0);

    /* scroll cels */
    jrect_replace(rect2,
		  widget->rc->x1 + anieditor->separator_x + anieditor->separator_w,
		  widget->rc->y1 + HDRSIZE,
		  widget->rc->x2,
		  widget->rc->y2);
    jregion_reset(reg2, rect2);
    jregion_copy(reg1, region);
    jregion_intersect(reg1, reg1, reg2);
    jwidget_scroll(widget, reg1, dx, dy);

    jmouse_show();

    jregion_free(region);
    jregion_free(reg1);
    jregion_free(reg2);
    jrect_free(rect2);
  }
}

static int anieditor_get_layer_index(JWidget widget, Layer* layer)
{
  AniEditor* anieditor = anieditor_data(widget);
  int i;

  for (i=0; i<anieditor->nlayers; i++)
    if (anieditor->layers[i] == layer)
      return i;

  return -1;
}

/* auxiliary routine to draw an icon in the layer-part */
static void icon_rect(BITMAP *icon, int x1, int y1, int x2, int y2,
		      bool is_selected, bool is_hot, bool is_clk)
{
  int icon_x = x1+ICONBORDER;
  int icon_y = (y1+y2)/2-icon->h/2;
  int facelight = is_hot && is_clk ? ji_color_faceshadow(): ji_color_facelight();
  int faceshadow = is_hot && is_clk ? ji_color_facelight(): ji_color_faceshadow();

  if (is_hot) {
    jrectedge(ji_screen, x1, y1, x2, y2, facelight, faceshadow);

    if (!is_selected)
      rectfill(ji_screen,
	       x1+1, y1+1, x2-1, y2-1,
	       ji_color_hotface());
  }

  if (is_selected)
    jdraw_inverted_sprite(ji_screen, icon, icon_x, icon_y);
  else
    draw_sprite(ji_screen, icon, icon_x, icon_y);
}