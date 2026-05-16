/*
 * labwc example plugin: hello
 *
 * Logs view lifecycle events. Demonstrates the basic plugin API.
 *
 * Build:
 *   cc -shared -fPIC -DWLR_USE_UNSTABLE -o libhello.so hello.c \
 *     -Iinclude -Ibuild/include \
 *     $(pkg-config --cflags wlroots-0.20 wayland-server xkbcommon)
 *
 * Install:
 *   cp libhello.so ~/.local/share/labwc/plugins/
 *
 * Enable in rc.xml:
 *   <plugins>
 *     <plugin name="hello"/>
 *   </plugins>
 */

#include <wlr/util/log.h>
#include "plugin/plugin.h"
#include "plugin/events.h"
#include "view.h"

LABWC_PLUGIN_ABI_VERSION_FN

static labwc_plugin_t self;
static struct labwc_event_sub *on_map;
static struct labwc_event_sub *on_destroy;
static struct labwc_event_sub *on_focus;

static void
handle_view_map(void *data, void *user_data)
{
	struct labwc_event_view *ev = data;
	const char *title = ev->view->title ? ev->view->title : "(untitled)";
	const char *app_id = ev->view->app_id ? ev->view->app_id : "(unknown)";
	wlr_log(WLR_INFO, "[hello] view mapped: '%s' (%s)", title, app_id);
}

static void
handle_view_destroy(void *data, void *user_data)
{
	struct labwc_event_view *ev = data;
	wlr_log(WLR_INFO, "[hello] view destroyed: %p", (void *)ev->view);
}

static void
handle_view_focus(void *data, void *user_data)
{
	struct labwc_event_view *ev = data;
	const char *title = ev->view->title ? ev->view->title : "(untitled)";
	wlr_log(WLR_INFO, "[hello] view focused: '%s'", title);
}

LABWC_EXPORT int
labwc_plugin_init(labwc_plugin_t plugin)
{
	self = plugin;
	labwc_plugin_set_info(plugin, &(struct labwc_plugin_info){
		.name = "hello",
		.description = "Example plugin that logs view events",
		.author = "labwc",
		.version = "0.1.0",
		.allow_hot_unload = true,
	});

	on_map = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_MAP, handle_view_map, NULL);
	on_destroy = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_DESTROY, handle_view_destroy, NULL);
	on_focus = labwc_event_subscribe(plugin,
		LABWC_EVENT_VIEW_FOCUS, handle_view_focus, NULL);

	wlr_log(WLR_INFO, "[hello] plugin initialized");
	return 0;
}

LABWC_EXPORT void
labwc_plugin_fini(labwc_plugin_t plugin)
{
	labwc_event_unsubscribe(on_map);
	labwc_event_unsubscribe(on_destroy);
	labwc_event_unsubscribe(on_focus);
	wlr_log(WLR_INFO, "[hello] plugin finalized");
}
