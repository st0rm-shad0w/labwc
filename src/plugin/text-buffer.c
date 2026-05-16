// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#if HAVE_PLUGINS
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "buffer.h"
#include "common/font.h"
#include "common/graphic-helpers.h"
#include "common/mem.h"
#include "common/string-helpers.h"
#include "plugin/helpers.h"
#include "scaled-buffer/scaled-buffer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Plugin text buffer with arbitrary rotation support.
 * Built on the scaled_buffer framework for auto-scaling.
 */

struct labwc_text_buffer {
	struct scaled_buffer *scaled_buffer;
	struct wlr_scene_buffer *scene_buffer;

	/* Text parameters */
	char *text;
	int max_width;
	struct font font;
	float color[4];
	float bg_color[4];

	/* Rotation */
	double rotation_degrees;

	/* Computed logical size (post-rotation) */
	int width;
	int height;
	/* Pre-rotation text size */
	int text_width;
	int text_height;
};

static struct lab_data_buffer *
rotate_buffer(struct lab_data_buffer *src, double angle_rad,
	int dst_w, int dst_h)
{
	struct lab_data_buffer *dst = buffer_create_cairo(dst_w, dst_h, 1.0f);
	if (!dst) {
		return NULL;
	}

	cairo_t *cr = cairo_create(dst->surface);

	/* Translate to center, rotate, paint source centered */
	cairo_translate(cr, dst_w / 2.0, dst_h / 2.0);
	cairo_rotate(cr, angle_rad);
	cairo_translate(cr, -(double)src->logical_width / 2.0,
		-(double)src->logical_height / 2.0);

	cairo_set_source_surface(cr, src->surface, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);

	return dst;
}

static struct lab_data_buffer *
_create_buffer(struct scaled_buffer *scaled_buffer, double scale)
{
	struct labwc_text_buffer *self = scaled_buffer->data;

	if (string_null_or_empty(self->text)) {
		return NULL;
	}

	/* Render text at the requested scale */
	struct lab_data_buffer *text_buf = NULL;
	cairo_pattern_t *bg_pattern = color_to_pattern(self->bg_color);

	font_buffer_create(&text_buf, self->max_width, -1, self->text,
		&self->font, self->color, bg_pattern, scale, false);
	cairo_pattern_destroy(bg_pattern);

	if (!text_buf) {
		wlr_log(WLR_ERROR, "plugin text-buffer: font_buffer_create failed");
		return NULL;
	}

	/* If no rotation, return the text buffer directly */
	if (fabs(self->rotation_degrees) < 0.01) {
		return text_buf;
	}

	/* Compute rotated bounding box at this scale */
	double angle_rad = self->rotation_degrees * M_PI / 180.0;
	double cos_a = fabs(cos(angle_rad));
	double sin_a = fabs(sin(angle_rad));

	int scaled_tw = (int)(self->text_width * scale);
	int scaled_th = (int)(self->text_height * scale);
	int dst_w = (int)ceil(scaled_tw * cos_a + scaled_th * sin_a);
	int dst_h = (int)ceil(scaled_tw * sin_a + scaled_th * cos_a);

	if (dst_w <= 0 || dst_h <= 0) {
		wlr_buffer_drop(&text_buf->base);
		return NULL;
	}

	struct lab_data_buffer *rotated = rotate_buffer(
		text_buf, angle_rad, dst_w, dst_h);

	wlr_buffer_drop(&text_buf->base);

	if (!rotated) {
		wlr_log(WLR_ERROR, "plugin text-buffer: rotation failed");
		return NULL;
	}

	return rotated;
}

static void
_destroy(struct scaled_buffer *scaled_buffer)
{
	struct labwc_text_buffer *self = scaled_buffer->data;
	if (!self) {
		return;
	}
	scaled_buffer->data = NULL;
	zfree(self->text);
	zfree(self->font.name);
	free(self);
}

static bool
_equal(struct scaled_buffer *a, struct scaled_buffer *b)
{
	struct labwc_text_buffer *ta = a->data;
	struct labwc_text_buffer *tb = b->data;

	return str_equal(ta->text, tb->text)
		&& ta->max_width == tb->max_width
		&& str_equal(ta->font.name, tb->font.name)
		&& ta->font.size == tb->font.size
		&& ta->font.slant == tb->font.slant
		&& ta->font.weight == tb->font.weight
		&& !memcmp(ta->color, tb->color, sizeof(ta->color))
		&& !memcmp(ta->bg_color, tb->bg_color, sizeof(ta->bg_color))
		&& fabs(ta->rotation_degrees - tb->rotation_degrees) < 0.01;
}

static const struct scaled_buffer_impl plugin_text_impl = {
	.create_buffer = _create_buffer,
	.destroy = _destroy,
	.equal = _equal,
};

static void
compute_dimensions(struct labwc_text_buffer *self)
{
	font_get_buffer_size(self->max_width, self->text, &self->font,
		&self->text_width, &self->text_height);

	if (fabs(self->rotation_degrees) < 0.01) {
		self->width = self->text_width;
		self->height = self->text_height;
	} else {
		double angle_rad = self->rotation_degrees * M_PI / 180.0;
		double cos_a = fabs(cos(angle_rad));
		double sin_a = fabs(sin(angle_rad));
		self->width = (int)ceil(
			self->text_width * cos_a + self->text_height * sin_a);
		self->height = (int)ceil(
			self->text_width * sin_a + self->text_height * cos_a);
	}
}

/* ---- Public API ---- */

struct labwc_text_buffer *
labwc_text_buffer_create(struct wlr_scene_tree *parent)
{
	if (!parent) {
		return NULL;
	}

	struct labwc_text_buffer *self = znew(*self);
	struct scaled_buffer *sb = scaled_buffer_create(
		parent, &plugin_text_impl, /* drop_buffer */ true);
	if (!sb) {
		free(self);
		return NULL;
	}

	sb->data = self;
	self->scaled_buffer = sb;
	self->scene_buffer = sb->scene_buffer;
	return self;
}

void
labwc_text_buffer_update(struct labwc_text_buffer *buf,
	const char *text, int max_width,
	const char *font_name, int font_size,
	int font_weight, int font_slant,
	const float color[static 4], const float bg_color[static 4])
{
	if (!buf || !text) {
		return;
	}

	/* Update stored parameters */
	zfree(buf->text);
	zfree(buf->font.name);

	buf->text = xstrdup(text);
	buf->max_width = max_width;
	if (font_name) {
		buf->font.name = xstrdup(font_name);
	}
	buf->font.size = font_size;
	buf->font.weight = (PangoWeight)font_weight;
	buf->font.slant = (PangoStyle)font_slant;
	memcpy(buf->color, color, sizeof(buf->color));
	memcpy(buf->bg_color, bg_color, sizeof(buf->bg_color));

	compute_dimensions(buf);
	scaled_buffer_request_update(buf->scaled_buffer,
		buf->width, buf->height);
}

void
labwc_text_buffer_set_rotation(struct labwc_text_buffer *buf,
	double angle_degrees)
{
	if (!buf) {
		return;
	}
	if (fabs(buf->rotation_degrees - angle_degrees) < 0.01) {
		return; /* No change */
	}

	buf->rotation_degrees = angle_degrees;

	/* Recompute dimensions and re-render if we have text */
	if (!string_null_or_empty(buf->text)) {
		compute_dimensions(buf);
		scaled_buffer_request_update(buf->scaled_buffer,
			buf->width, buf->height);
	}
}

int
labwc_text_buffer_get_width(struct labwc_text_buffer *buf)
{
	return buf ? buf->width : 0;
}

int
labwc_text_buffer_get_height(struct labwc_text_buffer *buf)
{
	return buf ? buf->height : 0;
}

struct wlr_scene_buffer *
labwc_text_buffer_get_scene_buffer(struct labwc_text_buffer *buf)
{
	return buf ? buf->scene_buffer : NULL;
}

void
labwc_text_buffer_destroy(struct labwc_text_buffer *buf)
{
	if (!buf) {
		return;
	}
	/*
	 * Destroying the scene_buffer triggers the scaled_buffer destroy
	 * listener, which calls our _destroy() callback to free the
	 * labwc_text_buffer.
	 */
	wlr_scene_node_destroy(&buf->scene_buffer->node);
}
#endif /* HAVE_PLUGINS */
