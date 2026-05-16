// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#if HAVE_PLUGINS
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "common/mem.h"
#include "common/list.h"
#include "plugin/events.h"
#include "plugin/manager.h"
#include "labwc.h"

/**
 * The event manager holds a list of subscriptions per event type,
 * sorted by priority (lower first).
 */
static struct {
	struct wl_list subs[LABWC_EVENT_COUNT]; /* labwc_event_sub.manager_link */
	bool initialized;
} event_mgr;

void
plugin_events_init(void)
{
	for (int i = 0; i < LABWC_EVENT_COUNT; i++) {
		wl_list_init(&event_mgr.subs[i]);
	}
	event_mgr.initialized = true;
}

void
plugin_events_finish(void)
{
	/* All subscriptions should have been cleaned up by plugin unload.
	 * Just mark as uninitialized. */
	event_mgr.initialized = false;
}

bool
plugin_events_emit(enum labwc_event_type type, void *data)
{
	if (!event_mgr.initialized) {
		return false;
	}
	if (type < 0 || type >= LABWC_EVENT_COUNT) {
		return false;
	}

	struct labwc_event_base *base = data;
	base->consumed = false;

	struct labwc_event_sub *sub;
	wl_list_for_each(sub, &event_mgr.subs[type], manager_link) {
		sub->callback(data, sub->user_data);
		if (base->consumed) {
			return true;
		}
	}
	return false;
}

static struct labwc_event_sub *
subscribe_internal(labwc_plugin_t plugin, enum labwc_event_type type,
	int priority, labwc_event_fn callback, void *user_data)
{
	if (!event_mgr.initialized || type < 0 || type >= LABWC_EVENT_COUNT) {
		wlr_log(WLR_ERROR,
			"plugin_events: invalid subscribe (type=%d)", type);
		return NULL;
	}

	struct labwc_event_sub *sub = znew(*sub);
	sub->type = type;
	sub->priority = priority;
	sub->callback = callback;
	sub->user_data = user_data;
	sub->plugin = plugin;

	/* Insert sorted by priority (lower first) */
	struct labwc_event_sub *iter;
	struct wl_list *insert_after = &event_mgr.subs[type];
	wl_list_for_each(iter, &event_mgr.subs[type], manager_link) {
		if (iter->priority > priority) {
			insert_after = iter->manager_link.prev;
			break;
		}
		insert_after = &iter->manager_link;
	}
	wl_list_insert(insert_after, &sub->manager_link);

	/* Also track on the plugin for cleanup */
	if (plugin) {
		wl_list_append(&plugin->event_subs, &sub->link);
	} else {
		wl_list_init(&sub->link);
	}

	return sub;
}

struct labwc_event_sub *
labwc_event_subscribe(labwc_plugin_t plugin, enum labwc_event_type type,
	labwc_event_fn callback, void *user_data)
{
	return subscribe_internal(plugin, type, 0, callback, user_data);
}

struct labwc_event_sub *
labwc_event_subscribe_prio(labwc_plugin_t plugin, enum labwc_event_type type,
	int priority, labwc_event_fn callback, void *user_data)
{
	return subscribe_internal(plugin, type, priority, callback, user_data);
}

void
labwc_event_unsubscribe(struct labwc_event_sub *sub)
{
	if (!sub) {
		return;
	}
	wl_list_remove(&sub->manager_link);
	wl_list_remove(&sub->link);
	free(sub);
}
#endif /* HAVE_PLUGINS */
