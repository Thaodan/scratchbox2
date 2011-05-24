/*
 * Copyright (C) 2006,2007 Lauri Leukkunen <lle@rahina.org>
 * Portion Copyright (c) 2008 Nokia Corporation.
 * (symlink- and path resolution code refactored by Lauri T. Aarnio at Nokia)
 *
 * Licensed under LGPL version 2.1, see top level LICENSE file for details.
 *
 * ----------------
 *
 * Path mapping subsystem of SB2; interfaces to Lua functions.
*/

/* FIXME: Temporary hack */
#define lua_instance sb2context

#if 0
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <pthread.h>
#endif

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <mapping.h>
#include <sb2.h>
#include "libsb2.h"
#include "exported.h"

#if 0
#ifdef EXTREME_DEBUGGING
#include <execinfo.h>
#endif
#endif

#include "pathmapping.h" /* get private definitions of this subsystem */

static void check_mapping_flags(int flags, const char *fn)
{
	if (flags & (~SB2_MAPPING_RULE_ALL_FLAGS)) {
		SB_LOG(SB_LOGLEVEL_WARNING,
			"%s returned unknown flags (0%o)",
			fn, flags & (~SB2_MAPPING_RULE_ALL_FLAGS));
	}
}

/* ========== Interfaces to Lua functions: ========== */

/* note: this expects that the lua stack already contains the mapping rule,
 * needed by sbox_translate_path (lua code).
 * at exit the rule is still there.
*/
char *call_lua_function_sbox_translate_path(
	const path_mapping_context_t *ctx,
	int result_log_level,
	const char *abs_clean_virtual_path,
	int *flagsp,
	char **exec_policy_name_ptr)
{
	struct lua_instance	*luaif = ctx->pmc_luaif;
	int flags;
	char *host_path = NULL;

	SB_LOG(SB_LOGLEVEL_NOISE, "calling sbox_translate_path for %s(%s)",
		ctx->pmc_func_name, abs_clean_virtual_path);
	SB_LOG(SB_LOGLEVEL_NOISE,
		"call_lua_function_sbox_translate_path: gettop=%d",
		lua_gettop(luaif->lua));
	if(SB_LOG_IS_ACTIVE(SB_LOGLEVEL_NOISE3)) {
		dump_lua_stack("call_lua_function_sbox_translate_path entry",
			luaif->lua);
	}

	lua_getfield(luaif->lua, LUA_GLOBALSINDEX, "sbox_translate_path");
	/* stack now contains the rule object and string "sbox_translate_path",
         * move the string to the bottom: */
	lua_insert(luaif->lua, -2);
	/* add other parameters */
	lua_pushstring(luaif->lua, ctx->pmc_binary_name);
	lua_pushstring(luaif->lua, ctx->pmc_func_name);
	lua_pushstring(luaif->lua, abs_clean_virtual_path);
	 /* 4 arguments, returns rule,policy,path,flags */
	lua_call(luaif->lua, 4, 4);

	host_path = (char *)lua_tostring(luaif->lua, -2);
	if (host_path && (*host_path != '/')) {
		SB_LOG(SB_LOGLEVEL_ERROR,
			"Mapping failed: Result is not absolute ('%s'->'%s')",
			abs_clean_virtual_path, host_path);
		host_path = NULL;
	} else if (host_path) {
		host_path = strdup(host_path);
	}
	flags = lua_tointeger(luaif->lua, -1);
	check_mapping_flags(flags, "sbox_translate_path");
	if (flagsp) *flagsp = flags;

	if (exec_policy_name_ptr) {
		char *exec_policy_name;

		if (*exec_policy_name_ptr) {
			free(*exec_policy_name_ptr);
			*exec_policy_name_ptr = NULL;
		}
		exec_policy_name = (char *)lua_tostring(luaif->lua, -3);
		if (exec_policy_name) {
			*exec_policy_name_ptr = strdup(exec_policy_name);
		}
	}

	lua_pop(luaif->lua, 3); /* leave the rule to the stack */

	if (host_path) {
		/* sometimes a mapping rule may create paths that contain
		 * doubled slashes ("//") or end with a slash. We'll
		 * need to clean the path here.
		*/
		char *cleaned_host_path;
		struct path_entry_list list;

		split_path_to_path_list(host_path, &list);
		list.pl_flags|= PATH_FLAGS_HOST_PATH;

		switch (is_clean_path(&list)) {
		case 0: /* clean */
			break;
		case 1: /* . */
			remove_dots_from_path_list(&list);
			break;
		case 2: /* .. */
			/* The rule inserted ".." to the path?
			 * not very wise move, maybe we should even log
			 * warning about this? However, cleaning is
			 * easy in this case; the result is a host
			 * path => cleanup doesn't need to make
			 * recursive calls to sb_path_resolution.
			*/
			remove_dots_from_path_list(&list);
			clean_dotdots_from_path(ctx, &list);
			break;
		}
		cleaned_host_path = path_list_to_string(&list);
		free_path_list(&list);

		if (*cleaned_host_path != '/') {
			/* oops, got a relative path. CWD is too long. */
			SB_LOG(SB_LOGLEVEL_DEBUG,
				"OOPS, call_lua_function_sbox_translate_path:"
				" relative");
		}
		free(host_path);
		host_path = NULL;

		/* log the result */
		if (strcmp(cleaned_host_path, abs_clean_virtual_path) == 0) {
			/* NOTE: Following SB_LOG() call is used by the log
			 *       postprocessor script "sb2logz". Do not change
			 *       without making a corresponding change to
			 *       the script!
			*/
			SB_LOG(result_log_level, "pass: %s '%s'%s",
				ctx->pmc_func_name, abs_clean_virtual_path,
				((flags & SB2_MAPPING_RULE_FLAGS_READONLY) ? " (readonly)" : ""));
		} else {
			/* NOTE: Following SB_LOG() call is used by the log
			 *       postprocessor script "sb2logz". Do not change
			 *       without making a corresponding change to
			 *       the script!
			*/
			SB_LOG(result_log_level, "mapped: %s '%s' -> '%s'%s",
				ctx->pmc_func_name, abs_clean_virtual_path, cleaned_host_path,
				((flags & SB2_MAPPING_RULE_FLAGS_READONLY) ? " (readonly)" : ""));
		}
		host_path = cleaned_host_path;
	}
	if (!host_path) {
		SB_LOG(SB_LOGLEVEL_ERROR,
			"No result from sbox_translate_path for: %s '%s'",
			ctx->pmc_func_name, abs_clean_virtual_path);
	}
	SB_LOG(SB_LOGLEVEL_NOISE,
		"call_lua_function_sbox_translate_path: at exit, gettop=%d",
		lua_gettop(luaif->lua));
	if(SB_LOG_IS_ACTIVE(SB_LOGLEVEL_NOISE3)) {
		dump_lua_stack("call_lua_function_sbox_translate_path exit",
			luaif->lua);
	}
	return(host_path);
}

/* - returns 1 if ok (then *min_path_lenp is valid)
 * - returns 0 if failed to find the rule
 * Note: this leaves the rule to the stack!
*/
int call_lua_function_sbox_get_mapping_requirements(
	const path_mapping_context_t *ctx,
	const struct path_entry_list *abs_virtual_source_path_list,
	int *min_path_lenp,
	int *call_translate_for_all_p)
{
	struct lua_instance	*luaif = ctx->pmc_luaif;
	int rule_found;
	int min_path_len;
	int flags;
	char	*abs_virtual_source_path_string;

	abs_virtual_source_path_string = path_list_to_string(abs_virtual_source_path_list);

	SB_LOG(SB_LOGLEVEL_NOISE,
		"calling sbox_get_mapping_requirements for %s(%s)",
		ctx->pmc_func_name, abs_virtual_source_path_string);
	SB_LOG(SB_LOGLEVEL_NOISE,
		"call_lua_function_sbox_get_mapping_requirements: gettop=%d",
		lua_gettop(luaif->lua));

	lua_getfield(luaif->lua, LUA_GLOBALSINDEX,
		"sbox_get_mapping_requirements");
	lua_pushstring(luaif->lua, ctx->pmc_binary_name);
	lua_pushstring(luaif->lua, ctx->pmc_func_name);
	lua_pushstring(luaif->lua, abs_virtual_source_path_string);
	/* 3 arguments, returns 4: (rule, rule_found_flag,
	 * min_path_len, flags) */
	lua_call(luaif->lua, 3, 4);

	rule_found = lua_toboolean(luaif->lua, -3);
	min_path_len = lua_tointeger(luaif->lua, -2);
	flags = lua_tointeger(luaif->lua, -1);
	check_mapping_flags(flags, "sbox_get_mapping_requirements");
	if (min_path_lenp) *min_path_lenp = min_path_len;
	if (call_translate_for_all_p)
		*call_translate_for_all_p =
			(flags & SB2_MAPPING_RULE_FLAGS_CALL_TRANSLATE_FOR_ALL);

	/* remove last 3 values; leave "rule" to the stack */
	lua_pop(luaif->lua, 3);

	SB_LOG(SB_LOGLEVEL_DEBUG, "sbox_get_mapping_requirements -> %d,%d,0%o",
		rule_found, min_path_len, flags);

	SB_LOG(SB_LOGLEVEL_NOISE,
		"call_lua_function_sbox_get_mapping_requirements:"
		" at exit, gettop=%d",
		lua_gettop(luaif->lua));
	free(abs_virtual_source_path_string);
	return(rule_found);
}

/* returns virtual_path */
char *call_lua_function_sbox_reverse_path(
	const path_mapping_context_t *ctx,
	const char *abs_host_path)
{
	struct lua_instance	*luaif = ctx->pmc_luaif;
	char *virtual_path = NULL;
	int flags;

	SB_LOG(SB_LOGLEVEL_NOISE, "calling sbox_reverse_path for %s(%s)",
		ctx->pmc_func_name, abs_host_path);

	lua_getfield(luaif->lua, LUA_GLOBALSINDEX, "sbox_reverse_path");
	lua_pushstring(luaif->lua, ctx->pmc_binary_name);
	lua_pushstring(luaif->lua, ctx->pmc_func_name);
	lua_pushstring(luaif->lua, abs_host_path);
	 /* 3 arguments, returns virtual_path and flags */
	lua_call(luaif->lua, 3, 2);

	virtual_path = (char *)lua_tostring(luaif->lua, -2);
	if (virtual_path) {
		virtual_path = strdup(virtual_path);
	}

	flags = lua_tointeger(luaif->lua, -1);
	check_mapping_flags(flags, "sbox_reverse_path");
	/* Note: "flags" is not yet used for anything, intentionally */
 
	lua_pop(luaif->lua, 2); /* remove return values */

	if (virtual_path) {
		SB_LOG(SB_LOGLEVEL_DEBUG, "virtual_path='%s'", virtual_path);
	} else {
		SB_LOG(SB_LOGLEVEL_INFO,
			"No result from sbox_reverse_path for: %s '%s'",
			ctx->pmc_func_name, abs_host_path);
	}
	return(virtual_path);
}

/* clean up path resolution environment from lua stack */
void drop_rule_from_lua_stack(struct lua_instance *luaif)
{
	lua_pop(luaif->lua, 1);

	SB_LOG(SB_LOGLEVEL_NOISE,
		"drop rule from stack: at exit, gettop=%d",
		lua_gettop(luaif->lua));
}

