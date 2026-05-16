/*
 * labwc sidetitlebar plugin
 *
 * Replaces the top titlebar with a side (left or right) titlebar using the
 * extended plugin API: theme info, rotatable text buffers, and
 * themed button creation.
 *
 * Demonstrates:
 *   - labwc_theme_get_info()         read theme colors / dimensions / button layout
 *   - labwc_text_buffer_create()     rotatable text rendering
 *   - labwc_button_create()          themed button with cursor integration
 *   - labwc_view_set_margin_override() adjust SSD margins
 *   - labwc_ssd_set_titlebar_visible() persistently hide the default top titlebar
 *   - labwc_view_set_data()          per-view plugin state
 *
 * Config (rc.xml):
 *   <plugins>
 *     <plugin name="sidetitlebar">
 *       <side>left</side>   <!-- "left" or "right" -->
 *     </plugin>
 *   </plugins>
 *
 * Build:
 *   cc -shared -fPIC -DWLR_USE_UNSTABLE -DHAVE_PLUGINS=1 \
 *     -o libsidetitlebar.so sidetitlebar.c \
 *     -Iinclude -Ibuild/include \
 *     $(pkg-config --cflags wlroots-0.20 wayland-server xkbcommon \
 *       libxml-2.0 pixman-1 libdrm glib-2.0 cairo pangocairo) -lm
 */

#define _POSIX_C_SOURCE 200809L
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "plugin/plugin.h"
#include "plugin/events.h"
#include "plugin/helpers.h"
#include "common/border.h"
#include "common/node-type.h"
#include "labwc.h"
#include "menu/menu.h"
#include "view.h"

/* ---- Constants ---- */

#define SIDE_KEY       "sidetitlebar"
#define MAX_BUTTONS    14  /* left + right, max 7 each */

/* ---- Per-view state ---- */

struct side_view {
	struct view *view;
	struct wl_list link;               /* all_side_views */

	/* Scene graph */
	struct wlr_scene_tree *tree;       /* root, child of view scene_tree */
	struct wlr_scene_rect *bg;         /* background bar */
	struct labwc_text_buffer *title;   /* rotated title text */

	/* Dynamic buttons from configured layout */
	struct labwc_button *buttons[MAX_BUTTONS];
	int button_types[MAX_BUTTONS];     /* lab_node_type for toggled tracking */
	int nr_buttons;
	int nr_buttons_left;               /* first group count (top of bar) */

	/* Border rects */
	struct wlr_scene_rect *border_outer;  /* outer edge of side bar */
	struct wlr_scene_rect *border_top;    /* top cap */
	struct wlr_scene_rect *border_bottom; /* bottom cap */
};

/* ---- Plugin state ---- */

static labwc_plugin_t self_handle;
static struct labwc_theme_info theme;
static bool use_right_side;  /* false = left, true = right */

/* Global list of all side_view instances for button-event iteration */
static struct wl_list all_side_views;

/* Event subscriptions */
static struct labwc_event_sub *sub_ssd_create;
static struct labwc_event_sub *sub_ssd_destroy;
static struct labwc_event_sub *sub_view_focus;
static struct labwc_event_sub *sub_view_resized;
static struct labwc_event_sub *sub_view_maximized;
static struct labwc_event_sub *sub_view_title;
static struct labwc_event_sub *sub_config_reload;
static struct labwc_event_sub *sub_workspace_changed;
static struct labwc_event_sub *sub_button;

/* ---- Helpers ---- */

static int
bar_width(void)
{
	return theme.titlebar_height;
}

static struct border
side_margin(struct view *view, struct border def, void *data)
{
	(void)data;
	(void)view;
	int w = bar_width() + theme.border_width;
	if (use_right_side) {
		def.right = w;
	} else {
		def.left = w;
	}
	return def;
}

static void
update_title(struct side_view *sv, bool active)
{
	const char *title = labwc_view_get_title(sv->view);
	if (!title) {
		title = "";
	}

	struct labwc_font_info *fi = active
		? &theme.font_active : &theme.font_inactive;
	const float *color = active
		? theme.window[1].label_text_color
		: theme.window[0].label_text_color;
	const float bg[4] = { 0, 0, 0, 0 };

	struct wlr_box geo = labwc_view_get_geometry(sv->view);
	int h = geo.height;

	int btn_step = theme.button_height + theme.button_spacing;
	int top_area = sv->nr_buttons_left * btn_step
		+ theme.titlebar_padding_height;
	int nr_right = sv->nr_buttons - sv->nr_buttons_left;
	int bottom_area = nr_right * btn_step
		+ theme.titlebar_padding_height;
	int max_w = h - top_area - bottom_area
		- 2 * theme.titlebar_padding_width;
	if (max_w < 10) {
		max_w = 10;
	}

	labwc_text_buffer_update(sv->title, title, max_w,
		fi->name, fi->size, fi->weight, fi->slant,
		color, bg);
}

static void
layout_side(struct side_view *sv)
{
	struct wlr_box geo = labwc_view_get_geometry(sv->view);
	int bw = bar_width();
	int bord = theme.border_width;
	int h = geo.height;

	/*
	 * When horizontally shaded the SSD left and right borders
	 * collapse to adjacent strips (effective width = 0), creating
	 * a double-width border on the content-facing edge.  Extend
	 * the bar inward by one border_width to paint over the inner
	 * SSD border so only a single border remains on the edge.
	 */
	bool shaded = labwc_view_is_shaded(sv->view);
	int extra = shaded ? bord : 0;

	/* Position the root tree relative to the view */
	if (use_right_side) {
		labwc_scene_node_set_position(&sv->tree->node,
			geo.width, 0);
	} else {
		labwc_scene_node_set_position(&sv->tree->node,
			-(bw + bord), 0);
	}

	/* Size the background bar - extend to absorb inner border when shaded */
	int bg_x = use_right_side ? -extra : 0;
	labwc_scene_node_set_position(&sv->bg->node, bg_x, 0);
	labwc_scene_rect_set_size(sv->bg, bw + extra, h);

	/*
	 * Position border rects.
	 * For left side:  border_outer is at x=-bord (left of bar)
	 *                 border_top is at y=-bord (above bar)
	 *                 border_bottom is at y=h (below bar)
	 * For right side: border_outer is at x=bw (right of bar)
	 *                 border_top/bottom same
	 */
	int full_w = bw + bord + extra;

	if (sv->border_outer) {
		int bx = use_right_side ? bw : -bord;
		labwc_scene_rect_set_size(sv->border_outer, bord, h);
		labwc_scene_node_set_position(
			&sv->border_outer->node, bx, 0);
	}
	if (sv->border_top) {
		int bx = use_right_side ? -extra : -bord;
		labwc_scene_rect_set_size(sv->border_top, full_w, bord);
		labwc_scene_node_set_position(
			&sv->border_top->node, bx, -bord);
	}
	if (sv->border_bottom) {
		int bx = use_right_side ? -extra : -bord;
		labwc_scene_rect_set_size(sv->border_bottom, full_w, bord);
		labwc_scene_node_set_position(
			&sv->border_bottom->node, bx, h);
	}

	/*
	 * Layout order: second group (right buttons) at top,
	 * title text in the middle, first group (left buttons)
	 * anchored to the bottom.
	 */
	int btn_x = (bw - theme.button_width) / 2;
	int btn_step = theme.button_height + theme.button_spacing;

	/* Second group (right buttons): top-down from the top padding */
	int top_y = theme.titlebar_padding_height;
	for (int i = sv->nr_buttons_left; i < sv->nr_buttons; i++) {
		if (sv->buttons[i]) {
			labwc_scene_node_set_position(
				labwc_button_get_node(sv->buttons[i]),
				btn_x, top_y);
			top_y += btn_step;
		}
	}

	/* First group (left buttons): bottom-up from the bottom padding */
	int bot_y = h - theme.titlebar_padding_height;
	for (int i = sv->nr_buttons_left - 1; i >= 0; i--) {
		if (sv->buttons[i]) {
			bot_y -= theme.button_height;
			labwc_scene_node_set_position(
				labwc_button_get_node(sv->buttons[i]),
				btn_x, bot_y);
			bot_y -= theme.button_spacing;
		}
	}

	/* Title text: rotated 270 degrees, centered between the two groups */
	int title_top = top_y + theme.titlebar_padding_width;
	int title_bot = bot_y - theme.titlebar_padding_width;
	int title_h = labwc_text_buffer_get_height(sv->title);
	int title_gap = title_bot - title_top;

	/* Center the rotated text vertically in the gap */
	int title_y = title_top + (title_gap - title_h) / 2;
	if (title_y < title_top) {
		title_y = title_top;
	}

	/* Center the rotated text horizontally within the bar */
	int title_x = (bw - labwc_text_buffer_get_width(sv->title)) / 2;
	if (title_x < 0) {
		title_x = 0;
	}

	struct wlr_scene_buffer *sbuf =
		labwc_text_buffer_get_scene_buffer(sv->title);
	if (sbuf) {
		labwc_scene_node_set_position(&sbuf->node,
			title_x, title_y);
		/* Hide title if there's not enough space between groups */
		labwc_scene_node_set_enabled(&sbuf->node,
			title_h <= title_gap);
	}
}

static void
set_active_state(struct side_view *sv, bool active)
{
	int idx = active ? 1 : 0;

	/* Update background color */
	labwc_scene_rect_set_color(sv->bg,
		theme.window[idx].titlebar_bg_color);

	/* Update border colors */
	if (sv->border_outer) {
		labwc_scene_rect_set_color(sv->border_outer,
			theme.window[idx].border_color);
	}
	if (sv->border_top) {
		labwc_scene_rect_set_color(sv->border_top,
			theme.window[idx].border_color);
	}
	if (sv->border_bottom) {
		labwc_scene_rect_set_color(sv->border_bottom,
			theme.window[idx].border_color);
	}

	/* Update buttons */
	for (int i = 0; i < sv->nr_buttons; i++) {
		if (sv->buttons[i]) {
			labwc_button_set_active(sv->buttons[i], active);
		}
	}

	/* Update title text colors */
	update_title(sv, active);
}

static void
update_button_toggles(struct side_view *sv)
{
	bool maximized = labwc_view_is_maximized(sv->view);
	bool shaded = labwc_view_is_shaded(sv->view);
	bool omnipresent = sv->view->visible_on_all_workspaces;

	for (int i = 0; i < sv->nr_buttons; i++) {
		if (!sv->buttons[i]) {
			continue;
		}
		switch (sv->button_types[i]) {
		case LAB_NODE_BUTTON_MAXIMIZE:
			labwc_button_set_toggled(sv->buttons[i], maximized);
			break;
		case LAB_NODE_BUTTON_SHADE:
			labwc_button_set_toggled(sv->buttons[i], shaded);
			break;
		case LAB_NODE_BUTTON_OMNIPRESENT:
			labwc_button_set_toggled(sv->buttons[i], omnipresent);
			break;
		default:
			break;
		}
	}
}

static void
side_view_destroy(struct side_view *sv)
{
	if (!sv) {
		return;
	}
	wl_list_remove(&sv->link);
	/*
	 * Destroy the root tree if the view is still alive.
	 * During compositor shutdown the plugin manager calls data
	 * destructors before views are destroyed -- in that case
	 * the scene tree is still valid and we must clean it up.
	 * The cascade handles all children (buttons, title, etc.).
	 */
	if (sv->tree) {
		labwc_scene_node_destroy(&sv->tree->node);
		sv->tree = NULL;
	}
	for (int i = 0; i < sv->nr_buttons; i++) {
		free(sv->buttons[i]);
	}
	free(sv);
}

static void
side_view_data_destructor(void *data)
{
	side_view_destroy((struct side_view *)data);
}

/* ---- Event handlers ---- */

static void
on_ssd_create(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_ssd *ev = event_data;
	struct view *view = ev->view;

	/* Persistently hide the default top titlebar */
	labwc_ssd_set_titlebar_visible(view, false);

	/* Shade should collapse horizontally (left) not vertically (up) */
	labwc_view_set_shade_horizontal(view, true);

	/* Install margin override (includes border_width) */
	labwc_view_set_margin_override(view, side_margin, NULL);

	/* Create per-view state */
	struct side_view *sv = calloc(1, sizeof(*sv));
	if (!sv) {
		return;
	}
	sv->view = view;

	/* Root tree under the view's scene tree */
	struct wlr_scene_tree *vtree = labwc_view_get_scene_tree(view);
	sv->tree = labwc_scene_create_tree(self_handle, vtree);
	if (!sv->tree) {
		free(sv);
		return;
	}

	/* Background rect -- marked as TITLEBAR for cursor context */
	sv->bg = labwc_scene_rect_create(sv->tree,
		bar_width(), 100, theme.window[1].titlebar_bg_color);
	labwc_scene_node_set_type(&sv->bg->node, LAB_NODE_TITLEBAR, view);

	/* Border rects */
	sv->border_outer = labwc_scene_rect_create(sv->tree,
		theme.border_width, 100, theme.window[1].border_color);
	sv->border_top = labwc_scene_rect_create(sv->tree,
		bar_width() + theme.border_width, theme.border_width,
		theme.window[1].border_color);
	sv->border_bottom = labwc_scene_rect_create(sv->tree,
		bar_width() + theme.border_width, theme.border_width,
		theme.window[1].border_color);

	/*
	 * Title text (270 degrees = reads bottom-to-top).
	 * Created BEFORE buttons so buttons render on top in z-order
	 * (matching the standard SSD which creates title before buttons).
	 * Tagged with LAB_NODE_TITLE so cursor context correctly
	 * identifies it for drag-to-move and hover tracking.
	 */
	sv->title = labwc_text_buffer_create(sv->tree);
	labwc_text_buffer_set_rotation(sv->title, 270.0);
	struct wlr_scene_buffer *title_sbuf =
		labwc_text_buffer_get_scene_buffer(sv->title);
	if (title_sbuf) {
		labwc_scene_node_set_type(&title_sbuf->node,
			LAB_NODE_TITLE, view);
	}
	update_title(sv, true);

	/*
	 * Create buttons from the configured titlebar layout.
	 * Both groups are reversed so the visual order on the
	 * vertical side bar mirrors the horizontal titlebar
	 * (outermost buttons at the edges).
	 */
	for (int i = theme.nr_title_buttons_left - 1; i >= 0; i--) {
		int type = theme.title_buttons_left[i];
		sv->button_types[sv->nr_buttons] = type;
		sv->buttons[sv->nr_buttons] = labwc_button_create(
			sv->tree, type, view, true, 0, 0);
		sv->nr_buttons++;
	}
	sv->nr_buttons_left = sv->nr_buttons;
	for (int i = theme.nr_title_buttons_right - 1; i >= 0; i--) {
		int type = theme.title_buttons_right[i];
		sv->button_types[sv->nr_buttons] = type;
		sv->buttons[sv->nr_buttons] = labwc_button_create(
			sv->tree, type, view, true, 0, 0);
		sv->nr_buttons++;
	}

	/* Layout everything */
	layout_side(sv);

	/* Track in global list for button-event iteration */
	wl_list_insert(&all_side_views, &sv->link);

	/* Attach to view for lifecycle tracking */
	labwc_view_set_data(view, self_handle, SIDE_KEY,
		sv, side_view_data_destructor);
}

static void
on_ssd_destroy(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_ssd *ev = event_data;

	labwc_view_clear_margin_override(ev->view);
	labwc_view_remove_data(ev->view, self_handle, SIDE_KEY);
}

static void
on_view_focus(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_view *ev = event_data;
	struct view *view = ev->view;

	struct side_view *sv = labwc_view_get_data(view,
		self_handle, SIDE_KEY);
	if (!sv) {
		return;
	}

	/* Determine if this view just gained focus */
	struct view *focused = labwc_get_focused_view();
	bool active = (focused == view);
	set_active_state(sv, active);
	update_button_toggles(sv);
	layout_side(sv);
}

static void
on_view_resized(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_view *ev = event_data;

	struct side_view *sv = labwc_view_get_data(ev->view,
		self_handle, SIDE_KEY);
	if (!sv) {
		return;
	}

	bool active = (labwc_get_focused_view() == sv->view);
	update_title(sv, active);
	update_button_toggles(sv);
	layout_side(sv);
}

static void
on_view_maximized(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_view *ev = event_data;

	struct side_view *sv = labwc_view_get_data(ev->view,
		self_handle, SIDE_KEY);
	if (!sv) {
		return;
	}

	update_button_toggles(sv);
	bool active = (labwc_get_focused_view() == sv->view);
	update_title(sv, active);
	layout_side(sv);
}

static void
on_workspace_changed(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_view *ev = event_data;

	struct side_view *sv = labwc_view_get_data(ev->view,
		self_handle, SIDE_KEY);
	if (!sv) {
		return;
	}

	update_button_toggles(sv);
}

static void
on_view_title_change(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_view *ev = event_data;

	struct side_view *sv = labwc_view_get_data(ev->view,
		self_handle, SIDE_KEY);
	if (!sv) {
		return;
	}

	bool active = (labwc_get_focused_view() == sv->view);
	update_title(sv, active);
	layout_side(sv);
}

static void
on_config_reload(void *event_data, void *user_data)
{
	(void)event_data;
	(void)user_data;

	/* Re-read theme data and button layout */
	labwc_theme_get_info(&theme);

	/* Re-read config */
	const char *side_str = labwc_plugin_config_str(
		self_handle, "side", "left");
	use_right_side = (side_str && !strcasecmp(side_str, "right"));
}

/* ---- Button event: client menu via icon/menu button ---- */

static void
on_button(void *event_data, void *user_data)
{
	(void)user_data;
	struct labwc_event_button *ev = event_data;

	/* Only intercept left-button events */
	if (ev->button != BTN_LEFT) {
		return;
	}

	struct side_view *sv;
	wl_list_for_each(sv, &all_side_views, link) {
		for (int i = 0; i < sv->nr_buttons; i++) {
			int type = sv->button_types[i];
			if (type != LAB_NODE_BUTTON_WINDOW_MENU
					&& type != LAB_NODE_BUTTON_WINDOW_ICON) {
				continue;
			}
			if (!sv->buttons[i]) {
				continue;
			}
			struct wlr_scene_node *node =
				labwc_button_get_node(sv->buttons[i]);
			if (!node) {
				continue;
			}
			int lx, ly;
			if (!wlr_scene_node_coords(node, &lx, &ly)) {
				continue;
			}
			int bw = theme.button_width;
			int bh = theme.button_height;
			if (ev->x >= lx && ev->x < lx + bw
					&& ev->y >= ly
					&& ev->y < ly + bh) {
				/*
				 * Consume both press and release so the
				 * normal release path does not immediately
				 * close the menu we just opened.
				 */
				if (ev->pressed) {
					struct menu *menu =
						labwc_menu_get_by_id(
							"client-menu");
					if (menu) {
						/*
						 * Place at the bottom corner
						 * of the window, overlapping
						 * the border by border_width.
						 * Clamp so the menu stays
						 * within the window area.
						 */
						struct wlr_box geo =
							labwc_view_get_geometry(
								sv->view);
						int bord = theme.border_width;
						int mw = menu->size.width;
						int mh = menu->size.height;
						int mx, my;

						if (use_right_side) {
							mx = geo.x + geo.width
								+ bord - mw;
						} else {
							mx = geo.x - bord;
						}

						/*
						 * When shaded the bar extends
						 * inward by border_width to
						 * absorb the collapsed SSD
						 * border.  Shift the menu away
						 * so it does not overlap the
						 * extended bar.
						 */
						if (labwc_view_is_shaded(
								sv->view)) {
							if (use_right_side) {
								mx -= bord;
							} else {
								mx += bord;
							}
						}

						my = geo.y + geo.height
							+ bord - mh;

						/* Keep within window */
						if (mx < geo.x - bord) {
							mx = geo.x - bord;
						}
						if (mx + mw > geo.x
								+ geo.width
								+ bord) {
							mx = geo.x + geo.width
								+ bord - mw;
						}
						if (my < geo.y - bord) {
							my = geo.y - bord;
						}

						menu->triggered_by_view =
							sv->view;
						labwc_menu_open(menu,
							mx, my);
					}
				}
				ev->base.consumed = true;
				return;
			}
		}
	}
}

/* ---- Plugin entry points ---- */

LABWC_PLUGIN_ABI_VERSION_FN

LABWC_EXPORT int
labwc_plugin_init(labwc_plugin_t plugin)
{
	self_handle = plugin;

	labwc_plugin_set_info(plugin, &(struct labwc_plugin_info){
		.name = "sidetitlebar",
		.description = "Side titlebar with themed buttons and rotated text",
		.author = "labwc",
		.version = "0.2",
	});

	/* Read config */
	const char *side_str = labwc_plugin_config_str(
		plugin, "side", "left");
	use_right_side = (side_str && !strcasecmp(side_str, "right"));

	/* Cache theme info (includes button layout) */
	labwc_theme_get_info(&theme);

	/* Init global side_view list */
	wl_list_init(&all_side_views);

	/* Subscribe to events */
	sub_ssd_create = labwc_event_subscribe(plugin,
		LABWC_EVENT_SSD_CREATE, on_ssd_create, NULL);
	sub_ssd_destroy = labwc_event_subscribe(plugin,
		LABWC_EVENT_SSD_DESTROY, on_ssd_destroy, NULL);
	sub_view_focus = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_FOCUS, on_view_focus, NULL);
	sub_view_resized = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_RESIZED, on_view_resized, NULL);
	sub_view_maximized = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_MAXIMIZED, on_view_maximized, NULL);
	sub_view_title = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_TITLE_CHANGE, on_view_title_change, NULL);
	sub_config_reload = labwc_event_subscribe(plugin,
		LABWC_EVENT_CONFIG_RELOAD, on_config_reload, NULL);
	sub_workspace_changed = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_WORKSPACE_CHANGED, on_workspace_changed, NULL);
	sub_button = labwc_event_subscribe(plugin,
		LABWC_EVENT_BUTTON, on_button, NULL);

	wlr_log(WLR_INFO, "sidetitlebar: loaded (side=%s, buttons=%d+%d)",
		use_right_side ? "right" : "left",
		theme.nr_title_buttons_left, theme.nr_title_buttons_right);
	return 0;
}

LABWC_EXPORT void
labwc_plugin_fini(labwc_plugin_t plugin)
{
	(void)plugin;
	wlr_log(WLR_INFO, "sidetitlebar: unloaded");
}
