// ASE gui library
// Copyright (C) 2001-2011  David Capello
//
// This source file is ditributed under a BSD-like license, please
// read LICENSE.txt for more information.

// #define REPORT_EVENTS
// #define LIMIT_DISPATCH_TIME

#include "config.h"

#ifdef REPORT_EVENTS
#include <stdio.h>
#endif
#include <vector>
#include <allegro.h>

#include "gui/gui.h"
#include "gui/intern.h"

#define TOPWND(manager) reinterpret_cast<Frame*>(jlist_first_data((manager)->children))

#define ACCEPT_FOCUS(widget)				\
  (((widget)->flags & (JI_FOCUSREST |			\
		       JI_DISABLED |			\
		       JI_HIDDEN |			\
		       JI_DECORATIVE)) == JI_FOCUSREST)

#define DOUBLE_CLICK_TIMEOUT_MSECS   400

enum {
  DOUBLE_CLICK_NONE,
  DOUBLE_CLICK_DOWN,
  DOUBLE_CLICK_UP
};

enum {
  STAGE_NORMAL,
  STAGE_WANT_CLOSE,
  STAGE_WAITING_REPLY,
  STAGE_CLOSE_ALL,
};

struct Timer {
  Widget* widget;
  int interval;
  int last_time;
};

#define NFILTERS	(JM_REGISTERED_MESSAGES+1)

struct Filter
{
  int message;
  Widget* widget;

  Filter(int message, JWidget widget)
    : message(message)
    , widget(widget) { }
};

class Manager : public Widget
{
public:
  Manager()
    : Widget(JI_MANAGER)
  {
  }

protected:
  virtual void onBroadcastMouseMessage(WidgetsList& targets) OVERRIDE
  {
    // Ask to the first frame in the "children" list to know how to
    // propagate mouse messages.
    Widget* widget = reinterpret_cast<Widget*>(jlist_first_data(children));
    if (widget)
      widget->broadcastMouseMessage(targets);
  }

};

static int double_click_level;
static int double_click_buttons;
static int double_click_ticks;

static int want_close_stage;	   /* variable to handle the external
				      close button in some Windows
				      enviroments */

static Manager* default_manager = NULL;

static std::vector<Timer*> timers; // Registered timers

static JList new_windows;	      // Windows that we should show
static WidgetsList mouse_widgets_list; // List of widgets to send mouse events
static WidgetsList garbage;
static JList msg_queue;		      // Messages queue
static JList msg_filters[NFILTERS];   // Filters for every enqueued message

static Widget* focus_widget;	// The widget with the focus
static Widget* mouse_widget;	// The widget with the mouse
static Widget* capture_widget;	// The widget that captures the mouse

static bool first_time_poll;    /* true when we don't enter in poll yet */

static char old_readed_key[KEY_MAX]; /* keyboard status of previous
					poll */

static unsigned key_repeated[KEY_MAX];

/* manager widget */
static bool manager_msg_proc(JWidget widget, Message* msg);
static void manager_request_size(JWidget widget, int *w, int *h);
static void manager_set_position(JWidget widget, JRect rect);
static void manager_pump_queue(JWidget widget);

/* auxiliary */
static void generate_setcursor_message();
static void remove_msgs_for(JWidget widget, Message* msg);
static int some_parent_is_focusrest(JWidget widget);
static JWidget find_magnetic_widget(JWidget widget);
static Message* new_mouse_msg(int type, JWidget destination);
static void broadcast_key_msg(JWidget manager, Message* msg);

/* keyboard focus movement stuff */
static bool move_focus(JWidget manager, Message* msg);
static int count_widgets_accept_focus(JWidget widget);
static bool childs_accept_focus(JWidget widget, bool first);
static JWidget next_widget(JWidget widget);
static int cmp_left(JWidget widget, int x, int y);
static int cmp_right(JWidget widget, int x, int y);
static int cmp_up(JWidget widget, int x, int y);
static int cmp_down(JWidget widget, int x, int y);

/* hooks the close-button in some platform with window support */
static void allegro_window_close_hook()
{
  if (want_close_stage == STAGE_NORMAL)
    want_close_stage = STAGE_WANT_CLOSE;
}

static void collect_garbage()
{
  for (WidgetsList::iterator
	 it = garbage.begin(),
	 end = garbage.end(); it != end; ++it) {
    delete *it;
  }
  garbage.clear();
}

JWidget ji_get_default_manager()
{
  return default_manager;
}

JWidget jmanager_new()
{
  Manager* widget;
  int c;

  if (!default_manager) {
    if (!ji_screen)
      ji_set_screen(screen, SCREEN_W, SCREEN_H);

    /* hook the window close message */
    want_close_stage = STAGE_NORMAL;
    set_close_button_callback(allegro_window_close_hook);

    // Empty lists
    msg_queue = jlist_new();
    new_windows = jlist_new();
    mouse_widgets_list.clear();

    for (c=0; c<NFILTERS; ++c)
      msg_filters[c] = jlist_new();

    /* reset variables */
    focus_widget = NULL;
    mouse_widget = NULL;
    capture_widget = NULL;

    first_time_poll = true;
    double_click_level = DOUBLE_CLICK_NONE;
    double_click_ticks = 0;

    /* reset keyboard */
    for (c=0; c<KEY_MAX; c++) {
      old_readed_key[c] = 0;
      key_repeated[c] = 0;
    }
  }

  widget = new Manager();

  jwidget_add_hook(widget, JI_MANAGER, manager_msg_proc, NULL);

  jrect_replace(widget->rc, 0, 0, JI_SCREEN_W, JI_SCREEN_H);
  widget->setVisible(true);

  /* default manager is the first one (and is ever visible) */
  if (!default_manager)
    default_manager = widget;

  return widget;
}

void jmanager_free(JWidget widget)
{
  JLink link;
  int c;

  ASSERT_VALID_WIDGET(widget);

  /* there are some messages in queue? */
  jmanager_dispatch_messages(widget);

  collect_garbage();

  /* finish with main manager */
  if (default_manager == widget) {
    /* no more cursor */
    jmouse_set_cursor(JI_CURSOR_NULL);

    /* destroy this widget */
    jwidget_free(widget);

    // Destroy timers
    for (c=0; c<(int)timers.size(); ++c)
      delete timers[c];
    timers.clear();

    /* destroy filters */
    for (c=0; c<NFILTERS; ++c) {
      JI_LIST_FOR_EACH(msg_filters[c], link) {
	delete reinterpret_cast<Filter*>(link->data);
      }
      jlist_free(msg_filters[c]);
      msg_filters[c] = NULL;
    }

    /* no more default manager */
    default_manager = NULL;

    /* shutdown system */
    jlist_free(msg_queue);
    jlist_free(new_windows);
    mouse_widgets_list.clear();
  }
  else {
    /* destroy this widget */
    jwidget_free(widget);
  }
}

void jmanager_run(JWidget widget)
{
  while (!jlist_empty(widget->children)) {
    if (jmanager_generate_messages(widget)) {
      jmanager_dispatch_messages(widget);
    }
    else if (!garbage.empty()) {
      collect_garbage();
    }
  }
}

/**
 * @return true if there are messages in the queue to be distpatched
 *         through jmanager_dispatch_messages().
 */
bool jmanager_generate_messages(JWidget manager)
{
  JWidget widget;
  JWidget window;
  int mousemove;
  Message* msg;
  JLink link;
  int c;

  /* poll keyboard */
  poll_keyboard();

  if (first_time_poll) {
    first_time_poll = false;
    
    jmanager_refresh_screen();
    jmouse_set_cursor(JI_CURSOR_NORMAL);
  }

  /* first check: there are windows to manage? */
  if (jlist_empty(manager->children))
    return false;

  /* new windows to show? */
  if (!jlist_empty(new_windows)) {
    JWidget magnet;

    JI_LIST_FOR_EACH(new_windows, link) {
      window = reinterpret_cast<JWidget>(link->data);

      // Relayout
      window->layout();

      // Dirty the entire window and show it
      window->setVisible(true);
      window->invalidate();

      // Attract the focus to the magnetic widget...
      /* 1) get the magnetic widget */
      magnet = find_magnetic_widget(window->getRoot());
      /* 2) if magnetic widget exists and it doesn't have the focus */
      if (magnet && !magnet->hasFocus())
	jmanager_set_focus(magnet);
      /* 3) if not, put the focus in the first child */
      else
	jmanager_focus_first_child(window);
    }

    jlist_clear(new_windows);
  }

  // Update mouse status
  mousemove = jmouse_poll();
  if (mousemove || !mouse_widget) {
    // Get the list of widgets to send mouse messages.
    mouse_widgets_list.clear();
    manager->broadcastMouseMessage(mouse_widgets_list);

    // Get the widget under the mouse
    widget = NULL;

    for (WidgetsList::iterator
	   it = mouse_widgets_list.begin(),
	   end = mouse_widgets_list.end(); it != end; ++it) {
      widget = (*it)->pick(jmouse_x(0), jmouse_y(0));
      if (widget)
	break;
    }

    // Fixup "mouse" flag
    if (widget != mouse_widget) {
      if (!widget)
	jmanager_free_mouse();
      else
	jmanager_set_mouse(widget);
    }

    // Mouse movement
    if (mousemove) {
      JWidget dst;

      // Reset double click status
      double_click_level = DOUBLE_CLICK_NONE;

      if (capture_widget)
	dst = capture_widget;
      else
	dst = mouse_widget;

      // Send the mouse movement message
      msg = new_mouse_msg(JM_MOTION, dst);
      jmanager_enqueue_message(msg);

      generate_setcursor_message();
    }
  }

  // Mouse wheel
  if (jmouse_z(0) != jmouse_z(1)) {
    msg = new_mouse_msg(JM_WHEEL,
			capture_widget ? capture_widget:
					 mouse_widget);
    jmanager_enqueue_message(msg);
  }

  // Mouse clicks
  if (jmouse_b(0) != jmouse_b(1)) {
    int current_ticks = ji_clock;
    bool pressed =
      ((jmouse_b(1) & 1) == 0 && (jmouse_b(0) & 1) == 1) ||
      ((jmouse_b(1) & 2) == 0 && (jmouse_b(0) & 2) == 2) ||
      ((jmouse_b(1) & 4) == 0 && (jmouse_b(0) & 4) == 4);

    msg = new_mouse_msg(pressed ? JM_BUTTONPRESSED:
				  JM_BUTTONRELEASED,
			capture_widget ? capture_widget:
					 mouse_widget);

    /**********************************************************************/
    /* Double Click */
    if (msg->type == JM_BUTTONPRESSED) {
      if (double_click_level != DOUBLE_CLICK_NONE) {
	/* time out, back to NONE */
	if (current_ticks - double_click_ticks > DOUBLE_CLICK_TIMEOUT_MSECS) {
	  double_click_level = DOUBLE_CLICK_NONE;
	}
	else if (double_click_buttons == msg->mouse.flags) {
	  if (double_click_level == DOUBLE_CLICK_UP) {
	    msg->type = JM_DOUBLECLICK;
	  }
	  else {
	    double_click_level = DOUBLE_CLICK_NONE;
	  }
	}
	/* press other button, back to NONE */
	else {
	  double_click_level = DOUBLE_CLICK_NONE;
	}
      }

      /* this could be the beginning of the state */
      if (double_click_level == DOUBLE_CLICK_NONE) {
	double_click_level = DOUBLE_CLICK_DOWN;
	double_click_buttons = msg->mouse.flags;
	double_click_ticks = current_ticks;
      }
    }
    else if (msg->type == JM_BUTTONRELEASED) {
      if (double_click_level != DOUBLE_CLICK_NONE) {
	/* time out, back to NONE */
	if (current_ticks - double_click_ticks > DOUBLE_CLICK_TIMEOUT_MSECS) {
	  double_click_level = DOUBLE_CLICK_NONE;
	}
	else if (double_click_buttons == msg->mouse.flags) {
	  if (double_click_level == DOUBLE_CLICK_DOWN) {
	    double_click_level = DOUBLE_CLICK_UP;
	    double_click_ticks = current_ticks;
	  }
	}
	/* press other button, back to NONE */
	else {
	  double_click_level = DOUBLE_CLICK_NONE;
	}
      }
    }

    // Handle Z order: Send the window to top (only when you click in
    // a window that aren't the desktop).
    if (msg->type == JM_BUTTONPRESSED &&
	!capture_widget && mouse_widget) {
      // The clicked window
      Frame* window = static_cast<Frame*>(mouse_widget->getRoot());
      JWidget win_manager = window ? window->getManager(): NULL;

      if ((window) &&
	  // We cannot change Z-order of desktop windows
	  (!window->is_desktop()) &&
	  // We cannot change Z order of foreground windows because a
	  // foreground window can launch other background windows
	  // which should be kept on top of the foreground one.
	  (!window->is_foreground()) &&
	  // If the window is not already the top window of the manager.
	  (window != TOPWND(win_manager))) {
	// Put it in the top of the list
	jlist_remove(win_manager->children, window);

	if (window->is_ontop())
	  jlist_prepend(win_manager->children, window);
	else {
	  int pos = jlist_length(win_manager->children);
	  JI_LIST_FOR_EACH_BACK(win_manager->children, link) {
	    if (((Frame*)link->data)->is_ontop())
	      break;
	    pos--;
	  }
	  jlist_insert(win_manager->children, window, pos);
	}

	window->invalidate();
      }

      // Put the focus
      jmanager_set_focus(mouse_widget);
    }

    jmanager_enqueue_message(msg);
  }

  /* generate ESC key when the user press close button in the system window */
  if (want_close_stage == STAGE_WANT_CLOSE) {
    want_close_stage = STAGE_NORMAL;

    msg = jmessage_new_key_related(JM_KEYPRESSED, (KEY_ESC << 8) | 27);
    broadcast_key_msg(manager, msg);
    jmanager_enqueue_message(msg);

    msg = jmessage_new_key_related(JM_KEYRELEASED, (KEY_ESC << 8) | 27);
    broadcast_key_msg(manager, msg);
    jmanager_enqueue_message(msg);
  }

  /* generate JM_CHAR/JM_KEYPRESSED messages */
  while (keypressed()) {
    int readkey_value = readkey();

    /* char message first */
    msg = jmessage_new_key_related(JM_KEYPRESSED, readkey_value);

    c = readkey_value >> 8;
    old_readed_key[c] = key[c];
    msg->key.repeat = key_repeated[c]++;

    broadcast_key_msg(manager, msg);
    jmanager_enqueue_message(msg);
  }

  for (c=0; c<KEY_MAX; c++) {
    if (old_readed_key[c] != key[c]) {
      /* generate JM_KEYRELEASED messages (old key state is activated,
	 the new one is deactivated) */
      if (old_readed_key[c]) {
	/* press/release key interface */
	msg = jmessage_new_key_related(JM_KEYRELEASED,
				       (c << 8) | scancode_to_ascii(c));
	old_readed_key[c] = key[c];
	key_repeated[c] = 0;

	broadcast_key_msg(manager, msg);
	jmanager_enqueue_message(msg);
      }
      /* generate JM_KEYPRESSED messages for modifiers */
      else if (c >= KEY_MODIFIERS) {
	/* press/release key interface */
	msg = jmessage_new_key_related(JM_KEYPRESSED,
				       (c << 8) | scancode_to_ascii(c));
	old_readed_key[c] = key[c];
	msg->key.repeat = key_repeated[c]++;

	broadcast_key_msg(manager, msg);
	jmanager_enqueue_message(msg);
      }
    }
  }

  // Generate messages for timers
  if (!timers.empty()) {
    int t = ji_clock;
    int count;

    for (c=0; c<(int)timers.size(); ++c) {
      if (timers[c] && timers[c]->last_time >= 0) {
	count = 0;
	while (t - timers[c]->last_time > timers[c]->interval) {
	  timers[c]->last_time += timers[c]->interval;
	  ++count;

	  /* we spend too much time here */
	  if (ji_clock - t > timers[c]->interval) {
	    timers[c]->last_time = ji_clock;
	    break;
	  }
	}

	if (count > 0) {
	  msg = jmessage_new(JM_TIMER);
	  msg->timer.count = count;
	  msg->timer.timer_id = c;
	  jmessage_add_dest(msg, timers[c]->widget);
	  jmanager_enqueue_message(msg);
	}
      }
    }
  }

  /* generate redraw events */
  jwidget_flush_redraw(manager);

  /* make some OSes happy */
  rest(0);

  if (!jlist_empty(msg_queue))
    return true;
  else
    return false;
}

void jmanager_dispatch_messages(JWidget manager)
{
  Message* msg;

  ASSERT(manager != NULL);
  
  /* add the "Queue Processing" message for the manager */
  msg = new_mouse_msg(JM_QUEUEPROCESSING, manager);
  jmanager_enqueue_message(msg);

  manager_pump_queue(manager);
}

void jmanager_add_to_garbage(Widget* widget)
{
  garbage.push_back(widget);
}

/**
 * Adds a timer event for the specified widget.
 *
 * @return A timer ID that can be used with @ref jmanager_remove_timer
 */
int jmanager_add_timer(JWidget widget, int interval)
{
  int c, new_id = -1;

  ASSERT_VALID_WIDGET(widget);

  for (c=0; c<(int)timers.size(); ++c) {
    // There are an empty slot
    if (timers[c] == NULL) {
      new_id = c;
      break;
    }
  }

  if (new_id < 0) {
    new_id = timers.size();
    timers.push_back(NULL);
  }

  Timer* timer = new Timer;
  timer->widget = widget;
  timer->interval = interval;
  timer->last_time = -1;
  timers[new_id] = timer;

  return new_id;
}

void jmanager_remove_timer(int timer_id)
{
  Message* message;
  JLink link, next;

  ASSERT(timer_id >= 0 && timer_id < (int)timers.size());
  ASSERT(timers[timer_id] != NULL);

  delete timers[timer_id];
  timers[timer_id] = NULL;

  /* remove messages of this timer in the queue */
  JI_LIST_FOR_EACH_SAFE(msg_queue, link, next) {
    message = reinterpret_cast<Message*>(link->data);
    if (!message->any.used &&
	message->any.type == JM_TIMER &&
	message->timer.timer_id == timer_id) {
      printf("REMOVING A TIMER MESSAGE FROM THE QUEUE!!\n"); fflush(stdout);
      jmessage_free(reinterpret_cast<Message*>(link->data));
      jlist_delete_link(msg_queue, link);
    }
  }
}

void jmanager_start_timer(int timer_id)
{
  ASSERT(timer_id >= 0 && timer_id < (int)timers.size());
  ASSERT(timers[timer_id] != NULL);

  timers[timer_id]->last_time = ji_clock;
}

void jmanager_stop_timer(int timer_id)
{
  ASSERT(timer_id >= 0 && timer_id < (int)timers.size());
  ASSERT(timers[timer_id] != NULL);

  timers[timer_id]->last_time = -1;
}

void jmanager_set_timer_interval(int timer_id, int interval)
{
  ASSERT(timer_id >= 0 && timer_id < (int)timers.size());
  ASSERT(timers[timer_id] != NULL);

  timers[timer_id]->interval = interval;
}

bool jmanager_timer_is_running(int timer_id)
{
  ASSERT(timer_id >= 0 && timer_id < (int)timers.size());
  ASSERT(timers[timer_id] != NULL);

  return (timers[timer_id]->last_time >= 0);
}

/**
 * @param msg You can't use the this message after calling this
 *            routine. The message will be automatically freed through
 *            @ref jmessage_free
 */
void jmanager_enqueue_message(Message* msg)
{
  int c;

  ASSERT(msg_queue != NULL);
  ASSERT(msg != NULL);

  /* check if this message must be filtered by some widget before */
  c = msg->type;
  if (c >= JM_REGISTERED_MESSAGES)
    c = JM_REGISTERED_MESSAGES;

  if (!jlist_empty(msg_filters[c])) { /* ok, so are filters to add ... */
    JLink link;

    /* add all the filters in the destination list of the message */
    JI_LIST_FOR_EACH_BACK(msg_filters[c], link) {
      Filter* filter = reinterpret_cast<Filter*>(link->data);
      if (msg->type == filter->message)
	jmessage_add_pre_dest(msg, filter->widget);
    }
  }

  /* there are a destination widget at least? */
  if (!jlist_empty(msg->any.widgets))
    jlist_append(msg_queue, msg);
  else
    jmessage_free(msg);
}

JWidget jmanager_get_top_window()
{
  if (default_manager)
    return TOPWND(default_manager);
  else
    return NULL;
}

JWidget jmanager_get_focus()
{
  return focus_widget;
}

JWidget jmanager_get_mouse()
{
  return mouse_widget;
}

JWidget jmanager_get_capture()
{
  return capture_widget;
}

void jmanager_set_focus(JWidget widget)
{
  if ((focus_widget != widget)
      && (!(widget)
	  || (!(widget->flags & JI_DISABLED)
	      && !(widget->flags & JI_HIDDEN)
	      && !(widget->flags & JI_DECORATIVE)
	      && some_parent_is_focusrest(widget)))) {
    JList widget_parents = NULL;
    JWidget common_parent = NULL;
    JLink link, link2;
    Message* msg;

    if (widget)
      widget_parents = widget->getParents(false);
    else
      widget_parents = jlist_new();

    /* fetch the focus */

    if (focus_widget) {
      JList focus_parents = focus_widget->getParents(true);
      msg = jmessage_new(JM_FOCUSLEAVE);

      JI_LIST_FOR_EACH(focus_parents, link) {
	if (widget) {
	  JI_LIST_FOR_EACH(widget_parents, link2) {
	    if (link->data == link2->data) {
	      common_parent = reinterpret_cast<JWidget>(link->data);
	      break;
	    }
	  }
	  if (common_parent)
	    break;
	}

	if (reinterpret_cast<JWidget>(link->data)->hasFocus()) {
	  ((JWidget)link->data)->flags &= ~JI_HASFOCUS;
	  jmessage_add_dest(msg, reinterpret_cast<JWidget>(link->data));
	}
      }

      jmanager_enqueue_message(msg);
      jlist_free(focus_parents);
    }

    /* put the focus */

    focus_widget = widget;

    if (widget) {
      if (common_parent)
	link = jlist_find(widget_parents, common_parent)->next;
      else
	link = jlist_first(widget_parents);

      msg = jmessage_new(JM_FOCUSENTER);

      for (; link != widget_parents->end; link=link->next) {
	JWidget w = (JWidget)link->data;

	if (w->flags & JI_FOCUSREST) {
	  w->flags |= JI_HASFOCUS;

	  jmessage_add_dest(msg, w);
	}
      }

      jmanager_enqueue_message(msg);
    }

    jlist_free(widget_parents);
  }
}

void jmanager_set_mouse(JWidget widget)
{
  if ((mouse_widget != widget) && (!capture_widget)) {
    JList widget_parents = NULL;
    JWidget common_parent = NULL;
    JLink link, link2;
    Message* msg;

    if (widget)
      widget_parents = widget->getParents(false);
    else
      widget_parents = jlist_new();

    /* fetch the mouse */

    if (mouse_widget) {
      JList mouse_parents = mouse_widget->getParents(true);
      msg = jmessage_new(JM_MOUSELEAVE);

      JI_LIST_FOR_EACH(mouse_parents, link) {
	if (widget) {
	  JI_LIST_FOR_EACH(widget_parents, link2) {
	    if (link->data == link2->data) {
	      common_parent = reinterpret_cast<JWidget>(link->data);
	      break;
	    }
	  }
	  if (common_parent)
	    break;
	}

	if (reinterpret_cast<JWidget>(link->data)->hasMouse()) {
	  ((JWidget)link->data)->flags &= ~JI_HASMOUSE;
	  jmessage_add_dest(msg, reinterpret_cast<JWidget>(link->data));
	}
      }

      jmanager_enqueue_message(msg);
      jlist_free(mouse_parents);
    }

    /* put the mouse */

    mouse_widget = widget;

    if (widget) {
      if (common_parent)
	link = jlist_find(widget_parents, common_parent)->next;
      else
	link = jlist_first(widget_parents);

      msg = jmessage_new(JM_MOUSEENTER);

      for (; link != widget_parents->end; link=link->next) {
	reinterpret_cast<JWidget>(link->data)->flags |= JI_HASMOUSE;
	jmessage_add_dest(msg, reinterpret_cast<JWidget>(link->data));
      }

      jmanager_enqueue_message(msg);
      generate_setcursor_message();
    }

    jlist_free(widget_parents);
  }
}

void jmanager_set_capture(JWidget widget)
{
  widget->flags |= JI_HASCAPTURE;
  capture_widget = widget;
}

/* sets the focus to the "magnetic" widget inside the window */
void jmanager_attract_focus(JWidget widget)
{
  /* get the magnetic widget */
  JWidget magnet = find_magnetic_widget(widget->getRoot());

  /* if magnetic widget exists and it doesn't have the focus */
  if (magnet && !magnet->hasFocus())
    jmanager_set_focus(magnet);
}

void jmanager_focus_first_child(JWidget widget)
{
  for (Widget* it=widget->getRoot(); it; it=next_widget(it)) {
    if (ACCEPT_FOCUS(it) && !(childs_accept_focus(it, true))) {
      jmanager_set_focus(it);
      break;
    }
  }
}

void jmanager_free_focus()
{
  jmanager_set_focus(NULL);
}

void jmanager_free_mouse()
{
  jmanager_set_mouse(NULL);
}

void jmanager_free_capture()
{
  if (capture_widget) {
    capture_widget->flags &= ~JI_HASCAPTURE;
    capture_widget = NULL;
  }
}

void jmanager_free_widget(JWidget widget)
{
  // Break any relationship with the GUI manager

  if (widget->hasCapture() || (widget == capture_widget))
    jmanager_free_capture();

  if (widget->hasMouse() || (widget == mouse_widget))
    jmanager_free_mouse();

  if (widget->hasFocus() || (widget == focus_widget))
    jmanager_free_focus();
}

void jmanager_remove_message(Message* msg)
{
  jlist_remove(msg_queue, msg);
}

void jmanager_remove_messages_for(JWidget widget)
{
  JLink link;
  JI_LIST_FOR_EACH(msg_queue, link)
    remove_msgs_for(widget, reinterpret_cast<Message*>(link->data));
}

void jmanager_refresh_screen()
{
  if (default_manager)
    default_manager->invalidate();
}

void jmanager_add_msg_filter(int message, JWidget widget)
{
  int c = message;
  if (c >= JM_REGISTERED_MESSAGES)
    c = JM_REGISTERED_MESSAGES;
  
  jlist_append(msg_filters[c], new Filter(message, widget));
}

void jmanager_remove_msg_filter(int message, JWidget widget)
{
  JLink link, next;
  int c = message;
  if (c >= JM_REGISTERED_MESSAGES)
    c = JM_REGISTERED_MESSAGES;

  JI_LIST_FOR_EACH_SAFE(msg_filters[c], link, next) {
    Filter* filter = reinterpret_cast<Filter*>(link->data);
    if (filter->widget == widget) {
      delete filter;
      jlist_delete_link(msg_filters[c], link);
    }
  }
}

void jmanager_remove_msg_filter_for(JWidget widget)
{
  JLink link, next;
  int c;

  for (c=0; c<NFILTERS; ++c) {
    JI_LIST_FOR_EACH_SAFE(msg_filters[c], link, next) {
      Filter* filter = reinterpret_cast<Filter*>(link->data);
      if (filter->widget == widget) {
	delete filter;
	jlist_delete_link(msg_filters[c], link);
      }
    }
  }
}

/* configures the window for begin the loop */
void _jmanager_open_window(JWidget manager, Frame* window)
{
  Message* msg;

  // free all widgets of special states
  if (window->is_wantfocus()) {
    jmanager_free_capture();
    jmanager_free_mouse();
    jmanager_free_focus();
  }

  /* add the window to manager */
  jlist_prepend(manager->children, window);
  window->parent = manager;

  /* append signal */
  jwidget_emit_signal(manager, JI_SIGNAL_MANAGER_ADD_WINDOW);

  /* broadcast the open message */
  msg = jmessage_new(JM_OPEN);
  jmessage_add_dest(msg, window);
  jmanager_enqueue_message(msg);

  /* update the new windows list to show */
  jlist_append(new_windows, window);
}

void _jmanager_close_window(JWidget manager, Frame* window, bool redraw_background)
{
  Message* msg;
  JRegion reg1;

  if (!manager->hasChild(window))
    return;

  if (redraw_background)
    reg1 = jwidget_get_region(window);
  else
    reg1 = NULL;

  /* close all windows to this desktop */
  if (window->is_desktop()) {
    JLink link, next;

    JI_LIST_FOR_EACH_SAFE(manager->children, link, next) {
      if (link->data == window)
	break;
      else {
	JRegion reg2 = jwidget_get_region(window);
	jregion_union(reg1, reg1, reg2);
	jregion_free(reg2);

	_jmanager_close_window(manager, reinterpret_cast<Frame*>(link->data), false);
      }
    }
  }

  /* free all widgets of special states */
  if (capture_widget != NULL && capture_widget->getRoot() == window)
    jmanager_free_capture();

  if (mouse_widget != NULL && mouse_widget->getRoot() == window)
    jmanager_free_mouse();

  if (focus_widget != NULL && focus_widget->getRoot() == window)
    jmanager_free_focus();

  /* hide window */
  window->setVisible(false);

  /* close message */
  msg = jmessage_new(JM_CLOSE);
  jmessage_add_dest(msg, window);
  jmanager_enqueue_message(msg);

  /* update manager list stuff */
  jlist_remove(manager->children, window);
  window->parent = NULL;

  /* remove signal */
  jwidget_emit_signal(manager, JI_SIGNAL_MANAGER_REMOVE_WINDOW);

  /* redraw background */
  if (reg1) {
    manager->invalidateRegion(reg1);
    jregion_free(reg1);
  }

  /* maybe the window is in the "new_windows" list */
  jlist_remove(new_windows, window);
}

/**********************************************************************
				Manager
 **********************************************************************/

static bool manager_msg_proc(JWidget widget, Message* msg)
{
  switch (msg->type) {

    case JM_REQSIZE:
      manager_request_size(widget, &msg->reqsize.w, &msg->reqsize.h);
      return true;

    case JM_SETPOS:
      manager_set_position(widget, &msg->setpos.rect);
      return true;

    case JM_DRAW:
      jdraw_rectfill(&msg->draw.rect, widget->getTheme()->desktop_color);
      return true;

    case JM_KEYPRESSED:
    case JM_KEYRELEASED: {
      JLink link, link2;

      msg->key.propagate_to_children = true;
      msg->key.propagate_to_parent = false;

      // Continue sending the message to the children of all windows
      // (until a desktop or foreground window).
      JI_LIST_FOR_EACH(widget->children, link) {
	Frame* w = (Frame*)link->data;

	// Send to the window.
	JI_LIST_FOR_EACH(w->children, link2)
	  if (reinterpret_cast<JWidget>(link2->data)->sendMessage(msg))
	    return true;

	if (w->is_foreground() ||
	    w->is_desktop())
	  break;
      }

      // Check the focus movement.
      if (msg->type == JM_KEYPRESSED)
	move_focus(widget, msg);

      return true;
    }
      
  }

  return false;
}

static void manager_request_size(JWidget widget, int *w, int *h)
{
  if (!widget->parent) {	/* hasn't parent? */
    *w = jrect_w(widget->rc);
    *h = jrect_h(widget->rc);
  }
  else {
    JRect cpos, pos = jwidget_get_child_rect(widget->parent);
    JLink link;

    JI_LIST_FOR_EACH(widget->children, link) {
      cpos = jwidget_get_rect(reinterpret_cast<JWidget>(link->data));
      jrect_union(pos, cpos);
      jrect_free(cpos);
    }

    *w = jrect_w(pos);
    *h = jrect_h(pos);
    jrect_free(pos);
  }
}

static void manager_set_position(JWidget widget, JRect rect)
{
  JRect cpos, old_pos;
  JWidget child;
  JLink link;
  int x, y;

  old_pos = jrect_new_copy(widget->rc);
  jrect_copy(widget->rc, rect);

  /* offset for all windows */
  x = widget->rc->x1 - old_pos->x1;
  y = widget->rc->y1 - old_pos->y1;

  JI_LIST_FOR_EACH(widget->children, link) {
    child = (JWidget)link->data;

    cpos = jwidget_get_rect(child);
    jrect_displace(cpos, x, y);
    jwidget_set_rect(child, cpos);
    jrect_free(cpos);
  }

  jrect_free(old_pos);
}

static void manager_pump_queue(JWidget widget_manager)
{
  Message* msg, *first_msg;
  JLink link, link2, next;
  Widget* widget;
  bool done;
#ifdef LIMIT_DISPATCH_TIME
  int t = ji_clock;
#endif

  /* TODO get the msg_queue from 'widget_manager' */

  ASSERT(msg_queue != NULL);

  link = jlist_first(msg_queue);
  while (link != msg_queue->end) {
#ifdef LIMIT_DISPATCH_TIME
    if (ji_clock-t > 250)
      break;
#endif

    /* the message to process */
    msg = reinterpret_cast<Message*>(link->data);

    /* go to next message */
    if (msg->any.used) {
      link = link->next;
      continue;
    }

    /* this message is in use */
    msg->any.used = true;
    first_msg = msg;

    done = false;
    JI_LIST_FOR_EACH(msg->any.widgets, link2) {
      widget = reinterpret_cast<JWidget>(link2->data);

#ifdef REPORT_EVENTS
      {
	static char *msg_name[] = {
	  "JM_OPEN",
	  "JM_CLOSE",
	  "JM_DESTROY",
	  "JM_DRAW",
	  "JM_SIGNAL",
	  "JM_TIMER",
	  "JM_REQSIZE",
	  "JM_SETPOS",
	  "JM_WINMOVE",
	  "JM_DEFERREDFREE",
	  "JM_DIRTYCHILDREN",
	  "JM_QUEUEPROCESSING",
	  "JM_KEYPRESSED",
	  "JM_KEYRELEASED",
	  "JM_FOCUSENTER",
	  "JM_FOCUSLEAVE",
	  "JM_BUTTONPRESSED",
	  "JM_BUTTONRELEASED",
	  "JM_DOUBLECLICK",
	  "JM_MOUSEENTER",
	  "JM_MOUSELEAVE",
	  "JM_MOTION",
	  "JM_SETCURSOR",
	  "JM_WHEEL",
	};
	const char *string =
	  (msg->type >= JM_OPEN &&
	   msg->type <= JM_WHEEL) ? msg_name[msg->type]:
				    "Unknown";

	printf("Event #%d: %s (%d)\n", msg->type, string, widget->id);
	fflush(stdout);
      }
#endif

      /* draw message? */
      if (msg->type == JM_DRAW) {
	/* hidden? */
	if (widget->flags & JI_HIDDEN)
	  continue;

	jmouse_hide();
	acquire_bitmap(ji_screen);

	/* set clip */
	ASSERT(get_clip_state(ji_screen));
	set_clip_rect(ji_screen,
		      msg->draw.rect.x1, msg->draw.rect.y1,
		      msg->draw.rect.x2-1, msg->draw.rect.y2-1);
#ifdef REPORT_EVENTS
	printf(" - clip(%d, %d, %d, %d)\n",
	       msg->draw.rect.x1, msg->draw.rect.y1,
	       msg->draw.rect.x2-1, msg->draw.rect.y2-1);
	fflush(stdout);
#endif
	/* rectfill(ji_screen, 0, 0, JI_SCREEN_W-1, JI_SCREEN_H-1, makecol(255, 0, 0)); */
	/* vsync(); vsync(); vsync(); vsync(); */
      }

      /* call message handler */
      done = widget->sendMessage(msg);

      /* restore clip */
      if (msg->type == JM_DRAW) {
	set_clip_rect(ji_screen, 0, 0, JI_SCREEN_W-1, JI_SCREEN_H-1);

	/* dirty rectangles */
	if (ji_dirty_region)
	  ji_add_dirty_rect(&msg->draw.rect);

	release_bitmap(ji_screen);
	jmouse_show();
      }

      if (done)
	break;
    }

    /* remove the message from the msg_queue */
    next = link->next;
    jlist_delete_link(msg_queue, link);
    link = next;

    /* destroy the message */
    jmessage_free(first_msg);
  }
}

void jmanager_invalidate_region(JWidget widget, JRegion region)
{
  Frame* window;
  JRegion reg1 = jregion_new(NULL, 0);
  JRegion reg2 = jregion_new(widget->rc, 0);
  JRegion reg3;
  JLink link;

  /* TODO intersect with jwidget_get_drawable_region()??? */
  jregion_intersect(reg1, region, reg2);

  /* redraw windows from top to background */
  JI_LIST_FOR_EACH(widget->children, link) {
    window = (Frame*)link->data;

    // invalidate regions of this window
    window->invalidateRegion(reg1);

    /* there is desktop? */
    if (window->is_desktop())
      break;					/* work done */

    /* clip this window area for the next window */
    reg3 = jwidget_get_region(window);
    jregion_copy(reg2, reg1);
    jregion_subtract(reg1, reg2, reg3);
    jregion_free(reg3);
  }

  // invalidate areas outside windows (only when there are not a desktop window)
  if (link == widget->children->end)
    widget->invalidateRegion(reg1);

  jregion_free(reg1);
  jregion_free(reg2);
}

/**********************************************************************
			Internal routines
 **********************************************************************/

static void generate_setcursor_message()
{
  JWidget dst;
  Message* msg;
  
  if (capture_widget)
    dst = capture_widget;
  else
    dst = mouse_widget;

  if (dst) {
    msg = new_mouse_msg(JM_SETCURSOR, dst);
    jmanager_enqueue_message(msg);
  }
  else
    jmouse_set_cursor(JI_CURSOR_NORMAL);
}

static void remove_msgs_for(JWidget widget, Message* msg)
{
  JLink link, next;

  JI_LIST_FOR_EACH_SAFE(msg->any.widgets, link, next) {
    if (link->data == widget) 
      jlist_delete_link(msg->any.widgets, link);
  }
}

static int some_parent_is_focusrest(JWidget widget)
{
  if (jwidget_is_focusrest(widget))
    return true;

  if (widget->parent)
    return some_parent_is_focusrest(widget->parent);
  else
    return false;
}

static JWidget find_magnetic_widget(JWidget widget)
{
  JWidget found;
  JLink link;

  JI_LIST_FOR_EACH(widget->children, link) {
    found = find_magnetic_widget(reinterpret_cast<JWidget>(link->data));
    if (found)
      return found;
  }

  if (jwidget_is_magnetic(widget))
    return widget;
  else
    return NULL;
}

static Message* new_mouse_msg(int type, JWidget widget)
{
  Message* msg = jmessage_new(type);
  if (!msg)
    return NULL;

  msg->mouse.x = jmouse_x(0);
  msg->mouse.y = jmouse_y(0);
  msg->mouse.flags =
    type == JM_BUTTONRELEASED ? jmouse_b(1):
				jmouse_b(0);
  msg->mouse.left = msg->mouse.flags & 1 ? true: false;
  msg->mouse.right = msg->mouse.flags & 2 ? true: false;
  msg->mouse.middle = msg->mouse.flags & 4 ? true: false;

  if (widget != NULL)
    jmessage_add_dest(msg, widget);

  return msg;
}

static void broadcast_key_msg(JWidget manager, Message* msg)
{
  // Send the message to the widget with capture
  if (capture_widget) {
    jmessage_add_dest(msg, capture_widget);
  }
  // Send the msg to the focused widget
  else if (focus_widget) {
    jmessage_add_dest(msg, focus_widget);
  }
  // Finally, send the message to the manager, it'll know what to do
  else {
    jmessage_add_dest(msg, manager);
  }
}

/***********************************************************************
			    Focus Movement
 ***********************************************************************/

static bool move_focus(JWidget manager, Message* msg)
{
  int (*cmp)(JWidget, int, int) = NULL;
  Widget* focus = NULL;
  Widget* it;
  bool ret = false;
  Frame* window = NULL;
  int c, count;

  // Who have the focus
  if (focus_widget) {
    window = dynamic_cast<Frame*>(focus_widget->getRoot());
  }
  else if (!jlist_empty(manager->children)) {
    window = TOPWND(manager);
  }

  if (!window)
    return false;

  // How many child-widget want the focus in this widget?
  count = count_widgets_accept_focus(window);

  // One at least
  if (count > 0) {
    std::vector<Widget*> list(count);

    c = 0;

    /* list's 1st element is the focused widget */
    for (it=focus_widget; it; it=next_widget(it)) {
      if (ACCEPT_FOCUS(it) && !(childs_accept_focus(it, true)))
	list[c++] = it;
    }

    for (it=window; it != focus_widget; it=next_widget(it)) {
      if (ACCEPT_FOCUS(it) && !(childs_accept_focus(it, true)))
	list[c++] = it;
    }

    /* depend of the pressed key */
    switch (msg->key.scancode) {

      case KEY_TAB:
        /* reverse tab */
        if (msg->any.shifts & (KB_SHIFT_FLAG | KB_CTRL_FLAG | KB_ALT_FLAG)) {
          focus = list[count-1];
        }
        /* normal tab */
        else if (count > 1) {
          focus = list[1];
        }
        ret = true;
        break;

      /* arrow keys */
      case KEY_LEFT:  if (!cmp) cmp = cmp_left;
      case KEY_RIGHT: if (!cmp) cmp = cmp_right;
      case KEY_UP:    if (!cmp) cmp = cmp_up;
      case KEY_DOWN:  if (!cmp) cmp = cmp_down;

        /* more than one widget */
        if (count > 1) {
          int i, j, x, y;

          /* position where the focus come */
          x = ((focus_widget) ? focus_widget->rc->x1+focus_widget->rc->x2:
				window->rc->x1+window->rc->x2)
	    / 2;
          y = ((focus_widget) ? focus_widget->rc->y1+focus_widget->rc->y2:
				window->rc->y1+window->rc->y2)
	    / 2;

          c = focus_widget ? 1: 0;

          /* rearrange the list */
          for (i=c; i<count-1; i++) {
            for (j=i+1; j<count; j++) {
              /* sort the list in ascending order */
              if ((*cmp) (list[i], x, y) > (*cmp) (list[j], x, y)) {
		JWidget tmp = list[i];
                list[i] = list[j];
                list[j] = tmp;
              }
            }
          }

          /* check if the new widget to put the focus is not
             in the wrong way */
          if ((*cmp) (list[c], x, y) < INT_MAX)
            focus = list[c];
        }
        /* if only there are one widget, put the focus in this */
        else
          focus = list[0];

        ret = true;
        break;
    }

    if ((focus) && (focus != focus_widget))
      jmanager_set_focus(focus);
  }

  return ret;
}

static int count_widgets_accept_focus(JWidget widget)
{
  ASSERT(widget != NULL);

  int count = 0;
  JLink link;

  JI_LIST_FOR_EACH(widget->children, link)
    count += count_widgets_accept_focus(reinterpret_cast<JWidget>(link->data));

  if ((count == 0) && (ACCEPT_FOCUS(widget)))
    count++;

  return count;
}

static bool childs_accept_focus(JWidget widget, bool first)
{
  JLink link;

  JI_LIST_FOR_EACH(widget->children, link)
    if (childs_accept_focus(reinterpret_cast<JWidget>(link->data), false))
      return true;

  return first ? false: ACCEPT_FOCUS(widget);
}

static JWidget next_widget(JWidget widget)
{
  if (!jlist_empty(widget->children))
    return reinterpret_cast<JWidget>(jlist_first(widget->children)->data);

  while (widget->parent->type != JI_MANAGER) {
    JLink link = jlist_find(widget->parent->children, widget);
    if (link->next != widget->parent->children->end)
      return reinterpret_cast<JWidget>(link->next->data);
    else
      widget = widget->parent;
  }

  return NULL;
}

static int cmp_left(JWidget widget, int x, int y)
{
  int z = x - (widget->rc->x1+widget->rc->x2)/2;
  if (z <= 0)
    return INT_MAX;
  return z + ABS((widget->rc->y1+widget->rc->y2)/2 - y) * 8;
}

static int cmp_right(JWidget widget, int x, int y)
{
  int z = (widget->rc->x1+widget->rc->x2)/2 - x;
  if (z <= 0)
    return INT_MAX;
  return z + ABS((widget->rc->y1+widget->rc->y2)/2 - y) * 8;
}

static int cmp_up(JWidget widget, int x, int y)
{
  int z = y - (widget->rc->y1+widget->rc->y2)/2;
  if (z <= 0)
    return INT_MAX;
  return z + ABS((widget->rc->x1+widget->rc->x2)/2 - x) * 8;
}

static int cmp_down(JWidget widget, int x, int y)
{
  int z = (widget->rc->y1+widget->rc->y2)/2 - y;
  if (z <= 0)
    return INT_MAX;
  return z + ABS((widget->rc->x1+widget->rc->x2)/2 - x) * 8;
}

