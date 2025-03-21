/*
 * EmbeddingBridge - Set Merge Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_MERGE_H
#define EB_MERGE_H

#include "../core/types.h"
#include "../core/error.h"
#include <stdbool.h>

/**
 * Main entry point for the merge command
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Status code (0 = success)
 */
int cmd_merge(int argc, char** argv);

/**
 * CLI handler for merge command
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Status code (0 = success)
 */
int handle_merge(int argc, char** argv);

/**
 * Available merge strategies for embedding sets
 */
typedef enum {
    EB_MERGE_UNION,     /* Union of sets, keep target versions for conflicts */
    EB_MERGE_MEAN,      /* Compute element-wise mean for conflict resolution */
    EB_MERGE_MAX,       /* Take element-wise maximum for conflict resolution */
    EB_MERGE_WEIGHTED   /* Apply weighted combination based on metadata */
} eb_merge_strategy_t;

/**
 * Embedding reference structure for tracking files
 */
typedef struct eb_embedding_ref {
    char* source_file;          /* Original source file path */
    char* hash_ref;             /* Hash reference to embedding */
    char* metadata;             /* Optional metadata */
    struct eb_embedding_ref* next;
} eb_embedding_ref_t;

/**
 * Merge result structure with statistics
 */
typedef struct {
    int new_count;             /* Number of new embeddings added */
    int updated_count;         /* Number of embeddings updated */
    int conflict_count;        /* Number of conflicts encountered */
    int error_count;           /* Number of errors encountered */
} eb_merge_result_t;

/**
 * Merge two sets of embeddings
 * 
 * @param source_set Source set to merge from
 * @param target_set Target set to merge into (NULL for current)
 * @param strategy Merge strategy (union, mean, max, weighted)
 * @param result Optional pointer to store merge statistics
 * @return Status code
 */
eb_status_t eb_merge_sets(
    const char* source_set,
    const char* target_set,
    eb_merge_strategy_t strategy,
    eb_merge_result_t* result
);

/**
 * Parse string strategy name to strategy enum
 * 
 * @param strategy_name Name of the strategy (union, mean, max, weighted)
 * @param strategy_out Pointer to store the strategy enum
 * @return true if valid strategy name, false otherwise
 */
bool eb_parse_merge_strategy(
    const char* strategy_name,
    eb_merge_strategy_t* strategy_out
);

/**
 * Get string name for merge strategy
 * 
 * @param strategy Strategy enum
 * @return String name of the strategy
 */
const char* eb_merge_strategy_name(eb_merge_strategy_t strategy);

/**
 * Load embedding references from a set
 * 
 * @param set_path Path to the set directory
 * @param refs_out Pointer to store the loaded references
 * @return Status code
 */
eb_status_t eb_load_embedding_refs(
    const char* set_path,
    eb_embedding_ref_t** refs_out
);

/**
 * Free embedding references linked list
 * 
 * @param refs Head of the references linked list
 */
void eb_free_embedding_refs(eb_embedding_ref_t* refs);

#endif /* EB_MERGE_H */ 