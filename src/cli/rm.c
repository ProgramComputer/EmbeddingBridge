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

// Helper function to convert file path to reference path
static char* file_path_to_ref_path(const char* file_path) {
    char* ref_path = strdup(file_path);
    if (!ref_path) return NULL;
    
    // Replace all slashes with underscores
    for (char* p = ref_path; *p; p++) {
        if (*p == '/') *p = '_';
    }
    
    return ref_path;
}

// Check if a file is tracked in the embedding index
static bool is_file_tracked(const char* repo_root, const char* file_path) {
    // First check if file exists in the index
    char index_path[MAX_PATH_LEN];
    snprintf(index_path, sizeof(index_path), "%s/.eb/index", repo_root);
    
    printf("DEBUG: Checking if file '%s' is tracked in index '%s'\n", file_path, index_path);
    
    FILE* index_file = fopen(index_path, "r");
    if (!index_file) {
        printf("DEBUG: Failed to open index file\n");
        return false;  // No index file
    }
    
    char line[MAX_LINE_LEN];
    bool found = false;
    
    while (fgets(line, sizeof(line), index_file)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        printf("DEBUG: Index line: '%s'\n", line);
        
        char hash[65], path[MAX_PATH_LEN];
        if (sscanf(line, "%s %s", hash, path) == 2) {
            printf("DEBUG: Comparing '%s' with '%s'\n", path, file_path);
            if (strcmp(path, file_path) == 0) {
                found = true;
                printf("DEBUG: Match found!\n");
                break;
            }
        }
    }
    
    fclose(index_file);
    return found;
}

// Remove entries from the index file
static int remove_from_index(const char* repo_root, const char* file_path, 
                            const char* model, const char* chunk_id, bool all) {
    // First read the index file to find matching hashes
    char index_path[MAX_PATH_LEN];
    snprintf(index_path, sizeof(index_path), "%s/.eb/index", repo_root);
    
    printf("DEBUG: Opening index file: %s\n", index_path);
    FILE* index_file = fopen(index_path, "r");
    if (!index_file) {
        cli_error("Failed to open index file: %s", index_path);
        return 1;
    }
    
    // Read all lines
    char** lines = NULL;
    char** hashes = NULL;
    char** matched_models = NULL;
    int line_count = 0;
    int removed = 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), index_file)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        char hash[65], path[MAX_PATH_LEN];
        if (sscanf(line, "%s %s", hash, path) != 2) {
            continue;  // Invalid line format
        }
        
        // Check if this is for our file
        if (strcmp(path, file_path) != 0) {
            // Keep lines for other files
            lines = realloc(lines, sizeof(char*) * (line_count + 1));
            if (!lines) {
                cli_error("Memory allocation failed");
                fclose(index_file);
                return 1;
            }
            lines[line_count++] = strdup(line);
            continue;
        }
        
        // Get metadata to find model info
        char meta_path[MAX_PATH_LEN];
        snprintf(meta_path, sizeof(meta_path), "%s/.eb/objects/%s.meta", repo_root, hash);
        
        printf("DEBUG: Checking metadata: %s\n", meta_path);
        
        char* provider = NULL;
        FILE* meta_file = fopen(meta_path, "r");
        if (meta_file) {
            char meta_line[MAX_LINE_LEN];
            while (fgets(meta_line, sizeof(meta_line), meta_file)) {
                // Remove newline
                size_t meta_len = strlen(meta_line);
                if (meta_len > 0 && meta_line[meta_len-1] == '\n') {
                    meta_line[meta_len-1] = '\0';
                }
                
                if (strncmp(meta_line, "provider=", 9) == 0) {
                    provider = strdup(meta_line + 9);
                    printf("DEBUG: Found provider in metadata: %s\n", provider);
                    break;
                }
            }
            fclose(meta_file);
        }
        
        // Determine if we should remove this entry
        bool should_remove = false;
        
        if (all) {
            should_remove = true;
        } else if (model && !chunk_id) {
            // Normalize model comparison (strip off version numbers)
            char* model_base = NULL;
            char* provider_base = NULL;
            
            // Get base model name by stripping off version numbers
            if (model) {
                model_base = strdup(model);
                char* dash = strchr(model_base, '-');
                if (dash) *dash = '\0';
            }
            
            if (provider) {
                provider_base = strdup(provider);
                char* dash = strchr(provider_base, '-');
                if (dash) *dash = '\0';
            }
            
            // Compare normalized model names
            if (model_base && provider_base && 
                (strcmp(model_base, provider_base) == 0 || 
                 strcmp(model, provider) == 0)) {
                should_remove = true;
                printf("DEBUG: Model match found: %s ~ %s\n", model, provider);
            }
            
            free(model_base);
            free(provider_base);
        } else if (!model && chunk_id) {
            // Not implemented yet
            should_remove = false;
        } else if (model && chunk_id) {
            // Not implemented yet
            should_remove = false;
        } else {
            // Default: remove all entries for this file
            should_remove = true;
        }
        
        if (should_remove) {
            printf("DEBUG: Will remove hash: %s (provider: %s)\n", hash, provider ? provider : "unknown");
            
            // Store hash for removal
            hashes = realloc(hashes, sizeof(char*) * (removed + 1));
            matched_models = realloc(matched_models, sizeof(char*) * (removed + 1));
            if (!hashes || !matched_models) {
                cli_error("Memory allocation failed");
                fclose(index_file);
                if (provider) free(provider);
                return 1;
            }
            
            hashes[removed] = strdup(hash);
            matched_models[removed] = provider ? provider : strdup("unknown");
            removed++;
        } else {
            // Keep this line
            lines = realloc(lines, sizeof(char*) * (line_count + 1));
            if (!lines) {
                cli_error("Memory allocation failed");
                fclose(index_file);
                if (provider) free(provider);
                return 1;
            }
            lines[line_count++] = strdup(line);
            if (provider) free(provider);
        }
    }
    
    fclose(index_file);
    
    // If no lines were removed, nothing to do
    if (removed == 0) {
        cli_warning("No matching embeddings found to remove");
        return 0;
    }
    
    // Write back the updated index file
    index_file = fopen(index_path, "w");
    if (!index_file) {
        cli_error("Failed to open index file for writing: %s", index_path);
        return 1;
    }
    
    for (int i = 0; i < line_count; i++) {
        fprintf(index_file, "%s\n", lines[i]);
                free(lines[i]);
            }
            free(lines);
            
    fclose(index_file);
    
    // Remove the embedding files
            for (int i = 0; i < removed; i++) {
        // Remove from model refs
        if (matched_models[i]) {
            char model_ref_path[MAX_PATH_LEN];
            snprintf(model_ref_path, sizeof(model_ref_path), "%s/.eb/refs/models/%s", 
                     repo_root, matched_models[i]);
            
            printf("DEBUG: Checking model ref: %s\n", model_ref_path);
            
            FILE* model_ref = fopen(model_ref_path, "r");
            if (model_ref) {
                char** ref_lines = NULL;
                int ref_count = 0;
                
                char ref_line[MAX_LINE_LEN];
                while (fgets(ref_line, sizeof(ref_line), model_ref)) {
                    // Remove newline
                    size_t ref_len = strlen(ref_line);
                    if (ref_len > 0 && ref_line[ref_len-1] == '\n') {
                        ref_line[ref_len-1] = '\0';
                    }
                    
                    char ref_hash[65], ref_path[MAX_PATH_LEN];
                    if (sscanf(ref_line, "%s %s", ref_hash, ref_path) == 2) {
                        if (strcmp(ref_hash, hashes[i]) != 0 && strcmp(ref_path, file_path) != 0) {
                            // Keep line if hash and path don't match
                            ref_lines = realloc(ref_lines, sizeof(char*) * (ref_count + 1));
                            if (ref_lines) {
                                ref_lines[ref_count++] = strdup(ref_line);
                            }
                        }
                    }
                }
                
                fclose(model_ref);
                
                // Write updated model ref file
                model_ref = fopen(model_ref_path, "w");
                if (model_ref) {
                    for (int j = 0; j < ref_count; j++) {
                        fprintf(model_ref, "%s\n", ref_lines[j]);
                        free(ref_lines[j]);
                    }
                    free(ref_lines);
                    fclose(model_ref);
                }
            }
        }
        
        // Delete the object and metadata files directly
        char obj_path[MAX_PATH_LEN];
        snprintf(obj_path, sizeof(obj_path), "%s/.eb/objects/%s.raw", repo_root, hashes[i]);
        char meta_path[MAX_PATH_LEN];
        snprintf(meta_path, sizeof(meta_path), "%s/.eb/objects/%s.meta", repo_root, hashes[i]);
        
        printf("DEBUG: Removing object file: %s\n", obj_path);
        if (unlink(obj_path) != 0 && errno != ENOENT) {
            cli_warning("Failed to remove object file: %s", obj_path);
        }
        
        printf("DEBUG: Removing metadata file: %s\n", meta_path);
        if (unlink(meta_path) != 0 && errno != ENOENT) {
            cli_warning("Failed to remove metadata file: %s", meta_path);
        }
        
        free(hashes[i]);
        free(matched_models[i]);
    }
    
    free(hashes);
    free(matched_models);
    
    return 0;
}

// Remove embedding files from storage
static int remove_embedding_files(const char* repo_root, const char* file_path,
                                 const char* model, const char* chunk_id, bool all, bool verbose) {
    // First read the index file to find matching hashes
    char index_path[MAX_PATH_LEN];
    snprintf(index_path, sizeof(index_path), "%s/.eb/index", repo_root);
    
    FILE* index_file = fopen(index_path, "r");
    if (!index_file) {
        cli_error("Failed to open index file: %s", index_path);
        return 1;
    }
    
    int error_count = 0;
    int removed_count = 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), index_file)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        char hash[65], path[MAX_PATH_LEN];
        if (sscanf(line, "%s %s", hash, path) != 2) {
            continue;  // Invalid line format
        }
        
        // Check if this is for our file
        if (strcmp(path, file_path) != 0) {
            continue;  // Not our file
        }
        
        // Get model for this hash from metadata
        char hash_model[64] = {0};
        
        // Read metadata file to get model info
        char meta_path[MAX_PATH_LEN];
        snprintf(meta_path, sizeof(meta_path), "%s/.eb/objects/%s.meta", repo_root, hash);
        
        FILE* meta_file = fopen(meta_path, "r");
        if (meta_file) {
            char meta_line[MAX_LINE_LEN];
            while (fgets(meta_line, sizeof(meta_line), meta_file)) {
                // Remove newline
                size_t meta_len = strlen(meta_line);
                if (meta_len > 0 && meta_line[meta_len-1] == '\n') {
                    meta_line[meta_len-1] = '\0';
                }
                
                if (strncmp(meta_line, "provider=", 9) == 0) {
                    strncpy(hash_model, meta_line + 9, sizeof(hash_model) - 1);
                    break;
                }
            }
            fclose(meta_file);
        }
        
        // Determine if we should remove this file
        bool should_remove = false;
        
        if (all) {
            should_remove = true;
        } else if (model && !chunk_id) {
            should_remove = (strcmp(hash_model, model) == 0);
        } else if (!model && chunk_id) {
            // This implementation doesn't support chunk-based removal yet
            should_remove = false;
        } else if (model && chunk_id) {
            // This implementation doesn't support simultaneous model+chunk removal
            should_remove = false;
        } else {
            // Default: remove all entries for this file
            should_remove = true;
        }
        
        if (should_remove) {
            // Remove the object file
            char obj_path[MAX_PATH_LEN];
            snprintf(obj_path, sizeof(obj_path), "%s/.eb/objects/%s.raw", repo_root, hash);
            
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
            snprintf(obj_path, sizeof(obj_path), "%s/.eb/objects/%s.meta", repo_root, hash);
            
            if (unlink(obj_path) != 0 && errno != ENOENT) {
                    cli_warning("Failed to remove metadata file: %s", obj_path);
                    error_count++;
                }
            }
        }
        
    fclose(index_file);
    
    if (verbose) {
        cli_info("Removed %d embedding objects with %d errors", removed_count, error_count);
    }
    
    return (error_count > 0) ? 1 : 0;
}

// Update history file to record the removal
static int update_history_for_removal(const char* repo_root, const char* file_path, 
                                     const char* model, const char* chunk_id, bool all) {
    // Git doesn't record removals in the log, so we'll follow that pattern
    // Just return success without writing to the history file
    return 0;
}

int cmd_rm(int argc, char *argv[])
{
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", RM_USAGE);
        return (argc < 2) ? 1 : 0;
    }

    const char *file = NULL;
    
    // Find the file argument (should be the last non-option argument)
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && (i == 1 || argv[i-1][0] != '-' || 
                                   strcmp(argv[i-1], "-m") != 0 &&
                                   strcmp(argv[i-1], "--model") != 0 &&
                                   strcmp(argv[i-1], "-c") != 0 &&
                                   strcmp(argv[i-1], "--chunk") != 0)) {
            file = argv[i];
            break;
        }
    }
    
    if (!file) {
        printf("%s", RM_USAGE);
        return 1;
    }
    
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