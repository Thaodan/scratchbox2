/*
 * Copyright (C) 2006,2007 Lauri Leukkunen <lle@rahina.org>
 *
 * Licensed under LGPL version 2.1, see top level LICENSE file for details.
 */

#include <unistd.h>
#include <stdint.h>
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

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "mapping.h"
#include "sb2.h"
#include "rule_tree.h"

/* ------------ WARNING WARNING WARNING ------------
 * A SERIOUS WARNING ABOUT THE "pthread" LIBRARY:
 * The libpthread.so library interferes with some other libraries on
 * Linux. It is enough to just load the library to memory. For example,
 * /usr/bin/html2text on debian "etch" crashes if libpthread.so is loaded,
 * but works fine if libpthread is not loaded (the crash is caused by
 * a segfault inside libstdc++)
 *
 * Because of this, a special interface to the pthread library must be used:
 * We must use the run-time interface to dynamic linker and detect if the
 * pthread library has already been loaded (most likely, by the real program
 * that we are serving).
*/

#include <dlfcn.h>

int pthread_library_is_available = 0; /* flag */
static int pthread_detection_done = 0;
/* pointers to pthread library functions, if the pthread library is in use.
*/
static int (*pthread_key_create_fnptr)(pthread_key_t *key,
	 void (*destructor)(void*)) = NULL;
static void *(*pthread_getspecific_fnptr)(pthread_key_t key) = NULL;
static int (*pthread_setspecific_fnptr)(pthread_key_t key,
	const void *value) = NULL;
static int (*pthread_once_fnptr)(pthread_once_t *, void (*)(void)) = NULL;
pthread_t (*pthread_self_fnptr)(void) = NULL;
int (*pthread_mutex_lock_fnptr)(pthread_mutex_t *mutex) = NULL;
int (*pthread_mutex_unlock_fnptr)(pthread_mutex_t *mutex) = NULL;

/* lua_sb_test_net_addr_match() is in network_sb2if.c */
extern int lua_sb_test_net_addr_match(lua_State *l);

static void check_pthread_library()
{
	if (pthread_detection_done == 0) {
		/* these are available only in libpthread: */
		pthread_key_create_fnptr = dlsym(RTLD_DEFAULT,
			"pthread_key_create");
		pthread_getspecific_fnptr = dlsym(RTLD_DEFAULT,
			"pthread_getspecific");
		pthread_setspecific_fnptr = dlsym(RTLD_DEFAULT,
			"pthread_setspecific");
		pthread_once_fnptr = dlsym(RTLD_DEFAULT,
			"pthread_once");

		pthread_mutex_lock_fnptr = dlsym(RTLD_DEFAULT,
			"pthread_mutex_lock");
		pthread_mutex_unlock_fnptr = dlsym(RTLD_DEFAULT,
			"pthread_mutex_unlock");

		/* Linux/glibc: pthread_self seems to exist in both
		 * glibc and libpthread. */
		pthread_self_fnptr = dlsym(RTLD_DEFAULT,
			"pthread_self");

		if (pthread_key_create_fnptr &&
		    pthread_getspecific_fnptr &&
		    pthread_setspecific_fnptr &&
		    pthread_once_fnptr) {
			SB_LOG(SB_LOGLEVEL_DEBUG,
				"pthread library FOUND");
			pthread_detection_done = 1;
			pthread_library_is_available = 1;
		} else if (!pthread_key_create_fnptr &&
		   !pthread_getspecific_fnptr &&
		   !pthread_setspecific_fnptr &&
		   !pthread_once_fnptr) {
			SB_LOG(SB_LOGLEVEL_DEBUG,
				"pthread library not found");
			pthread_detection_done = -1;
			pthread_library_is_available = 0;
		} else {
			SB_LOG(SB_LOGLEVEL_ERROR,
				"pthread library is only partially available"
				" - operation may become unstable");
			pthread_detection_done = -2;
			pthread_library_is_available = 0;
		}

	} else {
		SB_LOG(SB_LOGLEVEL_NOISE,
			"pthread detection already done (%d)",
			pthread_detection_done);
	}
}

/* ------------ End Of pthreads Warnings & Interface Code ------------ */

#define __set_errno(e) errno = e

void mapping_log_write(char *msg);
static int lua_bind_sb_functions(lua_State *l);

static pthread_key_t sb2context_key;
static pthread_once_t sb2context_key_once = PTHREAD_ONCE_INIT;

static char *read_string_variable_from_lua(
	struct sb2context *sb2if,
	const char *name)
{
	char *result = NULL;

	if (sb2if && name && *name) {
		lua_getglobal(sb2if->lua, name);
		result = (char *)lua_tostring(sb2if->lua, -1);
		if (result) {
			result = strdup(result);
		}
		lua_pop(sb2if->lua, 1);
		SB_LOG(SB_LOGLEVEL_DEBUG,
			"Lua variable %s = '%s', gettop=%d",
			name, (result ? result : "<NULL>"),
			lua_gettop(sb2if->lua));
	}
	return(result);
}

static void free_lua(void *buf)
{
	free(buf);
}

static void alloc_sb2context_key(void)
{
	if (pthread_key_create_fnptr)
		(*pthread_key_create_fnptr)(&sb2context_key, free_lua);
}

/* used only if pthread lib is not available: */
static	struct sb2context *my_sb2context = NULL;

static void load_and_execute_lua_file(struct sb2context *sb2if, const char *filename)
{
	const char *errmsg;

	switch(luaL_loadfile(sb2if->lua, filename)) {
	case LUA_ERRFILE:
		fprintf(stderr, "Error loading %s\n", filename);
		exit(1);
	case LUA_ERRSYNTAX:
		errmsg = lua_tostring(sb2if->lua, -1);
		fprintf(stderr, "Syntax error in %s (%s)\n", filename, 
			(errmsg?errmsg:""));
		exit(1);
	case LUA_ERRMEM:
		fprintf(stderr, "Memory allocation error while "
				"loading %s\n", filename);
		exit(1);
	default:
		;
	}
	lua_call(sb2if->lua, 0, 0);
}

/* Lua calls this at panic: */
static int sb2_lua_panic(lua_State *l)
{
	fprintf(stderr,
		"Scratchbox2: Lua interpreter PANIC: unprotected error in call to Lua API (%s)\n",
		lua_tostring(l, -1));
	sblog_init(); /* make sure the logger has been initialized */
	SB_LOG(SB_LOGLEVEL_ERROR,
		"Lua interpreter PANIC: unprotected error in call to Lua API (%s)\n",
		lua_tostring(l, -1));
	return 0;
}

static struct sb2context *alloc_sb2context(void)
{
	struct sb2context *tmp;

	if (pthread_getspecific_fnptr) {
		tmp = (*pthread_getspecific_fnptr)(sb2context_key);
		if (tmp != NULL) {
			SB_LOG(SB_LOGLEVEL_DEBUG,
				"alloc_sb2context: already done (pt-getspec.)");
			return(tmp);
		}
	} else if (my_sb2context) {
		SB_LOG(SB_LOGLEVEL_DEBUG,
			"alloc_sb2context: already done (has my_sb2context)");
		return(my_sb2context);
	}

	tmp = malloc(sizeof(struct sb2context));
	if (!tmp) {
		SB_LOG(SB_LOGLEVEL_ERROR,
			"alloc_sb2context: Failed to allocate memory");
		return(NULL);
	}
	memset(tmp, 0, sizeof(struct sb2context));

	if (pthread_setspecific_fnptr) {
		(*pthread_setspecific_fnptr)(sb2context_key, tmp);
	} else {
		my_sb2context = tmp;
	}
	
	if (!sbox_session_dir || !*sbox_session_dir) {
		SB_LOG(SB_LOGLEVEL_ERROR,
			"alloc_sb2context: no SBOX_SESSION_DIR");
		return(NULL); /* can't live without a session */
	}
	sbox_session_dir = strdup(sbox_session_dir);
	tmp->lua = NULL;
	return(tmp);
}

void sb2context_initialize_lua(struct sb2context *sb2ctx)
{
	char *main_lua_script = NULL;
	char *lua_if_version = NULL;

	/* return immediately if already been here */
	if (!sb2ctx || sb2ctx->lua) return;

	if (asprintf(&main_lua_script, "%s/lua_scripts/main.lua",
	     sbox_session_dir) < 0) {
		SB_LOG(SB_LOGLEVEL_ERROR,
			"sb2context_initialize_lua: asprintf failed to allocate memory");
		return;
	}
		
	SB_LOG(SB_LOGLEVEL_INFO, "Loading '%s'", main_lua_script);

	sb2ctx->lua = luaL_newstate();
	lua_atpanic(sb2ctx->lua, sb2_lua_panic);

	disable_mapping(sb2ctx);
	luaL_openlibs(sb2ctx->lua);
	lua_bind_sb_functions(sb2ctx->lua); /* register our sb_ functions */
	lua_bind_ruletree_functions(sb2ctx->lua); /* register our ruletree_ functions */

	load_and_execute_lua_file(sb2ctx, main_lua_script);

	enable_mapping(sb2ctx);

	/* check Lua/C interface version. */
	lua_if_version = read_string_variable_from_lua(sb2ctx,
		"sb2_lua_c_interface_version");
	if (!lua_if_version) {
		SB_LOG(SB_LOGLEVEL_ERROR, "FATAL ERROR: "
			"sb2's Lua scripts didn't provide"
			" 'sb2_lua_c_interface_version' identifier!");
		exit(1);
	}
	if (strcmp(lua_if_version, SB2_LUA_C_INTERFACE_VERSION)) {
		SB_LOG(SB_LOGLEVEL_ERROR, "FATAL ERROR: "
			"sb2's Lua script interface version mismatch:"
			" scripts provide '%s', but '%s' was expected",
			lua_if_version, SB2_LUA_C_INTERFACE_VERSION);
		exit(1);
	}
	free(lua_if_version);

	SB_LOG(SB_LOGLEVEL_INFO, "lua initialized.");
	SB_LOG(SB_LOGLEVEL_NOISE, "gettop=%d", lua_gettop(sb2ctx->lua));

	free(main_lua_script);
}

static void increment_sb2if_usage_counter(volatile struct sb2context *ptr)
{
	if (SB_LOG_IS_ACTIVE(SB_LOGLEVEL_DEBUG)) {
		/* Well, to make this bullet-proof the sb2if structure
		 * should be locked, but since this code is now used only for
		 * producing debugging information and the pointer is marked
		 * "volatile", the results are good enough. No need to slow 
		 * down anything with additional locks - this function is 
		 * called frequently. */
		if (ptr->sb2context_in_use > 0) SB_LOG(SB_LOGLEVEL_DEBUG,
			"Lua instance already in use! (%d)",
			ptr->sb2context_in_use);

		(ptr->sb2context_in_use)++;
	}
}

void release_sb2context(struct sb2context *sb2if)
{
	if (SB_LOG_IS_ACTIVE(SB_LOGLEVEL_DEBUG)) {
		int	i;
		volatile struct sb2context *ptr = sb2if;

		SB_LOG(SB_LOGLEVEL_NOISE, "release_sb2context()");

		if (!ptr) {
			SB_LOG(SB_LOGLEVEL_DEBUG,
				"release_sb2context(): ptr is NULL ");
			return;
		}

		i = ptr->sb2context_in_use;
		if (i > 1) SB_LOG(SB_LOGLEVEL_DEBUG,
			"Lua instance usage counter was %d", i);

		(ptr->sb2context_in_use)--;
	}
}

/* get access to sb2 context, create the structure 
 * if it didn't exist; in that case, the structure is only
 * cleared (most notably, the Lua system is not initialized
 * by this routine!)
 *
 * Remember to call release_sb2context() after the
 * pointer is not needed anymore.
*/
struct sb2context *get_sb2context(void)
{
	struct sb2context *ptr = NULL;

	if (!sb2_global_vars_initialized__) sb2_initialize_global_variables();

	if (!SB_LOG_INITIALIZED()) sblog_init();

	SB_LOG(SB_LOGLEVEL_NOISE, "get_sb2context()");

	if (pthread_detection_done == 0) check_pthread_library();

	if (pthread_library_is_available) {
		if (pthread_once_fnptr)
			(*pthread_once_fnptr)(&sb2context_key_once, alloc_sb2context_key);
		if (pthread_getspecific_fnptr)
			ptr = (*pthread_getspecific_fnptr)(sb2context_key);
		if (!ptr) ptr = alloc_sb2context();
		if (!ptr) {
			SB_LOG(SB_LOGLEVEL_ERROR,
				"Something's wrong with"
				" the pthreads support");
			fprintf(stderr, "FATAL: sb2 preload library:"
				" Something's wrong with"
				" the pthreads support.\n");
			exit(1);
		}
	} else {
		/* no pthreads, single-thread application */
		ptr = my_sb2context;
		if (!ptr) ptr = alloc_sb2context();
		if (!ptr) {
			SB_LOG(SB_LOGLEVEL_ERROR,
				"Failed to get Lua instance"
				" (and the pthreads support is "
				" disabled!)");
			fprintf(stderr, "FATAL: sb2 preload library:"
				" Failed to get Lua instance"
				" (and the pthreads support is disabled!)\n");
			exit(1);
		}
	}

	if (SB_LOG_IS_ACTIVE(SB_LOGLEVEL_DEBUG)) {
		increment_sb2if_usage_counter(ptr);
	}
	return(ptr);
}

/* get access to sb2 context, and make sure that Lua
 * has been initialized.
*/
struct sb2context *get_sb2context_lua(void)
{
	struct sb2context *ptr = get_sb2context();

	if (ptr) sb2context_initialize_lua(ptr);
	return(ptr);
}

/* Preload library constructor. Unfortunately this can
 * be called after other parts of this library have been called
 * if the program uses multiple threads (unbelievable, but true!),
 * so this isn't really too useful. Lua initialization was
 * moved to get_sb2context_lua() because of this.
*/
#ifndef SB2_TESTER
#ifdef __GNUC__
void sb2_preload_library_constructor(void) __attribute((constructor));
#endif
#endif /* SB2_TESTER */
void sb2_preload_library_constructor(void)
{
	SB_LOG(SB_LOGLEVEL_DEBUG, "sb2_preload_library_constructor called");
	sblog_init();
	SB_LOG(SB_LOGLEVEL_DEBUG, "sb2_preload_library_constructor: done");
}

/* Read string variables from lua.
 * Note that this function is exported from libsb2.so (for sb2-show etc): */
char *sb2__read_string_variable_from_lua__(const char *name)
{
	struct sb2context *sb2if;
	char *cp;

	sb2if = get_sb2context_lua();
	cp = read_string_variable_from_lua(sb2if, name);
	release_sb2context(sb2if);
	return(cp);
}

/* Return the Lua/C interface version string = the library interface version.
 * Note that this function is exported from libsb2.so (for sb2-show etc): */
const char *sb2__lua_c_interface_version__(void)
{
	/* currently it is enough to return pointer to the constant string. */
	return(SB2_LUA_C_INTERFACE_VERSION);
}

/* Read and execute an lua file. Used from sb2-show, useful
 * for debugging and benchmarking since the script is executed
 * in a context which already contains all SB2's varariables etc.
 * Note that this function is exported from libsb2.so:
*/
void sb2__load_and_execute_lua_file__(const char *filename)
{
	struct sb2context *sb2if;

	sb2if = get_sb2context_lua();
	load_and_execute_lua_file(sb2if, filename);
	release_sb2context(sb2if);
}

#if 0 /* DISABLED 2008-10-23/LTA: sb_decolonize_path() is not currently available*/
/* "sb.decolonize_path", to be called from lua code */
static int lua_sb_decolonize_path(lua_State *l)
{
	int n;
	char *path;
	char *resolved_path;

	n = lua_gettop(l);
	if (n != 1) {
		lua_pushstring(l, NULL);
		return 1;
	}

	path = strdup(lua_tostring(l, 1));

	resolved_path = sb_decolonize_path(path);
	lua_pushstring(l, resolved_path);
	free(resolved_path);
	free(path);
	return 1;
}
#endif

/* "sb.readlink", to be called from lua code */
static int lua_sb_readlink(lua_State *l)
{
	int n;
	char *path;
	char resolved_path[PATH_MAX + 1];

	n = lua_gettop(l);
	if (n != 1) {
		lua_pushstring(l, NULL);
		return 1;
	}

	memset(resolved_path, '\0', PATH_MAX + 1);

	path = strdup(lua_tostring(l, 1));
	if (readlink(path, resolved_path, PATH_MAX) < 0) {
		free(path);
		lua_pushstring(l, NULL);
		return 1;
	} else {
		free(path);
		lua_pushstring(l, resolved_path);
		return 1;
	}
}

/* returns an allocated string */
/* FIXME: This should be somewhere else! it does not belong to this file. */
char *prep_union_dir(const char *dst_path, char **src_paths, int num_real_dir_entries)
{
	DIR *d = NULL;
	struct dirent *de;
	int count = 0;
	int i;
	char *udir_name;
	char *cp;
	int slash_count = 0;
	char *mod_dst_path = NULL;
	char *result_path;

	if (num_real_dir_entries < 1) goto error_out;

	SB_LOG(SB_LOGLEVEL_DEBUG,
		"prep_union_dir: dst=%s #%d source directories",
		dst_path, num_real_dir_entries);

	/* add number of slashes to the path. this makes it possible
	 * to have union directories that have other union directories
	 * as subdirectories (because the names in the directories
	 * always refer to ordinary, empty files).
	*/
	mod_dst_path = strdup(dst_path);
	cp = mod_dst_path;
	while(*cp) {
		if (*cp == '/') slash_count++;
		cp++;
	}
	if (asprintf(&udir_name, "%s/uniondirs/%d", sbox_session_dir, slash_count) < 0)
		goto asprint_failed_error_out;
	SB_LOG(SB_LOGLEVEL_DEBUG, "prep_union_dir: mkdir(%s)", udir_name);
	mkdir_nomap_nolog(udir_name, 0700);
	free(udir_name);
	
	/* this is same as mkdir -p, effectively */
	cp = mod_dst_path;
	while(*cp == '/') *cp++ = '@'; /* replace leading slashes */
	do {
		if(cp && *cp) {
			cp = strchr(cp, '/');
			if (cp) *cp = '\0'; /* temporarily terminate the string here */
		}
		if (asprintf(&udir_name, "%s/uniondirs/%d/%s",
			 sbox_session_dir, slash_count, mod_dst_path) < 0)
				goto asprint_failed_error_out;
		SB_LOG(SB_LOGLEVEL_DEBUG,
			"prep_union_dir: mkdir(%s)", udir_name);
		mkdir_nomap_nolog(udir_name, 0700);
		free(udir_name);
		if (cp) *cp++ = '/'; /* restore the slash, if there is more */
	} while(cp);

	for (i = 0; i < num_real_dir_entries; i++) {
		const char *src_path = src_paths[i];

		SB_LOG(SB_LOGLEVEL_DEBUG,
			"prep_union_dir: src dir '%s'", src_path);

		if ( (d = opendir_nomap_nolog(src_path)) == NULL )
			continue;

		SB_LOG(SB_LOGLEVEL_DEBUG,
			"prep_union_dir: opened src dir '%s'", src_path);

		while ( (de = readdir(d)) != NULL) { /* get one dirent at a time */
			int fd;
			char *tmp_name;

			if (de->d_name[0] == '.') {
				if (de->d_name[1] == '\0') continue;
				if ((de->d_name[1] == '.') &&
				    (de->d_name[2] == '\0')) continue;
			}
			if (asprintf(&tmp_name, "%s/uniondirs/%d/%s/%s",
				sbox_session_dir, slash_count,
				mod_dst_path, de->d_name) < 0) {
					closedir(d);
					goto asprint_failed_error_out;
				}
			SB_LOG(SB_LOGLEVEL_DEBUG,
				"prep_union_dir: tmp=%s", tmp_name);

			fd = creat_nomap(tmp_name, 0644);
			SB_LOG(SB_LOGLEVEL_DEBUG,
				"prep_union_dir: fd=%d", fd);
			close(fd);

			free(tmp_name);
			count++;
		}
		closedir(d);
	}
	if (!count) goto error_out;

	if (asprintf(&result_path, "%s/uniondirs/%d/%s",
		sbox_session_dir, slash_count, mod_dst_path) < 0)
			goto asprint_failed_error_out;
	free(mod_dst_path);
	return result_path;

    asprint_failed_error_out:
	SB_LOG(SB_LOGLEVEL_ERROR, "asprintf failed to allocate memory");
    error_out:
	if(mod_dst_path) free(mod_dst_path);
	return NULL;
}

/* Make preparations for an union directory:
 * (FIXME. This is not very efficient)
 * (FIXME. Does not remove removed entries)
 * (FIXME. add description)
 *
 * Parameters:
 * 1. Path to the directory, which is presented as
 *    union of more than one real directories
 * 2. Number of real directory paths
 * 3. Array of real directory paths
 * Returns:
 * 1. Status (boolean): false if error, true if ok
 * 2. Path to the created union directory.
*/
static int lua_sb_prep_union_dir(lua_State *l)
{
	char *dst_path = NULL;
	int num_real_dir_entries;
	char **src_paths = NULL;
	char *result_path = NULL;

	int n = lua_gettop(l);
	
	if (n == 3) {
		dst_path = strdup(lua_tostring(l, 1));
		num_real_dir_entries = lua_tointeger(l, 2);

		if (dst_path && (num_real_dir_entries > 0)) {
			lua_string_table_to_strvec(l, 3, &src_paths, num_real_dir_entries);
			result_path = prep_union_dir(dst_path, src_paths, num_real_dir_entries);
			strvec_free(src_paths);
		}
	}
	lua_pushboolean(l, result_path ? 1 : 0);
	lua_pushstring(l, result_path);
	free(dst_path);
	if (result_path) free(result_path);
	return 2;
}

#if 0 /* Not used anymore. */
/* "sb.getdirlisting", to be called from lua code */
static int lua_sb_getdirlisting(lua_State *l)
{
	DIR *d;
	struct dirent *de;
	char *path;
	int count;

	int n = lua_gettop(l);
	
	if (n != 1) {
		lua_pushstring(l, NULL);
		return 1;
	}

	path = strdup(lua_tostring(l, 1));

	if ( (d = opendir(path)) == NULL ) {
		lua_pushstring(l, NULL);
		return 1;
	}
	count = 1; /* Lua indexes tables from 1 */
	lua_newtable(l); /* create a new table on the stack */
	
	while ( (de = readdir(d)) != NULL) { /* get one dirent at a time */
		lua_pushnumber(l, count);
		lua_pushstring(l, de->d_name); /* push the entries to
						* lua stack */
		lua_settable(l, -3);
		count++;
	}
	closedir(d);

	free(path);
	return 1;
}
#endif

/* "sb.log": interface from lua to the logging system.
 * Parameters:
 *  1. log level (string)
 *  2. log message (string)
*/
static int lua_sb_log(lua_State *luastate)
{
	char	*logmsg;
	char	*loglevel;
	int	n = lua_gettop(luastate);

	if (n != 2) {
		SB_LOG(SB_LOGLEVEL_DEBUG,
			"sb_log_from_lua: wrong number of params (%d)", n);
		lua_pushstring(luastate, NULL);
		return 1;
	}

	/* FIXME: is it necessary to use strdup here? */
	loglevel = strdup(lua_tostring(luastate, 1));
	logmsg = strdup(lua_tostring(luastate, 2));

	if(!strcmp(loglevel, "debug"))
		SB_LOG(SB_LOGLEVEL_DEBUG, ">> %s", logmsg);
	else if(!strcmp(loglevel, "info"))
		SB_LOG(SB_LOGLEVEL_INFO, "INFO: %s", logmsg);
	else if(!strcmp(loglevel, "warning"))
		SB_LOG(SB_LOGLEVEL_WARNING, "WARNING: %s", logmsg);
	else if(!strcmp(loglevel, "network"))
		SB_LOG(SB_LOGLEVEL_NETWORK, "NETWORK: %s", logmsg);
	else if(!strcmp(loglevel, "notice"))
		SB_LOG(SB_LOGLEVEL_NOTICE, "NOTICE: %s", logmsg);
	else if(!strcmp(loglevel, "error"))
		SB_LOG(SB_LOGLEVEL_ERROR, "ERROR: %s", logmsg);
	else if(!strcmp(loglevel, "noise"))
		SB_LOG(SB_LOGLEVEL_NOISE, ">>>>: %s", logmsg);
	else if(!strcmp(loglevel, "noise2"))
		SB_LOG(SB_LOGLEVEL_NOISE2, ">>>>>>: %s", logmsg);
	else if(!strcmp(loglevel, "noise3"))
		SB_LOG(SB_LOGLEVEL_NOISE3, ">>>>>>>>: %s", logmsg);
	else /* default to level "error"  */
		SB_LOG(SB_LOGLEVEL_ERROR, "%s", logmsg);

	free(loglevel);
	free(logmsg);

	lua_pushnumber(luastate, 1);
	return 1;
}

static int lua_sb_setenv(lua_State *luastate)
{
	int	n = lua_gettop(luastate);

	if (n != 2) {
		SB_LOG(SB_LOGLEVEL_DEBUG,
			"sb_log_from_lua: wrong number of params (%d)", n);
		lua_pushstring(luastate, NULL);
		return 1;
	}
	setenv(strdup(lua_tostring(luastate, 1)), strdup(lua_tostring(luastate, 2)), 1);
	return 1;
}

int sb_path_exists(const char *path)
{
	SB_LOG(SB_LOGLEVEL_DEBUG, "sb_path_exists testing '%s'",
		path);
#ifdef AT_FDCWD
	/* this is easy, can use faccessat() */
	if (faccessat_nomap_nolog(AT_FDCWD, path, F_OK, AT_SYMLINK_NOFOLLOW) == 0) {
		/* target exists */
		return(1);
	}
#else
	/* Not so easy. The OS does not have faccessat() [it has
	 * been added to Linux 2.6.16 - older systems don't have it]
	 * so the operation must make the check by two separate calls:
	 * First check if the target is symlink (by readlink()),
	 * next check if it is an ordinary file or directoy.
	 * N.B. the result is that this returns 'true' if a symlink
	 * exists, even if the target of the symlink does not
	 * exist. Plain access() returns 'false' if it is a symlink
	 * which points to a non-existent destination)
	 * N.B.2. we'll use readlink because lstat() can't be used, 
	 * full explanation for this can be found in paths.c.
	*/
	{
		char	link_dest[PATH_MAX+1];
		int link_len;

		link_len = readlink_nomap(path, link_dest, PATH_MAX);
		if (link_len > 0) {
			/* was a symlink */
			return(1);
		} else {
			if (access_nomap_nolog(path, F_OK) == 0)
				return(1);
		}
	}
#endif
	return(0);
}

/* "sb.path_exists", to be called from lua code
 * returns true if file, directory or symlink exists at the specified real path,
 * false if not.
*/
static int lua_sb_path_exists(lua_State *l)
{
	int n;

	n = lua_gettop(l);
	if (n != 1) {
		lua_pushboolean(l, 0);
	} else {
		char	*path = strdup(lua_tostring(l, 1));
		int	result = sb_path_exists(path);

		lua_pushboolean(l, result);
		SB_LOG(SB_LOGLEVEL_DEBUG, "lua_sb_path_exists got %d",
			result);
		free(path);
	}
	return 1;
}

/* "sb.debug_messages_enabled", to be called from lua code
 * returns true if SB_LOG messages have been enabled for the debug levels
 * (debug,noise,noise2...)
*/
static int lua_sb_debug_messages_enabled(lua_State *l)
{
	if (SB_LOG_IS_ACTIVE(SB_LOGLEVEL_DEBUG)) {
		lua_pushboolean(l, 1);
	} else {
		lua_pushboolean(l, 0);
	}
	return 1;
}

/* "sb.getcwd", to be called from lua code */
static int lua_sb_getcwd(lua_State *l)
{
	char cwd[PATH_MAX + 1];

	if (getcwd_nomap_nolog(cwd, sizeof(cwd))) {
		lua_pushstring(l, cwd);
	} else {
		lua_pushstring(l, NULL);
	}
	return 1;
}

/* "sb.get_binary_name", to be called from lua code */
static int lua_sb_get_binary_name(lua_State *l)
{
	lua_pushstring(l, sbox_binary_name);
	return 1;
}

/* "sb.get_active_exec_policy_name", to be called from lua code */
static int lua_sb_get_active_exec_policy_name(lua_State *l)
{
	lua_pushstring(l, sbox_active_exec_policy_name);
	return 1;
}

/* "sb.get_forced_mapmode", to be called from lua code */
static int lua_sb_get_forced_mapmode(lua_State *l)
{
	if (sbox_session_mode) lua_pushstring(l, sbox_session_mode);
	else lua_pushnil(l);
	return 1;
}

/* "sb.get_forced_network_mode", to be called from lua code */
static int lua_sb_get_forced_network_mode(lua_State *l)
{
	if (sbox_network_mode) lua_pushstring(l, sbox_network_mode);
	else lua_pushnil(l);
	return 1;
}

/* "sb.get_session_perm", to be called from lua code */
static int lua_sb_get_session_perm(lua_State *l)
{
	if (sbox_session_perm) lua_pushstring(l, sbox_session_perm);
	else lua_pushnil(l);
	return 1;
}

/* "sb.get_session_dir", to be called from lua code */
static int lua_sb_get_session_dir(lua_State *l)
{
	if (sbox_session_dir) lua_pushstring(l, sbox_session_dir);
	else lua_pushnil(l);
	return 1;
}

/* "sb.isprefix(a,b)", to be called from lua code
 * Return true if "a" is prefix of "b"
*/
static int lua_sb_isprefix(lua_State *l)
{
	int	n = lua_gettop(l);

	if (n != 2) {
		lua_pushboolean(l, 0);
	} else {
		const char	*str_a = lua_tostring(l, 1);
		const char	*str_b = lua_tostring(l, 2);
		int	result = 0;

		if (str_a && str_b) {
			int	prefixlen = strlen(str_a);

			if (!strncmp(str_a, str_b, prefixlen)) result = 1;
		}

		SB_LOG(SB_LOGLEVEL_DEBUG, "lua_sb_isprefix '%s','%s' => %d",
			str_a, str_b, result);

		lua_pushboolean(l, result);
	}
	return 1;
}

/* "sb.test_path_match(path, rule.dir, rule.prefix, rule.path)":
 * This is used from find_rule(); implementing this in C improves preformance.
 * Returns min.path length if a match is found, otherwise returns -1
*/
static int lua_sb_test_path_match(lua_State *l)
{
	int	n = lua_gettop(l);
	int	result = -1;

	if (n == 4) {
		const char	*str_path = lua_tostring(l, 1);
		const char	*str_rule_dir = lua_tostring(l, 2);
		const char	*str_rule_prefix = lua_tostring(l, 3);
		const char	*str_rule_path = lua_tostring(l, 4);
		const char	*match_type = "no match";

		if (str_path && str_rule_dir && (*str_rule_dir != '\0')) {
			int	prefixlen = strlen(str_rule_dir);

			/* test a directory prefix: the next char after the
			 * prefix must be '\0' or '/', unless we are accessing
			 * the root directory */
			if (!strncmp(str_path, str_rule_dir, prefixlen)) {
				if ( ((prefixlen == 1) && (*str_path=='/')) ||
				     (str_path[prefixlen] == '/') ||
				     (str_path[prefixlen] == '\0') ) {
					result = prefixlen;
					match_type = "dir";
				}
			}
		}
		if ((result == -1) && str_path
		    && str_rule_prefix && (*str_rule_prefix != '\0')) {
			int	prefixlen = strlen(str_rule_prefix);

			if (!strncmp(str_path, str_rule_prefix, prefixlen)) {
				result = prefixlen;
				match_type = "prefix";
			}
		}
		if ((result == -1) && str_path && str_rule_path) {
			int rule_path_len = strlen(str_rule_path);
			if (!strcmp(str_path, str_rule_path)) {
				result = rule_path_len;
				match_type = "path";
			} else {
				/* if "path" has a trailing slash, we may
				 * want to try again, ignoring the trailing
				 * slash. */
				int	path_len = strlen(str_path);
				if ((path_len > 2) &&
				    (str_path[path_len-1] == '/') &&
				    (path_len == (rule_path_len+1)) &&
				    !strncmp(str_path, str_rule_path, rule_path_len)) {
					result = rule_path_len;
					match_type = "path/";
				}
			}
		}
		SB_LOG(SB_LOGLEVEL_NOISE2,
			"lua_sb_test_path_match '%s','%s','%s' => %d (%s)",
			str_path, str_rule_prefix, str_rule_path,
			result, match_type);
	}
	lua_pushnumber(l, result);
	return 1;
}

/* "sb.procfs_mapping_request", to be called from lua code */
static int lua_sb_procfs_mapping_request(lua_State *l)
{
	int n;
	char *path;
	char *resolved_path;

	n = lua_gettop(l);
	if (n != 1) {
		lua_pushstring(l, NULL);
		return 1;
	}

	path = strdup(lua_tostring(l, 1));

	resolved_path = procfs_mapping_request(path);

	if (resolved_path) {
		/* mapped to somewhere else */
		lua_pushstring(l, resolved_path);
		free(resolved_path);
	} else {
		/* no need to map this path */
		lua_pushnil(l);
	}
	free(path);
	return 1;
}

int test_if_str_in_colon_separated_list_from_env(
	const char *str, const char *env_var_name)
{
	int	result = 0;	/* boolean; default result is "not found" */
	char	*list;
	char	*tok = NULL;
	char	*tok_state = NULL;

	list = getenv(env_var_name);
	if (!list) {
		SB_LOG(SB_LOGLEVEL_DEBUG, "no %s", env_var_name);
		return(0);
	}
	list = strdup(list);	/* will be modified by strtok_r */
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s is '%s'", env_var_name, list);

	tok = strtok_r(list, ":", &tok_state);
	while (tok) {
		result = !strcmp(str, tok);
		if (result) break; /* return if matched */
		tok = strtok_r(NULL, ":", &tok_state);
	}
	free(list);
	return(result);
}

/* "sb.test_if_listed_in_envvar", to be called from lua code
 * Parameters (in stack):
 *	1. string: unmapped path
 *	2. string: name of environment variable containing colon-separated list
 * Returns (in stack):
 *	1. flag (boolean): true if the path is listed in the environment
 *			   variable "SBOX_REDIRECT_IGNORE", false otherwise
 * (this is used to examine values of "SBOX_REDIRECT_IGNORE" and
 * "SBOX_REDIRECT_FORCE")
 *
 * Note: these variables typically can't be cached
 * they can be changed by the current process.
*/
static int lua_sb_test_if_listed_in_envvar(lua_State *l)
{
	int result = 0; /* boolean; default result is "false" */
	int n;
	const char *path = NULL;
	const char *env_var_name = NULL;

	n = lua_gettop(l);
	if (n != 2) {
		SB_LOG(SB_LOGLEVEL_DEBUG,
			"test_if_listed_in_envvar FAILS: lua_gettop = %d", n);
	} else {
		path = lua_tostring(l, 1);
		env_var_name = lua_tostring(l, 2);
		if (path && env_var_name) {
			result = test_if_str_in_colon_separated_list_from_env(
				path, env_var_name);
		}
	}
	lua_pushboolean(l, result);
	SB_LOG(SB_LOGLEVEL_DEBUG, "test_if_listed_in_envvar(%s) => %d",
		path, result);
	return 1;
}

/* mappings from c to lua */
static const luaL_reg reg[] =
{
	{"prep_union_dir",		lua_sb_prep_union_dir},
#if 0
	{"getdirlisting",		lua_sb_getdirlisting},
#endif
	{"readlink",			lua_sb_readlink},
#if 0
	{"decolonize_path",		lua_sb_decolonize_path},
#endif
	{"log",				lua_sb_log},
	{"setenv",			lua_sb_setenv},
	{"path_exists",			lua_sb_path_exists},
	{"debug_messages_enabled",	lua_sb_debug_messages_enabled},
	{"getcwd",			lua_sb_getcwd},
	{"get_binary_name",		lua_sb_get_binary_name},
	{"get_active_exec_policy_name",	lua_sb_get_active_exec_policy_name},
	{"get_forced_mapmode",		lua_sb_get_forced_mapmode},
	{"get_forced_network_mode",	lua_sb_get_forced_network_mode},
	{"get_session_perm",		lua_sb_get_session_perm},
	{"get_session_dir",		lua_sb_get_session_dir},
	{"isprefix",			lua_sb_isprefix},
	{"test_path_match",		lua_sb_test_path_match},
	{"test_net_addr_match",		lua_sb_test_net_addr_match},
	{"procfs_mapping_request",	lua_sb_procfs_mapping_request},
	{"test_if_listed_in_envvar",	lua_sb_test_if_listed_in_envvar},
	{NULL,				NULL}
};


static int lua_bind_sb_functions(lua_State *l)
{
	luaL_register(l, "sb", reg);
	lua_pushliteral(l,"version");
	lua_pushstring(l, "2.0" );
	lua_settable(l,-3);

	return 0;
}
