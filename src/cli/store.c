/*
 * EmbeddingBridge - Store Command Implementation
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
#include <inttypes.h>  // For PRIx64
#include "cli.h"
#include "../core/store.h"
#include "../core/error.h"
#include "../core/embedding.h"
#include "../core/git_types.h"
#include "colors.h"  // For colored output
#include <getopt.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>  // For PATH_MAX
#include <time.h>
#include "../core/debug.h"  // For DEBUG_PRINT
#include "../core/path_utils.h"
#include <npy_array_list.h>  // Changed from npy_array.h
#include <linux/limits.h>  // For PATH_MAX
#include <openssl/sha.h>  // For SHA256

// Add after the includes, before any functions
#define MAX_LINE_LEN 2048
#define MAX_PATH_LEN PATH_MAX
#define MAX_HASH_LEN 65

// Function declarations
eb_status_t eb_git_get_metadata(const char* file_path, eb_git_metadata_t** meta);

static const char* STORE_USAGE = 
    "Usage: embr store [options] <embedding> <file>\n"
    "\n"
    "Store embeddings for documents\n"
    "\n"
    "Options:\n"
    "  -d, --dims <dims>     Dimensions for .bin files (required)\n"
    "  -m, --model <name>    Model name to record with embedding\n" 
    "  -v, --verbose         Show detailed output\n"
    "  -q, --quiet           Suppress warning messages\n"
    "  -h, --help            Show this help message\n"
    "\n"
    "Arguments:\n"
    "  <embedding>           Precomputed embedding file (.bin or .npy)\n"
    "  <file>                Original document file\n"
    "\n"
    "Examples:\n"
    "  embr store vector.bin -d 1536 doc.txt    # Store binary embedding\n"
    "  embr store vector.npy doc.txt            # Store numpy embedding\n"
    "  embr store -m openai-3 vector.npy doc.txt  # Specify model name\n";

static bool validate_file(const char* file_path, bool quiet) {
    struct stat st;
    int err;

    err = stat(file_path, &st);
    if (err) {
        if (!quiet)
            cli_error("%s: %s", file_path, strerror(errno));
        return false;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!quiet)
            cli_error("%s: Is a directory", file_path);
        return false;
    }

    if (st.st_size == 0) {
        if (!quiet)
            cli_error("%s: Empty file", file_path);
        return false;
    }

    if (st.st_size > 10 * 1024 * 1024) {
        if (!quiet)
            cli_error("%s: File too large (max 10MB)", file_path);
        return false;
    }

    return true;
}

/* Write metadata in JSON format */
static int write_metadata(const char *meta_path, const char *source_file, const char *model_type) {
    FILE *f = fopen(meta_path, "w");
    if (!f)
        return -errno;

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    /* Write proper JSON format */
    fprintf(f, "{\n");
    fprintf(f, "  \"source\": \"%s\",\n", source_file);
    fprintf(f, "  \"timestamp\": \"%s\",\n", timestamp);
    fprintf(f, "  \"model\": \"%s\"\n", model_type ? model_type : "default");
    fprintf(f, "}\n");

    if (ferror(f)) {
        fclose(f);
        return -EIO;
    }

    fclose(f);
    return 0;
}

/* Data format handlers for different embedding types */
struct embedding_handler {
    const char *suffix;
    bool (*store)(const char *src, const char *dst);
    bool (*validate)(const char *path);
};

/* Store raw float32 data from .npy file */
static bool store_npy_data(const char *src_path, const char *dst_path) 
{
    DEBUG_PRINT("Storing .npy file: %s -> %s", src_path, dst_path);
    
    // Use npy_array library to load the file
    npy_array_t *src_arr = npy_array_load(src_path);
    if (!src_arr) {
        DEBUG_PRINT("Failed to load .npy file");
        return false;
    }

    // Validate format - we expect float32 1D arrays
    if (src_arr->typechar != 'f' || src_arr->ndim != 1) {
        DEBUG_PRINT("Invalid .npy format: type=%c dims=%d (expected: type=f dims=1)", 
                   src_arr->typechar, src_arr->ndim);
        npy_array_free(src_arr);
        return false;
    }

    // Get dimensions and data
    size_t float_count = src_arr->shape[0];
    float *data = (float*)src_arr->data;

    // Calculate hash of just the float data
    unsigned char hash[32];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, float_count * sizeof(float));
    SHA256_Final(hash, &sha256);
    
    char hash_str[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hash_str[i * 2], "%02x", hash[i]);
    }
    hash_str[64] = '\0';
    
    DEBUG_PRINT("Generated hash from data only: %s", hash_str);

    // Create array structure
    npy_array_t array = {
        .data = (char*)data,
        .shape = { float_count },
        .ndim = 1,
        .typechar = 'f',
        .elem_size = sizeof(float),
        .fortran_order = false
    };

    // Save using npy_array library
    npy_array_save(dst_path, &array);

    // Cleanup
    npy_array_free(src_arr);
    return true;
}

/* Store raw binary data */
static bool store_bin_data(const char *src_path, const char *dst_path)
{
    DEBUG_PRINT("Storing binary file: %s -> %s", src_path, dst_path);
    
    struct stat st;
    if (stat(src_path, &st) != 0) {
        DEBUG_PRINT("Cannot stat source file");
        return false;
    }

    // Validate size is multiple of float
    if (st.st_size % sizeof(float) != 0) {
        DEBUG_PRINT("Invalid binary size: %ld", (long)st.st_size);
        return false;
    }

    FILE *src = fopen(src_path, "rb");
    FILE *dst = fopen(dst_path, "wb");
    if (!src || !dst) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        return false;
    }

    // Copy in chunks of floats for validation
    float buffer[4096];
    size_t floats_per_read = sizeof(buffer) / sizeof(float);
    size_t total_floats = 0;

    while (!feof(src)) {
        size_t read = fread(buffer, sizeof(float), floats_per_read, src);
        if (read > 0) {
            // Validate values
            for (size_t i = 0; i < read && total_floats < 5; i++) {
                DEBUG_PRINT("Value[%zu] = %f", total_floats + i, buffer[i]);
            }
            
            if (fwrite(buffer, sizeof(float), read, dst) != read) {
                DEBUG_PRINT("Write failed");
                fclose(src);
                fclose(dst);
                return false;
            }
            total_floats += read;
        }
    }

    DEBUG_PRINT("Copied %zu floats", total_floats);
    
    fclose(src);
    fclose(dst);
    return true;
}

static const struct embedding_handler handlers[] = {
    { ".npy", store_npy_data, NULL },
    { ".bin", store_bin_data, NULL },
    { NULL, NULL, NULL }
};

static bool ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (str_len < suffix_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static bool cli_store_embedding_file(const char *embedding_path, const char *source_file,
                               const char *base_dir, const char *model) {
    DEBUG_PRINT("cli_store_embedding_file: Starting storage operation");
    DEBUG_PRINT("  embedding_path: %s", embedding_path);
    DEBUG_PRINT("  source_file: %s", source_file);
    DEBUG_PRINT("  base_dir: %s", base_dir);
    DEBUG_PRINT("  model: %s", model ? model : "unknown");

    // Call the core implementation
    eb_status_t status = store_embedding_file(embedding_path, source_file, base_dir, model);
    if (status != EB_SUCCESS) {
        cli_error("Failed to store embedding");
        return false;
    }

    printf("✓ %s\n", source_file);
    return true;
}

int store_precomputed(const char *embedding_file, size_t dims, const char *source_file, const char *model) {
    DEBUG_PRINT("store_precomputed: embedding_file=%s, source_file=%s", 
                embedding_file, source_file);

    if (!embedding_file || !source_file) {
        cli_error("Invalid input parameters");
        return EB_ERROR_INVALID_INPUT;
    }

    // Find repository root first
    char* repo_root = find_repo_root(".");
    if (!repo_root) {
        cli_error("Not in an eb repository");
        return EB_ERROR_NOT_INITIALIZED;
    }

    // Get relative paths
    char* rel_source = get_relative_path(source_file, repo_root);
    char* rel_embedding = get_relative_path(embedding_file, repo_root);
    
    if (!rel_source || !rel_embedding) {
        cli_error("Files must be within repository");
        free(repo_root);
        free(rel_source);
        free(rel_embedding);
        return EB_ERROR_INVALID_INPUT;
    }

    // Store the embedding
    eb_status_t status = cli_store_embedding_file(rel_embedding, rel_source, repo_root, model);

    free(repo_root);
    free(rel_source);
    free(rel_embedding);

    return status;
}

int store_from_source(const char *source_file, int argc, char **argv)
{
        char cwd[PATH_MAX];
        char hash[65];
        eb_embedding_t* embedding = NULL;
        int ret = 1;
        const char* model = NULL;
        
        if (!getcwd(cwd, sizeof(cwd))) {
                cli_error("Failed to get current directory");
                return 1;
        }

        if (!validate_file(source_file, false)) {
                return 1;
        }

        // Get model from command line or config
        model = get_model(argc, argv);
        if (!model) {
            fprintf(stderr, "error: no model specified\n");
            fprintf(stderr, "hint: specify a model with --model or configure a default with 'eb config set model.default <name>'\n");
            return 1;
        }

        // Generate embedding using configured model
        eb_status_t status = eb_create_embedding_from_file(source_file, model, &embedding);
        if (status != EB_SUCCESS) {
            cli_error("Failed to create embedding: %s", eb_status_str(status));
                goto cleanup;
        }

        if (!eb_calculate_file_hash(source_file, hash)) {
                cli_error("Failed to calculate file hash");
                goto cleanup;
        }

        DEBUG_PRINT("store_from_source: source_file=%s\n", source_file);

        if (!cli_store_embedding_file(source_file, source_file, cwd, model)) {
                goto cleanup;
        }

        printf("✓ %s (%s)\n", source_file, hash);
        ret = 0;

cleanup:
        if (embedding)
                eb_destroy_embedding(embedding);
        return ret;
}

// Define a context structure to hold parsing results
typedef struct {
    const char *embedding_file;
    const char *source_file;
    const char *model;
    size_t dims;
    bool verbose;
    bool quiet;
} store_context_t;

// Callback for option processing
static int store_option_callback(char short_opt, const char* long_opt, const char* arg, void* ctx) {
    store_context_t* context = (store_context_t*)ctx;
    
    switch(short_opt) {
        case 'm':
            context->model = arg;
            DEBUG_PRINT("Model specified: %s", arg);
            break;
        case 'd':
            context->dims = atoi(arg);
            if (context->dims == 0) {
                fprintf(stderr, "error: Invalid dimensions\n");
                return 1;
            }
            break;
        case 'v':
            context->verbose = true;
            break;
        case 'q':
            context->quiet = true;
            break;
        default:
            // Unknown option
            if (long_opt)
                fprintf(stderr, "Unknown option: %s\n", long_opt);
            else
                fprintf(stderr, "Unknown option: -%c\n", short_opt);
            return 1;
    }
    
    return 0;
}

int cmd_store(int argc, char *argv[])
{
    // Add help option check at the beginning
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", STORE_USAGE);
        return (argc < 2) ? 1 : 0;
    }

    // Define context for option parsing
    store_context_t context = {
        .embedding_file = NULL,
        .source_file = NULL,
        .model = NULL,
        .dims = 0,
        .verbose = false,
        .quiet = false
    };
    
    // Define option definitions
    const char* short_opts = "m:d:vqh";
    const char* long_opts[] = {
        "--model",
        "--dims",
        "--verbose",
        "--quiet",
        "--help",
        NULL  // Must be NULL-terminated
    };
    
    // Array to collect positional arguments
    char* positional[argc];
    int pos_count = 0;
    
    // Parse options using Git-style parser
    int result = parse_git_style_options(
        argc, argv, 
        short_opts, long_opts,
        store_option_callback, &context,
        positional, &pos_count
    );
    
    if (result != 0) {
        return result;
    }
    
    // Process positional arguments
    if (pos_count >= 1) {
        context.embedding_file = positional[0];
        DEBUG_PRINT("Embedding file specified as positional argument: %s", context.embedding_file);
    }
    
    if (pos_count >= 2) {
        context.source_file = positional[1];
        DEBUG_PRINT("Source file specified as positional argument: %s", context.source_file);
    }
    
    // Additional validation
    if (!context.source_file) {
        fprintf(stderr, "error: No source file specified\n");
        return 1;
    }
    
    if (!context.embedding_file) {
        if (!context.quiet) {
            fprintf(stderr, "error: Direct embedding generation not yet supported\n");
            fprintf(stderr, "hint: Specify an embedding file as first positional argument\n");
        }
        return 1;
    }

    // Validate dimensions for .bin files
    if (strstr(context.embedding_file, ".bin")) {
        if (context.dims == 0) {
            // Try to get dimensions from model if specified
            if (context.model && eb_is_model_registered(context.model)) {
                eb_model_info_t model_info;
                if (eb_get_model_info(context.model, &model_info) == EB_SUCCESS) {
                    context.dims = model_info.dimensions;
                    DEBUG_PRINT("Using dimensions %zu from registered model %s", context.dims, context.model);
                }
            }
            
            // If still no dimensions, error out
            if (context.dims == 0) {
                if (!context.quiet) {
                    fprintf(stderr, "error: --dims required for .bin files\n");
                    fprintf(stderr, "hint: Either provide --dims or use a registered model\n");
                }
                return 1;
            }
        }
    }

    if (context.verbose) {
        printf("→ Reading %s\n", context.source_file);
        if (context.dims)
            printf("→ Using embedding with %zu dimensions\n", context.dims);
        if (context.model)
            printf("→ Using model: %s\n", context.model);
    }

    // Find repository root first
    char* repo_root = find_repo_root(".");
    if (!repo_root) {
        if (!context.quiet) {
            fprintf(stderr, "Error: Not in an eb repository\n");
            fprintf(stderr, "hint: Run 'eb init' to create a new repository\n");
        }
        return 1;
    }
    DEBUG_PRINT("Repository root: %s", repo_root);

    // Get relative paths for source and embedding files
    char* rel_source = get_relative_path(context.source_file, repo_root);
    char* rel_embedding = context.embedding_file ? get_relative_path(context.embedding_file, repo_root) : NULL;
    
    if (!rel_source || (context.embedding_file && !rel_embedding)) {
        if (!context.quiet) {
            fprintf(stderr, "Error: Files must be within repository\n");
        }
        free(repo_root);
        free(rel_source);
        free(rel_embedding);
        return 1;
    }

    // Use relative paths for storage
    int process_result;
    if (context.embedding_file) {
        // If model not specified on command line, try to extract from filename
        if (!context.model) {
            // Check if embedding file has a model indicator in its name
            // Format: filename.model.npy or filename.model.bin
            const char* filename = strrchr(rel_embedding, '/');
            if (!filename) filename = rel_embedding;
            else filename++; // Skip the slash
            
            char* last_dot = strrchr(filename, '.');
            if (last_dot) {
                // Go back to previous dot
                char* temp = strdup(filename);
                char* temp_last_dot = strrchr(temp, '.');
                *temp_last_dot = '\0'; // Temporarily terminate string at last dot
                char* prev_dot = strrchr(temp, '.');
                
                if (prev_dot) {
                    // Extract model name between dots
                    context.model = strdup(prev_dot + 1);
                    DEBUG_PRINT("Extracted model from filename: %s", context.model);
                }
                
                free(temp);
            }
        }
        
        // Store with explicit model parameter
        process_result = store_precomputed(rel_embedding, context.dims, rel_source, context.model);
    } else {
        process_result = store_from_source(rel_source, argc, argv);
    }
    
    free(repo_root);
    free(rel_source);
    free(rel_embedding);
    return process_result;
} 