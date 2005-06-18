/*
 * Copyright (c) 2005 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_BSD_LICENSE_HEADER_START@
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * @APPLE_BSD_LICENSE_HEADER_END@
 */

#include <CoreFoundation/CoreFoundation.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fts.h>
#include <libgen.h>

#include "cfutils.h"
#include "DBPlugin.h"
#include "DBPluginPriv.h"

//////
// Public interfaces for plugins
// For more implementation, also see DBDataStore.c
//////
void DBPluginSetType(UInt32 type) {
	DBPlugin* plugin = _DBPluginGetCurrentPlugin();
	switch (type) {
		case kDBPluginBasicType:
		case kDBPluginPropertyType:
			plugin->type = type;
			break;
		default:
			// XXX: error
			break;
	}
}

void DBPluginSetName(CFStringRef name) {
	DBPlugin* plugin = _DBPluginGetCurrentPlugin();
	plugin->name = CFStringCreateCopy(NULL, name);
}

void DBPluginSetRunFunc(DBPluginRunFunc func) {
	DBPlugin* plugin = _DBPluginGetCurrentPlugin();
	plugin->run = func;
}

void DBPluginSetUsageFunc(DBPluginUsageFunc func) {
	DBPlugin* plugin = _DBPluginGetCurrentPlugin();
	plugin->usage = func;
}

void DBPluginSetDataType(CFTypeID type) {
	DBPlugin* plugin = _DBPluginGetCurrentPlugin();
	plugin->datatype = type;
}



//////
// Private interfaces to DBPlugin for use by darwinxref internals
//////

//////
// NOT THREAD SAFE
// We currently operate under the assumption that there is only
// one thread, with no plugin re-entrancy.
//////
const DBPlugin* __DBPluginCurrentPlugin;
void _DBPluginSetCurrentPlugin(const DBPlugin* plugin) {
	__DBPluginCurrentPlugin = plugin;
}
DBPlugin* _DBPluginGetCurrentPlugin() {
	return (DBPlugin*)__DBPluginCurrentPlugin;
}

CFDictionaryValueCallBacks cfDictionaryPluginValueCallBacks = {
	0, NULL, NULL, NULL, NULL
};

static CFMutableDictionaryRef plugins;

DBPlugin* _DBPluginInitialize() {
	DBPlugin* plugin = malloc(sizeof(DBPlugin));
	assert(plugin != NULL);
	memset(plugin, 0, sizeof(DBPlugin));
	plugin->type = kDBPluginNullType;
	return plugin;
}

int load_plugins(const char* plugin_path) {
	if (plugins == NULL) {
		plugins = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &cfDictionaryPluginValueCallBacks);
	}
	if (plugins == NULL) return -1;
	
	char fullpath[PATH_MAX];
	realpath(plugin_path, fullpath);
	
	const char* path_argv[] = {plugin_path, NULL};
	FTS* dir = fts_open((char * const *)path_argv, FTS_LOGICAL, NULL);
	(void)fts_read(dir);
	FTSENT* ent = fts_children (dir, FTS_NAMEONLY);
	while (ent != NULL) {
		DBPlugin* plugin = NULL;
		if (strstr(ent->fts_name, ".so")) {
			char* filename;
			asprintf(&filename, "%s/%s", fullpath, ent->fts_name);
//			fprintf(stderr, "plugin: loading %s\n", ent->fts_name);
			void* handle = dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
			if (handle) {
				DBPluginInitializeFunc func = dlsym(handle, "initialize");
				plugin = _DBPluginInitialize();
				_DBPluginSetCurrentPlugin(plugin);
				(*func)(kDBPluginCurrentVersion);	// Call out to C plugin
				// XXX: check for error?
			} else {
				fprintf(stderr, "Could not dlopen plugin: %s\n", ent->fts_name);
			}
#if HAVE_TCL_PLUGINS
		} else if (strstr(ent->fts_name, ".tcl")) {
			char* filename;
			asprintf(&filename, "%s/%s", fullpath, ent->fts_name);
			plugin = _DBPluginInitialize();
			_DBPluginSetCurrentPlugin(plugin);
			load_tcl_plugin(plugin, filename);	// Calls out to Tcl plugin
#endif
		}
		if (plugin) {
			if (plugin->name == NULL) {
				fprintf(stderr, "warning: plugin has no name (skipping): %s\n", ent->fts_name);
			} else if (plugin->type == kDBPluginNullType) {
				fprintf(stderr, "warning: plugin has no type (skipping): %s\n", ent->fts_name);
			} else {
				CFDictionarySetValue(plugins, plugin->name, plugin);
			}
		}
		ent = ent->fts_link;
	}
	fts_close(dir);
}

void print_usage(char* progname, int argc, char* argv[]) {
	progname = basename(progname);
	if (!plugins) return;

	if (argc >= 1) {
		CFStringRef name = cfstr(argv[0]);
		const DBPlugin* plugin = DBGetPluginWithName(name);
		if (plugin) {
			_DBPluginSetCurrentPlugin(plugin);
#if HAVE_TCL_PLUGINS
			CFStringRef usage = NULL;
			if ((plugin->type & kDBPluginTclType) != 0) {
				usage = call_tcl_usage((DBPlugin*)plugin);
			} else {
				usage = plugin->usage();
			}
#else
			CFStringRef usage = plugin->usage();
#endif
			cfprintf(stderr, "usage: %s [-f db] [-b build] %@ %@\n", progname, name, usage);
			CFRelease(usage);
			return;
		}
	}

	CFArrayRef pluginNames = dictionaryGetSortedKeys(plugins);
	CFIndex i, count = CFArrayGetCount(pluginNames);
	for (i = 0; i < count; ++i) {
		CFStringRef name = CFArrayGetValueAtIndex(pluginNames, i);
		const DBPlugin* plugin = DBGetPluginWithName(name);
		_DBPluginSetCurrentPlugin(plugin);
#if HAVE_TCL_PLUGINS
		CFStringRef usage = NULL;
		if ((plugin->type & kDBPluginTclType) != 0) {
			usage = call_tcl_usage((DBPlugin*)plugin);
		} else {
			usage = plugin->usage();
		}
#else
		CFStringRef usage = plugin->usage();
#endif
		cfprintf(stderr, "usage: %s [-f db] [-b build] %@ %@\n", progname, name, usage);
		CFRelease(usage);
	}
}

const DBPlugin* DBGetPluginWithName(CFStringRef name) {
	const void* plugin = CFDictionaryGetValue(plugins, name);
	return (DBPlugin*)plugin;
}

int run_plugin(int argc, char* argv[]) {
	int res = -1;
	int i;
	if (argc < 1) return -1;
	CFStringRef name = cfstr(argv[0]);
	CFMutableArrayRef args = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	for (i = 1; i < argc; ++i) {
		CFArrayAppendValue(args, cfstr(argv[i]));
	}
	const DBPlugin* plugin = DBGetPluginWithName(name);
	if (plugin) {
		_DBPluginSetCurrentPlugin(plugin);
#if HAVE_TCL_PLUGINS
		if ((plugin->type & kDBPluginTclType) != 0) {
			res = call_tcl_run((DBPlugin*)plugin, args);
		} else {
			res = plugin->run(args);
		}
#else
		res = plugin->run(args);
#endif
	} else {
		print_usage("darwinxref", argc, argv);
	}
	CFRelease(name);
	return res;
}

extern CFDictionaryRef _CFCopySystemVersionDictionary();
static CFStringRef currentBuild = NULL;

void DBSetCurrentBuild(char* build) {
	if (currentBuild) CFRelease(currentBuild);
	currentBuild = cfstr(build);
}

CFStringRef DBGetCurrentBuild() {
	if (currentBuild) return currentBuild;

	// The following is Private API.
	// Please don't use this in your programs as it may break.
	// Notice the careful dance around these symbols as they may
	// someday disappear entirely, in which case this program
	// will need to be revved.
	CFDictionaryRef (*fptr)() = dlsym(RTLD_DEFAULT, "_CFCopySystemVersionDictionary");
	if (fptr) {
		CFDictionaryRef dict = fptr();
		if (dict != NULL) {
			CFStringRef str = CFDictionaryGetValue(dict, CFSTR("ProductBuildVersion"));
			currentBuild = CFRetain(str);
			CFRelease(dict);
		}
	}
	return currentBuild;
}
