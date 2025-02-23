/*
 * EmbeddingBridge - Core Storage Header
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_STORE_H
#define EB_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"
#include "embedding.h"

#define EB_INVALID_ARGS EB_ERROR_INVALID_INPUT
#define EB_NOT_FOUND EB_ERROR_NOT_FOUND

/* Core store structure */
struct eb_store {
        char* storage_path;          /* Path to storage root */
        eb_stored_vector_t* vectors; /* Vector storage array */
        size_t vector_count;         /* Number of stored vectors */
};

/*
 * Core initialization and cleanup
 * These must be called before/after using any other functions
 */
eb_status_t eb_store_init(const eb_store_config_t* config, eb_store_t** out);
eb_status_t eb_store_destroy(eb_store_t* store);

/*
 * Vector storage and retrieval
 * Main functions for storing and accessing embedding vectors
 */
eb_status_t eb_store_vector(
        eb_store_t* store,
        const eb_embedding_t* embedding,
        const eb_metadata_t* metadata,
        const char* model_version,
        uint64_t* out_id
);

eb_status_t eb_get_vector(
        eb_store_t* store,
        uint64_t vector_id,
        eb_embedding_t** out_embedding,
        eb_metadata_t** out_metadata
);

eb_status_t eb_store_get_latest(
        eb_store_t* store,
        const char* file,
        eb_stored_vector_t** vectors
);

void eb_destroy_stored_vectors(eb_stored_vector_t* versions, size_t count);

/*
 * Hash management and resolution
 * Functions for working with content-addressable storage
 */
eb_status_t eb_store_resolve_hash(
        eb_store_t* store,
        const char* partial_hash,
        char full_hash[65],  /* Output buffer for full hash (64 chars + null) */
        size_t buf_size
);

eb_status_t eb_store_get_path(
        eb_store_t* store,
        const char* hash,
        char* path_out,
        size_t path_size
);

/* Get current hash for a source file */
eb_status_t get_current_hash(const char* root, const char* source, 
                           char* hash, size_t hash_size); 

/*
 * Metadata management
 * Functions for storing and retrieving metadata
 */
eb_status_t eb_store_metadata(
        eb_store_t* store,
        const eb_metadata_t* metadata,
        char out_hash[65]  /* Output buffer for metadata hash */
);

eb_status_t eb_get_metadata(
        eb_store_t* store,
        const char* hash,
        eb_metadata_t** out_metadata
);

/*
 * Reference management
 * Functions for managing relationships between vectors and metadata
 */
eb_status_t eb_update_refs(
        eb_store_t* store,
        const char* vector_hash,
        const char* meta_hash,
        const char* model_version
);

eb_status_t eb_get_ref(
        eb_store_t* store,
        const char* vector_hash,
        char meta_hash[65]  /* Output buffer for metadata hash */
);

#ifdef EB_ENABLE_MEMORY_STORE
/*
 * Memory-only storage backend
 * Used for testing and temporary storage
 */
eb_status_t eb_store_init_memory(eb_store_t** out);

eb_status_t eb_store_vector_memory(
        eb_store_t* store,
        const eb_embedding_t* embedding,
        const eb_metadata_t* metadata,
        const char* model_version,
        uint64_t* out_id
);

eb_status_t eb_get_vector_memory(
        eb_store_t* store,
        uint64_t vector_id,
        eb_embedding_t** out_embedding,
        eb_metadata_t** out_metadata
);
#endif /* EB_ENABLE_MEMORY_STORE */

/* Store metadata object */
eb_status_t store_embedding_file(const char* embedding_path,
                                const char* source_file,
                                const char* base_dir);

eb_status_t get_version_history(const char* root, const char* source, 
                              eb_stored_vector_t** out_versions, size_t* out_count); 

eb_status_t read_object(eb_store_t* store,
                       const char* hash,
                       void** out_data,
                       size_t* out_size,
                       eb_object_header_t* out_header); 

#endif /* EB_STORE_H */