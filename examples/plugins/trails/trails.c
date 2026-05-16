/*
 * labwc trails plugin
 *
 * Draws a fading, tapering colored trail behind moving windows,
 * inspired by Hyprland's hyprtrails.
 *
 * Uses wlr_scene_rect nodes placed below each view in the scene
 * graph for correct z-ordering (trail behind the window), automatic
 * damage tracking, and smooth fade-out.
 *
 * A periodic timer (~60 Hz) drives position sampling and fade-out;
 * the scene graph takes care of scheduling repaints when rect
 * properties change.
 *
 * Config (rc.xml):
 *   <plugins>
 *     <plugin name="trails">
 *       <color>#ffaa0080</color>      <!-- #RRGGBB or #RRGGBBAA -->
 *       <maxPoints>20</maxPoints>      <!-- ring buffer size     -->
 *       <sampleRate>2</sampleRate>     <!-- ticks between samples while moving -->
 *       <fadeRate>3</fadeRate>          <!-- ticks between drain  while stopped -->
 *     </plugin>
 *   </plugins>
 *
 * Build:
 *   cc -shared -fPIC -DWLR_USE_UNSTABLE -o libtrails.so trails.c \
 *     -Iinclude -Ibuild/include \
 *     $(pkg-config --cflags wlroots-0.20 wayland-server xkbcommon \
 *       libxml-2.0 pixman-1 libdrm glib-2.0 cairo pangocairo) -lm
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "plugin/plugin.h"
#include "plugin/events.h"
#include "plugin/helpers.h"
#include "labwc.h"
#include "view.h"

/* ---- Constants ---- */

#define TRAIL_KEY          "trails"
#define TRAIL_TICK_MS      16   /* ~60 Hz */
#define TRAIL_MAX_POINTS   40
#define TRAIL_DEFAULT_PTS  20

/* ---- Per-view trail data ---- */

struct trail_point {
	struct wlr_box geo;   /* layout-coords snapshot */
	bool valid;
};

struct view_trail {
	struct view *view;
	struct wlr_scene_tree *tree;        /* parent of all rects */
	struct wlr_scene_rect **rects;      /* [max_points] scene rects */
	struct trail_point *points;         /* [max_points] ring buffer */
	int max_points;
	int num_points;                     /* valid points stored */
	int head;                           /* ring write index */
	int tick_counter;
	struct wlr_box last_geo;
	bool moving;
};

/* ---- Plugin state ---- */

static labwc_plugin_t self_handle;
static struct labwc_event_sub *sub_map;
static struct labwc_event_sub *sub_destroy;
static struct wl_event_source *trail_timer;

/* Config */
static float cfg_color[4] = { 0.5f, 0.333f, 0.0f, 0.5f }; /* pre-mul orange @50% */
static int cfg_max_points = TRAIL_DEFAULT_PTS;
static int cfg_sample_rate = 2;   /* sample every N ticks while moving */
static int cfg_fade_rate = 3;     /* drain one point every N ticks when stopped */

/* ---- Helpers ---- */

static void
parse_hex_color(const char *hex)
{
	if (!hex) {
		return;
	}
	/* skip leading '#' */
	if (hex[0] == '#') {
		hex++;
	}
	size_t len = strlen(hex);
	if (len < 6) {
		return;
	}
	unsigned int r, g, b, a = 255;
	if (len >= 8) {
		sscanf(hex, "%2x%2x%2x%2x", &r, &g, &b, &a);
	} else {
		sscanf(hex, "%2x%2x%2x", &r, &g, &b);
	}
	/* Store as pre-multiplied alpha */
	float af = a / 255.0f;
	cfg_color[0] = (r / 255.0f) * af;
	cfg_color[1] = (g / 255.0f) * af;
	cfg_color[2] = (b / 255.0f) * af;
	cfg_color[3] = af;
}

/* ---- Ring buffer ---- */

static void
trail_push(struct view_trail *t, const struct wlr_box *geo)
{
	t->head = (t->head + 1) % t->max_points;
	t->points[t->head].geo = *geo;
	t->points[t->head].valid = true;
	if (t->num_points < t->max_points) {
		t->num_points++;
	}
}

/*
 * 0 = most recent, num_points-1 = oldest.
 * Returns NULL if i >= num_points.
 */
static struct trail_point *
trail_get(struct view_trail *t, int i)
{
	if (i >= t->num_points) {
		return NULL;
	}
	int idx = (t->head - i + t->max_points) % t->max_points;
	return &t->points[idx];
}

/* ---- Update a single view's scene rects ---- */

static void
trail_update_rects(struct view_trail *t)
{
	for (int i = 0; i < t->max_points; i++) {
		struct wlr_scene_rect *rect = t->rects[i];
		struct trail_point *pt = trail_get(t, i);

		if (!pt || !pt->valid) {
			labwc_scene_node_set_enabled(&rect->node, false);
			continue;
		}

		/* Index 0 overlaps the actual window -- hide it */
		if (i == 0) {
			labwc_scene_node_set_enabled(&rect->node, false);
			continue;
		}

		/*
		 * Alpha fades linearly: newest (i near 0) is brightest,
		 * oldest (i near max_points) is most transparent.
		 */
		float age_frac = (float)i / (float)t->max_points;
		float alpha = cfg_color[3] * (1.0f - age_frac);
		if (alpha < 0.005f) {
			labwc_scene_node_set_enabled(&rect->node, false);
			continue;
		}

		float color[4] = {
			cfg_color[0] * (alpha / cfg_color[3]),
			cfg_color[1] * (alpha / cfg_color[3]),
			cfg_color[2] * (alpha / cfg_color[3]),
			alpha,
		};

		/*
		 * Taper: shrink older rects toward center.
		 * Scale goes from 1.0 (newest) to 0.3 (oldest).
		 */
		float scale = 1.0f - 0.7f * age_frac;
		int w = (int)(pt->geo.width * scale);
		int h = (int)(pt->geo.height * scale);
		int x = pt->geo.x + (pt->geo.width - w) / 2;
		int y = pt->geo.y + (pt->geo.height - h) / 2;

		labwc_scene_rect_set_size(rect, w, h);
		labwc_scene_rect_set_color(rect, color);
		labwc_scene_node_set_position(&rect->node, x, y);
		labwc_scene_node_set_enabled(&rect->node, true);
	}
}

/* ---- Per-view tick update ---- */

static void
trail_update(struct view *v, struct view_trail *t)
{
	bool moved = (v->current.x != t->last_geo.x
		|| v->current.y != t->last_geo.y
		|| v->current.width != t->last_geo.width
		|| v->current.height != t->last_geo.height);

	if (moved) {
		t->moving = true;
		t->tick_counter++;
		if (t->tick_counter >= cfg_sample_rate) {
			trail_push(t, &t->last_geo);
			t->tick_counter = 0;
		}
		t->last_geo = v->current;
		trail_update_rects(t);
	} else if (t->moving) {
		/* Window just stopped -- push final position */
		trail_push(t, &v->current);
		t->moving = false;
		t->tick_counter = 0;
		trail_update_rects(t);
	} else if (t->num_points > 0) {
		/* Stationary -- gradually drain the trail */
		t->tick_counter++;
		if (t->tick_counter >= cfg_fade_rate) {
			int oldest = (t->head - t->num_points + 1
				+ t->max_points) % t->max_points;
			t->points[oldest].valid = false;
			t->num_points--;
			t->tick_counter = 0;
			trail_update_rects(t);
		}
	}
}

/* ---- Lifecycle ---- */

static void
trail_free(void *data)
{
	struct view_trail *t = data;
	if (!t) {
		return;
	}
	if (t->tree) {
		/* Destroying the tree cascades to all child rects */
		labwc_scene_node_destroy(&t->tree->node);
	}
	free(t->rects);
	free(t->points);
	free(t);
}

static void
trail_create(struct view *v)
{
	struct wlr_scene_tree *view_tree = labwc_view_get_scene_tree(v);
	if (!view_tree) {
		return;
	}

	int max_pts = cfg_max_points;
	if (max_pts < 2) {
		max_pts = 2;
	}
	if (max_pts > TRAIL_MAX_POINTS) {
		max_pts = TRAIL_MAX_POINTS;
	}

	struct view_trail *t = calloc(1, sizeof(*t));
	if (!t) {
		return;
	}
	t->view = v;
	t->max_points = max_pts;
	t->points = calloc(max_pts, sizeof(*t->points));
	t->rects = calloc(max_pts, sizeof(*t->rects));
	if (!t->points || !t->rects) {
		free(t->points);
		free(t->rects);
		free(t);
		return;
	}
	t->last_geo = v->current;

	/*
	 * Create the trail tree as a sibling of the view's scene tree,
	 * placed immediately below it in z-order.
	 */
	struct wlr_scene_tree *parent = view_tree->node.parent;
	t->tree = labwc_scene_create_tree(self_handle, parent);
	if (!t->tree) {
		free(t->points);
		free(t->rects);
		free(t);
		return;
	}
	labwc_scene_node_place_below(&t->tree->node, &view_tree->node);

	/* Pre-allocate all rect nodes (initially hidden) */
	float transparent[4] = { 0, 0, 0, 0 };
	for (int i = 0; i < max_pts; i++) {
		t->rects[i] = labwc_scene_rect_create(
			t->tree, 1, 1, transparent);
		if (t->rects[i]) {
			labwc_scene_node_set_enabled(
				&t->rects[i]->node, false);
		}
	}

	labwc_view_set_data(v, self_handle, TRAIL_KEY, t, trail_free);
}

static struct view_trail *
get_trail(struct view *v)
{
	return labwc_view_get_data(v, self_handle, TRAIL_KEY);
}

/* ---- Timer callback ---- */

static int
on_trail_tick(void *data)
{
	struct view *v;
	wl_list_for_each(v, &server.views, link) {
		if (!v->mapped) {
			continue;
		}
		struct view_trail *t = get_trail(v);
		if (t) {
			trail_update(v, t);
		}
	}

	/* Reschedule */
	wl_event_source_timer_update(trail_timer, TRAIL_TICK_MS);
	return 0;
}

/* ---- Event handlers ---- */

static void
on_view_map(void *data, void *user_data)
{
	struct labwc_event_view *ev = data;
	if (!get_trail(ev->view)) {
		trail_create(ev->view);
	}
}

static void
on_view_destroy(void *data, void *user_data)
{
	/* Data store auto-cleans via trail_free */
}

/* ---- Plugin entry points ---- */

LABWC_PLUGIN_ABI_VERSION_FN

LABWC_EXPORT int
labwc_plugin_init(labwc_plugin_t plugin)
{
	self_handle = plugin;
	labwc_plugin_set_info(plugin, &(struct labwc_plugin_info){
		.name = "trails",
		.description = "Window trail effect using scene-graph rects",
		.author = "labwc",
		.version = "0.1.0",
		.allow_hot_unload = true,
	});

	/* Parse config */
	const char *color_str = labwc_plugin_config_str(plugin,
		"color", NULL);
	if (color_str) {
		parse_hex_color(color_str);
	}
	cfg_max_points = labwc_plugin_config_int(plugin,
		"maxPoints", TRAIL_DEFAULT_PTS);
	if (cfg_max_points > TRAIL_MAX_POINTS) {
		cfg_max_points = TRAIL_MAX_POINTS;
	}
	if (cfg_max_points < 2) {
		cfg_max_points = 2;
	}
	cfg_sample_rate = labwc_plugin_config_int(plugin,
		"sampleRate", 2);
	if (cfg_sample_rate < 1) {
		cfg_sample_rate = 1;
	}
	cfg_fade_rate = labwc_plugin_config_int(plugin,
		"fadeRate", 3);
	if (cfg_fade_rate < 1) {
		cfg_fade_rate = 1;
	}

	wlr_log(WLR_INFO,
		"[trails] color=%.2f,%.2f,%.2f,%.2f points=%d "
		"sample_rate=%d fade_rate=%d",
		cfg_color[0], cfg_color[1], cfg_color[2], cfg_color[3],
		cfg_max_points, cfg_sample_rate, cfg_fade_rate);

	/* Subscribe to view lifecycle events */
	sub_map = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_MAP, on_view_map, NULL);
	sub_destroy = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_DESTROY, on_view_destroy, NULL);

	/* Attach trails to any already-open views */
	struct view *v;
	wl_list_for_each(v, &server.views, link) {
		if (v->mapped) {
			trail_create(v);
		}
	}

	/* Start the tick timer */
	trail_timer = labwc_timer_add(TRAIL_TICK_MS, on_trail_tick, NULL);

	wlr_log(WLR_INFO, "[trails] plugin initialized");
	return 0;
}

LABWC_EXPORT void
labwc_plugin_fini(labwc_plugin_t plugin)
{
	/* Stop the timer */
	if (trail_timer) {
		wl_event_source_remove(trail_timer);
		trail_timer = NULL;
	}

	labwc_event_unsubscribe(sub_map);
	labwc_event_unsubscribe(sub_destroy);
	sub_map = NULL;
	sub_destroy = NULL;

	/*
	 * Trail data (including scene nodes) is cleaned up automatically
	 * by the plugin data store when the plugin is unloaded.
	 */
	wlr_log(WLR_INFO, "[trails] plugin finalized");
}
