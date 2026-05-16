/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_PLUGIN_RULES_H
#define LABWC_PLUGIN_RULES_H

#include <stdbool.h>
#include <wayland-util.h>
#include "plugin/plugin.h"

struct view;

/** Custom window rule criterion match function */
typedef bool (*labwc_rule_match_fn)(struct view *v, const char *value,
	void *user_data);

/** Custom window rule property apply function */
typedef void (*labwc_rule_apply_fn)(struct view *v, const char *value,
	void *user_data);

/**
 * Register a custom matching criterion for window rules.
 * When the criterion name appears in rc.xml <windowRule>, the
 * matcher function is called to determine if the rule applies.
 */
bool labwc_rule_register_criterion(labwc_plugin_t plugin,
	const char *name, labwc_rule_match_fn fn, void *user_data);

/**
 * Register a custom property for window rules.
 * When the property name appears as a child element of a matching
 * <windowRule>, the applier function is called.
 */
bool labwc_rule_register_property(labwc_plugin_t plugin,
	const char *name, labwc_rule_apply_fn fn, void *user_data);

/** Unregister all rule extensions for a plugin */
void labwc_rules_unregister_all(labwc_plugin_t plugin);

/* ---- Internal (used by labwc core) ---- */

struct plugin_rule_criterion {
	struct wl_list link;
	labwc_plugin_t plugin;
	char *name;
	labwc_rule_match_fn fn;
	void *user_data;
};

struct plugin_rule_property {
	struct wl_list link;
	labwc_plugin_t plugin;
	char *name;
	labwc_rule_apply_fn fn;
	void *user_data;
};

void plugin_rules_init(void);
void plugin_rules_finish(void);

/**
 * Try to match a view against a plugin-registered criterion.
 * Returns true if the criterion name was found and the match function
 * returned true. Returns false if not found or not matching.
 * Sets *found to true if the criterion name was recognized.
 */
bool plugin_rules_try_match(const char *name, const char *value,
	struct view *v, bool *found);

/**
 * Try to apply a plugin-registered property to a view.
 * Returns true if the property name was recognized.
 */
bool plugin_rules_try_apply(const char *name, const char *value,
	struct view *v);

#endif /* LABWC_PLUGIN_RULES_H */
