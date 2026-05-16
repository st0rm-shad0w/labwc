// SPDX-License-Identifier: GPL-2.0-only

/*
 * Helpers for view server side decorations
 *
 * Copyright (C) Johan Malm 2020-2021
 */

#include "ssd.h"
#include <assert.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

#if HAVE_PLUGINS
#include "plugin/events.h"
#endif

struct border
ssd_thickness(struct view *view)
{
	assert(view);
	/*
	 * Check preconditions for displaying SSD. Note that this
	 * needs to work even before ssd_create() has been called.
	 *
	 * For that reason we are not using the .enabled state of
	 * the titlebar node here but rather check for the view
	 * boolean. If we were to use the .enabled state this would
	 * cause issues on Reconfigure events with views which were
	 * in border-only deco mode as view->ssd would only be set
	 * after ssd_create() returns.
	 */
	if (!view->ssd_mode || view->fullscreen) {
		return (struct border){ 0 };
	}

	struct theme *theme = rc.theme;

	if (view->maximized == VIEW_AXIS_BOTH) {
		struct border thickness = { 0 };
		if (view_titlebar_visible(view)) {
			thickness.top += theme->titlebar_height;
		}
#if HAVE_PLUGINS
		if (view->plugin_margin_override.fn) {
			thickness = view->plugin_margin_override.fn(
				view, thickness, view->plugin_margin_override.user_data);
		}
#endif
		return thickness;
	}

	struct border thickness = {
		.top = theme->titlebar_height + theme->border_width,
		.right = theme->border_width,
		.bottom = theme->border_width,
		.left = theme->border_width,
	};

	if (!view_titlebar_visible(view)) {
		thickness.top -= theme->titlebar_height;
	}
#if HAVE_PLUGINS
	if (view->plugin_margin_override.fn) {
		thickness = view->plugin_margin_override.fn(
			view, thickness, view->plugin_margin_override.user_data);
	}
#endif
	return thickness;
}

struct wlr_box
ssd_max_extents(struct view *view)
{
	assert(view);
	struct border border = ssd_thickness(view);

	int eff_width = view_effective_width(view, /* use_pending */ false);
	int eff_height = view_effective_height(view, /* use_pending */ false);

	return (struct wlr_box){
		.x = view->current.x - border.left,
		.y = view->current.y - border.top,
		.width = eff_width + border.left + border.right,
		.height = eff_height + border.top + border.bottom,
	};
}

/*
 * Resizing and mouse contexts like 'Left', 'TLCorner', etc. in the vicinity of
 * SSD borders, titlebars and extents can have effective "corner regions" that
 * behave differently from single-edge contexts.
 *
 * Corner regions are active whenever the cursor is within a prescribed size
 * (generally rc.resize_corner_range, but clipped to view size) of the view
 * bounds, so check the cursor against the view here.
 */
enum lab_node_type
ssd_get_resizing_type(const struct ssd *ssd, struct wlr_cursor *cursor)
{
	struct view *view = ssd ? ssd->view : NULL;
	if (!view || !cursor || !view->ssd_mode || view->fullscreen) {
		return LAB_NODE_NONE;
	}

	struct wlr_box view_box = view->current;
	view_box.height = view_effective_height(view, /* use_pending */ false);

	/*
	 * Expand the view box to include SSD decoration areas that
	 * should NOT be treated as resize zones.  We use the cached
	 * SSD margin (which accounts for plugin margin overrides)
	 * minus border_width on each side, since the thin border
	 * strips ARE resize zones.
	 *
	 * For the standard top-titlebar layout this reproduces the
	 * previous behaviour: expand_top = titlebar_height, others = 0.
	 * For a plugin side-titlebar, expand_left = bar_width, etc.
	 */
	struct border margin = ssd_get_margin(ssd);
	int bw = rc.theme->border_width;
	int expand_left   = MAX(margin.left   - bw, 0);
	int expand_top    = MAX(margin.top    - bw, 0);
	int expand_right  = MAX(margin.right  - bw, 0);
	int expand_bottom = MAX(margin.bottom - bw, 0);
	view_box.x      -= expand_left;
	view_box.y      -= expand_top;
	view_box.width  += expand_left + expand_right;
	view_box.height += expand_top  + expand_bottom;

	if (wlr_box_contains_point(&view_box, cursor->x, cursor->y)) {
		/* A cursor in bounds of the view is never in an SSD context */
		return LAB_NODE_NONE;
	}

	int corner_height = MAX(0, MIN(rc.resize_corner_range, view_box.height / 2));
	int corner_width = MAX(0, MIN(rc.resize_corner_range, view_box.width / 2));
	bool left = cursor->x < view_box.x + corner_width;
	bool right = cursor->x > view_box.x + view_box.width - corner_width;
	bool top = cursor->y < view_box.y + corner_height;
	bool bottom = cursor->y > view_box.y + view_box.height - corner_height;

	if (top && left) {
		return LAB_NODE_CORNER_TOP_LEFT;
	} else if (top && right) {
		return LAB_NODE_CORNER_TOP_RIGHT;
	} else if (bottom && left) {
		return LAB_NODE_CORNER_BOTTOM_LEFT;
	} else if (bottom && right) {
		return LAB_NODE_CORNER_BOTTOM_RIGHT;
	} else if (top) {
		return LAB_NODE_BORDER_TOP;
	} else if (bottom) {
		return LAB_NODE_BORDER_BOTTOM;
	} else if (left) {
		return LAB_NODE_BORDER_LEFT;
	} else if (right) {
		return LAB_NODE_BORDER_RIGHT;
	}

	return LAB_NODE_NONE;
}

struct ssd *
ssd_create(struct view *view, bool active)
{
	assert(view);
	struct ssd *ssd = znew(*ssd);

	ssd->view = view;
	ssd->tree = lab_wlr_scene_tree_create(view->scene_tree);

	/*
	 * Attach node_descriptor to the root node so that get_cursor_context()
	 * detect cursor hovering on borders and extents.
	 */
	node_descriptor_create(&ssd->tree->node,
		LAB_NODE_SSD_ROOT, view, /*data*/ NULL);

	wlr_scene_node_lower_to_bottom(&ssd->tree->node);
	ssd->titlebar.height = rc.theme->titlebar_height;
	ssd_shadow_create(ssd);
	ssd_extents_create(ssd);
	/*
	 * We need to create the borders after the titlebar because it sets
	 * ssd->state.squared which ssd_border_create() reacts to.
	 * TODO: Set the state here instead so the order does not matter
	 * anymore.
	 */
	ssd_titlebar_create(ssd);
	ssd_border_create(ssd);
	if (!view_titlebar_visible(view)) {
		/* Ensure we keep the old state on Reconfigure or when exiting fullscreen */
		ssd_set_titlebar(ssd, false);
	}
	ssd->margin = ssd_thickness(view);
	ssd_set_active(ssd, active);
	ssd_enable_keybind_inhibit_indicator(ssd, view->inhibits_keybinds);
	ssd->state.geometry = view->current;

	/*
	 * Set view->ssd before emitting the plugin event so that
	 * plugin handlers can call helpers that read view->ssd
	 * (e.g. labwc_ssd_set_titlebar_visible, margin overrides).
	 */
	view->ssd = ssd;

#if HAVE_PLUGINS
	{
		struct labwc_event_ssd ev = {
			.base = { .type = LABWC_EVENT_SSD_CREATE },
			.view = view,
			.ssd = ssd,
		};
		plugin_events_emit(LABWC_EVENT_SSD_CREATE, &ev);
	}
#endif

	return ssd;
}

struct border
ssd_get_margin(const struct ssd *ssd)
{
	return ssd ? ssd->margin : (struct border){ 0 };
}

int
ssd_get_corner_width(void)
{
	/* ensure a minimum corner width */
	return MAX(rc.corner_radius, 5);
}

void
ssd_update_margin(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}
	ssd->margin = ssd_thickness(ssd->view);
}

void
ssd_update_geometry(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}

	struct view *view = ssd->view;
	assert(view);

	struct wlr_box cached = ssd->state.geometry;
	struct wlr_box current = view->current;

	int eff_width = view_effective_width(view, /* use_pending */ false);
	int eff_height = view_effective_height(view, /* use_pending */ false);

	bool update_area = eff_width != cached.width || eff_height != cached.height;
	bool update_extents = update_area
		|| current.x != cached.x || current.y != cached.y;

	bool maximized = view->maximized == VIEW_AXIS_BOTH;
	bool squared = ssd_should_be_squared(ssd);

	bool state_changed = ssd->state.was_maximized != maximized
		|| ssd->state.was_shaded != view->shaded
		|| ssd->state.was_squared != squared
		|| ssd->state.was_omnipresent != view->visible_on_all_workspaces;

	/*
	 * (Un)maximization updates titlebar visibility with
	 * maximizedDecoration=none
	 */
	ssd_set_titlebar(ssd, view_titlebar_visible(view));

	if (update_extents) {
		ssd_extents_update(ssd);
	}

	if (update_area || state_changed) {
		ssd_titlebar_update(ssd);
		ssd_border_update(ssd);
		ssd_shadow_update(ssd);
	}

	if (update_extents) {
		ssd->state.geometry = current;
	}
}

void
ssd_set_titlebar(struct ssd *ssd, bool enabled)
{
	if (!ssd || ssd->titlebar.tree->node.enabled == enabled) {
		return;
	}
	wlr_scene_node_set_enabled(&ssd->titlebar.tree->node, enabled);
	ssd->titlebar.height = enabled ? rc.theme->titlebar_height : 0;
	ssd_border_update(ssd);
	ssd_extents_update(ssd);
	ssd_shadow_update(ssd);
	ssd->margin = ssd_thickness(ssd->view);
}

void
ssd_destroy(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}

	struct view *view = ssd->view;

	/*
	 * Clear hovered_button BEFORE emitting the plugin event,
	 * because plugin SSD_DESTROY handlers may destroy scene
	 * nodes that the hovered button belongs to.
	 */
	if (server.hovered_button && node_view_from_node(
			server.hovered_button->node) == view) {
		server.hovered_button = NULL;
	}

#if HAVE_PLUGINS
	{
		struct labwc_event_ssd ev = {
			.base = { .type = LABWC_EVENT_SSD_DESTROY },
			.view = view,
			.ssd = ssd,
		};
		plugin_events_emit(LABWC_EVENT_SSD_DESTROY, &ev);
	}
#endif

	/* Destroy subcomponents */
	ssd_titlebar_destroy(ssd);
	ssd_border_destroy(ssd);
	ssd_extents_destroy(ssd);
	ssd_shadow_destroy(ssd);
	wlr_scene_node_destroy(&ssd->tree->node);

	free(ssd);
}

enum lab_ssd_mode
ssd_mode_parse(const char *mode)
{
	if (!mode) {
		return LAB_SSD_MODE_INVALID;
	}
	if (!strcasecmp(mode, "none")) {
		return LAB_SSD_MODE_NONE;
	} else if (!strcasecmp(mode, "border")) {
		return LAB_SSD_MODE_BORDER;
	} else if (!strcasecmp(mode, "full")) {
		return LAB_SSD_MODE_FULL;
	} else {
		return LAB_SSD_MODE_INVALID;
	}
}

void
ssd_set_active(struct ssd *ssd, bool active)
{
	if (!ssd) {
		return;
	}
	enum ssd_active_state active_state;
	FOR_EACH_ACTIVE_STATE(active_state) {
		wlr_scene_node_set_enabled(
			&ssd->border.subtrees[active_state].tree->node,
			active == active_state);
		wlr_scene_node_set_enabled(
			&ssd->titlebar.subtrees[active_state].tree->node,
			active == active_state);
		if (ssd->shadow.subtrees[active_state].tree) {
			wlr_scene_node_set_enabled(
				&ssd->shadow.subtrees[active_state].tree->node,
				active == active_state);
		}
	}
}

void
ssd_enable_shade(struct ssd *ssd, bool enable)
{
	if (!ssd) {
		return;
	}
	ssd_titlebar_update(ssd);
	ssd_border_update(ssd);
	wlr_scene_node_set_enabled(&ssd->extents.tree->node, !enable);
	ssd_shadow_update(ssd);
}

void
ssd_enable_keybind_inhibit_indicator(struct ssd *ssd, bool enable)
{
	if (!ssd) {
		return;
	}

	float *color = enable
		? rc.theme->window_toggled_keybinds_color
		: rc.theme->window[SSD_ACTIVE].border_color;
	wlr_scene_rect_set_color(ssd->border.subtrees[SSD_ACTIVE].top, color);
}

bool
ssd_debug_is_root_node(const struct ssd *ssd, struct wlr_scene_node *node)
{
	if (!ssd || !node) {
		return false;
	}
	return node == &ssd->tree->node;
}

const char *
ssd_debug_get_node_name(const struct ssd *ssd, struct wlr_scene_node *node)
{
	if (!ssd || !node) {
		return NULL;
	}
	if (node == &ssd->tree->node) {
		return "view->ssd";
	}
	if (node == &ssd->titlebar.subtrees[SSD_ACTIVE].tree->node) {
		return "titlebar.active";
	}
	if (node == &ssd->titlebar.subtrees[SSD_INACTIVE].tree->node) {
		return "titlebar.inactive";
	}
	if (node == &ssd->border.subtrees[SSD_ACTIVE].tree->node) {
		return "border.active";
	}
	if (node == &ssd->border.subtrees[SSD_INACTIVE].tree->node) {
		return "border.inactive";
	}
	if (node == &ssd->extents.tree->node) {
		return "extents";
	}
	return NULL;
}
