/*
 * EmbeddingBridge - Search Interface
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_SEARCH_H
#define EB_SEARCH_H

#include "types.h"

// Search result structure
typedef struct {
    char* filepath;           // Path to the file
    float similarity;         // Similarity score (0.0 to 1.0)
    char* context;           // Optional context snippet
    char* last_modified;     // Last modification time
} eb_search_result_t;

// Search functions
eb_status_t eb_create_embedding_from_text(
    const char* text,
    const char* model_version,
    eb_embedding_t** out
);

eb_status_t eb_search_embeddings(
    const eb_embedding_t* query,
    float threshold,
    size_t top_k,
    eb_search_result_t** out_results,
    size_t* out_count
);

// Memory management
void eb_destroy_embedding(eb_embedding_t* embedding);
void eb_free_search_results(eb_search_result_t* results, size_t count);

#endif // EB_SEARCH_H 