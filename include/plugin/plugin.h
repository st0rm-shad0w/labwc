/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_PLUGIN_H
#define LABWC_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

/**
 * labwc Plugin ABI Version
 *
 * Date-based integer (YYYYMMDD). Bumped when the plugin API changes.
 * Plugins are compiled against this value; the manager rejects mismatches.
 * There is no ABI stability guarantee -- plugins must be compiled against
 * the labwc version they will run with (same approach as Hyprland).
 */
#define LABWC_PLUGIN_ABI_VERSION 20250516

/* Visibility macro for exported plugin symbols */
#define LABWC_EXPORT __attribute__((visibility("default")))

/* Forward declarations */
struct server;
struct view;
struct output;
struct loaded_plugin;

/**
 * Opaque handle for a loaded plugin instance.
 * Plugins receive this during init and pass it to all API calls.
 */
typedef struct loaded_plugin *labwc_plugin_t;

/**
 * Plugin metadata, set by the plugin during init.
 */
struct labwc_plugin_info {
	const char *name;
	const char *description;
	const char *author;
	const char *version;
	int order_hint;          /* reserved for future use (default 0) */
	bool allow_hot_unload;   /* reserved for future use (default false) */
};

/*
 * Every plugin .so must export these symbols:
 *
 * uint32_t labwc_plugin_abi_version(void);
 *   - Must return LABWC_PLUGIN_ABI_VERSION at compile time.
 *
 * int labwc_plugin_init(labwc_plugin_t plugin);
 *   - Called after dlopen(). Set up hooks, register actions, etc.
 *   - Must call labwc_plugin_set_info() to provide metadata.
 *   - Return 0 on success, non-zero on failure.
 *
 * void labwc_plugin_fini(labwc_plugin_t plugin);   [optional]
 *   - Called before dlclose(). Clean up all resources.
 */

/* Convenience macro: generates the ABI version export */
#define LABWC_PLUGIN_ABI_VERSION_FN \
	LABWC_EXPORT uint32_t labwc_plugin_abi_version(void) { \
		return LABWC_PLUGIN_ABI_VERSION; \
	}

/**
 * labwc_plugin_set_info() - Register plugin metadata.
 * Must be called during labwc_plugin_init().
 */
void labwc_plugin_set_info(labwc_plugin_t plugin,
	const struct labwc_plugin_info *info);

/**
 * labwc_get_server() - Get the global server struct.
 * Gives full access to all compositor state.
 */
struct server *labwc_get_server(void);

#endif /* LABWC_PLUGIN_H */
