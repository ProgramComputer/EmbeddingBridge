/*
 * EmbeddingBridge - Log Command Implementation
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
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cli.h"
#include "colors.h"
#include "../core/debug.h"
#include "../core/error.h"
#include "../core/types.h"
#include "../core/store.h"

/* Return codes */
#define LOG_SUCCESS          0
#define LOG_ERROR_ARGS       1
#define LOG_ERROR_REPO       2
#define LOG_ERROR_FILE       3
#define LOG_ERROR_MEMORY     4

static const char* LOG_USAGE =
    "Usage: eb log [options] [file...]\n"
    "\n"
    "Display embedding log for specified files\n"
    "\n"
    "Options:\n"
    "  -m, --model <model>     Filter by model/provider\n"
    "  -l, --limit <n>         Limit to last n entries (default: all)\n"
    "  -v, --verbose           Show detailed information\n"
    "  -h, --help              Show this help message\n"
    "\n"
    "Examples:\n"
    "  # Show log for a file\n"
    "  eb log path/to/file.txt\n"
    "\n"
    "  # Show log for a file filtered by model\n"
    "  eb log --model openai-3 path/to/file.txt\n"
    "\n"
    "  # Show detailed log for the last 5 versions of a file\n"
    "  eb log --verbose --limit 5 path/to/file.txt\n";

typedef struct {
    char hash[65];
    char provider[32];
    time_t timestamp;
    bool is_current;
} log_entry_t;

/* Helper function to check if a directory exists */
static bool directory_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int compare_entries_by_time(const void* a, const void* b) {
    const log_entry_t* entry_a = (const log_entry_t*)a;
    const log_entry_t* entry_b = (const log_entry_t*)b;
    
    /* Sort in reverse order (newest first) */
    if (entry_a->timestamp > entry_b->timestamp)
        return -1;
    if (entry_a->timestamp < entry_b->timestamp)
        return 1;
    return 0;
}

static bool find_repo_root(char* path_out, size_t path_size) {
    char cwd[PATH_MAX];
    
    if (!getcwd(cwd, sizeof(cwd)))
        return false;
    
    /* Start from current directory and go up */
    char search_path[PATH_MAX];
    strncpy(search_path, cwd, sizeof(search_path));
    
    while (1) {
        /* Check if .eb directory exists in current path */
        char eb_dir[PATH_MAX];
        snprintf(eb_dir, sizeof(eb_dir), "%s/.eb", search_path);
        
        if (directory_exists(eb_dir)) {
            strncpy(path_out, search_path, path_size);
            return true;
        }
        
        /* Go up one directory */
        char* last_slash = strrchr(search_path, '/');
        if (!last_slash || last_slash == search_path) {
            /* Either no slash found or we're at root already */
            return false;
        }
        
        /* Terminate string at last slash */
        *last_slash = '\0';
    }
}

static char* get_metadata(const char* root, const char* hash) {
    char meta_path[PATH_MAX];
    FILE* f;
    long size;
    char* content;
    size_t read_size;
    
    if (!root || !hash)
        return NULL;
    
    snprintf(meta_path, sizeof(meta_path), "%s/.eb/objects/%s.meta", root, hash);
    
    f = fopen(meta_path, "r");
    if (!f)
        return NULL;
    
    /* Read entire file content */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    
    content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    read_size = fread(content, 1, size, f);
    if (read_size != (size_t)size) {
        free(content);
        fclose(f);
        return NULL;
    }
    
    content[size] = '\0';
    fclose(f);
    return content;
}

static void format_time(time_t timestamp, char* buffer, size_t buffer_size) {
    struct tm* tm_info = localtime(&timestamp);
    
    if (!tm_info) {
        strncpy(buffer, "Unknown time", buffer_size);
        return;
    }
    
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static void free_current_model_data(char** models, char** hashes, int count) {
    int i;
    
    if (!models || !hashes)
        return;
        
    for (i = 0; i < count; i++) {
        free(models[i]);
        free(hashes[i]);
    }
    
    free(models);
    free(hashes);
}

static void display_metadata(const char* metadata) {
    if (!metadata || *metadata == '\0')
        return;
        
    printf("\n    ");
    
    char* copy = strdup(metadata);
    if (!copy)
        return;
        
    char* line = copy;
    char* next_line;
    
    while (line && *line) {
        /* Find next line */
        next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }
        
        /* Print current line */
        printf("%s", line);
        
        /* If there's another line, print newline and indentation */
        if (next_line && *next_line)
            printf("\n    ");
            
        line = next_line;
    }
    
    free(copy);
}

static int show_log(const char* file_path, const char* model_filter, int limit, bool verbose) {
    char repo_root[PATH_MAX];
    const char* rel_path;
    size_t root_len;
    char log_path[PATH_MAX];
    FILE* f;
    char** current_models = NULL;
    char** current_hashes = NULL;
    int current_model_count = 0;
    char index_path[PATH_MAX];
    FILE* idx_file;
    log_entry_t* entries = NULL;
    int entry_count = 0;
    char line[PATH_MAX + 256];
    int status = LOG_SUCCESS;
    int display_count;
    int i, j;
    
    /* Find repository root */
    if (!find_repo_root(repo_root, sizeof(repo_root))) {
        cli_error("Not in an embedding repository");
        return LOG_ERROR_REPO;
    }
    
    /* Make sure file path is relative to repository root */
    rel_path = file_path;
    root_len = strlen(repo_root);
    if (strncmp(file_path, repo_root, root_len) == 0) {
        rel_path = file_path + root_len;
        if (*rel_path == '/')
            rel_path++;
    }
    
    DEBUG_PRINT("show_log: repo_root=%s, rel_path=%s", repo_root, rel_path);
    
    /* Open log file */
    snprintf(log_path, sizeof(log_path), "%s/.eb/log", repo_root);
    
    f = fopen(log_path, "r");
    if (!f) {
        printf("No log found for %s\n", rel_path);
        return LOG_SUCCESS;
    }
    
    /* Read the index to determine current hashes */
    snprintf(index_path, sizeof(index_path), "%s/.eb/index", repo_root);
    
    idx_file = fopen(index_path, "r");
    if (idx_file) {
        char idx_line[PATH_MAX + 65];
        while (fgets(idx_line, sizeof(idx_line), idx_file)) {
            /* Remove newline */
            idx_line[strcspn(idx_line, "\n")] = 0;
            
            char idx_path[PATH_MAX] = {0};
            char idx_hash[65] = {0};
            
            if (sscanf(idx_line, "%s %s", idx_hash, idx_path) == 2 && 
                strcmp(idx_path, rel_path) == 0) {
                
                /* Get metadata to determine model */
                char* metadata = get_metadata(repo_root, idx_hash);
                if (metadata) {
                    char* provider_line = strstr(metadata, "provider=");
                    if (provider_line) {
                        char provider[32] = {0};
                        if (sscanf(provider_line, "provider=%31s", provider) == 1) {
                            /* Add this as a current hash */
                            char** new_models = realloc(current_models, 
                                              (current_model_count + 1) * sizeof(char*));
                            char** new_hashes = realloc(current_hashes, 
                                              (current_model_count + 1) * sizeof(char*));
                            
                            if (!new_models || !new_hashes) {
                                free(metadata);
                                free_current_model_data(current_models, current_hashes, 
                                                       current_model_count);
                                fclose(idx_file);
                                fclose(f);
                                return LOG_ERROR_MEMORY;
                            }
                            
                            current_models = new_models;
                            current_hashes = new_hashes;
                            
                            current_models[current_model_count] = strdup(provider);
                            current_hashes[current_model_count] = strdup(idx_hash);
                            
                            if (!current_models[current_model_count] || 
                                !current_hashes[current_model_count]) {
                                free(metadata);
                                free_current_model_data(current_models, current_hashes, 
                                                       current_model_count);
                                fclose(idx_file);
                                fclose(f);
                                return LOG_ERROR_MEMORY;
                            }
                            
                            current_model_count++;
                        }
                    }
                    free(metadata);
                }
            }
        }
        fclose(idx_file);
    }
    
    /* Read all log entries for this file */
    while (fgets(line, sizeof(line), f)) {
        /* Remove newline */
        line[strcspn(line, "\n")] = 0;
        
        /* Parse line */
        char file_path[PATH_MAX] = {0};
        char hash[65] = {0};
        char timestamp_str[32] = {0};
        char provider[32] = {0};
        
        int parsed = sscanf(line, "%s %s %s %s", timestamp_str, hash, file_path, provider);
        
        if (parsed >= 3 && strcmp(file_path, rel_path) == 0) {
            /* Skip if not matching model filter */
            if (model_filter && (parsed < 4 || strcmp(provider, model_filter) != 0))
                continue;
            
            /* Parse timestamp */
            time_t timestamp = (time_t)atol(timestamp_str);
            
            /* Add to entries */
            log_entry_t* new_entries = realloc(entries, 
                                        (entry_count + 1) * sizeof(log_entry_t));
            if (!new_entries) {
                free_current_model_data(current_models, current_hashes, current_model_count);
                free(entries);
                fclose(f);
                return LOG_ERROR_MEMORY;
            }
            
            entries = new_entries;
            strncpy(entries[entry_count].hash, hash, sizeof(entries[entry_count].hash));
            
            if (parsed >= 4) {
                strncpy(entries[entry_count].provider, provider, 
                        sizeof(entries[entry_count].provider));
            } else {
                strncpy(entries[entry_count].provider, "unknown", 
                        sizeof(entries[entry_count].provider));
            }
            
            entries[entry_count].timestamp = timestamp;
            
            /* Check if this is a current hash */
            entries[entry_count].is_current = false;
            for (i = 0; i < current_model_count; i++) {
                if (strcmp(current_hashes[i], hash) == 0) {
                    entries[entry_count].is_current = true;
                    break;
                }
            }
            
            entry_count++;
        }
    }
    
    fclose(f);
    
    /* Sort entries by timestamp (newest first) */
    if (entry_count > 0)
        qsort(entries, entry_count, sizeof(log_entry_t), compare_entries_by_time);
    
    /* Apply limit if specified */
    display_count = entry_count;
    if (limit > 0 && limit < entry_count)
        display_count = limit;
    
    /* Display log */
    if (display_count == 0) {
        printf("No log found for %s", rel_path);
        if (model_filter)
            printf(" with model %s", model_filter);
        printf("\n");
    } else {
        /* Group entries by model/provider */
        char** unique_models = NULL;
        int* model_entry_counts = NULL;
        int unique_model_count = 0;
        
        /* Find all unique models */
        for (i = 0; i < display_count; i++) {
            bool found = false;
            for (j = 0; j < unique_model_count; j++) {
                if (strcmp(entries[i].provider, unique_models[j]) == 0) {
                    model_entry_counts[j]++;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                /* Add new model */
                char** new_models = realloc(unique_models, 
                                 (unique_model_count + 1) * sizeof(char*));
                int* new_counts = realloc(model_entry_counts, 
                                (unique_model_count + 1) * sizeof(int));
                
                if (!new_models || !new_counts) {
                    free(unique_models);
                    free(model_entry_counts);
                    free_current_model_data(current_models, current_hashes, current_model_count);
                    free(entries);
                    return LOG_ERROR_MEMORY;
                }
                
                unique_models = new_models;
                model_entry_counts = new_counts;
                
                unique_models[unique_model_count] = strdup(entries[i].provider);
                if (!unique_models[unique_model_count]) {
                    free(unique_models);
                    free(model_entry_counts);
                    free_current_model_data(current_models, current_hashes, current_model_count);
                    free(entries);
                    return LOG_ERROR_MEMORY;
                }
                
                model_entry_counts[unique_model_count] = 1;
                unique_model_count++;
            }
        }
        
        /* For each model, if no entry is marked as current, mark the most recent one */
        for (i = 0; i < unique_model_count; i++) {
            bool has_current = false;
            
            /* Check if the model has a current entry */
            for (j = 0; j < display_count; j++) {
                if (strcmp(entries[j].provider, unique_models[i]) == 0 && entries[j].is_current) {
                    has_current = true;
                    break;
                }
            }
            
            /* If no current entry found, mark the most recent one */
            if (!has_current) {
                for (j = 0; j < display_count; j++) {
                    if (strcmp(entries[j].provider, unique_models[i]) == 0) {
                        /* This is the first (most recent) entry for this model */
                        entries[j].is_current = true;
                        
                        DEBUG_PRINT("No current entry found for model %s, marking %s as current",
                                   unique_models[i], entries[j].hash);
                        break;
                    }
                }
            }
        }
        
        printf("Embedding log for %s\n\n", rel_path);
        
        /* Display each model's log */
        for (i = 0; i < unique_model_count; i++) {
            if (i > 0)
                printf("\n");
                
            printf(TEXT_BOLD "Model: %s" COLOR_RESET "\n", unique_models[i]);
            printf("--------------------\n");
            
            int displayed = 0;
            for (j = 0; j < display_count; j++) {
                if (strcmp(entries[j].provider, unique_models[i]) == 0) {
                    char time_str[32];
                    format_time(entries[j].timestamp, time_str, sizeof(time_str));
                    
                    if (entries[j].is_current) {
                        printf(COLOR_BOLD_GREEN "* %.7s" COLOR_RESET, entries[j].hash);
                    } else {
                        printf("  %.7s", entries[j].hash);
                    }
                    
                    printf(" %s", time_str);
                    
                    if (verbose) {
                        /* Get and display metadata */
                        char* metadata = get_metadata(repo_root, entries[j].hash);
                        if (metadata) {
                            display_metadata(metadata);
                            free(metadata);
                        }
                    }
                    
                    printf("\n");
                    displayed++;
                    
                    /* Apply per-model limit if needed */
                    if (limit > 0 && displayed >= limit)
                        break;
                }
            }
            
            /* Show count info if there are more entries than displayed */
            if (displayed < model_entry_counts[i]) {
                printf("\n(Showing %d of %d entries for this model. Use --limit 0 to see all.)\n", 
                       displayed, model_entry_counts[i]);
            }
        }
        
        /* Clean up model data */
        for (i = 0; i < unique_model_count; i++) {
            free(unique_models[i]);
        }
        free(unique_models);
        free(model_entry_counts);
    }
    
    /* Cleanup */
    free_current_model_data(current_models, current_hashes, current_model_count);
    free(entries);
    
    return status;
}

int cmd_log(int argc, char** argv) {
    const char* model_filter;
    const char* limit_str;
    bool verbose;
    int limit = 0;  /* Default: show all */
    char** files = NULL;
    int file_count = 0;
    int status = LOG_SUCCESS;
    int i;
    int file_status;
    
    /* Handle help option */
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", LOG_USAGE);
        return (argc < 2) ? LOG_ERROR_ARGS : LOG_SUCCESS;
    }
    
    /* Parse options */
    model_filter = get_option_value(argc, argv, "-m", "--model");
    limit_str = get_option_value(argc, argv, "-l", "--limit");
    verbose = has_option(argc, argv, "-v") || has_option(argc, argv, "--verbose");
    
    if (limit_str) {
        limit = atoi(limit_str);
        if (limit < 0)
            limit = 0;  /* Invalid limit, show all */
    }
    
    /* Get files to process */
    for (i = 1; i < argc; i++) {
        /* Skip options and their values */
        if (argv[i][0] == '-') {
            if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--model") == 0 ||
                 strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--limit") == 0) && 
                i + 1 < argc) {
                i++;  /* Skip option value */
            }
            continue;
        }
        
        /* This is a file argument */
        char** new_files = realloc(files, (file_count + 1) * sizeof(char*));
        if (!new_files) {
            free(files);
            cli_error("Memory allocation failed");
            return LOG_ERROR_MEMORY;
        }
        
        files = new_files;
        files[file_count] = argv[i];
        file_count++;
    }
    
    /* If no files specified, show usage */
    if (file_count == 0) {
        printf("%s", LOG_USAGE);
        free(files);
        return LOG_ERROR_ARGS;
    }
    
    /* Process each file */
    for (i = 0; i < file_count; i++) {
        if (i > 0)
            printf("\n");  /* Add separator between files */
            
        file_status = show_log(files[i], model_filter, limit, verbose);
        if (file_status != LOG_SUCCESS)
            status = file_status;
    }
    
    free(files);
    return status;
} 