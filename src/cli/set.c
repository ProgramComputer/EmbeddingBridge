/*
 * EmbeddingBridge - Set Command Implementation
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
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include "set.h"
#include "cli.h"
#include "merge.h"
#include "../core/path_utils.h"
#include "../core/error.h"
#include "../core/debug.h"
#include "../core/store.h"
#include "colors.h"

#define SET_DIR ".eb/sets"
#define CURRENT_SET_FILE ".eb/HEAD"
#define DEFAULT_SET_NAME "main"

/* Command usage strings */
static const char* SET_USAGE = 
    "Usage: eb set [options] [<set-name>]\n"
    "\n"
    "List or create sets. When no arguments are provided, lists existing sets.\n"
    "With <set-name> argument, creates a new set.\n"
    "\n"
    "Operations:\n"
    "  eb set                   List all sets\n"
    "  eb set <set-name>        Create a new set\n"
    "  eb set -d <set-name>     Delete a set\n"
    "\n"
    "Options:\n"
    "  -h, --help               Show this help message\n"
    "  -d, --delete <set-name>  Delete a set\n"
    "  -v, --verbose            Show detailed information\n"
    "  -f, --force              Force operation (for delete)\n"
    "\n"
    "Examples:\n"
    "  eb set                   # List all sets\n"
    "  eb set my-feature        # Create a new set named \"my-feature\"\n"
    "  eb set -v                # List sets with details\n"
    "  eb set -d my-feature     # Delete a set\n"
    "\n"
    "Run 'eb switch <set-name>' to switch between sets\n"
    "Run 'eb merge <source-set>' to merge sets\n"
    "\n";

/* Subcommand handlers */
static int handle_create(int argc, char** argv);
static int handle_list(int argc, char** argv);
static int handle_switch(int argc, char** argv);
static int handle_diff(int argc, char** argv);
static int handle_delete(int argc, char** argv);
static int handle_status(int argc, char** argv);

/* Main set command dispatcher */
int cmd_set(int argc, char** argv)
{
	/* Help option */
	if ((argc >= 2) && (strcmp(argv[1], "--help") == 0 || 
	    strcmp(argv[1], "-h") == 0)) {
		printf("%s", SET_USAGE);
		return 0;
	}

	/* Parse options */
	bool verbose = false;
	bool force = false;
	bool delete_mode = false;
	const char* set_name = NULL;

	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];
		
		/* Handle options */
		if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
			verbose = true;
		}
		else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
			force = true;
		}
		else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--delete") == 0) {
			delete_mode = true;
			/* Get set name from next argument */
			if (i + 1 < argc) {
				set_name = argv[++i];
			} else {
				fprintf(stderr, "Error: -d/--delete requires a set name\n");
				return 1;
			}
		}
		/* First non-option argument is the set name (for create) */
		else if (arg[0] != '-' && set_name == NULL) {
			set_name = arg;
		}
	}

	/* Dispatch based on options and arguments */
	if (delete_mode) {
		/* Delete operation */
		if (set_name == NULL) {
			fprintf(stderr, "Error: No set name specified for delete operation\n");
			return 1;
		}
		
		eb_status_t status = set_delete(set_name, force);
		if (status != EB_SUCCESS) {
			handle_error(status, "Failed to delete set");
			return 1;
		}
		printf("Deleted set %s\n", set_name);
		return 0;
	}
	else if (set_name != NULL) {
		/* Create operation */
		eb_status_t status = set_create(set_name, NULL, NULL);
		if (status != EB_SUCCESS) {
			handle_error(status, "Failed to create set");
			return 1;
		}
		printf("Created set %s\n", set_name);
		return 0;
	}
	else {
		/* List operation (default) */
		eb_status_t status = set_list(verbose);
		if (status != EB_SUCCESS) {
			handle_error(status, "Failed to list sets");
			return 1;
		}
		return 0;
	}
}

/* Function to handle the 'set' command */
static int handle_create(int argc, char** argv)
{
	// This code is no longer used
	return 0;
}

static int handle_list(int argc, char** argv)
{
	// This code is no longer used
	return 0;
}

static int handle_switch(int argc, char** argv)
{
	// This code is no longer used
	return 0;
}

static int handle_diff(int argc, char** argv)
{
	// This code is no longer used
	return 0;
}

static int handle_delete(int argc, char** argv)
{
	// This code is no longer used
	return 0;
}

static int handle_status(int argc, char** argv)
{
	// This code is no longer used
	return 0;
}

/* Core implementation functions */

static char* get_set_dir_path(void)
{
	char* eb_root = find_repo_root(".");
	if (!eb_root)
		return NULL;
	
	char* set_dir = malloc(strlen(eb_root) + strlen(SET_DIR) + 2);
	if (!set_dir) {
		free(eb_root);
		return NULL;
	}
	
	sprintf(set_dir, "%s/%s", eb_root, SET_DIR);
	free(eb_root);
	return set_dir;
}

eb_status_t set_create(const char* name, const char* description, const char* base_set)
{
	if (!name || !*name)
		return EB_ERROR_INVALID_INPUT;

	/* Validate set name (no spaces, special chars, etc.) */
	for (const char* p = name; *p; p++) {
		if (!isalnum(*p) && *p != '-' && *p != '_' && *p != '.')
			return EB_ERROR_INVALID_INPUT;
	}

	char* set_dir = get_set_dir_path();
	if (!set_dir)
		return EB_ERROR_NOT_INITIALIZED;

	/* Create sets directory if it doesn't exist */
	mkdir(set_dir, 0755);

	/* Check if set already exists */
	char* set_path = malloc(strlen(set_dir) + strlen(name) + 2);
	if (!set_path) {
		free(set_dir);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(set_path, "%s/%s", set_dir, name);
	struct stat st;
	if (stat(set_path, &st) == 0) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_INVALID_INPUT;
	}

	/* Create set directory */
	if (mkdir(set_path, 0755) != 0) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_FILE_IO;
	}

	/* Create set config file */
	char* config_path = malloc(strlen(set_path) + 12);
	if (!config_path) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(config_path, "%s/config", set_path);
	FILE* config = fopen(config_path, "w");
	if (!config) {
		free(set_dir);
		free(set_path);
		free(config_path);
		return EB_ERROR_FILE_IO;
	}

	/* Write basic config */
	fprintf(config, "name=%s\n", name);
	fprintf(config, "created=%ld\n", (long)time(NULL));
	if (description)
		fprintf(config, "description=%s\n", description);
	if (base_set)
		fprintf(config, "base=%s\n", base_set);

	fclose(config);
	free(config_path);

	/* If this is the first set, make it the current set */
	char* current_set = malloc(100);
	if (!current_set) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	eb_status_t status = get_current_set(current_set, 100);
	if (status != EB_SUCCESS || !*current_set) {
		/* No current set, make this one current */
		status = set_switch(name);
	}
	
	free(current_set);
	free(set_dir);
	free(set_path);
	return EB_SUCCESS;
}

eb_status_t set_list(bool verbose)
{
	char* set_dir = get_set_dir_path();
	if (!set_dir)
		return EB_ERROR_NOT_INITIALIZED;

	DIR* dir = opendir(set_dir);
	if (!dir) {
		free(set_dir);
		return EB_ERROR_FILE_IO;
	}

	/* Get current set */
	char current_set[100] = {0};
	eb_status_t status = get_current_set(current_set, sizeof(current_set));
	if (status != EB_SUCCESS) {
		closedir(dir);
		free(set_dir);
		return status;
	}

	struct dirent* entry;
	bool found = false;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type != DT_DIR || 
		    strcmp(entry->d_name, ".") == 0 || 
		    strcmp(entry->d_name, "..") == 0)
			continue;

		found = true;
		
		/* Check if this is the current set */
		bool is_current = strcmp(entry->d_name, current_set) == 0;
		
		if (is_current)
			printf("* " COLOR_GREEN "%s" COLOR_RESET, entry->d_name);
		else
			printf("  %s", entry->d_name);

		if (verbose) {
			/* Read config for additional details */
			char* config_path = malloc(strlen(set_dir) + strlen(entry->d_name) + 12);
			if (!config_path)
				continue;
			
			sprintf(config_path, "%s/%s/config", set_dir, entry->d_name);
			FILE* config = fopen(config_path, "r");
			free(config_path);
			
			if (config) {
				char line[256];
				printf(" - ");
				bool printed_info = false;
				
				while (fgets(line, sizeof(line), config)) {
					/* Remove newline */
					line[strcspn(line, "\r\n")] = 0;
					
					if (strncmp(line, "created=", 8) == 0) {
						time_t created = atol(line + 8);
						char time_str[32];
						strftime(time_str, sizeof(time_str), "%Y-%m-%d", localtime(&created));
						printf(" - created %s", time_str);
					}
				}
				
				fclose(config);
				printf("\n");
			}
		}
		
		printf("\n");
	}

	closedir(dir);
	free(set_dir);

	if (!found)
		printf("No sets found. Create one with 'eb set <name>'\n");

	return EB_SUCCESS;
}

eb_status_t set_switch(const char* name)
{
	if (!name || !*name)
		return EB_ERROR_INVALID_INPUT;

	char* set_dir = get_set_dir_path();
	if (!set_dir)
		return EB_ERROR_NOT_INITIALIZED;

	/* Check if set exists */
	char* set_path = malloc(strlen(set_dir) + strlen(name) + 2);
	if (!set_path) {
		free(set_dir);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(set_path, "%s/%s", set_dir, name);
	struct stat st;
	if (stat(set_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_NOT_FOUND;
	}

	/* Get EB root path */
	char* eb_root = find_repo_root(".");
	if (!eb_root) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_NOT_INITIALIZED;
	}

	/* Update HEAD file */
	char* head_path = malloc(strlen(eb_root) + strlen(CURRENT_SET_FILE) + 2);
	if (!head_path) {
		free(eb_root);
		free(set_dir);
		free(set_path);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(head_path, "%s/%s", eb_root, CURRENT_SET_FILE);
	FILE* head = fopen(head_path, "w");
	if (!head) {
		free(eb_root);
		free(set_dir);
		free(set_path);
		free(head_path);
		return EB_ERROR_FILE_IO;
	}

	fprintf(head, "%s", name);
	fclose(head);

	free(eb_root);
	free(set_dir);
	free(set_path);
	free(head_path);
	return EB_SUCCESS;
}

eb_status_t get_current_set(char* name_out, size_t size)
{
	if (!name_out || size == 0)
		return EB_ERROR_INVALID_INPUT;

	/* Initialize with default in case of error */
	strncpy(name_out, DEFAULT_SET_NAME, size);
	name_out[size - 1] = '\0';

	char* eb_root = find_repo_root(".");
	if (!eb_root)
		return EB_ERROR_NOT_INITIALIZED;

	/* Read HEAD file */
	char* head_path = malloc(strlen(eb_root) + strlen(CURRENT_SET_FILE) + 2);
	if (!head_path) {
		free(eb_root);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(head_path, "%s/%s", eb_root, CURRENT_SET_FILE);
	FILE* head = fopen(head_path, "r");
	if (!head) {
		/* No HEAD file, create with default set */
		head = fopen(head_path, "w");
		if (head) {
			fprintf(head, "%s", DEFAULT_SET_NAME);
			fclose(head);
			
			/* Create default set if it doesn't exist */
			set_create(DEFAULT_SET_NAME, "Default set", NULL);
		}
		
		free(eb_root);
		free(head_path);
		return EB_SUCCESS;
	}

	/* Read current set from HEAD */
	if (fgets(name_out, size, head)) {
		/* Remove newline */
		name_out[strcspn(name_out, "\r\n")] = 0;
	}
	
	fclose(head);
	free(eb_root);
	free(head_path);

	/* Check if set exists, create default if not */
	char* set_dir = get_set_dir_path();
	if (set_dir) {
		char* set_path = malloc(strlen(set_dir) + strlen(name_out) + 2);
		if (set_path) {
			sprintf(set_path, "%s/%s", set_dir, name_out);
			struct stat st;
			if (stat(set_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
				/* Set doesn't exist, create default */
				strncpy(name_out, DEFAULT_SET_NAME, size);
				name_out[size - 1] = '\0';
				set_create(DEFAULT_SET_NAME, "Default set", NULL);
				set_switch(DEFAULT_SET_NAME);
			}
			free(set_path);
		}
		free(set_dir);
	}

	return EB_SUCCESS;
}

/* 
 * Placeholder implementations for future functionality 
 * These will need to be expanded as the codebase develops
 */

eb_status_t set_diff(const char* set1, const char* set2)
{
	if (!set1 || !*set1 || !set2 || !*set2)
		return EB_ERROR_INVALID_INPUT;

	/* Verify sets exist */
	char* set_dir = get_set_dir_path();
	if (!set_dir)
		return EB_ERROR_NOT_INITIALIZED;

	char* path1 = malloc(strlen(set_dir) + strlen(set1) + 2);
	if (!path1) {
		free(set_dir);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(path1, "%s/%s", set_dir, set1);
	struct stat st;
	if (stat(path1, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(set_dir);
		free(path1);
		return EB_ERROR_NOT_FOUND;
	}

	char* path2 = malloc(strlen(set_dir) + strlen(set2) + 2);
	if (!path2) {
		free(set_dir);
		free(path1);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(path2, "%s/%s", set_dir, set2);
	if (stat(path2, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(set_dir);
		free(path1);
		free(path2);
		return EB_ERROR_NOT_FOUND;
	}

	/*
	 * Actual diff implementation will be filled in later
	 * This will involve:
	 * 1. Loading embedding references from both sets
	 * 2. Identifying common and unique embeddings
	 * 3. Computing similarity for common embeddings
	 * 4. Formatting and displaying results
	 */
	printf("Set diff is not fully implemented yet.\n");
	printf("Will compare embeddings between sets %s and %s\n", set1, set2);

	free(set_dir);
	free(path1);
	free(path2);
	return EB_SUCCESS;
}

eb_status_t set_delete(const char* name, bool force)
{
	if (!name || !*name)
		return EB_ERROR_INVALID_INPUT;

	/* Check if this is the current set */
	char current_set[100] = {0};
	eb_status_t status = get_current_set(current_set, sizeof(current_set));
	if (status != EB_SUCCESS)
		return status;

	if (strcmp(current_set, name) == 0) {
		fprintf(stderr, "Error: Cannot delete the current set\n");
		return EB_ERROR_INVALID_INPUT;
	}

	/* Verify set exists */
	char* set_dir = get_set_dir_path();
	if (!set_dir)
		return EB_ERROR_NOT_INITIALIZED;

	char* set_path = malloc(strlen(set_dir) + strlen(name) + 2);
	if (!set_path) {
		free(set_dir);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(set_path, "%s/%s", set_dir, name);
	struct stat st;
	if (stat(set_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_NOT_FOUND;
	}

	/* TODO: Check if set contains unique embeddings not in other sets */
	if (!force) {
		printf("Warning: Use --force to delete without checking for unique embeddings\n");
	}

	/* Remove set directory and contents */
	/* For now, just remove the config file and directory */
	char* config_path = malloc(strlen(set_path) + 12);
	if (!config_path) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(config_path, "%s/config", set_path);
	unlink(config_path);
	free(config_path);

	/* Remove references directory if it exists */
	char* refs_path = malloc(strlen(set_path) + 12);
	if (!refs_path) {
		free(set_dir);
		free(set_path);
		return EB_ERROR_MEMORY_ALLOCATION;
	}
	
	sprintf(refs_path, "%s/refs", set_path);
	rmdir(refs_path);
	free(refs_path);

	/* Remove set directory */
	if (rmdir(set_path) != 0) {
		fprintf(stderr, "Warning: Could not fully remove set directory\n");
	}

	free(set_dir);
	free(set_path);
	return EB_SUCCESS;
}

eb_status_t set_status(void)
{
	/* Get current set */
	char current_set[100] = {0};
	eb_status_t status = get_current_set(current_set, sizeof(current_set));
	if (status != EB_SUCCESS)
		return status;

	printf(COLOR_GREEN "%s" COLOR_RESET "\n", current_set);

	return EB_SUCCESS;
} 