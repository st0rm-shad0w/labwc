/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_PLUGIN_HELPERS_H
#define LABWC_PLUGIN_HELPERS_H

#include <stdbool.h>
#include <sys/types.h>
#include <wayland-util.h>
#include <wlr/util/box.h>
#include "plugin/plugin.h"

/* Forward declarations */
struct view;
struct output;
struct workspace;
struct region;
struct menu;
struct ssd;
struct border;
struct wlr_scene_tree;
struct wlr_scene_node;
struct wlr_scene_rect;
struct wlr_scene_buffer;
struct wlr_buffer;
struct wl_event_source;

/* ---- View helpers (convenience wrappers) ---- */

struct view *labwc_get_focused_view(void);
void labwc_view_for_each(bool (*callback)(struct view *v, void *data),
	void *data);

void labwc_view_focus(struct view *v);
void labwc_view_close(struct view *v);
void labwc_view_set_geometry(struct view *v, struct wlr_box geo);
void labwc_view_maximize(struct view *v, bool maximized);
void labwc_view_set_fullscreen(struct view *v, bool fullscreen);
void labwc_view_minimize(struct view *v, bool minimized);
void labwc_view_raise(struct view *v);
void labwc_view_lower(struct view *v);
void labwc_view_set_always_on_top(struct view *v, bool on_top);

/* ---- View operations (extended) ---- */

void labwc_view_set_shade(struct view *v, bool shaded);
void labwc_view_toggle_decorations(struct view *v);
void labwc_view_set_decorations(struct view *v, int mode, bool force_ssd);
void labwc_view_move_to_edge(struct view *v, int direction,
	bool snap_to_windows);
void labwc_view_snap_to_edge(struct view *v, int direction,
	bool across_outputs, bool combine);
void labwc_view_grow_to_edge(struct view *v, int direction);
void labwc_view_shrink_to_edge(struct view *v, int direction);
void labwc_view_set_untiled(struct view *v);
void labwc_view_move_to_workspace(struct view *v, struct workspace *ws);
void labwc_view_toggle_omnipresent(struct view *v);
void labwc_view_set_layer(struct view *v, int layer);
void labwc_view_toggle_keybinds(struct view *v);

/** Begin interactive move/resize. mode: 1=move, 2=resize. */
void labwc_interactive_begin(struct view *v, int mode, int edges);
void labwc_interactive_finish(struct view *v);
void labwc_interactive_cancel(struct view *v);

/* ---- View property getters ---- */

const char *labwc_view_get_title(struct view *v);
const char *labwc_view_get_app_id(struct view *v);
struct wlr_box labwc_view_get_geometry(struct view *v);
bool labwc_view_is_maximized(struct view *v);
bool labwc_view_is_fullscreen(struct view *v);
bool labwc_view_is_minimized(struct view *v);
bool labwc_view_is_shaded(struct view *v);
bool labwc_view_is_tiled(struct view *v);
struct workspace *labwc_view_get_workspace(struct view *v);
struct output *labwc_view_get_output(struct view *v);
pid_t labwc_view_get_pid(struct view *v);

/* ---- SSD helpers ---- */

void labwc_ssd_set_titlebar_visible(struct view *v, bool visible);
int labwc_ssd_get_titlebar_height(void);

/**
 * Margin override callback.  Receives the default SSD margin and
 * returns a modified version.  Installed via labwc_view_set_margin_override().
 */
typedef struct border (*labwc_margin_override_fn)(
	struct view *view, struct border default_margin, void *user_data);

void labwc_view_set_margin_override(struct view *v,
	labwc_margin_override_fn fn, void *user_data);
void labwc_view_clear_margin_override(struct view *v);

/**
 * Set the shade direction for a view.  When @horizontal is true,
 * shading collapses width to 0 instead of height, so a side titlebar
 * plugin's bar remains visible.
 */
void labwc_view_set_shade_horizontal(struct view *v, bool horizontal);

/* ---- Output helpers ---- */

struct output *labwc_output_get_focused(void);
struct wlr_box labwc_output_get_usable_area(struct output *o);
const char *labwc_output_get_name(struct output *o);
void labwc_output_for_each(bool (*callback)(struct output *o, void *data),
	void *data);
struct output *labwc_output_get_adjacent(struct output *o, int edge,
	bool wrap);
float labwc_output_get_scale(struct output *o);
int labwc_output_get_transform(struct output *o);

/* ---- Workspace helpers ---- */

struct workspace *labwc_workspace_get_current(void);
struct workspace *labwc_workspace_get_by_name(const char *name);
void labwc_workspace_switch_to(struct workspace *ws);
const char *labwc_workspace_get_name(struct workspace *ws);

/* ---- Custom action registration ---- */

typedef void (*labwc_action_fn)(struct view *target,
	struct wl_list *args, void *user_data);

bool labwc_action_register(labwc_plugin_t plugin, const char *action_name,
	labwc_action_fn handler, void *user_data);
void labwc_action_unregister(labwc_plugin_t plugin, const char *action_name);

/* ---- Per-view / per-output custom data store ---- */

void labwc_view_set_data(struct view *v, labwc_plugin_t plugin,
	const char *key, void *data, void (*destructor)(void *));
void *labwc_view_get_data(struct view *v, labwc_plugin_t plugin,
	const char *key);
void labwc_view_remove_data(struct view *v, labwc_plugin_t plugin,
	const char *key);

void labwc_output_set_data(struct output *o, labwc_plugin_t plugin,
	const char *key, void *data, void (*destructor)(void *));
void *labwc_output_get_data(struct output *o, labwc_plugin_t plugin,
	const char *key);
void labwc_output_remove_data(struct output *o, labwc_plugin_t plugin,
	const char *key);

/* ---- Plugin config access ---- */

/**
 * Get a string config value from the plugin's <plugin> XML block.
 * WARNING: returns a pointer to a static 1024-byte buffer -- NOT reentrant.
 * Consecutive calls overwrite previous results.
 */
const char *labwc_plugin_config_str(labwc_plugin_t plugin,
	const char *key, const char *default_value);
int labwc_plugin_config_int(labwc_plugin_t plugin,
	const char *key, int default_value);
bool labwc_plugin_config_bool(labwc_plugin_t plugin,
	const char *key, bool default_value);
double labwc_plugin_config_double(labwc_plugin_t plugin,
	const char *key, double default_value);

/* ---- Scene graph helpers ---- */

struct wlr_scene_tree *labwc_scene_get_overlay_tree(void);

/** Create a plugin scene tree (not automatically tracked for cleanup). */
struct wlr_scene_tree *labwc_scene_create_tree(labwc_plugin_t plugin,
	struct wlr_scene_tree *parent);

struct wlr_scene_tree *labwc_view_get_scene_tree(struct view *v);

void labwc_scene_node_place_below(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);
void labwc_scene_node_place_above(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);

struct wlr_scene_rect *labwc_scene_rect_create(
	struct wlr_scene_tree *parent, int width, int height,
	const float color[static 4]);
void labwc_scene_rect_set_size(struct wlr_scene_rect *rect,
	int width, int height);
void labwc_scene_rect_set_color(struct wlr_scene_rect *rect,
	const float color[static 4]);
void labwc_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled);
void labwc_scene_node_set_position(struct wlr_scene_node *node, int x, int y);
void labwc_scene_node_destroy(struct wlr_scene_node *node);

/**
 * Attach a node_descriptor to a scene node so the cursor context system
 * recognises it.  Use LAB_NODE_TITLEBAR to make an area behave like the
 * titlebar (enables drag-to-move, right-click menu, focus-on-click).
 * Values from common/node-type.h.
 */
void labwc_scene_node_set_type(struct wlr_scene_node *node,
	int type, struct view *view);

struct wlr_scene_buffer *labwc_scene_buffer_create(
	struct wlr_scene_tree *parent, struct wlr_buffer *buffer);

/* ---- Timer helper ---- */

typedef int (*labwc_timer_fn)(void *data);

struct wl_event_source *labwc_timer_add(int delay_ms,
	labwc_timer_fn callback, void *data);

/* ---- Cursor helpers ---- */

void labwc_cursor_get_position(double *x, double *y);
void labwc_cursor_warp(double x, double y);

/* ---- Spawn ---- */

/**
 * Launch a process asynchronously.
 * NOTE: Always returns 0 on success -- the actual child PID is not available.
 */
pid_t labwc_spawn(const char *command);

/* ---- Menu helpers ---- */

struct menu *labwc_menu_get_by_id(const char *id);
void labwc_menu_open(struct menu *menu, int x, int y);
void labwc_menu_close(void);

/* ---- Region helpers ---- */

struct region *labwc_region_from_name(const char *name, struct output *output);
struct region *labwc_region_from_cursor(void);

/* ---- Magnifier helpers ---- */

void labwc_magnifier_toggle(void);
/** direction: 0 = increase, 1 = decrease */
void labwc_magnifier_set_scale(int direction);

/* ---- Show desktop ---- */

void labwc_show_desktop_toggle(void);

/* ---- Theme info ---- */

struct labwc_font_info {
	char name[128];     /* font family name */
	int size;           /* point size */
	int weight;         /* PangoWeight: 400=normal, 700=bold */
	int slant;          /* PangoStyle: 0=normal, 1=oblique, 2=italic */
};

struct labwc_theme_info {
	/* Global dimensions */
	int border_width;
	int titlebar_height;
	int button_width;
	int button_height;
	int button_spacing;
	int titlebar_padding_width;
	int titlebar_padding_height;
	int corner_radius;
	int label_text_justify;  /* 0=LEFT, 1=CENTER, 2=RIGHT */

	/* Fonts */
	struct labwc_font_info font_active;
	struct labwc_font_info font_inactive;

	/* Button hover effect */
	float button_hover_bg_color[4];
	int button_hover_bg_corner_radius;

	/* Configured titlebar button layout (enum lab_node_type values) */
	int title_buttons_left[8];
	int nr_title_buttons_left;
	int title_buttons_right[8];
	int nr_title_buttons_right;

	/* Per active/inactive state: [0]=inactive, [1]=active */
	struct {
		float border_color[4];
		float label_text_color[4];

		/* Titlebar background gradient */
		int titlebar_gradient;  /* 0=solid, 1=vertical, 2=splitvertical */
		float titlebar_bg_color[4];
		float titlebar_bg_color_to[4];
		float titlebar_bg_split_color[4];
		float titlebar_bg_split_color_to[4];

		/* Shadow */
		float shadow_color[4];
		int shadow_size;

		/* Per-button foreground colors */
		float button_close_color[4];
		float button_maximize_color[4];
		float button_iconify_color[4];
		float button_shade_color[4];
		float button_omnipresent_color[4];
		float button_menu_color[4];
		float button_icon_color[4];
	} window[2];
};

/**
 * Fill a labwc_theme_info struct with current theme data.
 * The struct is owned by the caller and can be stored.
 */
void labwc_theme_get_info(struct labwc_theme_info *info);

/* ---- Text buffer with arbitrary rotation ---- */

/** Opaque text buffer handle */
struct labwc_text_buffer;

struct labwc_text_buffer *labwc_text_buffer_create(
	struct wlr_scene_tree *parent);

void labwc_text_buffer_update(struct labwc_text_buffer *buf,
	const char *text, int max_width,
	const char *font_name, int font_size,
	int font_weight, int font_slant,
	const float color[static 4], const float bg_color[static 4]);

/**
 * Set rotation angle in degrees.  0 = no rotation.
 * Positive = counter-clockwise.  Triggers re-render.
 */
void labwc_text_buffer_set_rotation(struct labwc_text_buffer *buf,
	double angle_degrees);

int labwc_text_buffer_get_width(struct labwc_text_buffer *buf);
int labwc_text_buffer_get_height(struct labwc_text_buffer *buf);

struct wlr_scene_buffer *labwc_text_buffer_get_scene_buffer(
	struct labwc_text_buffer *buf);

void labwc_text_buffer_destroy(struct labwc_text_buffer *buf);

/* ---- Themed button creation ---- */

/** Opaque button handle */
struct labwc_button;

/**
 * Create a themed button that integrates with the cursor context system.
 * @param button_type One of LAB_NODE_BUTTON_CLOSE, LAB_NODE_BUTTON_MAXIMIZE,
 *                    LAB_NODE_BUTTON_ICONIFY, LAB_NODE_BUTTON_SHADE,
 *                    LAB_NODE_BUTTON_OMNIPRESENT, LAB_NODE_BUTTON_WINDOW_MENU.
 *                    Use values from common/node-type.h.
 * @param view        View for action dispatch context.
 * @param active      Initial active/inactive state.
 */
struct labwc_button *labwc_button_create(
	struct wlr_scene_tree *parent,
	int button_type,
	struct view *view,
	bool active,
	int x, int y);

void labwc_button_set_active(struct labwc_button *btn, bool active);
void labwc_button_set_toggled(struct labwc_button *btn, bool toggled);

struct wlr_scene_node *labwc_button_get_node(struct labwc_button *btn);

void labwc_button_destroy(struct labwc_button *btn);

/* ---- Buffer rotation utility ---- */

/**
 * Create a new buffer with the contents of @src rotated by @angle_degrees.
 * Positive = counter-clockwise.  Caller owns the returned buffer
 * and must call wlr_buffer_drop() when done.
 * Returns NULL on failure.
 */
struct wlr_buffer *labwc_buffer_rotate(
	struct wlr_buffer *src, double angle_degrees);

/* ---- Scene buffer transform helpers ---- */

/**
 * Set buffer transform (wl_output_transform values 0-7).
 * 0=normal, 1=90, 2=180, 3=270, 4-7=flipped variants.
 */
void labwc_scene_buffer_set_transform(
	struct wlr_scene_buffer *buffer, int transform);

void labwc_scene_buffer_set_dest_size(
	struct wlr_scene_buffer *buffer, int width, int height);

/* ---- Internal: called from action.c / window-rules.c ---- */

bool plugin_actions_try_run(const char *action_name, struct view *target,
	struct wl_list *args);
bool plugin_actions_is_registered(const char *action_name);

#endif /* LABWC_PLUGIN_HELPERS_H */
