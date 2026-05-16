/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_PLUGIN_MANAGER_H
#define LABWC_PLUGIN_MANAGER_H

#include <stdbool.h>
#include <wayland-util.h>
#include "plugin/plugin.h"

struct loaded_plugin {
	struct wl_list link;           /* server.plugins */
	char *path;                    /* full .so path */
	char *name;                    /* plugin name (from config) */
	void *dl_handle;               /* dlopen handle */
	struct labwc_plugin_info info; /* metadata from plugin */

	/* Entry points resolved from dlsym */
	int (*init_fn)(labwc_plugin_t);
	void (*fini_fn)(labwc_plugin_t);

	/* Tracked resources for cleanup on unload */
	struct wl_list event_subs;     /* struct labwc_event_sub.link */
	struct wl_list actions;        /* struct plugin_action.link */
	struct wl_list data_attachments; /* struct plugin_data_entry.link */

	/* Config XML node (for plugin_config_* access) */
	char *config_xml;              /* raw XML string of <plugin> children */
};

/**
 * Initialize the plugin system. Call after server_init() completes.
 */
void plugin_manager_init(void);

/**
 * Shut down the plugin system. Call during server_finish().
 */
void plugin_manager_finish(void);

/**
 * Reload plugins on Reconfigure (SIGHUP).
 * Detects added/removed plugins vs. the running set.
 */
void plugin_manager_reload(void);

#endif /* LABWC_PLUGIN_MANAGER_H */
