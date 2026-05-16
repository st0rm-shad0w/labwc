/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_PLUGIN_EVENTS_H
#define LABWC_PLUGIN_EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>
#include <wlr/util/box.h>
#include <xkbcommon/xkbcommon.h>
#include "plugin/plugin.h"

/* Forward declarations */
struct view;
struct output;
struct workspace;
struct ssd;
struct wlr_buffer;
struct wlr_renderer;

/**
 * Event types that plugins can subscribe to.
 *
 * "Interceptable" events have a `consumed` field in their data struct.
 * If a plugin sets consumed=true, subsequent handlers and the default
 * labwc handler are skipped.
 */
enum labwc_event_type {
	/* Compositor lifecycle */
	LABWC_EVENT_STARTUP = 0,
	LABWC_EVENT_SHUTDOWN,
	LABWC_EVENT_CONFIG_RELOAD,

	/* View lifecycle (notification, post-fact) */
	LABWC_EVENT_VIEW_MAP,
	LABWC_EVENT_VIEW_UNMAP,
	LABWC_EVENT_VIEW_DESTROY,
	LABWC_EVENT_VIEW_FOCUS,
	LABWC_EVENT_VIEW_TITLE_CHANGE,
	LABWC_EVENT_VIEW_APP_ID_CHANGE,

	/* View state changes (notification, post-fact) */
	LABWC_EVENT_VIEW_MAXIMIZED,
	LABWC_EVENT_VIEW_MINIMIZED,
	LABWC_EVENT_VIEW_FULLSCREENED,
	LABWC_EVENT_VIEW_MOVED,
	LABWC_EVENT_VIEW_RESIZED,
	LABWC_EVENT_VIEW_ALWAYS_ON_TOP,
	LABWC_EVENT_VIEW_WORKSPACE_CHANGED,

	/* View requests (interceptable) */
	LABWC_EVENT_VIEW_REQUEST_MOVE,
	LABWC_EVENT_VIEW_REQUEST_RESIZE,
	LABWC_EVENT_VIEW_REQUEST_CLOSE,
	LABWC_EVENT_VIEW_REQUEST_MAXIMIZE,
	LABWC_EVENT_VIEW_REQUEST_FULLSCREEN,
	LABWC_EVENT_VIEW_REQUEST_MINIMIZE,

	/* Input (interceptable) */
	LABWC_EVENT_KEY,
	LABWC_EVENT_BUTTON,
	LABWC_EVENT_POINTER_MOTION,
	LABWC_EVENT_POINTER_AXIS,
	LABWC_EVENT_TOUCH_DOWN,
	LABWC_EVENT_TOUCH_UP,
	LABWC_EVENT_TOUCH_MOTION,

	/* Output (notification) */
	LABWC_EVENT_OUTPUT_ADD,
	LABWC_EVENT_OUTPUT_REMOVE,

	/* Render (post-scene, pre-commit) */
	LABWC_EVENT_OUTPUT_RENDER,

	/* Workspace (notification) */
	LABWC_EVENT_WORKSPACE_SWITCH,

	/* SSD (interceptable for CREATE) */
	LABWC_EVENT_SSD_CREATE,
	LABWC_EVENT_SSD_DESTROY,

	LABWC_EVENT_COUNT,
};

/* ---- Event data structures ---- */

/** Common header for all event data */
struct labwc_event_base {
	enum labwc_event_type type;
	bool consumed;  /* For interceptable events: set true to block */
};

/** View lifecycle and state events */
struct labwc_event_view {
	struct labwc_event_base base;
	struct view *view;
};

/** View state change with old/new state */
struct labwc_event_view_state {
	struct labwc_event_base base;
	struct view *view;
	bool new_state;
};

/** Key press/release */
struct labwc_event_key {
	struct labwc_event_base base;
	uint32_t keycode;
	xkb_keysym_t keysym;
	uint32_t modifiers;
	bool pressed;
};

/** Pointer button press/release */
struct labwc_event_button {
	struct labwc_event_base base;
	uint32_t button;
	uint32_t modifiers;
	double x, y;
	struct view *view;  /* view under cursor, may be NULL */
	bool pressed;
};

/** Pointer motion */
struct labwc_event_pointer_motion {
	struct labwc_event_base base;
	double x, y;
	double dx, dy;
};

/** Pointer axis (scroll) */
struct labwc_event_axis {
	struct labwc_event_base base;
	double delta;
	uint32_t orientation;  /* enum wl_pointer_axis */
	uint32_t modifiers;
	struct view *view;
};

/** Output events */
struct labwc_event_output {
	struct labwc_event_base base;
	struct output *output;
};

/** Render hook event (post-scene, pre-commit) */
struct labwc_event_render {
	struct labwc_event_base base;
	struct output *output;
	struct wlr_buffer *buffer;       /* the rendered frame buffer */
	struct wlr_renderer *renderer;   /* for begin_buffer_pass */
	struct wlr_scene_output *scene_output; /* for damage registration */

	/**
	 * Bounding box of additional damage caused by plugin rendering.
	 * Set by the plugin in output-local physical coordinates.
	 * labwc will register this damage so the region is re-rendered
	 * next frame (clearing stale plugin-drawn pixels).
	 */
	struct wlr_box additional_damage;
};

/** Workspace switch event */
struct labwc_event_workspace {
	struct labwc_event_base base;
	struct workspace *from;
	struct workspace *to;
};

/** Touch events */
struct labwc_event_touch {
	struct labwc_event_base base;
	int32_t touch_id;
	double x, y;        /* [0,1] normalized for DOWN/MOTION; 0 for UP */
	uint32_t time_msec;
};

/** SSD create/destroy events */
struct labwc_event_ssd {
	struct labwc_event_base base;
	struct view *view;
	struct ssd *ssd;
};

/* ---- Event subscription API ---- */

/** Callback signature for all events */
typedef void (*labwc_event_fn)(void *event_data, void *user_data);

/** Opaque subscription handle */
struct labwc_event_sub {
	struct wl_list link;         /* loaded_plugin.event_subs */
	struct wl_list manager_link; /* plugin_event_manager.subs[type] */
	enum labwc_event_type type;
	int priority;                /* lower = called first */
	labwc_event_fn callback;
	void *user_data;
	labwc_plugin_t plugin;
};

/**
 * Subscribe to an event with default priority (0).
 * Returns subscription handle for later removal.
 */
struct labwc_event_sub *labwc_event_subscribe(
	labwc_plugin_t plugin,
	enum labwc_event_type type,
	labwc_event_fn callback,
	void *user_data);

/**
 * Subscribe with explicit priority (lower = called first).
 * Negative priorities run before default handlers.
 */
struct labwc_event_sub *labwc_event_subscribe_prio(
	labwc_plugin_t plugin,
	enum labwc_event_type type,
	int priority,
	labwc_event_fn callback,
	void *user_data);

/** Unsubscribe from an event. Frees the subscription. */
void labwc_event_unsubscribe(struct labwc_event_sub *sub);

/* ---- Internal event emission (used by labwc core) ---- */

/** Initialize the event manager. Called from plugin_manager_init(). */
void plugin_events_init(void);

/** Finalize the event manager. */
void plugin_events_finish(void);

/**
 * Emit an event to all subscribed plugins.
 * Returns true if any subscriber set consumed=true.
 */
bool plugin_events_emit(enum labwc_event_type type, void *data);

#endif /* LABWC_PLUGIN_EVENTS_H */
