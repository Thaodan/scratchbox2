/*
 * Copyright (C) 2006,2007 Lauri Leukkunen <lle@rahina.org>
 * Copyright (C) 2009 Nokia Corporation.
 *
 * Licensed under LGPL version 2.1, see top level LICENSE file for details.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _GNU_SOURCE
#undef _GNU_SOURCE
#include <string.h>
#include <libgen.h>
#define _GNU_SOURCE
#else
#include <string.h>
#include <libgen.h>
#endif

#include <limits.h>
#include <sys/param.h>
#include <sys/file.h>
#include <assert.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <mapping.h>
#include <sb2.h>

#include "rule_tree.h"


/* ensure that the rule tree has been mapped. */
static int lua_sb_attach_ruletree(lua_State *l)
{
	int attach_result = -1;

	if (sbox_session_dir) {
		char *rule_tree_path = NULL;

		/* map the rule tree to memory: */
		asprintf(&rule_tree_path, "%s/RuleTree.bin", sbox_session_dir);
		attach_result = attach_ruletree(rule_tree_path,
			1/*create if needed*/, 1/*keep open*/);
		SB_LOG(SB_LOGLEVEL_DEBUG, "attach(%s) = %d", rule_tree_path, attach_result);
		free(rule_tree_path);
	}
	lua_pushnumber(l, attach_result);
	SB_LOG(SB_LOGLEVEL_DEBUG, "lua_sb_attach_ruletree => %d",
		attach_result);
	return(1);
}

/* "sb.add_rule_to_ruletree(...)
*/
static int lua_sb_add_rule_to_ruletree(lua_State *l)
{
	int	n = lua_gettop(l);
	uint32_t rule_location = 0;

	if (n == 12) {
		const char	*rule_name = lua_tostring(l, 1);
		int		selector_type = lua_tointeger(l, 2);
		const char	*selector = lua_tostring(l, 3);
		int		action_type = lua_tointeger(l, 4);
		const char	*action_str = lua_tostring(l, 5);
		int		condition_type = lua_tointeger(l, 6);
		const char	*condition_str = lua_tostring(l, 7);
		ruletree_object_offset_t rule_list_link = lua_tointeger(l, 8);
		int		flags = lua_tointeger(l, 9);
		const char	*binary_name = lua_tostring(l, 10);
		int		func_class = lua_tointeger(l, 11);
		const char	*exec_policy_name = lua_tostring(l, 12);

		rule_location = add_rule_to_ruletree(rule_name,
			selector_type, selector,
			action_type, action_str,
			condition_type, condition_str,
			rule_list_link,
			flags, binary_name, func_class, exec_policy_name);

		SB_LOG(SB_LOGLEVEL_NOISE,
			"lua_sb_add_rule_to_ruletree '%s' => %d",
			rule_name, rule_location);
	}
	lua_pushnumber(l, rule_location);
	return 1;
}

#if 0
static int lua_sb_link_ruletree_rules(lua_State *l)
{
	int	n = lua_gettop(l);
	int	result = 0;

	if (n == 2) {
		uint32_t	rule1_location = lua_tointeger(l, 1);
		uint32_t	rule2_location = lua_tointeger(l, 2);

		result = link_ruletree_rules(rule1_location, rule2_location);

		SB_LOG(SB_LOGLEVEL_NOISE,
			"lua_sb_link_ruletree_rules => %d", result);
	}
	lua_pushnumber(l, result);
	return 1;
}
#endif

static int lua_sb_set_ruletree_fsrules(lua_State *l)
{
	int	n = lua_gettop(l);
	int	result = 0;

	if (n == 2) {
		const char	*ruleset_name = lua_tostring(l, 1);
		uint32_t	rule_location = lua_tointeger(l, 2);
		const char *modename = sbox_session_mode;

		if (!modename) modename = "Default";

		result = set_ruletree_fsrules(modename, ruleset_name, rule_location);

		SB_LOG(SB_LOGLEVEL_NOISE,
			"lua_sb_set_ruletree_fsrules => %d", result);
	}
	lua_pushnumber(l, result);
	return 1;
}

static int lua_sb_ruletree_objectlist_create_list(lua_State *l)
{
	int				n = lua_gettop(l);
	ruletree_object_offset_t	list_offs = 0;

	SB_LOG(SB_LOGLEVEL_NOISE,
		"lua_sb_ruletree_objectlist_create_list n=%d", n);
	if (n == 1) {
		uint32_t	list_size = lua_tointeger(l, 1);
		list_offs = ruletree_objectlist_create_list(list_size);
	}
	SB_LOG(SB_LOGLEVEL_NOISE,
		"lua_sb_ruletree_objectlist_create_list => %d", list_offs);
	lua_pushnumber(l, list_offs);
	return 1;
}

static int lua_sb_ruletree_objectlist_set_item(lua_State *l)
{
	int	n = lua_gettop(l);
	int	res = 0;

	if (n == 3) {
		ruletree_object_offset_t	list_offs = lua_tointeger(l, 1);
		uint32_t			n = lua_tointeger(l, 2);
		ruletree_object_offset_t	value = lua_tointeger(l, 3);
		res = ruletree_objectlist_set_item(list_offs, n, value);
	}
	SB_LOG(SB_LOGLEVEL_NOISE,
		"lua_sb_ruletree_objectlist_set_item => %d", res);
	return 0;
}

static int lua_sb_ruletree_objectlist_get_item(lua_State *l)
{
	int				n = lua_gettop(l);
	ruletree_object_offset_t	value = 0;

	if (n == 2) {
		ruletree_object_offset_t	list_offs = lua_tointeger(l, 1);
		uint32_t			n = lua_tointeger(l, 2);
		value = ruletree_objectlist_get_item(list_offs, n);
	}
	SB_LOG(SB_LOGLEVEL_NOISE,
		"lua_sb_ruletree_objectlist_get_item => %d", value);
	lua_pushnumber(l, value);
	return 1;
}

static int lua_sb_ruletree_objectlist_get_list_size(lua_State *l)
{
	int		n = lua_gettop(l);
	uint32_t	size = 0;

	if (n == 1) {
		ruletree_object_offset_t	list_offs = lua_tointeger(l, 1);
		size = ruletree_objectlist_get_list_size(list_offs);
	}
	SB_LOG(SB_LOGLEVEL_NOISE,
		"lua_sb_ruletree_objectlist_get_list_size => %d", size);
	lua_pushnumber(l, size);
	return 1;
}

/* mappings from c to lua */
static const luaL_reg reg[] =
{
	{"objectlist_create",		lua_sb_ruletree_objectlist_create_list},
	{"objectlist_set",		lua_sb_ruletree_objectlist_set_item},
	{"objectlist_get",		lua_sb_ruletree_objectlist_get_item},
	{"objectlist_size",		lua_sb_ruletree_objectlist_get_list_size},

	{"set_ruletree_fsrules",	lua_sb_set_ruletree_fsrules},

	/* OLD NAMES: */
	{"attach_ruletree",		lua_sb_attach_ruletree},

	{"add_rule_to_ruletree",	lua_sb_add_rule_to_ruletree},
#if 0
	{"link_ruletree_rules",		lua_sb_link_ruletree_rules},
#endif

	{NULL,				NULL}
};


int lua_bind_ruletree_functions(lua_State *l)
{
	luaL_register(l, "ruletree", reg);
	lua_pushliteral(l,"version");
	lua_pushstring(l, "2.0" );
	lua_settable(l,-3);

	return 0;
}

