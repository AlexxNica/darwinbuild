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

#include <stdio.h>
#include <stdlib.h>

extern int load_plugins(const char* plugin_path);
extern int run_plugin(void* session, int argc, char* argv[]);
extern void* DBDataStoreInitialize(const char* datafile);
extern void DBSetCurrentBuild(void* session, char* build);

// getopt globals
char* optarg;
int optind;
int optopt;
int opterr;
int optreset;

// user environment global
extern char** environ;


int main(int argc, char* argv[]) {
	char* progname = argv[0];
	char* dbfile = "/var/tmp/darwinxref.db";
	char* build = getenv("DARWINBUILD_BUILD");

	int ch;
	while ((ch = getopt(argc, argv, "f:b:")) != -1) {
		switch (ch) {
		case 'f':
			dbfile = optarg;
			break;
		case 'b':
			build = optarg;
			break;
		case '?':
		default:
			print_usage(progname, argc, argv);
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	void* session = DBDataStoreInitialize(dbfile);
	DBSetCurrentBuild(session, build);
	load_plugins("plugins");
	if (run_plugin(session, argc, argv) == -1) {
		print_usage(progname, argc, argv);
		exit(1);
	}
	return 0;
}

