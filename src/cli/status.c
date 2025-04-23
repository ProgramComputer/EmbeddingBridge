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
    "Usage: embr status [options] <source>\n"
    "\n" 
    TEXT_BOLD "Show embedding status and log for a source file" COLOR_RESET "\n"
    "\n"
    "Arguments:\n"
    "  <source>         Source file to check status for\n"
    "\n"
    "Options:\n"
    "  -v, --verbose    Show detailed output including timestamps and metadata\n"
    "  -m, --model      Filter log by specific model/provider\n"
    "  --help          Display this help message\n"
    "\n"
    "Examples:\n"
    "  embr status file.txt         # Show basic status\n"
    "  embr status -v file.txt      # Show detailed status with metadata\n"
    "  embr status --model openai file.txt  # Show status for openai model only\n"
    "  embr status file.txt -v      # Same as above (flexible ordering)\n";

// Forward declarations
static int show_status(char** rel_paths, size_t num_paths, const char* repo_root);
static __attribute__((unused)) char* get_metadata(const char* root, const char* hash);

// Forward declaration for get_current_hash_with_model from core/store.h
eb_status_t get_current_hash_with_model(const char* root, const char* source, const char* model,
                                      char* hash_out, size_t hash_size);

// Structure to hold version info for sorting
typedef struct {
    char hash[MAX_HASH_LEN];
    time_t timestamp;
} version_info_t;

// Get metadata from object file
static char* get_metadata(const char* root, const char* hash)
{
    DEBUG_PRINT("get_metadata: Starting with root=%s, hash=%s", root, hash);
    
    char meta_path[MAX_PATH_LEN];
    snprintf(meta_path, sizeof(meta_path), "%s/.embr/objects/%s.meta", root, hash);
    DEBUG_PRINT("get_metadata: Looking for metadata file at %s", meta_path);

    FILE *f = fopen(meta_path, "r");
    if (!f) {
        DEBUG_PRINT("get_metadata: Failed to open metadata file, returning empty string");
        return strdup("");
    }
    DEBUG_PRINT("get_metadata: Successfully opened metadata file");

    char *metadata = malloc(MAX_META_LEN);
    if (!metadata) {
        DEBUG_PRINT("get_metadata: Failed to allocate memory for metadata");
        fclose(f);
        return strdup("");
    }
    DEBUG_PRINT("get_metadata: Successfully allocated memory for metadata");

    size_t pos = 0;
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        size_t len = strlen(line);
        DEBUG_PRINT("get_metadata: Read line: %s (length=%zu)", line, len);
        
        if (pos + len + 2 >= MAX_META_LEN) {
            DEBUG_PRINT("get_metadata: Metadata buffer full, stopping");
            break;
        }

        strcpy(metadata + pos, line);
        pos += len;
        metadata[pos++] = '\n';
    }
    metadata[pos] = '\0';
    DEBUG_PRINT("get_metadata: Finished reading metadata, total length=%zu", pos);

    fclose(f);
    return metadata;
}

// Get log entries
static char* get_history(const char* root, const char* source, const char* model_filter) {
	char* log_path = get_current_set_log_path();
	FILE *f = fopen(log_path, "r");
	free(log_path);
	if (!f) return NULL;

	char *history = malloc(MAX_LINE_LEN);
	if (!history) {
		fclose(f);
		return NULL;
	}
	
	// Initialize log string
	history[0] = '\0';
	size_t pos = 0;
	char line[MAX_LINE_LEN];
	
	while (fgets(line, sizeof(line), f)) {
		// Remove newline if present
		line[strcspn(line, "\n")] = 0;
		
		char file[MAX_PATH_LEN] = {0};
		char hash[MAX_HASH_LEN] = {0};
		char timestamp[32] = {0};
		char provider[32] = {0};
		
		// Try to parse as new format first: source hash timestamp provider
		int parsed = sscanf(line, "%s %s %s %s", file, hash, timestamp, provider);
		
		if (parsed >= 3 && strcmp(file, source) == 0) {
			// Check if we have provider info (new format)
			bool has_provider = (parsed == 4 && provider[0] != '\0');
			
			DEBUG_PRINT("Parsed log line: file=%s hash=%s timestamp=%s model=%s format=%s", 
				file, hash, timestamp, has_provider ? provider : "none", 
				has_provider ? "new" : "old");
			
			// Skip if model filter is specified and doesn't match or provider info is missing
			if (model_filter != NULL) {
				if (!has_provider || strcmp(model_filter, provider) != 0) {
					DEBUG_PRINT("Skipping entry due to model filter mismatch: %s != %s", 
						has_provider ? provider : "none", model_filter);
					continue;
				}
			}
			
			// Add hash to log string
			if (pos > 0) {
				// Check if we have enough space for ", " + hash
				if (pos + 2 + strlen(hash) >= MAX_LINE_LEN) {
					DEBUG_PRINT("Log string too long, truncating");
					break;
				}
				strcat(history + pos, ", ");
				pos += 2;
			}
			
			// Check if we have enough space for hash
			if (pos + strlen(hash) >= MAX_LINE_LEN) {
				DEBUG_PRINT("Log string too long, truncating");
				break;
			}
			
			strcpy(history + pos, hash);
			pos += strlen(hash);
		} else {
			// Try old format: timestamp hash file
			time_t ts;
			parsed = sscanf(line, "%ld %s %s", &ts, hash, file);
			
			if (parsed != 3 || strcmp(file, source) != 0) {
				// Not matching either format or not for this file
				continue;
			}
			
			DEBUG_PRINT("Parsed old format log line: timestamp=%ld hash=%s file=%s", 
				ts, hash, file);
			
			// Old format doesn't have provider info, so if filtering by model, skip
			if (model_filter != NULL) {
				DEBUG_PRINT("Skipping old format entry due to model filter");
				continue;
			}
			
			// Add hash to log string
			if (pos > 0) {
				// Check if we have enough space for ", " + hash
				if (pos + 2 + strlen(hash) >= MAX_LINE_LEN) {
					DEBUG_PRINT("Log string too long, truncating");
					break;
				}
				strcat(history + pos, ", ");
				pos += 2;
			}
			
			// Check if we have enough space for hash
			if (pos + strlen(hash) >= MAX_LINE_LEN) {
				DEBUG_PRINT("Log string too long, truncating");
				break;
			}
			
			strcpy(history + pos, hash);
			pos += strlen(hash);
		}
	}
	
	// Ensure null termination
	history[pos] = '\0';
	fclose(f);
	return history;
}

static void print_status(const char* root, const char* current, const char* history, bool verbose)
{
    DEBUG_PRINT("print_status: Starting with root=%s, current=%s, history=%s, verbose=%d", 
                root, current, history ? history : "NULL", verbose);
    
    // Check for NULL or empty log
    if (!history) {
        DEBUG_PRINT("print_status: Log is NULL, setting to empty string");
        history = "";
    }
    
    if (verbose) {
        // Header
        DEBUG_PRINT("print_status: Printing verbose output");
        printf(COLOR_BOLD_GREEN "→ Current Embedding" COLOR_RESET "\n");
        printf("  Hash: %s\n", current);
        
        // Metadata section
        DEBUG_PRINT("print_status: Getting metadata for hash %s", current);
        char *metadata = get_metadata(root, current);
        if (metadata && strlen(metadata) > 0) {
            DEBUG_PRINT("print_status: Metadata found, length=%zu", strlen(metadata));
            printf("  Metadata:\n");
            char* saveptr;
            char* token = strtok_r(metadata, "\n", &saveptr);
            while (token != NULL) {
                printf("    %s\n", token);
                token = strtok_r(NULL, "\n", &saveptr);
            }
            
            // Extract provider from metadata
            char *provider_line = strstr(metadata, "model=");
            if (provider_line) {
                DEBUG_PRINT("print_status: Provider found in metadata");
                char provider[32] = {0};
                if (sscanf(provider_line, "model=%31s", provider) == 1) {
                    printf("  Provider: %s\n", provider);
                }
            } else {
                DEBUG_PRINT("print_status: No provider found in metadata");
            }
        } else {
            DEBUG_PRINT("print_status: No metadata found or empty");
        }
        free(metadata);
        
        // Log section
        DEBUG_PRINT("print_status: Printing log section");
        printf(COLOR_BOLD_GREEN "\n→ Version Log" COLOR_RESET "\n");
        
        // Check if log is empty
        if (!history || strlen(history) == 0) {
            DEBUG_PRINT("print_status: Log is empty");
            printf("  No log entries found with the specified criteria.\n");
            return;
        }
        
        DEBUG_PRINT("print_status: Duplicating log string: %s", history);
        char *log_copy = strdup(history);
        if (!log_copy) {
            DEBUG_PRINT("print_status: Failed to duplicate log string");
            return;
        }
        
        DEBUG_PRINT("print_status: Tokenizing log string");
        char *saveptr = NULL;
        char *token = strtok_r(log_copy, ", ", &saveptr);
        int version = 1;
        
        while (token) {
            DEBUG_PRINT("print_status: Processing token: %s", token);
            printf("  %d. %s", version++, token);
            
            DEBUG_PRINT("print_status: Getting metadata for token %s", token);
            char *hash_meta = get_metadata(root, token);
            
            if (hash_meta && strlen(hash_meta) > 0) {
                DEBUG_PRINT("print_status: Metadata found for token, length=%zu", strlen(hash_meta));
                printf("\n     %s", hash_meta);
                
                // Extract provider from metadata
                char *provider_line = strstr(hash_meta, "model=");
                if (provider_line) {
                    DEBUG_PRINT("print_status: Provider found in token metadata");
                    char provider[32] = {0};
                    if (sscanf(provider_line, "model=%31s", provider) == 1) {
                        printf("     Provider: %s\n", provider);
                    }
                } else {
                    DEBUG_PRINT("print_status: No provider found in token metadata");
                }
            } else {
                DEBUG_PRINT("print_status: No metadata found for token or empty");
            }
            
            printf("\n");
            free(hash_meta);
            
            DEBUG_PRINT("print_status: Getting next token");
            token = strtok_r(NULL, ", ", &saveptr);
        }
        
        DEBUG_PRINT("print_status: Finished processing log tokens");
        free(log_copy);
    } else {
        // Simple, clean output for basic use
        DEBUG_PRINT("print_status: Printing simple output");
        printf(COLOR_BOLD_GREEN "→ " COLOR_RESET);
        printf("Current: %.7s\n", current);
        
        // Get provider from metadata
        DEBUG_PRINT("print_status: Getting metadata for hash %s", current);
        char *metadata = get_metadata(root, current);
        if (metadata) {
            DEBUG_PRINT("print_status: Metadata found, length=%zu", strlen(metadata));
            char *provider_line = strstr(metadata, "model=");
            if (provider_line) {
                DEBUG_PRINT("print_status: Provider found in metadata");
                char provider[32] = {0};
                if (sscanf(provider_line, "model=%31s", provider) == 1) {
                    printf("Provider: %s\n", provider);
                }
            } else {
                DEBUG_PRINT("print_status: No provider found in metadata");
            }
            free(metadata);
        } else {
            DEBUG_PRINT("print_status: No metadata found");
        }
        
        // Show shortened log hashes
        DEBUG_PRINT("print_status: Printing log hashes");
        printf("Log: ");
        
        // Check if log is empty
        if (!history || strlen(history) == 0) {
            DEBUG_PRINT("print_status: Log is empty");
            printf("No log entries found with the specified criteria.\n");
            return;
        }
        
        DEBUG_PRINT("print_status: Duplicating log string: %s", history);
        char *log_copy = strdup(history);
        if (!log_copy) {
            DEBUG_PRINT("print_status: Failed to duplicate log string");
            return;
        }
        
        DEBUG_PRINT("print_status: Tokenizing log string");
        char *saveptr = NULL;
        char *token = strtok_r(log_copy, ", ", &saveptr);
        bool first = true;
        
        while (token) {
            DEBUG_PRINT("print_status: Processing token: %s", token);
            if (!first) printf(", ");
            printf("%.7s", token);
            first = false;
            
            DEBUG_PRINT("print_status: Getting next token");
            token = strtok_r(NULL, ", ", &saveptr);
        }
        
        printf("\n");
        DEBUG_PRINT("print_status: Finished processing log tokens");
        free(log_copy);
    }
    
    DEBUG_PRINT("print_status: Completed");
}

// Implementation of the previously implicit show_status function
static int show_status(char** rel_paths, size_t num_paths, const char* repo_root)
{
    DEBUG_PRINT("show_status: Starting with %zu paths", num_paths);
    
    if (!rel_paths || !repo_root) {
        DEBUG_PRINT("show_status: Invalid parameters");
        return 1;
    }

    // Check if model filter is specified
    const char* model_filter = NULL;
    for (size_t i = 0; i < num_paths; i++) {
        if (strcmp(rel_paths[i], "--model") == 0 || strcmp(rel_paths[i], "-m") == 0) {
            if (i + 1 < num_paths) {
                model_filter = rel_paths[i + 1];
                DEBUG_PRINT("show_status: Model filter specified: %s", model_filter);
                break;
            }
        }
    }

    for (size_t i = 0; i < num_paths; i++) {
        // Skip if this is a model option or its value
        if ((strcmp(rel_paths[i], "--model") == 0 || strcmp(rel_paths[i], "-m") == 0) ||
            (i > 0 && (strcmp(rel_paths[i-1], "--model") == 0 || strcmp(rel_paths[i-1], "-m") == 0))) {
            DEBUG_PRINT("show_status: Skipping model option or value: %s", rel_paths[i]);
            continue;
        }
        
        // Skip other options
        if (rel_paths[i][0] == '-') {
            DEBUG_PRINT("show_status: Skipping option: %s", rel_paths[i]);
            continue;
        }

        DEBUG_PRINT("show_status: Processing path: %s", rel_paths[i]);
        
        // If no model filter is specified, we need to find all models that have embeddings for this file
        if (!model_filter) {
            DEBUG_PRINT("show_status: No model filter, checking all models in log");
            
            // First, get the log file path
            char* log_path = get_current_set_log_path();
            FILE* fp = fopen(log_path, "r");
            free(log_path);
            if (!fp) {
                DEBUG_PRINT("show_status: Could not open log file");
                fprintf(stderr, "No embedding log found for %s\n", rel_paths[i]);
                continue;
            }
            
            // Create a set of model types for this file
            char* models[32] = {0}; // Maximum 32 different models
            int model_count = 0;
            
            // Scan the log file for model types used with this source
            char line[2048];
            while (fgets(line, sizeof(line), fp) && model_count < 32) {
                char timestamp[32], hash[65], file[PATH_MAX], provider[32];
                line[strcspn(line, "\n")] = 0;
                
                // Parse log line: timestamp hash path model
                int parsed = sscanf(line, "%s %s %s %s", timestamp, hash, file, provider);
                if (parsed == 4 && strcmp(file, rel_paths[i]) == 0) {
                    // Check if we already have this model in our array
                    bool model_exists = false;
                    for (int m = 0; m < model_count; m++) {
                        if (strcmp(models[m], provider) == 0) {
                            model_exists = true;
                            break;
                        }
                    }
                    
                    // If it's a new model, add it to our array
                    if (!model_exists) {
                        DEBUG_PRINT("show_status: Found new model %s for file %s", provider, rel_paths[i]);
                        models[model_count++] = strdup(provider);
                    }
                }
            }
            fclose(fp);
            
            // Now process each model type
            if (model_count == 0) {
                DEBUG_PRINT("show_status: No models found in log for %s", rel_paths[i]);
                fprintf(stderr, "No embedding log found for %s\n", rel_paths[i]);
                continue;
            }
            
            // Process each model one by one
            for (int m = 0; m < model_count; m++) {
                DEBUG_PRINT("show_status: Processing model %s for file %s", models[m], rel_paths[i]);
                
                char hash[65];
                eb_status_t status = get_current_hash_with_model(repo_root, rel_paths[i], models[m], hash, sizeof(hash));
                
                if (status != EB_SUCCESS) {
                    DEBUG_PRINT("show_status: Failed to get hash for model %s", models[m]);
                    continue; // Skip this model
                }
                
                // Print the embedding info for this model
                printf(COLOR_BOLD_GREEN "→ Current Embedding" COLOR_RESET "\n");
                printf("  Hash: %s\n", hash);
                printf("  Model: %s\n", models[m]);
                
                // Get metadata for current hash
                char *metadata = get_metadata(repo_root, hash);
                if (metadata && strlen(metadata) > 0) {
                    printf("  Metadata:\n");
                    char* saveptr;
                    char* token = strtok_r(metadata, "\n", &saveptr);
                    while (token != NULL) {
                        printf("    %s\n", token);
                        token = strtok_r(NULL, "\n", &saveptr);
                    }
                }
                free(metadata);
                printf("\n");
                
                // Free the model name
                free(models[m]);
            }
        } else {
            // Model filter is specified, use the existing code path
            char hash[65];  // Buffer for hash
            eb_status_t status = get_current_hash_with_model(repo_root, rel_paths[i], model_filter, hash, sizeof(hash));
            
            if (status != EB_SUCCESS) {
                DEBUG_PRINT("show_status: No embedding found for %s with model %s", rel_paths[i], model_filter);
                fprintf(stderr, "No embedding found for '%s' with model '%s'\n", rel_paths[i], model_filter);
                continue;
            }
            
            DEBUG_PRINT("show_status: Current hash for %s: %s", rel_paths[i], hash);

            // Print the current hash and metadata
            printf(COLOR_BOLD_GREEN "→ Current Embedding" COLOR_RESET "\n");
            printf("  Hash: %s\n", hash);
            printf("  Model: %s\n", model_filter);
            
            // Get metadata for current hash
            char *metadata = get_metadata(repo_root, hash);
            if (metadata && strlen(metadata) > 0) {
                printf("  Metadata:\n");
                char* saveptr;
                char* token = strtok_r(metadata, "\n", &saveptr);
                while (token != NULL) {
                    printf("    %s\n", token);
                    token = strtok_r(NULL, "\n", &saveptr);
                }
            }
            free(metadata);
            
            // Print model filter message
            printf("\nFiltered by model: %s\n", model_filter);
            printf("\n");
        }
    }

    DEBUG_PRINT("show_status: Completed");
    return 0;
}

int cmd_status(int argc, char* argv[])
{
    DEBUG_PRINT("cmd_status: Starting with %d arguments", argc);
    
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", STATUS_USAGE);
        return (argc < 2) ? 1 : 0;
    }
    
    // Find the source argument
    const char *source = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' && 
            (i == 1 || (strcmp(argv[i-1], "--model") != 0 && strcmp(argv[i-1], "-m") != 0))) {
            source = argv[i];
            DEBUG_PRINT("cmd_status: Found source argument: %s", source);
            break;
        }
    }

    if (!source) {
        DEBUG_PRINT("cmd_status: No source file specified");
        cli_error("No source file specified");
        return 1;
    }

    // Find repository root from current working directory
    char *repo_root = find_repo_root(NULL);
    if (!repo_root) {
        DEBUG_PRINT("cmd_status: Not in an eb repository");
        fprintf(stderr, "Error: Not in an eb repository\n");
        return 1;
    }
    DEBUG_PRINT("cmd_status: Found repository root: %s", repo_root);

    // Use the source argument as relative path
    char *rel_path = strdup(source);
    if (!rel_path) {
        DEBUG_PRINT("cmd_status: Memory allocation failed");
        fprintf(stderr, "Error: Memory allocation failed\n");
        free(repo_root);
        return 1;
    }
    DEBUG_PRINT("cmd_status: Relative path: %s", rel_path);

    // Create paths array with just the necessary arguments
    int new_argc = 0;
    char **rel_paths = malloc(sizeof(char*) * argc);
    if (!rel_paths) {
        DEBUG_PRINT("cmd_status: Memory allocation failed");
        fprintf(stderr, "Error: Memory allocation failed\n");
        free(rel_path);
        free(repo_root);
        return 1;
    }
    
    // Add the file path (only once)
    rel_paths[new_argc++] = rel_path;
    
    // Check for model filter and add it if present
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--model") == 0 || strcmp(argv[i], "-m") == 0) && i + 1 < argc) {
            rel_paths[new_argc++] = argv[i];     // Add --model
            rel_paths[new_argc++] = argv[i + 1]; // Add model value
            i++; // Skip the next argument since we've added it
            DEBUG_PRINT("cmd_status: Added model filter %s", argv[i]);
            break; // Only support one model filter
        }
    }
    
    // Show status with the cleaned up arguments list
    DEBUG_PRINT("cmd_status: Calling show_status with %d arguments", new_argc);
    int ret = show_status(rel_paths, new_argc, repo_root);
    DEBUG_PRINT("cmd_status: show_status returned %d", ret);

    // Cleanup
    DEBUG_PRINT("cmd_status: Cleaning up");
    free(rel_paths[0]);
    free(rel_paths);
    free(repo_root);
    DEBUG_PRINT("cmd_status: Completed with return code %d", ret);

    return ret;
} 