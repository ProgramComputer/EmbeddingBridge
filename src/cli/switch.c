/*
 * EmbeddingBridge - Switch Command Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "cli.h"
#include "../core/error.h"
#include "../core/path_utils.h"

/* Command usage strings */
static const char* SWITCH_USAGE = 
    "Usage: eb switch <set-name>\n"
    "\n"
    "Switch to a different embedding set.\n"
    "\n"
    "Arguments:\n"
    "  <set-name>  Name of the set to switch to\n"
    "\n"
    "Options:\n"
    "  -h, --help  Display this help message\n"
    "\n";

/* Function declaration */
extern eb_status_t set_switch(const char* name);

/* Main switch command implementation */
int cmd_switch(int argc, char** argv)
{
	if (argc < 2 || strcmp(argv[1], "--help") == 0 || 
	    strcmp(argv[1], "-h") == 0) {
		printf("%s", SWITCH_USAGE);
		return 0;
	}

	const char* set_name = argv[1];
	
	eb_status_t status = set_switch(set_name);
	if (status != EB_SUCCESS) {
		handle_error(status, "Failed to switch set");
		return 1;
	}
	
	printf("Switched to set %s\n", set_name);
	return 0;
} 