/*
 * EmbeddingBridge - Rollback Command Implementation
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
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "cli.h"
#include "colors.h"
#include "../core/store.h"
#include "../core/error.h"
#include "../core/embedding.h"
#include "../core/debug.h"
#include "../core/hash_utils.h"

#define MAX_LINE_LEN 2048
#define MAX_PATH_LEN PATH_MAX
#define MAX_HASH_LEN 65

static const char* ROLLBACK_USAGE =
    "Usage: eb rollback <hash> <source>\n"
    "\n"
    "Revert a source file's embedding to a previous hash.\n"
    "\n"
    "Arguments:\n"
    "  <hash>    Hash to rollback to\n"
    "  <source>  Source file to rollback\n"
    "\n"
    "Examples:\n"
    "  eb rollback eb82a9c file.txt  # Rollback file.txt to hash eb82a9c\n";

/* Find repository root directory */
static eb_status_t find_repo_root(char* root_path, size_t size) 
{
        char cwd[PATH_MAX];
        char check_path[PATH_MAX];
        
        if (!getcwd(cwd, sizeof(cwd)))
                return EB_ERROR_FILE_IO;
                
        strncpy(root_path, cwd, size);
        root_path[size - 1] = '\0';
        
        while (strlen(root_path) > 1) {
                snprintf(check_path, sizeof(check_path), "%s/.eb", root_path);
                
                if (access(check_path, F_OK) == 0)
                        return EB_SUCCESS;
                        
                char* last_slash = strrchr(root_path, '/');
                if (!last_slash)
                        break;
                *last_slash = '\0';
        }
        
        return EB_ERROR_NOT_FOUND;
}

/* Helper function to check if a hash exists in the history for a source file */
static bool hash_in_history(const char* repo_root, const char* source_file, const char* hash_to_rollback) 
{
        char history_path[PATH_MAX];
        snprintf(history_path, sizeof(history_path), "%s/.eb/history", repo_root);
        
        DEBUG_PRINT("Checking history file: %s\n", history_path);
        
        FILE* hist_file = fopen(history_path, "r");
        if (!hist_file) {
                DEBUG_PRINT("Could not open history file\n");
                return false;
        }

        // Convert absolute source path to relative path
        const char* rel_source = source_file;
        size_t root_len = strlen(repo_root);
        if (strncmp(source_file, repo_root, root_len) == 0) {
                rel_source = source_file + root_len;
                if (*rel_source == '/') rel_source++; // Skip leading slash
        }
        
        DEBUG_PRINT("Using relative source path: %s\n", rel_source);

        char* matching_hash = NULL;
        int match_count = 0;

        char line[2048];
        while (fgets(line, sizeof(line), hist_file)) {
                time_t timestamp;
                char file_path_hist[PATH_MAX];
                char hash_hist[65];
                line[strcspn(line, "\n")] = 0;
                
                DEBUG_PRINT("Reading line: %s\n", line);
                
                if (sscanf(line, "%ld %s %s", &timestamp, hash_hist, file_path_hist) == 3) {
                        DEBUG_PRINT("Comparing paths - History: %s, Source: %s\n", 
                                file_path_hist, rel_source);
                        DEBUG_PRINT("Comparing hashes - History: %s, Target: %s\n", 
                                hash_hist, hash_to_rollback);
                        
                        if (strcmp(file_path_hist, rel_source) == 0 &&
                            is_hash_prefix(hash_to_rollback, hash_hist)) {
                                match_count++;
                                if (match_count == 1) {
                                    matching_hash = strdup(hash_hist);
                                }
                        }
                }
        }

        fclose(hist_file);

        if (match_count > 1) {
                DEBUG_PRINT("Multiple matches found for hash prefix\n");
                free(matching_hash);
                return false;
        } else if (match_count == 1) {
                DEBUG_PRINT("Single match found: %s\n", matching_hash);
                // Update the hash_to_rollback to the full hash for the index update
                strncpy((char*)hash_to_rollback, matching_hash, 64);
                ((char*)hash_to_rollback)[64] = '\0';
                free(matching_hash);
                return true;
        }

        DEBUG_PRINT("No matches found\n");
        return false;
}

/* Helper function to update the index file for a specific source file and hash */
static eb_status_t update_index_entry(const char* repo_root, const char* source_file, const char* hash_to_rollback) {
        char index_path[PATH_MAX];
        snprintf(index_path, sizeof(index_path), "%s/.eb/index", repo_root);

        DEBUG_PRINT("update_index_entry: repo_root=%s, source_file=%s, hash_to_rollback=%s, index_path=%s\n",
                   repo_root, source_file, hash_to_rollback, index_path);

        // Convert absolute source path to relative path
        const char* rel_source = source_file;
        size_t root_len = strlen(repo_root);
        if (strncmp(source_file, repo_root, root_len) == 0) {
                rel_source = source_file + root_len;
                if (*rel_source == '/') rel_source++; // Skip leading slash
        }

        // Read current index content
        FILE* fp = fopen(index_path, "r");
        if (!fp) {
                return EB_ERROR_FILE_IO;
        }

        // Store all lines except the one we want to update
        char** lines = NULL;
        size_t line_count = 0;
        char line[2048];

        DEBUG_PRINT("update_index_entry: Original index content:\n");
        while (fgets(line, sizeof(line), fp)) {
                DEBUG_PRINT("%s", line);
                line[strcspn(line, "\n")] = 0;

                char file[PATH_MAX], hash[65];
                if (sscanf(line, "%s %s", hash, file) == 2) {
                        if (strcmp(file, rel_source) != 0) {
                                // Keep lines for other files
                                lines = realloc(lines, (line_count + 1) * sizeof(char*));
                                if (!lines) {
                                        fclose(fp);
                                        return EB_ERROR_MEMORY_ALLOCATION;
                                }
                                lines[line_count] = strdup(line);
                                line_count++;
                        }
                }
        }
        fclose(fp);

        // Write updated index
        fp = fopen(index_path, "w");
        if (!fp) {
                for (size_t i = 0; i < line_count; i++) {
                        free(lines[i]);
                }
                free(lines);
                return EB_ERROR_FILE_IO;
        }

        DEBUG_PRINT("update_index_entry: New index content to write:\n");

        // Write back all lines for other files
        for (size_t i = 0; i < line_count; i++) {
                fprintf(fp, "%s\n", lines[i]);
                DEBUG_PRINT("%s\n", lines[i]);
                free(lines[i]);
        }
        free(lines);

        // Write the updated entry
        fprintf(fp, "%s %s\n", hash_to_rollback, rel_source);
        DEBUG_PRINT("%s %s\n", hash_to_rollback, rel_source);

        fclose(fp);
        DEBUG_PRINT("update_index_entry: Index file updated successfully.\n");

        return EB_SUCCESS;
}

int cmd_rollback(int argc, char** argv) 
{
        if (argc != 3 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
                printf("%s", ROLLBACK_USAGE);
                return (argc != 3) ? 1 : 0;
        }

        const char* hash_to_rollback = argv[1];
        const char* source_file = argv[2];
        char repo_root[PATH_MAX];
        char abs_source_path[PATH_MAX];
        eb_status_t status;

        /* Debug output */
        DEBUG_PRINT("Rollback called with:\n  Hash: %s (length: %zu)\n  Source: %s\n",
                   hash_to_rollback, strlen(hash_to_rollback), source_file);

        /* Basic hash format validation */
        size_t hash_len = strlen(hash_to_rollback);
        if (hash_len < 7 || hash_len > 64) {
                cli_error("Invalid hash format: Hash must be between 7 and 64 characters (got %zu)", hash_len);
                return 1;
        }

        /* Find repository root */
        status = find_repo_root(repo_root, sizeof(repo_root));
        if (status != EB_SUCCESS) {
                cli_error("Not in an embedding repository");
                return 1;
        }

        /* Convert source file to absolute path */
        if (source_file[0] != '/') {
                char cwd[PATH_MAX];
                if (!getcwd(cwd, sizeof(cwd))) {
                        cli_error("Failed to get current directory");
                        return 1;
                }
                snprintf(abs_source_path, sizeof(abs_source_path), "%s/%s", cwd, source_file);
        } else {
                strncpy(abs_source_path, source_file, sizeof(abs_source_path) - 1);
                abs_source_path[sizeof(abs_source_path) - 1] = '\0';
        }

        DEBUG_PRINT("Using absolute source path: %s\n", abs_source_path);

        /* Check if hash exists in history for the source file */
        char full_hash[65];
        strncpy(full_hash, hash_to_rollback, sizeof(full_hash) - 1);
        full_hash[sizeof(full_hash) - 1] = '\0';

        if (!hash_in_history(repo_root, abs_source_path, full_hash)) {
                cli_error("Hash '%s' not found in history for source file '%s'", 
                         hash_to_rollback, source_file);
                return 1;
        }

        /* Update index file */
        status = update_index_entry(repo_root, abs_source_path, full_hash);
        if (status != EB_SUCCESS) {
                handle_error(status, "Failed to update index");
                return 1;
        }

        printf(COLOR_BOLD_GREEN "✓ Rolled back '%s' to %s" COLOR_RESET "\n", 
               source_file, get_short_hash(full_hash));
        return 0;
} 