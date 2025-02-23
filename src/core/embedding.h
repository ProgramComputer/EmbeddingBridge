/*
 * EmbeddingBridge - Embedding Core Functions
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EMBEDDING_BRIDGE_EMBEDDING_H
#define EMBEDDING_BRIDGE_EMBEDDING_H

#include "error.h"  // Include error.h first
#include "types.h"  // Then include types.h
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

// Model information structure
typedef struct {
    size_t dimensions;          // Number of dimensions in the model's output
    bool normalize_output;      // Whether model output should be normalized
    char* version;             // Model version string
    char* description;         // Model description
} eb_model_info_t;

// Model registry functions
eb_status_t eb_register_model(
    const char* name,
    size_t dimensions,
    bool normalize,
    const char* version,
    const char* description
);
void eb_unregister_model(const char* name);
bool eb_is_model_registered(const char* name);
eb_status_t eb_list_models(char*** out_names, size_t* out_count);
void eb_cleanup_registry(void);

// Function declarations
eb_status_t eb_get_model_info(const char* model_name, eb_model_info_t* out_info);
eb_status_t eb_generate_embedding(const char* text, const char* model_name, eb_embedding_t** out_embedding);
eb_status_t eb_create_embedding_from_file(const char* filepath, const char* model_name, eb_embedding_t** out_embedding);
eb_status_t eb_normalize_embedding(eb_embedding_t* embedding);

/**
 * Find similar model names to the given name
 * @param name The model name to find similar matches for
 * @param similar_names Array to store similar model names (caller must free each string and array)
 * @param count Number of similar names found
 * @return EB_SUCCESS on success, error code otherwise
 */
eb_status_t eb_find_similar_models(const char* name, char*** similar_names, size_t* count);

bool eb_calculate_file_hash(const char *file_path, char *hash_out);

#endif // EMBEDDING_BRIDGE_EMBEDDING_H