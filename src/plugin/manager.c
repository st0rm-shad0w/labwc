// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#if HAVE_PLUGINS
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include "common/mem.h"
#include "common/list.h"
#include "plugin/manager.h"
#include "plugin/events.h"
#include "plugin/rules.h"
#include "labwc.h"
#include "config/rcxml.h"

/*
 * Resolve a plugin .so path from a name.
 * Search order:
 *   1. $LABWC_PLUGIN_PATH (colon-separated dirs)
 *   2. $XDG_DATA_HOME/labwc/plugins/
 *   3. Compiled-in LABWC_PLUGIN_DIR
 * Returns heap-allocated path or NULL.
 */
static char *
resolve_plugin_path(const char *name)
{
	char buf[4096];
	const char *search_dirs[] = { NULL, NULL, NULL, NULL };
	int n = 0;

	/* $LABWC_PLUGIN_PATH */
	const char *env_path = getenv("LABWC_PLUGIN_PATH");
	if (env_path) {
		/* Handle colon-separated paths */
		static char pathbuf[4096];
		snprintf(pathbuf, sizeof(pathbuf), "%s", env_path);
		char *saveptr = NULL;
		char *tok = strtok_r(pathbuf, ":", &saveptr);
		while (tok && n < 3) {
			search_dirs[n++] = tok;
			tok = strtok_r(NULL, ":", &saveptr);
		}
	}

	/* $XDG_DATA_HOME/labwc/plugins */
	const char *xdg_data = getenv("XDG_DATA_HOME");
	static char xdg_buf[4096];
	if (xdg_data) {
		snprintf(xdg_buf, sizeof(xdg_buf),
			"%s/labwc/plugins", xdg_data);
		search_dirs[n++] = xdg_buf;
	} else {
		const char *home = getenv("HOME");
		if (home) {
			snprintf(xdg_buf, sizeof(xdg_buf),
				"%s/.local/share/labwc/plugins", home);
			search_dirs[n++] = xdg_buf;
		}
	}

	for (int i = 0; i < n; i++) {
		snprintf(buf, sizeof(buf), "%s/lib%s.so", search_dirs[i], name);
		if (access(buf, F_OK) == 0) {
			return strdup(buf);
		}
	}

	return NULL;
}

static struct loaded_plugin *
load_single_plugin(const char *name, const char *explicit_path,
	const char *config_xml)
{
	char *path = NULL;

	if (explicit_path) {
		path = strdup(explicit_path);
	} else if (name) {
		path = resolve_plugin_path(name);
	}

	if (!path) {
		wlr_log(WLR_ERROR, "plugin: cannot find plugin '%s'",
			name ? name : explicit_path);
		return NULL;
	}

	/* Check for duplicate */
	struct loaded_plugin *existing;
	wl_list_for_each(existing, &server.plugins, link) {
		if (!strcmp(existing->path, path)) {
			wlr_log(WLR_INFO,
				"plugin: '%s' already loaded, skipping", path);
			free(path);
			return NULL;
		}
	}

	void *dl = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	if (!dl) {
		wlr_log(WLR_ERROR, "plugin: dlopen(%s) failed: %s",
			path, dlerror());
		free(path);
		return NULL;
	}

	/* Resolve ABI version */
	uint32_t (*abi_fn)(void) = dlsym(dl, "labwc_plugin_abi_version");
	if (!abi_fn) {
		wlr_log(WLR_ERROR,
			"plugin: %s: missing labwc_plugin_abi_version symbol",
			path);
		dlclose(dl);
		free(path);
		return NULL;
	}

	uint32_t plugin_abi = abi_fn();
	if (plugin_abi != LABWC_PLUGIN_ABI_VERSION) {
		wlr_log(WLR_ERROR,
			"plugin: %s: ABI mismatch (plugin=%u, labwc=%u)",
			path, plugin_abi, LABWC_PLUGIN_ABI_VERSION);
		dlclose(dl);
		free(path);
		return NULL;
	}

	/* Resolve init */
	int (*init_fn)(labwc_plugin_t) = dlsym(dl, "labwc_plugin_init");
	if (!init_fn) {
		wlr_log(WLR_ERROR,
			"plugin: %s: missing labwc_plugin_init symbol", path);
		dlclose(dl);
		free(path);
		return NULL;
	}

	/* Resolve optional fini */
	void (*fini_fn)(labwc_plugin_t) = dlsym(dl, "labwc_plugin_fini");

	/* Create plugin entry */
	struct loaded_plugin *plugin = znew(*plugin);
	plugin->path = path;
	plugin->name = name ? strdup(name) : strdup("unknown");
	plugin->dl_handle = dl;
	plugin->init_fn = init_fn;
	plugin->fini_fn = fini_fn;
	plugin->config_xml = config_xml ? strdup(config_xml) : NULL;
	wl_list_init(&plugin->event_subs);
	wl_list_init(&plugin->actions);
	wl_list_init(&plugin->data_attachments);

	/* Set default info */
	plugin->info.name = plugin->name;
	plugin->info.description = "";
	plugin->info.author = "";
	plugin->info.version = "";
	plugin->info.order_hint = 0;
	plugin->info.allow_hot_unload = true;

	/* Call init */
	int ret = init_fn(plugin);
	if (ret != 0) {
		wlr_log(WLR_ERROR,
			"plugin: %s: labwc_plugin_init() returned %d", path, ret);
		free(plugin->name);
		free(plugin->config_xml);
		free(plugin->path);
		dlclose(dl);
		free(plugin);
		return NULL;
	}

	wl_list_append(&server.plugins, &plugin->link);

	wlr_log(WLR_INFO, "plugin: loaded '%s' (%s) by %s, version %s",
		plugin->info.name,
		plugin->info.description,
		plugin->info.author,
		plugin->info.version);

	return plugin;
}

static void
unload_single_plugin(struct loaded_plugin *plugin)
{
	wlr_log(WLR_INFO, "plugin: unloading '%s'", plugin->info.name);

	/* Call fini if present */
	if (plugin->fini_fn) {
		plugin->fini_fn(plugin);
	}

	/* Clean up event subscriptions */
	struct labwc_event_sub *sub, *sub_tmp;
	wl_list_for_each_safe(sub, sub_tmp, &plugin->event_subs, link) {
		labwc_event_unsubscribe(sub);
	}

	/* Clean up custom data attachments */
	struct plugin_data_entry {
		struct wl_list link;
		struct wl_list owner_link;
		labwc_plugin_t plugin;
		char *key;
		void *data;
		void (*destructor)(void *data);
	} *entry, *entry_tmp;
	wl_list_for_each_safe(entry, entry_tmp,
		&plugin->data_attachments, owner_link) {
		if (entry->destructor) {
			entry->destructor(entry->data);
		}
		wl_list_remove(&entry->owner_link);
		wl_list_remove(&entry->link);
		free(entry->key);
		free(entry);
	}

	/* Clean up registered rules */
	labwc_rules_unregister_all(plugin);

	/* Remove from list */
	wl_list_remove(&plugin->link);

	/* dlclose */
	dlclose(plugin->dl_handle);

	free(plugin->name);
	free(plugin->path);
	free(plugin->config_xml);
	free(plugin);
}

void
plugin_manager_init(void)
{
	wl_list_init(&server.plugins);
	plugin_events_init();
	plugin_rules_init();

	/* Load plugins from config */
	struct plugin_config_entry *entry;
	wl_list_for_each(entry, &rc.plugins, link) {
		load_single_plugin(entry->name, entry->path,
			entry->config_xml);
	}

	/* Emit startup event */
	struct labwc_event_base ev = { .type = LABWC_EVENT_STARTUP };
	plugin_events_emit(LABWC_EVENT_STARTUP, &ev);
}

void
plugin_manager_finish(void)
{
	/* Emit shutdown event */
	struct labwc_event_base ev = { .type = LABWC_EVENT_SHUTDOWN };
	plugin_events_emit(LABWC_EVENT_SHUTDOWN, &ev);

	/* Unload all plugins in reverse order */
	struct loaded_plugin *plugin, *tmp;
	wl_list_for_each_reverse_safe(plugin, tmp, &server.plugins, link) {
		unload_single_plugin(plugin);
	}

	plugin_rules_finish();
	plugin_events_finish();
}

void
plugin_manager_reload(void)
{
	/* Emit config reload event */
	struct labwc_event_base ev = { .type = LABWC_EVENT_CONFIG_RELOAD };
	plugin_events_emit(LABWC_EVENT_CONFIG_RELOAD, &ev);

	/*
	 * TODO: Diff the loaded plugin list against rc.plugins to detect
	 * added/removed plugins for hot-reload. For now, just emit the
	 * config reload event so plugins can re-read their config.
	 */
}

/* ---- API functions callable by plugins ---- */

void
labwc_plugin_set_info(labwc_plugin_t plugin, const struct labwc_plugin_info *info)
{
	if (!plugin || !info) {
		return;
	}
	plugin->info = *info;
	/* Keep our own copy of the name for the plugin struct */
	if (info->name && strcmp(info->name, plugin->name)) {
		free(plugin->name);
		plugin->name = strdup(info->name);
		plugin->info.name = plugin->name;
	}
}

struct server *
labwc_get_server(void)
{
	return &server;
}
#endif /* HAVE_PLUGINS */
