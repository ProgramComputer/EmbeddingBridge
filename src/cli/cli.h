/*
 * EmbeddingBridge - CLI Interface Definitions
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_CLI_H
#define EB_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>  // for getcwd
#include "../core/types.h"
#include "../core/git_types.h"
#include "../core/diff_types.h"
#include "../core/embedding.h"
#include "../core/eb_store.h"

// Version definition is now in types.h

// Command structure definition
typedef struct {
    const char* name;
    const char* description;
    int (*handler)(int argc, char** argv);
} eb_command_t;

typedef struct {
    const char* model;          // Model to use for embedding
    const char* models;         // Models to use for diff (comma-separated)
    const char* second_model;   // Second model parsed from models
    bool use_git;              // Whether to use Git integration
    bool use_file;             // Use file as input (for query)
    bool use_color;            // Use colored output
    bool verbose;              // Show detailed output
    bool quiet;                // Minimal output
    float threshold;           // Similarity threshold
    int top_k;                // Number of results (for query)
    bool interactive;         // Interactive mode
    int k_neighbors;         // Number of neighbors for diff
    bool force;                // Force operation
    const char* embedding_file; // Path to precomputed embedding file
    size_t dimensions;         // Dimensions for .bin embedding file
} eb_cli_options_t;

// Command handlers
int cmd_init(int argc, char** argv);
int cmd_store(int argc, char** argv);
int cmd_diff(int argc, char** argv);
int cmd_query(int argc, char** argv);
int cmd_hooks(int argc, char** argv);
int cmd_config(int argc, char** argv);
int cmd_remote(int argc, char** argv);
int cmd_model(int argc, char** argv);
int cmd_rollback(int argc, char** argv);
int cmd_status(int argc, char **argv);
int cmd_log(int argc, char **argv);
int cmd_set(int argc, char **argv);
int cmd_switch(int argc, char **argv);
int cmd_merge(int argc, char **argv);
int cmd_gc(int argc, char **argv);

// Option parsing
bool parse_cli_options(int argc, char** argv, eb_cli_options_t* opts);
const char* get_option_value(int argc, char** argv, const char* short_opt, const char* long_opt);
bool has_option(int argc, char** argv, const char* option);
bool file_exists(const char* path);
bool is_option_with_value(const char* arg);
float get_float_option(int argc, char** argv, const char* short_opt, const char* long_opt, float default_value);
int get_int_option(int argc, char** argv, const char* short_opt, const char* long_opt, int default_value);
const char* get_model(int argc, char** argv);

// Error handling
void handle_error(eb_status_t status, const char* context);
void cli_error(const char* fmt, ...);
void cli_warning(const char* fmt, ...);
void cli_info(const char* fmt, ...);

// Store operation functions
/* Store precomputed embedding file
 * @param embedding_file: Path to the precomputed embedding (.bin or .npy)
 * @param dims: Number of dimensions (0 for .npy auto-detect)
 * @param source_file: Original source file
 * @param model: Model/provider name (can be NULL)
 * @return: 0 on success, 1 on error
 */
int store_precomputed(const char *embedding_file, size_t dims, const char *source_file, const char *model);

/* Store embedding from source file
 * @param source_file: Source file to generate embedding from
 * @param argc: Argument count for model selection
 * @param argv: Argument vector for model selection
 * @return: 0 on success, 1 on error
 */
int store_from_source(const char *source_file, int argc, char **argv);

#endif // EB_CLI_H 