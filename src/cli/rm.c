/*
 * EmbeddingBridge - RM Command Implementation
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
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include "cli.h"
#include "../core/store.h"
#include "../core/error.h"
#include "../core/path_utils.h"
#include "colors.h"

#define MAX_LINE_LEN 2048
#define MAX_PATH_LEN PATH_MAX

static const char* RM_USAGE = 
    "Usage: eb rm [options] <file>\n"
    "\n"
    "Remove embeddings from tracking\n"
    "\n"
    "Options:\n"
    "  --cached        Only remove from index, keep embedding files in storage\n"
    "  --all           Remove all embeddings for the specified file (all chunks/models)\n"
    "  -m, --model <model> Only remove embedding for specific model\n"
    "  -c, --chunk <id>    Only remove specific chunk embedding\n"
    "  -v, --verbose    Show detailed output\n"
    "  -q, --quiet      Minimal output\n"
    "\n"
    "Examples:\n"
    "  eb rm file.txt              # Remove all embeddings for file.txt\n"
    "  eb rm --cached file.txt     # Remove from index but keep embedding files\n"
    "  eb rm -m openai-3 file.txt  # Remove only embeddings for openai-3 model\n"
    "  eb rm -c chunk1.npy file.txt # Remove only the specific chunk embedding\n";

// Forward declarations
static bool is_file_tracked(const char* repo_root, const char* file_path);
static int remove_from_index(const char* repo_root, const char* file_path, 
                            const char* model, const char* chunk_id, bool all);
static int remove_embedding_files(const char* repo_root, const char* file_path,
                                 const char* model, const char* chunk_id, bool all, bool verbose);
static int update_history_for_removal(const char* repo_root, const char* file_path, 
                                     const char* model, const char* chunk_id, bool all);

// Check if a file is tracked in the embedding index
static bool is_file_tracked(const char* repo_root, const char* file_path) {
    char ref_path[MAX_PATH_LEN];
    snprintf(ref_path, sizeof(ref_path), "%s/.eb/metadata/files/%s.ref", repo_root, file_path);
    
    // Check if ref file exists
    struct stat st;
    return (stat(ref_path, &st) == 0);
}

// Remove entries from the index file
static int remove_from_index(const char* repo_root, const char* file_path, 
                            const char* model, const char* chunk_id, bool all) {
    char ref_path[MAX_PATH_LEN];
    snprintf(ref_path, sizeof(ref_path), "%s/.eb/metadata/files/%s.ref", repo_root, file_path);
    
    FILE* ref = fopen(ref_path, "r");
    if (!ref) {
        cli_error("Failed to open reference file: %s", ref_path);
        return 1;
    }
    
    // Read all lines
    char** lines = NULL;
    char** hashes = NULL;
    char** models = NULL;
    char** chunks = NULL;
    int line_count = 0;
    int removed = 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), ref)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Parse line (format: chunk_id:model:hash)
        char* line_copy = strdup(line);
        char* first_colon = strchr(line_copy, ':');
        if (!first_colon) {
            free(line_copy);
            continue;  // Invalid line format
        }
        
        *first_colon = '\0';
        char* line_chunk = strdup(line_copy);
        
        char* second_colon = strchr(first_colon + 1, ':');
        if (!second_colon) {
            free(line_copy);
            free(line_chunk);
            continue;  // Invalid line format
        }
        
        *second_colon = '\0';
        char* line_model = strdup(first_colon + 1);
        char* line_hash = strdup(second_colon + 1);
        
        // Check if this line should be kept
        bool keep = true;
        
        if (all) {
            // Remove all entries
            keep = false;
        } else if (model && chunk_id) {
            // Remove specific model and chunk
            if (strcmp(line_model, model) == 0 && strcmp(line_chunk, chunk_id) == 0) {
                keep = false;
            }
        } else if (model) {
            // Remove specific model (all chunks)
            if (strcmp(line_model, model) == 0) {
                keep = false;
            }
        } else if (chunk_id) {
            // Remove specific chunk (all models)
            if (strcmp(line_chunk, chunk_id) == 0) {
                keep = false;
            }
        } else {
            // Default behavior: remove all entries
            keep = false;
        }
        
        if (keep) {
            // Keep this line
            lines = realloc(lines, sizeof(char*) * (line_count + 1));
            lines[line_count] = strdup(line);
            
            // We don't need these for kept lines
            free(line_hash);
            free(line_model);
            free(line_chunk);
        } else {
            // Store info about removed lines for object cleanup
            hashes = realloc(hashes, sizeof(char*) * (removed + 1));
            models = realloc(models, sizeof(char*) * (removed + 1));
            chunks = realloc(chunks, sizeof(char*) * (removed + 1));
            
            hashes[removed] = line_hash;
            models[removed] = line_model;
            chunks[removed] = line_chunk;
            removed++;
        }
        
        free(line_copy);
        line_count++;
    }
    fclose(ref);
    
    // If no lines were removed, nothing to do
    if (removed == 0) {
        cli_warning("No matching embeddings found to remove");
        return 0;
    }
    
    // Write back the file if there are remaining entries
    if (line_count - removed > 0) {
        ref = fopen(ref_path, "w");
        if (!ref) {
            cli_error("Failed to open reference file for writing: %s", ref_path);
            
            // Free memory
            for (int i = 0; i < line_count - removed; i++) {
                free(lines[i]);
            }
            free(lines);
            
            for (int i = 0; i < removed; i++) {
                free(hashes[i]);
                free(models[i]);
                free(chunks[i]);
            }
            free(hashes);
            free(models);
            free(chunks);
            
            return 1;
        }
        
        // Write remaining lines
        for (int i = 0; i < line_count - removed; i++) {
            fprintf(ref, "%s\n", lines[i]);
            free(lines[i]);
        }
        fclose(ref);
    } else {
        // All entries removed, delete the file
        if (unlink(ref_path) != 0) {
            cli_error("Failed to delete reference file: %s", ref_path);
            
            // Free memory
            for (int i = 0; i < removed; i++) {
                free(hashes[i]);
                free(models[i]);
                free(chunks[i]);
            }
            free(hashes);
            free(models);
            free(chunks);
            
            return 1;
        }
    }
    
    free(lines);
    
    // Return the removed hashes for object cleanup
    for (int i = 0; i < removed; i++) {
        free(hashes[i]);
        free(models[i]);
        free(chunks[i]);
    }
    free(hashes);
    free(models);
    free(chunks);
    
    return 0;
}

// Remove embedding files from storage
static int remove_embedding_files(const char* repo_root, const char* file_path,
                                 const char* model, const char* chunk_id, bool all, bool verbose) {
    char ref_path[MAX_PATH_LEN];
    snprintf(ref_path, sizeof(ref_path), "%s/.eb/metadata/files/%s.ref", repo_root, file_path);
    
    FILE* ref = fopen(ref_path, "r");
    if (!ref) {
        cli_error("Failed to open reference file: %s", ref_path);
        return 1;
    }
    
    int error_count = 0;
    int removed_count = 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), ref)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Parse line (format: chunk_id:model:hash)
        char* line_copy = strdup(line);
        char* first_colon = strchr(line_copy, ':');
        if (!first_colon) {
            free(line_copy);
            continue;  // Invalid line format
        }
        
        *first_colon = '\0';
        char* line_chunk = line_copy;
        
        char* second_colon = strchr(first_colon + 1, ':');
        if (!second_colon) {
            free(line_copy);
            continue;  // Invalid line format
        }
        
        *second_colon = '\0';
        char* line_model = first_colon + 1;
        char* line_hash = second_colon + 1;
        
        // Check if this line should be processed
        bool process = false;
        
        if (all) {
            // Process all entries
            process = true;
        } else if (model && chunk_id) {
            // Process specific model and chunk
            if (strcmp(line_model, model) == 0 && strcmp(line_chunk, chunk_id) == 0) {
                process = true;
            }
        } else if (model) {
            // Process specific model (all chunks)
            if (strcmp(line_model, model) == 0) {
                process = true;
            }
        } else if (chunk_id) {
            // Process specific chunk (all models)
            if (strcmp(line_chunk, chunk_id) == 0) {
                process = true;
            }
        } else {
            // Default behavior: process all entries
            process = true;
        }
        
        if (process) {
            // Remove the embedding file
            char obj_path[MAX_PATH_LEN];
            snprintf(obj_path, sizeof(obj_path), "%s/.eb/objects/%s.raw", repo_root, line_hash);
            
            if (verbose) {
                cli_info("Removing embedding object: %s", obj_path);
            }
            
            if (unlink(obj_path) != 0) {
                if (errno != ENOENT) {  // Ignore if file doesn't exist
                    cli_warning("Failed to remove embedding file: %s", obj_path);
                    error_count++;
                }
            } else {
                removed_count++;
            }
            
            // Remove metadata file
            snprintf(obj_path, sizeof(obj_path), "%s/.eb/objects/%s.meta", repo_root, line_hash);
            
            if (unlink(obj_path) != 0) {
                if (errno != ENOENT) {  // Ignore if file doesn't exist
                    cli_warning("Failed to remove metadata file: %s", obj_path);
                    error_count++;
                }
            }
        }
        
        free(line_copy);
    }
    
    fclose(ref);
    
    if (verbose) {
        cli_info("Removed %d embedding objects with %d errors", removed_count, error_count);
    }
    
    return (error_count > 0) ? 1 : 0;
}

// Update history file to record the removal
static int update_history_for_removal(const char* repo_root, const char* file_path, 
                                     const char* model, const char* chunk_id, bool all) {
    char history_path[MAX_PATH_LEN];
    snprintf(history_path, sizeof(history_path), "%s/.eb/log", repo_root);
    
    FILE* history = fopen(history_path, "a");
    if (!history) {
        cli_warning("Failed to open history file for writing");
        return 1;
    }
    
    time_t now = time(NULL);
    
    if (all) {
        fprintf(history, "%ld REMOVED %s ALL ALL\n", (long)now, file_path);
    } else if (model && chunk_id) {
        fprintf(history, "%ld REMOVED %s %s %s\n", (long)now, file_path, chunk_id, model);
    } else if (model) {
        fprintf(history, "%ld REMOVED %s ALL %s\n", (long)now, file_path, model);
    } else if (chunk_id) {
        fprintf(history, "%ld REMOVED %s %s ALL\n", (long)now, file_path, chunk_id);
    } else {
        fprintf(history, "%ld REMOVED %s ALL ALL\n", (long)now, file_path);
    }
    
    fclose(history);
    return 0;
}

int cmd_rm(int argc, char *argv[])
{
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", RM_USAGE);
        return (argc < 2) ? 1 : 0;
    }

    const char *file = argv[1];
    bool cached = has_option(argc, argv, "--cached");
    bool all = has_option(argc, argv, "--all");
    bool verbose = has_option(argc, argv, "-v") || has_option(argc, argv, "--verbose");
    bool quiet = has_option(argc, argv, "-q") || has_option(argc, argv, "--quiet");
    const char *model = get_option_value(argc, argv, "-m", "--model");
    const char *chunk_id = get_option_value(argc, argv, "-c", "--chunk");
    
    // Find repository root
    char *repo_root = find_repo_root(".");
    if (!repo_root) {
        cli_error("Not in an eb repository");
        return 1;
    }
    
    // Get relative path
    char *rel_file = get_relative_path(file, repo_root);
    if (!rel_file) {
        cli_error("File must be within repository");
        free(repo_root);
        return 1;
    }
    
    // Check if file is tracked
    if (!is_file_tracked(repo_root, rel_file)) {
        cli_error("File '%s' not tracked", rel_file);
        free(repo_root);
        free(rel_file);
        return 1;
    }
    
    // Remove from index based on options
    int ret = remove_from_index(repo_root, rel_file, model, chunk_id, all);
    if (ret != 0) {
        cli_error("Failed to remove from index");
        free(repo_root);
        free(rel_file);
        return 1;
    }
    
    // If not --cached, also remove embedding files
    if (!cached) {
        ret = remove_embedding_files(repo_root, rel_file, model, chunk_id, all, verbose);
        if (ret != 0 && !quiet) {
            cli_warning("Failed to remove some embedding files");
        }
    }
    
    // Update history file
    update_history_for_removal(repo_root, rel_file, model, chunk_id, all);
    
    if (!quiet) {
        printf("Removed '%s' from embedding tracking\n", rel_file);
        if (cached) {
            printf("Embeddings remain in storage (--cached option used)\n");
        }
    }
    
    free(repo_root);
    free(rel_file);
    return 0;
} 