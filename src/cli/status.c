/*
 * EmbeddingBridge - Status Command Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define _POSIX_C_SOURCE 200809L /* For strdup */
#define _XOPEN_SOURCE /* For strptime */
#define _GNU_SOURCE /* For additional features */

#include "cli.h"
#include "status.h"
#include "colors.h"
#include "../core/store.h"
#include "../core/error.h"
#include "../core/embedding.h"
#include "../core/debug.h"  // For DEBUG_PRINT
#include "../core/path_utils.h"  // For get_relative_path()
#include "../core/hash_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>  // Add this for PATH_MAX
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

// Add fallback for PATH_MAX if not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Keep existing defines but move after PATH_MAX check
#define MAX_LINE_LEN 2048
#define MAX_PATH_LEN PATH_MAX
#define MAX_HASH_LEN 65
#define MAX_META_LEN 1024

static const char *STATUS_USAGE = 
    "Usage: eb status [options] <source>\n"
    "\n" 
    TEXT_BOLD "Show embedding status and history for a source file" COLOR_RESET "\n"
    "\n"
    "Arguments:\n"
    "  <source>         Source file to check status for\n"
    "\n"
    "Options:\n"
    "  -v, --verbose    Show detailed output including timestamps and metadata\n"
    "  --help          Display this help message\n"
    "\n"
    "Examples:\n"
    "  eb status file.txt         # Show basic status\n"
    "  eb status -v file.txt      # Show detailed status with metadata\n"
    "  eb status file.txt -v      # Same as above (flexible ordering)\n";

// Forward declarations
static int show_status(char** rel_paths, size_t num_paths, const char* repo_root);
static __attribute__((unused)) char* get_metadata(const char* root, const char* hash);

// Structure to hold version info for sorting
typedef struct {
    char hash[MAX_HASH_LEN];
    time_t timestamp;
} version_info_t;

// Get metadata from object file
static char* get_metadata(const char* root, const char* hash)
{
	char meta_path[MAX_PATH_LEN];
	snprintf(meta_path, sizeof(meta_path), "%s/.eb/objects/%s.meta", root, hash);

	FILE *f = fopen(meta_path, "r");
	if (!f) {
		return strdup("");
	}

	char *metadata = malloc(MAX_META_LEN);
	if (!metadata) {
		fclose(f);
		return strdup("");
	}

	size_t pos = 0;
	char line[MAX_LINE_LEN];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		size_t len = strlen(line);
		if (pos + len + 2 >= MAX_META_LEN)
			break;

		strcpy(metadata + pos, line);
		pos += len;
		metadata[pos++] = '\n';
	}
	metadata[pos] = '\0';

	fclose(f);
	return metadata;
}

// Get history entries
static char* get_history(const char* root, const char* source) {
	char history_path[MAX_PATH_LEN];
	snprintf(history_path, sizeof(history_path), "%s/.eb/history", root);

	FILE *f = fopen(history_path, "r");
	if (!f) return NULL;

	char *history = malloc(MAX_LINE_LEN);
	if (!history) {
		fclose(f);
		return NULL;
	}

	size_t pos = 0;
	char line[MAX_LINE_LEN];
	while (fgets(line, sizeof(line), f)) {
		time_t timestamp;
		char file[MAX_PATH_LEN], hash[MAX_HASH_LEN];
		
		// Parse history line format: timestamp hash file
		if (sscanf(line, "%ld %s %s", &timestamp, hash, file) == 3 && strcmp(file, source) == 0) {
			if (pos > 0) {
				if (pos + 2 >= MAX_LINE_LEN)
					break;
				strcat(history + pos, ", ");
				pos += 2;
			}
			if (pos + strlen(hash) >= MAX_LINE_LEN)
				break;
			strcpy(history + pos, hash);
			pos += strlen(hash);
		}
	}
	history[pos] = '\0';
	fclose(f);
	return history;
}

static void print_status(const char* root, const char* current, const char* history, bool verbose)
{
	if (verbose) {
		// Header
		printf(COLOR_BOLD_GREEN "→ Current Embedding" COLOR_RESET "\n");
		printf("  Hash: %s\n", current);
		// Metadata section
		char *metadata = get_metadata(root, current);
		if (metadata && strlen(metadata) > 0) {
			printf("  Metadata:\n    %s\n", metadata);
		}
		free(metadata);
		// History section
		printf(COLOR_BOLD_GREEN "\n→ Version History" COLOR_RESET "\n");
		char *history_copy = strdup(history);
		if (!history_copy) {
			return;
		}
		char *token = strtok(history_copy, ", ");
		int version = 1;
		while (token) {
			printf("  %d. %s", version++, token);
			char *hash_meta = get_metadata(root, token);
			if (hash_meta && strlen(hash_meta) > 0) {
				printf("\n     %s", hash_meta);
			}
			printf("\n");
			free(hash_meta);
			token = strtok(NULL, ", ");
		}
		free(history_copy);
	} else {
		// Simple, clean output for basic use
		printf(COLOR_BOLD_GREEN "→ " COLOR_RESET);
		printf("Current: %.7s\n", current);
		
		// Show shortened history hashes
		printf("History: ");
		char *history_copy = strdup(history);
		if (history_copy) {
			char *token = strtok(history_copy, ", ");
			bool first = true;
			while (token) {
				if (!first) printf(", ");
				printf("%.7s", token);
				first = false;
				token = strtok(NULL, ", ");
			}
			printf("\n");
			free(history_copy);
		}
	}
}

// Implementation of the previously implicit show_status function
static int show_status(char** rel_paths, size_t num_paths, const char* repo_root)
{
	if (!rel_paths || !repo_root) {
		return 1;
	}

	for (size_t i = 0; i < num_paths; i++) {
		char hash[65];  // Buffer for hash
		eb_status_t status = get_current_hash(repo_root, rel_paths[i], hash, sizeof(hash));
		
		if (status != EB_SUCCESS) {
			fprintf(stderr, "No embedding found for %s\n", rel_paths[i]);
			continue;
		}

		char* history = get_history(repo_root, rel_paths[i]);
		print_status(repo_root, hash, history ? history : "", has_option(num_paths, rel_paths, "-v") || has_option(num_paths, rel_paths, "--verbose"));
		
		free(history);
	}

	return 0;
}

int cmd_status(int argc, char* argv[])
{
	if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
		printf("%s", STATUS_USAGE);
		return (argc < 2) ? 1 : 0;
	}
	
	// Find the source argument
	const char *source = NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			source = argv[i];
			break;
		}
	}

	if (!source) {
		cli_error("No source file specified");
		return 1;
	}

	// Get absolute path of source file
	char source_abs[PATH_MAX];
	if (!realpath(source, source_abs)) {
		fprintf(stderr, "Error: Cannot resolve path: %s\n", source);
		return 1;
	}

	// Get repository root from source path directly
	char *repo_root = find_repo_root(source_abs);
	if (!repo_root) {
		fprintf(stderr, "Error: Not in an eb repository\n");
		return 1;
	}

	// Get relative path using resolved paths
	char *rel_path = get_relative_path(source_abs, repo_root);
	if (!rel_path) {
		fprintf(stderr, "Error: File must be within repository\n");
		free(repo_root);
		return 1;
	}

	// Now check if file exists
	struct stat st;
	if (stat(source_abs, &st) != 0) {
		fprintf(stderr, "No embeddings found for %s\n", rel_path);
		free(rel_path);
		free(repo_root);
		return 1;
	}

	// Create paths array
	char **rel_paths = malloc(sizeof(char*));
	if (!rel_paths) {
		fprintf(stderr, "Error: Memory allocation failed\n");
		free(rel_path);
		free(repo_root);
		return 1;
	}
	rel_paths[0] = rel_path;

	// Show status
	int ret = show_status(rel_paths, 1, repo_root);

	// Cleanup
	free(rel_paths[0]);
	free(rel_paths);
	free(repo_root);

	return ret;
} 