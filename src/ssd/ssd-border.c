// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "buffer.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "config/rcxml.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

#define TITLEBAR_DROPSHADOW_HEIGHT 5

static bool
dropshadow_never_accepts_input(struct wlr_scene_buffer *buffer, double *sx,
	double *sy)
{
	return false;
}

static struct lab_data_buffer *
get_dropshadow_buffer(void)
{
	static struct lab_data_buffer *buf;
	if (buf) {
		return buf;
	}
	/* 1px wide vertical gradient: black with decreasing alpha */
	uint32_t *pixels =
		xmalloc(TITLEBAR_DROPSHADOW_HEIGHT * sizeof(*pixels));
	pixels[0] = 0x33000000; /* ~20% */
	pixels[1] = 0x21000000; /* ~13% */
	pixels[2] = 0x14000000; /*  ~8% */
	pixels[3] = 0x0A000000; /*  ~4% */
	pixels[4] = 0x05000000; /*  ~2% */
	buf = buffer_create_from_data(pixels, 1, TITLEBAR_DROPSHADOW_HEIGHT,
		sizeof(*pixels));
	return buf;
}

void
ssd_border_create(struct ssd *ssd)
{
	assert(ssd);
	assert(!ssd->border.tree);

	struct view *view = ssd->view;
	struct theme *theme = rc.theme;
	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int corner_width = ssd_get_corner_width();

	ssd->border.tree = lab_wlr_scene_tree_create(ssd->tree);
	wlr_scene_node_set_position(&ssd->border.tree->node, -theme->border_width, 0);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];
		subtree->tree = lab_wlr_scene_tree_create(ssd->border.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		float *color = theme->window[active].border_color;

		subtree->left = lab_wlr_scene_rect_create(parent,
			theme->border_width, height, color);
		wlr_scene_node_set_position(&subtree->left->node, 0, 0);

		subtree->right = lab_wlr_scene_rect_create(parent,
			theme->border_width, height, color);
		wlr_scene_node_set_position(&subtree->right->node,
			theme->border_width + width, 0);

		subtree->bottom = lab_wlr_scene_rect_create(parent,
			full_width, theme->border_width, color);
		wlr_scene_node_set_position(&subtree->bottom->node,
			0, height);

		subtree->top = lab_wlr_scene_rect_create(parent,
			MAX(width - 2 * corner_width, 0), theme->border_width, color);
		wlr_scene_node_set_position(&subtree->top->node,
			theme->border_width + corner_width,
			-(ssd->titlebar.height + theme->border_width));

		/* Separator between titlebar and content */
		subtree->separator = lab_wlr_scene_rect_create(parent,
			full_width, theme->border_width, color);
		wlr_scene_node_set_position(&subtree->separator->node,
			0, -theme->border_width);
		wlr_scene_node_set_enabled(&subtree->separator->node,
			ssd->titlebar.height > 0 && !view->shaded);
	}

	/* Titlebar drop shadow - overlay above content for visibility */
	struct lab_data_buffer *shadow_buf = get_dropshadow_buffer();
	if (shadow_buf) {
		ssd->titlebar_dropshadow.tree =
			lab_wlr_scene_tree_create(view->scene_tree);
		wlr_scene_node_set_position(
			&ssd->titlebar_dropshadow.tree->node,
			-theme->border_width, 0);
		ssd->titlebar_dropshadow.buffer = lab_wlr_scene_buffer_create(
			ssd->titlebar_dropshadow.tree, &shadow_buf->base);
		ssd->titlebar_dropshadow.buffer->point_accepts_input =
			dropshadow_never_accepts_input;
		ssd->titlebar_dropshadow.buffer->filter_mode =
			WLR_SCALE_FILTER_NEAREST;
		wlr_scene_buffer_set_dest_size(ssd->titlebar_dropshadow.buffer,
			full_width, TITLEBAR_DROPSHADOW_HEIGHT);
		wlr_scene_node_set_enabled(&ssd->titlebar_dropshadow.tree->node,
			ssd->titlebar.height > 0 && !view->shaded);
	}

	if (view->current.width > 0 && view->current.height > 0) {
		/*
		 * The SSD is recreated by a Reconfigure request
		 * thus we may need to handle squared corners.
		 */
		ssd_border_update(ssd);
	}
}

void
ssd_border_update(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	struct view *view = ssd->view;
	if (!ssd->border.tree->node.enabled) {
		wlr_scene_node_set_enabled(&ssd->border.tree->node, true);
	}
	ssd->margin = ssd_thickness(ssd->view);

	struct theme *theme = rc.theme;

	int width = view->current.width;
	int height = view_effective_height(view, /* use_pending */ false);
	int full_width = width + 2 * theme->border_width;
	int corner_width = ssd_get_corner_width();

	/*
	 * From here on we have to cover the following border scenarios:
	 * Non-tiled (partial border, rounded corners):
	 *    _____________
	 *   o           oox
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled (full border, squared corners):
	 *   _______________
	 *  |o           oox|
	 *  |---------------|
	 *  |_______________|
	 *
	 * Tiled or non-tiled with zero title height (full boarder, no title):
	 *   _______________
	 *  |_______________|
	 */

	int side_height = ssd->state.was_squared
		? height + ssd->titlebar.height
		: height;
	int side_y = ssd->state.was_squared
		? -ssd->titlebar.height
		: 0;
	int top_width = ssd->titlebar.height <= 0 || ssd->state.was_squared
		? full_width
		: MAX(width - 2 * corner_width, 0);
	int top_x = ssd->titlebar.height <= 0 || ssd->state.was_squared
		? 0
		: theme->border_width + corner_width;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_border_subtree *subtree = &ssd->border.subtrees[active];

		wlr_scene_rect_set_size(subtree->left,
			theme->border_width, side_height);
		wlr_scene_node_set_position(&subtree->left->node,
			0, side_y);

		wlr_scene_rect_set_size(subtree->right,
			theme->border_width, side_height);
		wlr_scene_node_set_position(&subtree->right->node,
			theme->border_width + width, side_y);

		wlr_scene_rect_set_size(subtree->bottom,
			full_width, theme->border_width);
		wlr_scene_node_set_position(&subtree->bottom->node,
			0, height);

		wlr_scene_rect_set_size(subtree->top,
			top_width, theme->border_width);
		wlr_scene_node_set_position(&subtree->top->node,
			top_x, -(ssd->titlebar.height + theme->border_width));

		wlr_scene_rect_set_size(subtree->separator,
			full_width, theme->border_width);
		wlr_scene_node_set_enabled(&subtree->separator->node,
			ssd->titlebar.height > 0 && !view->shaded);
	}

	/* Update titlebar drop shadow overlay */
	if (ssd->titlebar_dropshadow.tree) {
		wlr_scene_node_set_position(
			&ssd->titlebar_dropshadow.tree->node,
			-theme->border_width, 0);
		wlr_scene_buffer_set_dest_size(ssd->titlebar_dropshadow.buffer,
			full_width, TITLEBAR_DROPSHADOW_HEIGHT);
		wlr_scene_node_set_enabled(&ssd->titlebar_dropshadow.tree->node,
			ssd->titlebar.height > 0 && !view->shaded);
	}
}

void
ssd_border_destroy(struct ssd *ssd)
{
	assert(ssd);
	assert(ssd->border.tree);

	if (ssd->titlebar_dropshadow.tree) {
		wlr_scene_node_destroy(&ssd->titlebar_dropshadow.tree->node);
		ssd->titlebar_dropshadow.tree = NULL;
		ssd->titlebar_dropshadow.buffer = NULL;
	}
	wlr_scene_node_destroy(&ssd->border.tree->node);
	ssd->border = (struct ssd_border_scene){0};
}
