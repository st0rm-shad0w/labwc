// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#if HAVE_PLUGINS
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/log.h>
#include "common/mem.h"
#include "common/list.h"
#include "plugin/rules.h"
#include "plugin/manager.h"

static struct wl_list criteria_registry;   /* plugin_rule_criterion.link */
static struct wl_list property_registry;   /* plugin_rule_property.link */
static bool rules_initialized;

void
plugin_rules_init(void)
{
	wl_list_init(&criteria_registry);
	wl_list_init(&property_registry);
	rules_initialized = true;
}

void
plugin_rules_finish(void)
{
	struct plugin_rule_criterion *c, *ctmp;
	wl_list_for_each_safe(c, ctmp, &criteria_registry, link) {
		wl_list_remove(&c->link);
		free(c->name);
		free(c);
	}
	struct plugin_rule_property *p, *ptmp;
	wl_list_for_each_safe(p, ptmp, &property_registry, link) {
		wl_list_remove(&p->link);
		free(p->name);
		free(p);
	}
	rules_initialized = false;
}

bool
labwc_rule_register_criterion(labwc_plugin_t plugin, const char *name,
	labwc_rule_match_fn fn, void *user_data)
{
	if (!rules_initialized || !name || !fn) {
		return false;
	}
	struct plugin_rule_criterion *c = znew(*c);
	c->plugin = plugin;
	c->name = strdup(name);
	c->fn = fn;
	c->user_data = user_data;
	wl_list_append(&criteria_registry, &c->link);
	wlr_log(WLR_INFO, "plugin: registered window rule criterion '%s'",
		name);
	return true;
}

bool
labwc_rule_register_property(labwc_plugin_t plugin, const char *name,
	labwc_rule_apply_fn fn, void *user_data)
{
	if (!rules_initialized || !name || !fn) {
		return false;
	}
	struct plugin_rule_property *p = znew(*p);
	p->plugin = plugin;
	p->name = strdup(name);
	p->fn = fn;
	p->user_data = user_data;
	wl_list_append(&property_registry, &p->link);
	wlr_log(WLR_INFO, "plugin: registered window rule property '%s'",
		name);
	return true;
}

void
labwc_rules_unregister_all(labwc_plugin_t plugin)
{
	if (!rules_initialized) {
		return;
	}
	struct plugin_rule_criterion *c, *ctmp;
	wl_list_for_each_safe(c, ctmp, &criteria_registry, link) {
		if (c->plugin == plugin) {
			wl_list_remove(&c->link);
			free(c->name);
			free(c);
		}
	}
	struct plugin_rule_property *p, *ptmp;
	wl_list_for_each_safe(p, ptmp, &property_registry, link) {
		if (p->plugin == plugin) {
			wl_list_remove(&p->link);
			free(p->name);
			free(p);
		}
	}
}

bool
plugin_rules_try_match(const char *name, const char *value,
	struct view *v, bool *found)
{
	if (!rules_initialized) {
		if (found) {
			*found = false;
		}
		return false;
	}
	struct plugin_rule_criterion *c;
	wl_list_for_each(c, &criteria_registry, link) {
		if (!strcasecmp(c->name, name)) {
			if (found) {
				*found = true;
			}
			return c->fn(v, value, c->user_data);
		}
	}
	if (found) {
		*found = false;
	}
	return false;
}

bool
plugin_rules_try_apply(const char *name, const char *value, struct view *v)
{
	if (!rules_initialized) {
		return false;
	}
	struct plugin_rule_property *p;
	wl_list_for_each(p, &property_registry, link) {
		if (!strcasecmp(p->name, name)) {
			p->fn(v, value, p->user_data);
			return true;
		}
	}
	return false;
}
#endif /* HAVE_PLUGINS */
