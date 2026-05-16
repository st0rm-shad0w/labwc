// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#if HAVE_PLUGINS
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "buffer.h"
#include "common/border.h"
#include "common/mem.h"
#include "common/list.h"
#include "common/scene-helpers.h"
#include "common/spawn.h"
#include "config/rcxml.h"
#include "node.h"
#include "plugin/helpers.h"
#include "plugin/manager.h"
#include "labwc.h"
#include "magnifier.h"
#include "menu/menu.h"
#include "output.h"
#include "regions.h"
#include "show-desktop.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"
#include "workspaces.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- View helpers ---- */

struct view *
labwc_get_focused_view(void)
{
	return server.active_view;
}

void
labwc_view_for_each(bool (*callback)(struct view *v, void *data), void *data)
{
	struct view *view;
	wl_list_for_each(view, &server.views, link) {
		if (!callback(view, data)) {
			break;
		}
	}
}

void
labwc_view_focus(struct view *v)
{
	if (v) {
		desktop_focus_view(v, /*raise*/ true);
	}
}

void
labwc_view_close(struct view *v)
{
	if (v && v->impl->close) {
		v->impl->close(v);
	}
}

void
labwc_view_set_geometry(struct view *v, struct wlr_box geo)
{
	if (!v || !v->impl->configure) {
		return;
	}
	v->impl->configure(v, geo);
}

void
labwc_view_maximize(struct view *v, bool maximized)
{
	if (v && v->impl->maximize) {
		v->impl->maximize(v,
			maximized ? VIEW_AXIS_BOTH : VIEW_AXIS_NONE);
	}
}

void
labwc_view_set_fullscreen(struct view *v, bool fullscreen)
{
	if (v && v->impl->set_fullscreen) {
		v->impl->set_fullscreen(v, fullscreen);
	}
}

void
labwc_view_minimize(struct view *v, bool minimized)
{
	if (v && v->impl->minimize) {
		v->impl->minimize(v, minimized);
	}
}

void
labwc_view_raise(struct view *v)
{
	if (v) {
		view_move_to_front(v);
	}
}

void
labwc_view_lower(struct view *v)
{
	if (v) {
		view_move_to_back(v);
	}
}

void
labwc_view_set_always_on_top(struct view *v, bool on_top)
{
	if (!v) {
		return;
	}
	if (on_top && v->layer != VIEW_LAYER_ALWAYS_ON_TOP) {
		view_set_layer(v, VIEW_LAYER_ALWAYS_ON_TOP);
	} else if (!on_top && v->layer == VIEW_LAYER_ALWAYS_ON_TOP) {
		view_set_layer(v, VIEW_LAYER_NORMAL);
	}
}

/* ---- View operations (extended) ---- */

void
labwc_view_set_shade(struct view *v, bool shaded)
{
	if (v) {
		view_set_shade(v, shaded);
	}
}

void
labwc_view_toggle_decorations(struct view *v)
{
	if (v) {
		view_toggle_decorations(v);
	}
}

void
labwc_view_set_decorations(struct view *v, int mode, bool force_ssd)
{
	if (v) {
		view_set_decorations(v, (enum lab_ssd_mode)mode, force_ssd);
	}
}

void
labwc_view_move_to_edge(struct view *v, int direction, bool snap_to_windows)
{
	if (v) {
		view_move_to_edge(v, (enum lab_edge)direction, snap_to_windows);
	}
}

void
labwc_view_snap_to_edge(struct view *v, int direction,
	bool across_outputs, bool combine)
{
	if (v) {
		view_snap_to_edge(v, (enum lab_edge)direction,
			across_outputs, combine);
	}
}

void
labwc_view_grow_to_edge(struct view *v, int direction)
{
	if (v) {
		view_grow_to_edge(v, (enum lab_edge)direction);
	}
}

void
labwc_view_shrink_to_edge(struct view *v, int direction)
{
	if (v) {
		view_shrink_to_edge(v, (enum lab_edge)direction);
	}
}

void
labwc_view_set_untiled(struct view *v)
{
	if (v) {
		view_set_untiled(v);
	}
}

void
labwc_view_move_to_workspace(struct view *v, struct workspace *ws)
{
	if (v && ws) {
		view_move_to_workspace(v, ws);
	}
}

void
labwc_view_toggle_omnipresent(struct view *v)
{
	if (v) {
		view_toggle_visible_on_all_workspaces(v);
	}
}

void
labwc_view_set_layer(struct view *v, int layer)
{
	if (v) {
		view_set_layer(v, (enum view_layer)layer);
	}
}

void
labwc_view_toggle_keybinds(struct view *v)
{
	if (v) {
		view_toggle_keybinds(v);
	}
}

void
labwc_interactive_begin(struct view *v, int mode, int edges)
{
	if (v) {
		interactive_begin(v, (enum input_mode)mode, (enum lab_edge)edges);
	}
}

void
labwc_interactive_finish(struct view *v)
{
	if (v) {
		interactive_finish(v);
	}
}

void
labwc_interactive_cancel(struct view *v)
{
	if (v) {
		interactive_cancel(v);
	}
}

/* ---- View property getters ---- */

const char *
labwc_view_get_title(struct view *v)
{
	return v ? v->title : "";
}

const char *
labwc_view_get_app_id(struct view *v)
{
	return v ? v->app_id : "";
}

struct wlr_box
labwc_view_get_geometry(struct view *v)
{
	if (v) {
		return v->current;
	}
	return (struct wlr_box){ 0 };
}

bool
labwc_view_is_maximized(struct view *v)
{
	return v && v->maximized != VIEW_AXIS_NONE;
}

bool
labwc_view_is_fullscreen(struct view *v)
{
	return v && v->fullscreen;
}

bool
labwc_view_is_minimized(struct view *v)
{
	return v && v->minimized;
}

bool
labwc_view_is_shaded(struct view *v)
{
	return v && v->shaded;
}

bool
labwc_view_is_tiled(struct view *v)
{
	return v && view_is_tiled(v);
}

struct workspace *
labwc_view_get_workspace(struct view *v)
{
	return v ? v->workspace : NULL;
}

struct output *
labwc_view_get_output(struct view *v)
{
	return v ? v->output : NULL;
}

pid_t
labwc_view_get_pid(struct view *v)
{
	if (v && v->impl && v->impl->get_pid) {
		return v->impl->get_pid(v);
	}
	return -1;
}

/* ---- SSD helpers ---- */

void
labwc_ssd_set_titlebar_visible(struct view *v, bool visible)
{
	if (v) {
		v->plugin_titlebar_hidden = !visible;
		if (v->ssd) {
			ssd_set_titlebar(v->ssd, visible);
		}
	}
}

int
labwc_ssd_get_titlebar_height(void)
{
	return rc.theme ? rc.theme->titlebar_height : 0;
}

void
labwc_view_set_margin_override(struct view *v,
	labwc_margin_override_fn fn, void *user_data)
{
	if (!v) {
		return;
	}
	v->plugin_margin_override.fn = fn;
	v->plugin_margin_override.user_data = user_data;
	if (v->ssd) {
		ssd_update_margin(v->ssd);
	}
}

void
labwc_view_clear_margin_override(struct view *v)
{
	if (!v) {
		return;
	}
	v->plugin_margin_override.fn = NULL;
	v->plugin_margin_override.user_data = NULL;
	if (v->ssd) {
		ssd_update_margin(v->ssd);
	}
}

void
labwc_view_set_shade_horizontal(struct view *v, bool horizontal)
{
	if (v) {
		v->plugin_shade_horizontal = horizontal;
	}
}

/* ---- Output helpers ---- */

struct output *
labwc_output_get_focused(void)
{
	return output_nearest_to_cursor();
}

struct wlr_box
labwc_output_get_usable_area(struct output *o)
{
	if (o) {
		return o->usable_area;
	}
	return (struct wlr_box){ 0 };
}

const char *
labwc_output_get_name(struct output *o)
{
	if (o && o->wlr_output) {
		return o->wlr_output->name;
	}
	return NULL;
}

void
labwc_output_for_each(bool (*callback)(struct output *o, void *data),
	void *data)
{
	struct output *output;
	wl_list_for_each(output, &server.outputs, link) {
		if (!callback(output, data)) {
			break;
		}
	}
}

struct output *
labwc_output_get_adjacent(struct output *o, int edge, bool wrap)
{
	if (!o) {
		return NULL;
	}
	return output_get_adjacent(o, (enum lab_edge)edge, wrap);
}

float
labwc_output_get_scale(struct output *o)
{
	return (o && o->wlr_output) ? o->wlr_output->scale : 1.0f;
}

int
labwc_output_get_transform(struct output *o)
{
	return (o && o->wlr_output) ? (int)o->wlr_output->transform : 0;
}

/* ---- Workspace helpers ---- */

struct workspace *
labwc_workspace_get_current(void)
{
	return server.workspaces.current;
}

struct workspace *
labwc_workspace_get_by_name(const char *name)
{
	return workspaces_find(server.workspaces.current, name,
		/*wrap*/ false);
}

void
labwc_workspace_switch_to(struct workspace *ws)
{
	if (ws) {
		workspaces_switch_to(ws, /*update_focus*/ true);
	}
}

const char *
labwc_workspace_get_name(struct workspace *ws)
{
	if (ws) {
		return ws->name;
	}
	return NULL;
}

/* ---- Custom action registration ---- */

struct plugin_action {
	struct wl_list link;           /* loaded_plugin.actions */
	struct wl_list registry_link;  /* global plugin_action_registry */
	labwc_plugin_t plugin;
	char *name;
	labwc_action_fn handler;
	void *user_data;
};

static struct wl_list plugin_action_registry;
static bool action_registry_initialized;

static void
ensure_action_registry(void)
{
	if (!action_registry_initialized) {
		wl_list_init(&plugin_action_registry);
		action_registry_initialized = true;
	}
}

bool
labwc_action_register(labwc_plugin_t plugin, const char *action_name,
	labwc_action_fn handler, void *user_data)
{
	ensure_action_registry();

	/* Check for conflict */
	struct plugin_action *existing;
	wl_list_for_each(existing, &plugin_action_registry, registry_link) {
		if (!strcasecmp(existing->name, action_name)) {
			wlr_log(WLR_ERROR,
				"plugin action '%s' already registered",
				action_name);
			return false;
		}
	}

	struct plugin_action *action = znew(*action);
	action->plugin = plugin;
	action->name = strdup(action_name);
	action->handler = handler;
	action->user_data = user_data;

	wl_list_append(&plugin_action_registry, &action->registry_link);
	if (plugin) {
		wl_list_append(&plugin->actions, &action->link);
	} else {
		wl_list_init(&action->link);
	}

	wlr_log(WLR_INFO, "plugin: registered action '%s'", action_name);
	return true;
}

void
labwc_action_unregister(labwc_plugin_t plugin, const char *action_name)
{
	if (!action_registry_initialized) {
		return;
	}
	struct plugin_action *action, *tmp;
	wl_list_for_each_safe(action, tmp,
		&plugin_action_registry, registry_link) {
		if (!strcasecmp(action->name, action_name) &&
				action->plugin == plugin) {
			wl_list_remove(&action->registry_link);
			wl_list_remove(&action->link);
			free(action->name);
			free(action);
			return;
		}
	}
}

/*
 * Called from action.c to check if an action name matches a
 * plugin-registered action. Returns true if found and dispatched.
 */
bool
plugin_actions_try_run(const char *action_name, struct view *target,
	struct wl_list *args)
{
	if (!action_registry_initialized) {
		return false;
	}
	struct plugin_action *action;
	wl_list_for_each(action, &plugin_action_registry, registry_link) {
		if (!strcasecmp(action->name, action_name)) {
			action->handler(target, args, action->user_data);
			return true;
		}
	}
	return false;
}

/*
 * Called from action.c to check if an action name matches a
 * plugin-registered action (for action_create validation).
 */
bool
plugin_actions_is_registered(const char *action_name)
{
	if (!action_registry_initialized) {
		return false;
	}
	struct plugin_action *action;
	wl_list_for_each(action, &plugin_action_registry, registry_link) {
		if (!strcasecmp(action->name, action_name)) {
			return true;
		}
	}
	return false;
}

/* ---- Per-view / per-output custom data store ---- */

struct plugin_data_entry {
	struct wl_list link;           /* view/output plugin_data list */
	struct wl_list owner_link;     /* loaded_plugin.data_attachments */
	labwc_plugin_t plugin;
	char *key;
	void *data;
	void (*destructor)(void *data);
};

void
labwc_view_set_data(struct view *v, labwc_plugin_t plugin,
	const char *key, void *data, void (*destructor)(void *))
{
	if (!v || !key) {
		return;
	}

	/* Remove existing entry with same key */
	labwc_view_remove_data(v, plugin, key);

	struct plugin_data_entry *entry = znew(*entry);
	entry->plugin = plugin;
	entry->key = strdup(key);
	entry->data = data;
	entry->destructor = destructor;

	wl_list_append(&v->plugin_data, &entry->link);
	if (plugin) {
		wl_list_append(&plugin->data_attachments, &entry->owner_link);
	} else {
		wl_list_init(&entry->owner_link);
	}
}

void *
labwc_view_get_data(struct view *v, labwc_plugin_t plugin, const char *key)
{
	if (!v || !key) {
		return NULL;
	}
	struct plugin_data_entry *entry;
	wl_list_for_each(entry, &v->plugin_data, link) {
		if (entry->plugin == plugin && !strcmp(entry->key, key)) {
			return entry->data;
		}
	}
	return NULL;
}

void
labwc_view_remove_data(struct view *v, labwc_plugin_t plugin, const char *key)
{
	if (!v || !key) {
		return;
	}
	struct plugin_data_entry *entry, *tmp;
	wl_list_for_each_safe(entry, tmp, &v->plugin_data, link) {
		if (entry->plugin == plugin && !strcmp(entry->key, key)) {
			if (entry->destructor) {
				entry->destructor(entry->data);
			}
			wl_list_remove(&entry->link);
			wl_list_remove(&entry->owner_link);
			free(entry->key);
			free(entry);
			return;
		}
	}
}

void
labwc_output_set_data(struct output *o, labwc_plugin_t plugin,
	const char *key, void *data, void (*destructor)(void *))
{
	if (!o || !key) {
		return;
	}
	/* Remove existing entry with same key */
	labwc_output_remove_data(o, plugin, key);
	struct plugin_data_entry *entry = znew(*entry);
	entry->plugin = plugin;
	entry->key = strdup(key);
	entry->data = data;
	entry->destructor = destructor;

	wl_list_append(&o->plugin_data, &entry->link);
	if (plugin) {
		wl_list_append(&plugin->data_attachments, &entry->owner_link);
	} else {
		wl_list_init(&entry->owner_link);
	}
}

void *
labwc_output_get_data(struct output *o, labwc_plugin_t plugin, const char *key)
{
	if (!o || !key) {
		return NULL;
	}
	struct plugin_data_entry *entry;
	wl_list_for_each(entry, &o->plugin_data, link) {
		if (entry->plugin == plugin && !strcmp(entry->key, key)) {
			return entry->data;
		}
	}
	return NULL;
}

void
labwc_output_remove_data(struct output *o, labwc_plugin_t plugin,
	const char *key)
{
	if (!o || !key) {
		return;
	}
	struct plugin_data_entry *entry, *tmp;
	wl_list_for_each_safe(entry, tmp, &o->plugin_data, link) {
		if (entry->plugin == plugin && !strcmp(entry->key, key)) {
			if (entry->destructor) {
				entry->destructor(entry->data);
			}
			wl_list_remove(&entry->link);
			wl_list_remove(&entry->owner_link);
			free(entry->key);
			free(entry);
			return;
		}
	}
}

/* ---- Plugin config access ---- */

/*
 * Parse the plugin's config XML and find a child element by name.
 * Returns the text content of the element or NULL.
 */
static const char *
find_config_value(labwc_plugin_t plugin, const char *key)
{
	if (!plugin || !plugin->config_xml || !key) {
		return NULL;
	}

	/*
	 * Wrap config_xml in a root element for parsing.
	 * The plugin's config_xml contains the children of the <plugin> node.
	 */
	char buf[8192];
	snprintf(buf, sizeof(buf), "<root>%s</root>", plugin->config_xml);

	xmlDocPtr doc = xmlParseMemory(buf, strlen(buf));
	if (!doc) {
		return NULL;
	}

	xmlNodePtr root = xmlDocGetRootElement(doc);
	if (!root) {
		xmlFreeDoc(doc);
		return NULL;
	}

	static char result[1024]; /* NOT reentrant -- see helpers.h doc */
	result[0] = '\0';

	for (xmlNodePtr node = root->children; node; node = node->next) {
		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}
		if (!strcasecmp((const char *)node->name, key)) {
			xmlChar *content = xmlNodeGetContent(node);
			if (content) {
				snprintf(result, sizeof(result), "%s",
					(const char *)content);
				xmlFree(content);
				xmlFreeDoc(doc);
				return result;
			}
		}
	}

	xmlFreeDoc(doc);
	return NULL;
}

const char *
labwc_plugin_config_str(labwc_plugin_t plugin, const char *key,
	const char *default_value)
{
	const char *val = find_config_value(plugin, key);
	return val ? val : default_value;
}

int
labwc_plugin_config_int(labwc_plugin_t plugin, const char *key,
	int default_value)
{
	const char *val = find_config_value(plugin, key);
	return val ? atoi(val) : default_value;
}

bool
labwc_plugin_config_bool(labwc_plugin_t plugin, const char *key,
	bool default_value)
{
	const char *val = find_config_value(plugin, key);
	if (!val) {
		return default_value;
	}
	return !strcasecmp(val, "yes") || !strcasecmp(val, "true") ||
		!strcasecmp(val, "1");
}

double
labwc_plugin_config_double(labwc_plugin_t plugin, const char *key,
	double default_value)
{
	const char *val = find_config_value(plugin, key);
	return val ? atof(val) : default_value;
}

/* ---- Scene graph helpers ---- */

struct wlr_scene_tree *
labwc_scene_get_overlay_tree(void)
{
	return server.menu_tree;
}

struct wlr_scene_tree *
labwc_scene_create_tree(labwc_plugin_t plugin, struct wlr_scene_tree *parent)
{
	return wlr_scene_tree_create(parent);
}

struct wlr_scene_tree *
labwc_view_get_scene_tree(struct view *v)
{
	if (!v) {
		return NULL;
	}
	return v->scene_tree;
}

void
labwc_scene_node_place_below(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling)
{
	if (node && sibling) {
		wlr_scene_node_place_below(node, sibling);
	}
}

void
labwc_scene_node_place_above(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling)
{
	if (node && sibling) {
		wlr_scene_node_place_above(node, sibling);
	}
}

struct wlr_scene_rect *
labwc_scene_rect_create(struct wlr_scene_tree *parent,
	int width, int height, const float color[static 4])
{
	if (!parent) {
		return NULL;
	}
	return wlr_scene_rect_create(parent, width, height, color);
}

void
labwc_scene_rect_set_size(struct wlr_scene_rect *rect,
	int width, int height)
{
	if (rect) {
		wlr_scene_rect_set_size(rect, width, height);
	}
}

void
labwc_scene_rect_set_color(struct wlr_scene_rect *rect,
	const float color[static 4])
{
	if (rect) {
		wlr_scene_rect_set_color(rect, color);
	}
}

void
labwc_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled)
{
	if (node) {
		wlr_scene_node_set_enabled(node, enabled);
	}
}

void
labwc_scene_node_set_position(struct wlr_scene_node *node,
	int x, int y)
{
	if (node) {
		wlr_scene_node_set_position(node, x, y);
	}
}

void
labwc_scene_node_destroy(struct wlr_scene_node *node)
{
	if (node) {
		wlr_scene_node_destroy(node);
	}
}

void
labwc_scene_node_set_type(struct wlr_scene_node *node,
	int type, struct view *view)
{
	if (!node || !view) {
		return;
	}
	node_descriptor_create(node, (enum lab_node_type)type, view, NULL);
}

struct wlr_scene_buffer *
labwc_scene_buffer_create(struct wlr_scene_tree *parent,
	struct wlr_buffer *buffer)
{
	if (!parent) {
		return NULL;
	}
	return wlr_scene_buffer_create(parent, buffer);
}

/* ---- Timer helper ---- */

struct wl_event_source *
labwc_timer_add(int delay_ms, labwc_timer_fn callback, void *data)
{
	struct wl_event_source *source =
		wl_event_loop_add_timer(server.wl_event_loop, callback, data);
	if (source) {
		wl_event_source_timer_update(source, delay_ms);
	}
	return source;
}

/* ---- Cursor helpers ---- */

void
labwc_cursor_get_position(double *x, double *y)
{
	if (x) {
		*x = server.seat.cursor->x;
	}
	if (y) {
		*y = server.seat.cursor->y;
	}
}

void
labwc_cursor_warp(double x, double y)
{
	wlr_cursor_warp(server.seat.cursor, NULL, x, y);
}

/* ---- Spawn ---- */

pid_t
labwc_spawn(const char *command)
{
	if (!command) {
		return -1;
	}
	spawn_async_no_shell(command);
	return 0;
}

/* ---- Menu helpers ---- */

struct menu *
labwc_menu_get_by_id(const char *id)
{
	return menu_get_by_id(id);
}

void
labwc_menu_open(struct menu *menu, int x, int y)
{
	if (menu) {
		menu_open_root(menu, x, y);
	}
}

void
labwc_menu_close(void)
{
	menu_close_root();
}

/* ---- Region helpers ---- */

struct region *
labwc_region_from_name(const char *name, struct output *output)
{
	return regions_from_name(name, output);
}

struct region *
labwc_region_from_cursor(void)
{
	return regions_from_cursor();
}

/* ---- Magnifier helpers ---- */

void
labwc_magnifier_toggle(void)
{
	magnifier_toggle();
}

void
labwc_magnifier_set_scale(int direction)
{
	magnifier_set_scale((enum magnify_dir)direction);
}

/* ---- Show desktop ---- */

void
labwc_show_desktop_toggle(void)
{
	show_desktop_toggle();
}

/* ---- Theme info ---- */

static void
copy_font_info(struct labwc_font_info *dst, const struct font *src)
{
	if (src->name) {
		snprintf(dst->name, sizeof(dst->name), "%s", src->name);
	} else {
		dst->name[0] = '\0';
	}
	dst->size = src->size;
	dst->weight = (int)src->weight;
	dst->slant = (int)src->slant;
}

void
labwc_theme_get_info(struct labwc_theme_info *info)
{
	if (!info) {
		return;
	}
	memset(info, 0, sizeof(*info));

	struct theme *theme = rc.theme;
	if (!theme) {
		return;
	}

	info->border_width = theme->border_width;
	info->titlebar_height = theme->titlebar_height;
	info->button_width = theme->window_button_width;
	info->button_height = theme->window_button_height;
	info->button_spacing = theme->window_button_spacing;
	info->titlebar_padding_width = theme->window_titlebar_padding_width;
	info->titlebar_padding_height = theme->window_titlebar_padding_height;
	info->corner_radius = rc.corner_radius;
	info->label_text_justify = (int)theme->window_label_text_justify;

	copy_font_info(&info->font_active, &rc.font_activewindow);
	copy_font_info(&info->font_inactive, &rc.font_inactivewindow);

	memcpy(info->button_hover_bg_color, theme->window_button_hover_bg_color,
		sizeof(info->button_hover_bg_color));
	info->button_hover_bg_corner_radius =
		theme->window_button_hover_bg_corner_radius;

	info->nr_title_buttons_left = rc.nr_title_buttons_left;
	for (int i = 0; i < rc.nr_title_buttons_left; i++) {
		info->title_buttons_left[i] = (int)rc.title_buttons_left[i];
	}
	info->nr_title_buttons_right = rc.nr_title_buttons_right;
	for (int i = 0; i < rc.nr_title_buttons_right; i++) {
		info->title_buttons_right[i] = (int)rc.title_buttons_right[i];
	}

	for (int i = 0; i < 2; i++) {
		memcpy(info->window[i].border_color,
			theme->window[i].border_color, 4 * sizeof(float));
		memcpy(info->window[i].label_text_color,
			theme->window[i].label_text_color, 4 * sizeof(float));

		/* Titlebar gradient */
		info->window[i].titlebar_gradient =
			(int)theme->window[i].title_bg.gradient;
		memcpy(info->window[i].titlebar_bg_color,
			theme->window[i].title_bg.color, 4 * sizeof(float));
		memcpy(info->window[i].titlebar_bg_color_to,
			theme->window[i].title_bg.color_to, 4 * sizeof(float));
		memcpy(info->window[i].titlebar_bg_split_color,
			theme->window[i].title_bg.color_split_to,
			4 * sizeof(float));
		memcpy(info->window[i].titlebar_bg_split_color_to,
			theme->window[i].title_bg.color_to_split_to,
			4 * sizeof(float));

		/* Shadow */
		memcpy(info->window[i].shadow_color,
			theme->window[i].shadow_color, 4 * sizeof(float));
		info->window[i].shadow_size = theme->window[i].shadow_size;

		/* Per-button colors */
		memcpy(info->window[i].button_close_color,
			theme->window[i].button_colors[LAB_NODE_BUTTON_CLOSE],
			4 * sizeof(float));
		memcpy(info->window[i].button_maximize_color,
			theme->window[i].button_colors[LAB_NODE_BUTTON_MAXIMIZE],
			4 * sizeof(float));
		memcpy(info->window[i].button_iconify_color,
			theme->window[i].button_colors[LAB_NODE_BUTTON_ICONIFY],
			4 * sizeof(float));
		memcpy(info->window[i].button_shade_color,
			theme->window[i].button_colors[LAB_NODE_BUTTON_SHADE],
			4 * sizeof(float));
		memcpy(info->window[i].button_omnipresent_color,
			theme->window[i].button_colors[LAB_NODE_BUTTON_OMNIPRESENT],
			4 * sizeof(float));
		memcpy(info->window[i].button_menu_color,
			theme->window[i].button_colors[LAB_NODE_BUTTON_WINDOW_MENU],
			4 * sizeof(float));
		memcpy(info->window[i].button_icon_color,
			theme->window[i].button_colors[LAB_NODE_BUTTON_WINDOW_ICON],
			4 * sizeof(float));
	}
}

/* ---- Themed button creation ---- */

struct labwc_button {
	struct wlr_scene_tree *tree;
	struct wlr_scene_tree *subtrees[2]; /* [inactive, active] */
	struct wl_list button_lists[2];     /* list heads for ssd_button.link */
	struct ssd_button *buttons[2];
	enum lab_node_type type;
	bool current_active;
};

struct labwc_button *
labwc_button_create(struct wlr_scene_tree *parent, int button_type,
	struct view *view, bool active, int x, int y)
{
	if (!parent || !view) {
		return NULL;
	}

	struct theme *theme = rc.theme;
	if (!theme) {
		return NULL;
	}

	enum lab_node_type type = (enum lab_node_type)button_type;
	if (type < LAB_NODE_BUTTON_FIRST || type > LAB_NODE_BUTTON_LAST) {
		wlr_log(WLR_ERROR, "plugin: invalid button type %d", button_type);
		return NULL;
	}

	struct labwc_button *btn = znew(*btn);
	btn->type = type;
	btn->current_active = active;

	btn->tree = wlr_scene_tree_create(parent);
	if (!btn->tree) {
		free(btn);
		return NULL;
	}
	wlr_scene_node_set_position(&btn->tree->node, x, y);

	for (int i = 0; i < 2; i++) {
		wl_list_init(&btn->button_lists[i]);
		btn->subtrees[i] = wlr_scene_tree_create(btn->tree);
		if (!btn->subtrees[i]) {
			wlr_scene_node_destroy(&btn->tree->node);
			free(btn);
			return NULL;
		}
		btn->buttons[i] = attach_ssd_button(
			&btn->button_lists[i], type, btn->subtrees[i],
			theme->window[i].button_imgs[type],
			0, 0, view);

		wlr_scene_node_set_enabled(
			&btn->subtrees[i]->node, (i == (int)active));
	}

	return btn;
}

void
labwc_button_set_active(struct labwc_button *btn, bool active)
{
	if (!btn) {
		return;
	}
	int old_idx = btn->current_active ? 1 : 0;
	int new_idx = active ? 1 : 0;
	if (old_idx != new_idx && btn->buttons[old_idx]
			&& server.hovered_button == btn->buttons[old_idx]) {
		/*
		 * Transfer hover state to the newly visible subtree so
		 * the visual pressed/hovered feedback follows the
		 * active-state switch.
		 */
		ssd_button_update_state(btn->buttons[old_idx],
			LAB_BS_HOVERED, false);
		server.hovered_button = btn->buttons[new_idx];
		if (btn->buttons[new_idx]) {
			ssd_button_update_state(btn->buttons[new_idx],
				LAB_BS_HOVERED, true);
		}
	}
	btn->current_active = active;
	wlr_scene_node_set_enabled(&btn->subtrees[0]->node, !active);
	wlr_scene_node_set_enabled(&btn->subtrees[1]->node, active);
}

void
labwc_button_set_toggled(struct labwc_button *btn, bool toggled)
{
	if (!btn) {
		return;
	}
	for (int i = 0; i < 2; i++) {
		if (btn->buttons[i]) {
			ssd_button_update_state(btn->buttons[i],
				LAB_BS_TOGGLED, toggled);
		}
	}
}

struct wlr_scene_node *
labwc_button_get_node(struct labwc_button *btn)
{
	return btn ? &btn->tree->node : NULL;
}

void
labwc_button_destroy(struct labwc_button *btn)
{
	if (!btn) {
		return;
	}
	/*
	 * Destroying the scene tree will destroy all child nodes,
	 * which triggers node_descriptor destroy listeners that call
	 * ssd_button_free() for each button.
	 */
	wlr_scene_node_destroy(&btn->tree->node);
	free(btn);
}

/* ---- Buffer rotation utility ---- */

struct wlr_buffer *
labwc_buffer_rotate(struct wlr_buffer *src, double angle_degrees)
{
	if (!src) {
		return NULL;
	}

	double angle_rad = angle_degrees * M_PI / 180.0;
	double cos_a = fabs(cos(angle_rad));
	double sin_a = fabs(sin(angle_rad));

	int src_w = src->width;
	int src_h = src->height;

	/* Rotated bounding box */
	int dst_w = (int)ceil(src_w * cos_a + src_h * sin_a);
	int dst_h = (int)ceil(src_w * sin_a + src_h * cos_a);

	if (dst_w <= 0 || dst_h <= 0) {
		return NULL;
	}

	/* Access source pixels */
	void *src_data = NULL;
	uint32_t src_format = 0;
	size_t src_stride = 0;
	if (!wlr_buffer_begin_data_ptr_access(src,
			WLR_BUFFER_DATA_PTR_ACCESS_READ,
			&src_data, &src_format, &src_stride)) {
		return NULL;
	}

	/* Create cairo surface from source pixel data */
	cairo_surface_t *src_surface = cairo_image_surface_create_for_data(
		(unsigned char *)src_data, CAIRO_FORMAT_ARGB32,
		src_w, src_h, (int)src_stride);

	/* Create destination buffer */
	struct lab_data_buffer *dst_buf = buffer_create_cairo(
		dst_w, dst_h, 1.0f);
	if (!dst_buf) {
		cairo_surface_destroy(src_surface);
		wlr_buffer_end_data_ptr_access(src);
		return NULL;
	}

	cairo_t *cr = cairo_create(dst_buf->surface);

	/* Move to center of destination, rotate, paint source centered */
	cairo_translate(cr, dst_w / 2.0, dst_h / 2.0);
	cairo_rotate(cr, angle_rad);
	cairo_translate(cr, -src_w / 2.0, -src_h / 2.0);

	cairo_set_source_surface(cr, src_surface, 0, 0);
	cairo_paint(cr);

	cairo_destroy(cr);
	cairo_surface_destroy(src_surface);
	wlr_buffer_end_data_ptr_access(src);

	return &dst_buf->base;
}

/* ---- Scene buffer transform helpers ---- */

void
labwc_scene_buffer_set_transform(struct wlr_scene_buffer *buffer,
	int transform)
{
	if (buffer) {
		wlr_scene_buffer_set_transform(buffer,
			(enum wl_output_transform)transform);
	}
}

void
labwc_scene_buffer_set_dest_size(struct wlr_scene_buffer *buffer,
	int width, int height)
{
	if (buffer) {
		wlr_scene_buffer_set_dest_size(buffer, width, height);
	}
}
#endif /* HAVE_PLUGINS */
