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
#include <linux/limits.h>  // For PATH_MAX on Linux
#include <ctype.h>  // For isdigit
#include "cli.h"
#include "colors.h"
#include "../core/store.h"
#include "../core/error.h"
#include "../core/embedding.h"
#include "../core/debug.h"
#include "../core/hash_utils.h"
#include "../core/path_utils.h"

/* Function declarations */
void cli_info(const char* format, ...);

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_LINE_LEN 2048
#define MAX_PATH_LEN PATH_MAX
#define MAX_HASH_LEN 65

static const char* ROLLBACK_USAGE =
    "Usage: embr rollback [options] <hash> <source>\n"
    "\n"
    "Revert a source file's embedding to a previous hash.\n"
    "\n"
    "Arguments:\n"
    "  <hash>    Hash to rollback to\n"
    "  <source>  Source file to rollback\n"
    "\n"
    "Options:\n"
    "  --model <model>  Specify model to rollback (required for multi-model repos)\n"
    "\n"
    "Examples:\n"
    "  embr rollback eb82a9c file.txt                # Rollback file.txt to hash eb82a9c\n"
    "  embr rollback --model openai-3 eb82a9c file.txt  # Rollback OpenAI embedding to hash eb82a9c\n"
    "  embr rollback --model voyage-2 4639f61 file.txt  # Rollback Voyage embedding to hash 4639f61\n";

/* Find repository root directory */
static eb_status_t find_repo_root_path(char* root_path, size_t size) 
{
        char cwd[PATH_MAX];
        char check_path[PATH_MAX];
        
        if (!getcwd(cwd, sizeof(cwd)))
                return EB_ERROR_FILE_IO;
                
        strncpy(root_path, cwd, size);
        root_path[size - 1] = '\0';
        
        while (strlen(root_path) > 1) {
                snprintf(check_path, sizeof(check_path), "%s/.embr", root_path);
                
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
static bool hash_in_history(const char* repo_root, const char* source_file, const char* hash_to_rollback, const char* model) 
{
        char* log_path = get_current_set_log_path();
        if (!log_path) {
                DEBUG_PRINT("Could not get per-set log path\n");
                return false;
        }
        DEBUG_PRINT("Checking log file: %s\n", log_path);
        DEBUG_PRINT("Looking for hash: %s, source: %s, model: %s\n", 
                   hash_to_rollback, source_file, model ? model : "(default)");
        FILE* hist_file = fopen(log_path, "r");
        free(log_path);
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
                char file_path_hist[PATH_MAX];
                char hash_hist[65];
                char timestamp_str[32];
                char model_hist[128] = "";
                line[strcspn(line, "\n")] = 0;
                
                DEBUG_PRINT("Reading line: %s\n", line);
                
                // Try to parse with the format: timestamp hash source model
                // Format: [timestamp] [hash] [source_file] [model]
                int parsed = sscanf(line, "%s %s %s %s", timestamp_str, hash_hist, file_path_hist, model_hist);
                
                // If we have a model specified and the line has a model
                if (model && parsed == 4) {
                    DEBUG_PRINT("Line has model: %s\n", model_hist);
                    
                    // Skip if models don't match
                    if (strcmp(model, model_hist) != 0) {
                        DEBUG_PRINT("Models don't match: %s != %s\n", model, model_hist);
                        continue;
                    }
                }
                
                // If we parsed at least timestamp, hash, and source
                if (parsed >= 3) {
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
static eb_status_t update_index_entry(const char* repo_root, const char* source_file, const char* hash_to_rollback, const char* model) {
        char* index_path = get_current_set_index_path();
        if (!index_path) {
                DEBUG_PRINT("Could not get per-set index path\n");
                return EB_ERROR_FILE_IO;
        }
        DEBUG_PRINT("update_index_entry: repo_root=%s, source_file=%s, hash_to_rollback=%s, index_path=%s, model=%s\n",
                   repo_root, source_file, hash_to_rollback, index_path, model ? model : "(null)");

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
                free(index_path);
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
                        // If specific model is provided, check if this hash is for a different model
                        bool should_keep = false;
                        
                        if (strcmp(file, rel_source) != 0) {
                                // Different file, always keep
                                should_keep = true;
                        } else if (model) {
                                // Same file but need to check if it's for a different model
                                char meta_path[PATH_MAX];
                                snprintf(meta_path, sizeof(meta_path), "%s/.embr/objects/%s.meta", repo_root, hash);
                                
                                FILE* meta_fp = fopen(meta_path, "r");
                                if (meta_fp) {
                                        char meta_line[1024];
                                        bool found_model = false;
                                        
                                        while (fgets(meta_line, sizeof(meta_line), meta_fp)) {
                                                if (strncmp(meta_line, "model=", 6) == 0) {
                                                        char hash_model[32];
                                                        sscanf(meta_line + 6, "%s", hash_model);
                                                        
                                                        // Keep if models are different
                                                        if (strcmp(hash_model, model) != 0) {
                                                                should_keep = true;
                                                                DEBUG_PRINT("update_index_entry: Keeping entry for different model: %s\n", hash_model);
                                                        }
                                                        found_model = true;
                                                        break;
                                                }
                                        }
                                        fclose(meta_fp);
                                        
                                        // If we didn't find a model, keep it to be safe
                                        if (!found_model) {
                                                should_keep = true;
                                        }
                                } else {
                                        // Couldn't open metadata, keep to be safe
                                        should_keep = true;
                                }
                        }
                        
                        if (should_keep) {
                                lines = realloc(lines, (line_count + 1) * sizeof(char*));
                                if (!lines) {
                                        fclose(fp);
                                        free(index_path);
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
                free(index_path);
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

        free(index_path);
        return EB_SUCCESS;
}

/* Check if a file has embeddings from multiple models */
static bool has_multiple_models_for_file(const char* repo_root, const char* file_path) 
{
        char* log_path = get_current_set_log_path();
        if (!log_path) return false;
        FILE* fp = fopen(log_path, "r");
        free(log_path);
        if (!fp) return false;
        
        char line[1024];
        int model_count = 0;
        char models[10][64] = {0}; // Store up to 10 different models
        
        while (fgets(line, sizeof(line), fp)) {
                // Remove newline
                size_t len = strlen(line);
                if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                
                // Parse line to extract file path and model
                char* line_file_path = NULL;
                char* model = NULL;
                
                // Check for newer format (timestamp hash file model)
                char* token = strtok(line, " ");
                if (token && (isdigit(token[0]) || token[0] == '-')) {
                        // Skip timestamp and hash
                        token = strtok(NULL, " "); // hash
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // file path
                        if (!token) continue;
                        line_file_path = token;
                        
                        token = strtok(NULL, " "); // model
                        if (!token) continue;
                        model = token;
                } else {
                        // Older format (file hash timestamp model)
                        line_file_path = token;
                        
                        // Skip hash and timestamp
                        token = strtok(NULL, " "); // hash
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // timestamp
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // model
                        if (!token) continue;
                        model = token;
                }
                
                // Check if this is for our file
                if (line_file_path && strcmp(line_file_path, file_path) == 0) {
                        // Check if we've seen this model before
                        bool found = false;
                        for (int i = 0; i < model_count; i++) {
                                if (strcmp(models[i], model) == 0) {
                                        found = true;
                                        break;
                                }
                        }
                        
                        // If not, add it to our list
                        if (!found && model_count < 10) {
                                strncpy(models[model_count], model, 63);
                                models[model_count][63] = '\0';
                                model_count++;
                        }
                }
        }
        
        fclose(fp);
        return model_count > 1;
}

/* Get a comma-separated list of available models for a file */
static char* get_available_models_for_file(const char* repo_root, const char* file_path) 
{
        static char model_list[512] = {0};
        model_list[0] = '\0';
        char* log_path = get_current_set_log_path();
        if (!log_path) return model_list;
        FILE* fp = fopen(log_path, "r");
        free(log_path);
        if (!fp) return model_list;
        
        char line[1024];
        int model_count = 0;
        char models[10][64] = {0}; // Store up to 10 different models
        
        while (fgets(line, sizeof(line), fp)) {
                // Remove newline
                size_t len = strlen(line);
                if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                
                // Parse line to extract file path and model
                char* line_file_path = NULL;
                char* model = NULL;
                
                // Check for newer format (timestamp hash file model)
                char* token = strtok(line, " ");
                if (token && (isdigit(token[0]) || token[0] == '-')) {
                        // Skip timestamp and hash
                        token = strtok(NULL, " "); // hash
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // file path
                        if (!token) continue;
                        line_file_path = token;
                        
                        token = strtok(NULL, " "); // model
                        if (!token) continue;
                        model = token;
                } else {
                        // Older format (file hash timestamp model)
                        line_file_path = token;
                        
                        // Skip hash and timestamp
                        token = strtok(NULL, " "); // hash
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // timestamp
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // model
                        if (!token) continue;
                        model = token;
                }
                
                // Check if this is for our file
                if (line_file_path && strcmp(line_file_path, file_path) == 0) {
                        // Check if we've seen this model before
                        bool found = false;
                        for (int i = 0; i < model_count; i++) {
                                if (strcmp(models[i], model) == 0) {
                                        found = true;
                                        break;
                                }
                        }
                        
                        // If not, add it to our list
                        if (!found && model_count < 10) {
                                strncpy(models[model_count], model, 63);
                                models[model_count][63] = '\0';
                                model_count++;
                        }
                }
        }
        
        fclose(fp);
        
        // Build comma-separated list
        for (int i = 0; i < model_count; i++) {
                if (i > 0) strcat(model_list, ", ");
                strcat(model_list, models[i]);
        }
        
        return model_list;
}

/* Get the default model for a file (the only model, or NULL if multiple) */
static const char* get_default_model_for_file(const char* repo_root, const char* file_path) 
{
        static char default_model[64] = {0};
        default_model[0] = '\0';
        char* log_path = get_current_set_log_path();
        if (!log_path) return NULL;
        FILE* fp = fopen(log_path, "r");
        free(log_path);
        if (!fp) return NULL;
        
        char line[1024];
        int model_count = 0;
        
        while (fgets(line, sizeof(line), fp)) {
                // Remove newline
                size_t len = strlen(line);
                if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                
                // Parse line to extract file path and model
                char* line_file_path = NULL;
                char* model = NULL;
                
                // Check for newer format (timestamp hash file model)
                char* token = strtok(line, " ");
                if (token && (isdigit(token[0]) || token[0] == '-')) {
                        // Skip timestamp and hash
                        token = strtok(NULL, " "); // hash
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // file path
                        if (!token) continue;
                        line_file_path = token;
                        
                        token = strtok(NULL, " "); // model
                        if (!token) continue;
                        model = token;
                } else {
                        // Older format (file hash timestamp model)
                        line_file_path = token;
                        
                        // Skip hash and timestamp
                        token = strtok(NULL, " "); // hash
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // timestamp
                        if (!token) continue;
                        
                        token = strtok(NULL, " "); // model
                        if (!token) continue;
                        model = token;
                }
                
                // Check if this is for our file
                if (line_file_path && strcmp(line_file_path, file_path) == 0) {
                        // If we already found a different model, return NULL (multiple models)
                        if (model_count > 0 && strcmp(default_model, model) != 0) {
                                fclose(fp);
                                return NULL;
                        }
                        
                        // Store this model
                        strncpy(default_model, model, 63);
                        default_model[63] = '\0';
                        model_count++;
                }
        }
        
        fclose(fp);
        
        // Return the model if we found exactly one
        return model_count == 1 ? default_model : NULL;
}

static eb_status_t update_head_file(const char* repo_root, const char* source_file, 
                                   const char* hash_to_rollback, const char* model) {
        char head_path[PATH_MAX];
        char temp_path[PATH_MAX];
        FILE *head_fp, *temp_fp;
        char line[MAX_LINE_LEN];
        bool found = false;
        
        DEBUG_PRINT("update_head_file: Starting with repo_root=%s, source=%s, hash=%s, model=%s\n",
                   repo_root, source_file, hash_to_rollback, model);
        
        /* Convert absolute path to relative if needed */
        const char* rel_source = source_file;
        size_t root_len = strlen(repo_root);
        if (strncmp(source_file, repo_root, root_len) == 0) {
                rel_source = source_file + root_len;
                if (*rel_source == '/')
                        rel_source++; /* Skip leading slash */
        }
        
        /* Build paths */
        snprintf(head_path, sizeof(head_path), "%s/.embr/HEAD", repo_root);
        snprintf(temp_path, sizeof(temp_path), "%s/.embr/HEAD.tmp", repo_root);
        
        /* Open HEAD file for reading */
        head_fp = fopen(head_path, "r");
        if (!head_fp) {
                /* If HEAD doesn't exist, create it */
                head_fp = fopen(head_path, "w");
                if (!head_fp) {
                        DEBUG_PRINT("update_head_file: Failed to create HEAD file\n");
                        return EB_ERROR_FILE_IO;
                }
                fprintf(head_fp, "ref: %s %s\n", model, hash_to_rollback);
                fclose(head_fp);
                return EB_SUCCESS;
        }
        
        /* Create temp file for writing */
        temp_fp = fopen(temp_path, "w");
        if (!temp_fp) {
                fclose(head_fp);
                DEBUG_PRINT("update_head_file: Failed to create temp file\n");
                return EB_ERROR_FILE_IO;
        }
        
        /* Read HEAD and update/add entry */
        while (fgets(line, sizeof(line), head_fp)) {
                char curr_model[128];
                char curr_hash[65];
                
                /* Remove newline */
                line[strcspn(line, "\n")] = 0;
                
                if (sscanf(line, "ref: %s %s", curr_model, curr_hash) == 2) {
                        if (strcmp(curr_model, model) == 0) {
                                /* Update existing model reference */
                                fprintf(temp_fp, "ref: %s %s\n", model, hash_to_rollback);
                                found = true;
                        } else {
                                /* Keep other model references unchanged */
                                fprintf(temp_fp, "%s\n", line);
                        }
                } else {
                        /* Keep any other lines unchanged */
                        fprintf(temp_fp, "%s\n", line);
                }
        }
        
        /* Add new model reference if not found */
        if (!found) {
                fprintf(temp_fp, "ref: %s %s\n", model, hash_to_rollback);
        }
        
        /* Close files */
        fclose(head_fp);
        fclose(temp_fp);
        
        /* Replace original HEAD with temp file */
        if (rename(temp_path, head_path) != 0) {
                DEBUG_PRINT("update_head_file: Failed to replace HEAD file\n");
                return EB_ERROR_FILE_IO;
        }
        
        DEBUG_PRINT("update_head_file: Successfully updated HEAD file\n");
        return EB_SUCCESS;
}

/* Get current hash for a specific model from HEAD */
static eb_status_t get_head_hash(const char* repo_root, const char* model, char* hash_out, size_t hash_size) {
        char head_path[PATH_MAX];
        FILE* head_fp;
        char line[MAX_LINE_LEN];
        
        snprintf(head_path, sizeof(head_path), "%s/.embr/HEAD", repo_root);
        
        head_fp = fopen(head_path, "r");
        if (!head_fp) {
                DEBUG_PRINT("get_head_hash: HEAD file not found\n");
                return EB_ERROR_NOT_FOUND;
        }
        
        while (fgets(line, sizeof(line), head_fp)) {
                char curr_model[128];
                char curr_hash[65];
                
                line[strcspn(line, "\n")] = 0;
                
                if (sscanf(line, "ref: %s %s", curr_model, curr_hash) == 2) {
                        if (strcmp(curr_model, model) == 0) {
                                strncpy(hash_out, curr_hash, hash_size - 1);
                                hash_out[hash_size - 1] = '\0';
                                fclose(head_fp);
                                return EB_SUCCESS;
                        }
                }
        }
        
        fclose(head_fp);
        return EB_ERROR_NOT_FOUND;
}

/* Find the complete hash from a partial hash */
static eb_status_t resolve_hash(
    const char* repo_root,
    const char* partial_hash,
    const char* source_file, 
    const char* model,
    char* full_hash_out,
    size_t hash_size
) {
    char* log_path = get_current_set_log_path();
    if (!log_path) {
        DEBUG_PRINT("resolve_hash: Could not get per-set log path\n");
        return EB_ERROR_FILE_IO;
    }
    DEBUG_PRINT("resolve_hash: Log path: %s\n", log_path);
    char* index_path = get_current_set_index_path();
    if (!index_path) {
        free(log_path);
        DEBUG_PRINT("resolve_hash: Could not get per-set index path\n");
        return EB_ERROR_FILE_IO;
    }
    char full_matched_hash[65] = {0};
    bool single_match_found = false;
    int match_count = 0;
    DEBUG_PRINT("resolve_hash: Starting with repo_root=%s, partial_hash=%s, model=%s\n", 
               repo_root, partial_hash, model ? model : "(none)");
    // Convert absolute source path to relative path
    const char* rel_source = source_file;
    size_t root_len = strlen(repo_root);
    if (strncmp(source_file, repo_root, root_len) == 0) {
        rel_source = source_file + root_len;
        if (*rel_source == '/') rel_source++; // Skip leading slash
    }
    DEBUG_PRINT("resolve_hash: Using relative source path: %s\n", rel_source);
    FILE* hist_file = fopen(log_path, "r");
    free(log_path);
    if (!hist_file) {
        free(index_path);
        DEBUG_PRINT("resolve_hash: Could not open log file\n");
        return EB_ERROR_FILE_IO;
    }
    DEBUG_PRINT("resolve_hash: Successfully opened history file\n");
    char line[2048];
    while (fgets(line, sizeof(line), hist_file)) {
        char file_path_hist[PATH_MAX];
        char hash_hist[65];
        char timestamp_str[32];
        char model_hist[128] = "";
        line[strcspn(line, "\n")] = 0;
        
        DEBUG_PRINT("resolve_hash: Processing line: %s\n", line);
        
        // Try to parse with the format: timestamp hash source model
        int parsed = sscanf(line, "%s %s %s %s", timestamp_str, hash_hist, file_path_hist, model_hist);
        
        DEBUG_PRINT("resolve_hash: Parsed %d fields: timestamp=%s, hash=%s, file=%s, model=%s\n", 
                  parsed, timestamp_str, hash_hist, file_path_hist, 
                  parsed == 4 ? model_hist : "(none)");
        
        // If we have a model specified and the line has a model
        if (model && parsed == 4) {
            DEBUG_PRINT("resolve_hash: Comparing models: specified [%s] vs. history [%s]\n", 
                      model, model_hist);
            
            // Skip if models don't match
            if (strcmp(model, model_hist) != 0) {
                DEBUG_PRINT("resolve_hash: Models don't match, skipping\n");
                continue;
            }
        }
        
        // If we parsed at least timestamp, hash, and source
        if (parsed >= 3) {
            DEBUG_PRINT("resolve_hash: Comparing files: [%s] vs. [%s]\n", file_path_hist, rel_source);
            DEBUG_PRINT("resolve_hash: Checking if [%s] is a prefix of [%s]\n", partial_hash, hash_hist);
            
            if (strcmp(file_path_hist, rel_source) == 0) {
                DEBUG_PRINT("resolve_hash: File paths match\n");
                
                // Check if the partial hash is a prefix of the full hash
                size_t prefix_len = strlen(partial_hash);
                if (strncmp(partial_hash, hash_hist, prefix_len) == 0) {
                    DEBUG_PRINT("resolve_hash: Hash prefix matches! partial=%s, full=%s\n", 
                              partial_hash, hash_hist);
                    match_count++;
                    if (match_count == 1) {
                        strncpy(full_matched_hash, hash_hist, sizeof(full_matched_hash) - 1);
                        full_matched_hash[sizeof(full_matched_hash) - 1] = '\0';
                        DEBUG_PRINT("resolve_hash: Stored first match: %s\n", full_matched_hash);
                    }
                } else {
                    DEBUG_PRINT("resolve_hash: Hash prefix does not match\n");
                }
            } else {
                DEBUG_PRINT("resolve_hash: File paths don't match\n");
            }
        }
    }
    
    fclose(hist_file);
    free(index_path);
    
    DEBUG_PRINT("resolve_hash: Found %d matches\n", match_count);
    
    if (match_count > 1) {
        DEBUG_PRINT("resolve_hash: Multiple matches found for hash prefix\n");
        return EB_ERROR_HASH_AMBIGUOUS;
    } else if (match_count == 1) {
        single_match_found = true;
        DEBUG_PRINT("resolve_hash: Single match found: %s\n", full_matched_hash);
    } else {
        DEBUG_PRINT("resolve_hash: No matches found\n");
        return EB_ERROR_NOT_FOUND;
    }

    /* At end of function, update the full hash with matched hash */
    if (single_match_found) {
        DEBUG_PRINT("resolve_hash: Returning full hash: %s\n", full_matched_hash);
        
        if (full_hash_out && hash_size > 0) {
            strncpy(full_hash_out, full_matched_hash, hash_size - 1);
            full_hash_out[hash_size - 1] = '\0';
            DEBUG_PRINT("resolve_hash: Copied hash to output buffer: %s\n", full_hash_out);
        }
        
        return EB_SUCCESS;
    }
    
    return EB_ERROR_NOT_FOUND;
}

/* Main rollback command handler */
int cmd_rollback(int argc, char** argv) {
    char repo_root[PATH_MAX];
    char abs_source_path[PATH_MAX];
    char full_hash[65] = {0};
    const char* model = NULL;
    const char* hash = NULL;
    const char* source = NULL;
    eb_status_t status;
    
    // Parse command line arguments
    DEBUG_PRINT("cmd_rollback: Starting with %d arguments\n", argc);
    
    // Skip the command name (argv[0] is "rollback")
    for (int i = 1; i < argc; i++) {
        DEBUG_PRINT("cmd_rollback: argv[%d] = '%s'\n", i, argv[i]);
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model = argv[++i];
            DEBUG_PRINT("cmd_rollback: Found model argument: %s\n", model);
        } else if (hash == NULL) {
            hash = argv[i];
            DEBUG_PRINT("cmd_rollback: Found hash argument: %s\n", hash);
        } else if (source == NULL) {
            source = argv[i];
            DEBUG_PRINT("cmd_rollback: Found source argument: %s\n", source);
        }
    }
    
    // Check if required arguments are provided
    if (!hash || !source) {
        fprintf(stderr, "Error: Missing required arguments.\n");
        fprintf(stderr, "%s", ROLLBACK_USAGE);
        DEBUG_PRINT("cmd_rollback: Missing required arguments, hash=%p, source=%p\n", 
                  (void*)hash, (void*)source);
        return 1;
    }
    
    DEBUG_PRINT("cmd_rollback: Using hash=%s, source=%s, model=%s\n", 
               hash, source, model ? model : "(none)");
    
    // Find repository root
    status = find_repo_root_path(repo_root, sizeof(repo_root));
    if (status != EB_SUCCESS) {
        fprintf(stderr, "Error: Not in an embedding-bridge repository.\n");
        DEBUG_PRINT("cmd_rollback: Failed to find repo root, status=%d\n", status);
        return 1;
    }
    
    DEBUG_PRINT("cmd_rollback: Found repository root: %s\n", repo_root);
    
    // Get absolute path for source file
    if (!realpath(source, abs_source_path)) {
        fprintf(stderr, "Error: Could not resolve path for %s.\n", source);
        DEBUG_PRINT("cmd_rollback: Failed to resolve path for %s\n", source);
        return 1;
    }
    
    DEBUG_PRINT("cmd_rollback: Resolved absolute source path: %s\n", abs_source_path);
    
    // Resolve partial hash to full hash
    DEBUG_PRINT("cmd_rollback: Calling resolve_hash with hash=%s\n", hash);
    status = resolve_hash(repo_root, hash, abs_source_path, model, full_hash, sizeof(full_hash));
    
    /* After successful hash resolution, update the index and HEAD file */
    if (status == EB_SUCCESS) {
        DEBUG_PRINT("cmd_rollback: resolve_hash succeeded, full_hash=%s\n", full_hash);
        
        /* Update the index first */
        status = update_index_entry(repo_root, abs_source_path, full_hash, model);
        
        if (status == EB_SUCCESS) {
            DEBUG_PRINT("cmd_rollback: update_index_entry succeeded\n");
            
            /* Update the model ref file to ensure consistency */
            if (model) {
                char* model_refs_dir = get_current_set_model_refs_dir();
                if (!model_refs_dir) {
                    DEBUG_PRINT("cmd_rollback: Failed to get model refs dir for current set\n");
                    fprintf(stderr, "Warning: Failed to update model reference file\n");
                } else {
                    char model_ref_path[PATH_MAX];
                    snprintf(model_ref_path, sizeof(model_ref_path), "%s/%s", model_refs_dir, model);
                    free(model_refs_dir);
                    
                    DEBUG_PRINT("cmd_rollback: Updating model ref file: %s\n", model_ref_path);
                    
                    // Get relative source path for model ref file
                    char rel_source_path[PATH_MAX] = {0};
                    size_t repo_root_len = strlen(repo_root);
                    if (strncmp(abs_source_path, repo_root, repo_root_len) == 0) {
                        const char* rel_src = abs_source_path + repo_root_len;
                        if (rel_src[0] == '/') {
                            rel_src++; // Skip leading slash
                        }
                        strncpy(rel_source_path, rel_src, sizeof(rel_source_path) - 1);
                    } else {
                        strncpy(rel_source_path, abs_source_path, sizeof(rel_source_path) - 1);
                    }
                    
                    // Read existing model references first
                    FILE* model_read_fp = fopen(model_ref_path, "r");
                    char** existing_lines = NULL;
                    size_t line_count = 0;
                    
                    if (model_read_fp) {
                        char line[PATH_MAX + 65]; // Hash (64) + space + path + null terminator
                        while (fgets(line, sizeof(line), model_read_fp)) {
                            // Skip lines with same source file - comparing to relative path
                            char file_path[PATH_MAX];
                            char file_hash[65];
                            
                            // Remove newline if present
                            line[strcspn(line, "\n")] = 0;
                            
                            if (sscanf(line, "%s %s", file_hash, file_path) == 2) {
                                // Need to check if relative path == rel_source_path
                                if (strcmp(file_path, rel_source_path) != 0) {
                                    // Keep lines that don't match this source file
                                    existing_lines = realloc(existing_lines, (line_count + 1) * sizeof(char*));
                                    if (existing_lines)
                                        existing_lines[line_count++] = strdup(line);
                                }
                            }
                        }
                        fclose(model_read_fp);
                    }
                    
                    // Write updated model reference file
                    FILE* model_fp = fopen(model_ref_path, "w");
                    if (!model_fp) {
                        DEBUG_PRINT("cmd_rollback: Warning: Failed to create model reference file for %s\n", model);
                        fprintf(stderr, "Warning: Failed to update model reference file\n");
                        
                        // Free allocated memory for existing lines
                        for (size_t i = 0; i < line_count; i++) {
                            free(existing_lines[i]);
                        }
                        free(existing_lines);
                        
                        /* Continue anyway since index was updated */
                    } else {
                        // Write back existing lines first
                        for (size_t i = 0; i < line_count; i++) {
                            fprintf(model_fp, "%s\n", existing_lines[i]);
                            free(existing_lines[i]);
                        }
                        free(existing_lines);
                        
                        // Add the new file entry with relative path
                        fprintf(model_fp, "%s %s\n", full_hash, rel_source_path);
                        
                        fclose(model_fp);
                        DEBUG_PRINT("cmd_rollback: Successfully updated model ref file for %s\n", model);
                    }
                }
            }
            
            printf("Successfully rolled back %s to version %s\n", source, full_hash);
            return 0;
        } else {
            fprintf(stderr, "Error: Failed to update index, status=%d\n", status);
            DEBUG_PRINT("cmd_rollback: update_index_entry failed, status=%d\n", status);
        }
    } else {
        fprintf(stderr, "Error: Could not resolve hash %s, status=%d\n", hash, status);
        DEBUG_PRINT("cmd_rollback: resolve_hash failed, status=%d\n", status);
    }
    
    return 1;
} 