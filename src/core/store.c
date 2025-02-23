/*
 * EmbeddingBridge - Core Storage Implementation
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <dirent.h>
#include "types.h"
#include "debug.h"
#include "eb_store.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define HASH_TABLE_SIZE 1024
#define VECTOR_FILE_EXTENSION ".ebv"
#define METADATA_FILE_EXTENSION ".ebm"
#define EB_VECTOR_MAGIC 0x4542564D  // "EBVM" in ASCII

/* Forward declarations for internal functions */
static void hash_data(const float* values, size_t size, uint8_t* hash);
static void hash_bytes(const unsigned char* data, size_t size, uint8_t* hash);
static eb_status_t calculate_file_hash(const char* file_path, char* hash_out, size_t hash_size);
static eb_status_t copy_file(const char* src, const char* dst);
static eb_status_t append_to_history(const char* root, const char* source, const char* hash);
static eb_status_t create_binary_delta(const char* base_path, const char* new_path, const char* delta_path);
static eb_status_t apply_binary_delta(const char* base_path, const char* delta_path, const char* output_path);

/* Function implementations */
static void hash_data(const float* values, size_t size, uint8_t* hash) {
    hash_bytes((const unsigned char*)values, size * sizeof(float), hash);
}

static void hash_bytes(const unsigned char* data, size_t size, uint8_t* hash) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        DEBUG_PRINT("Failed to create EVP context\n");
        return;
    }

    const EVP_MD* md = EVP_sha256();
    if (!md) {
        DEBUG_PRINT("Failed to get SHA256 algorithm\n");
        EVP_MD_CTX_free(ctx);
        return;
    }
    
    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        DEBUG_PRINT("Failed to initialize digest\n");
        EVP_MD_CTX_free(ctx);
        return;
    }

    if (EVP_DigestUpdate(ctx, data, size) != 1) {
        DEBUG_PRINT("Failed to update digest\n");
        EVP_MD_CTX_free(ctx);
        return;
    }

    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        DEBUG_PRINT("Failed to finalize digest\n");
    }

    EVP_MD_CTX_free(ctx);
}

static uint64_t generate_id(const void* data, size_t size) {
    uint8_t hash[32];
    hash_data((const float*)data, size, hash);
    return *(uint64_t*)hash;  // Use first 8 bytes of hash as ID
}

static char* hash_to_hex(const uint8_t hash[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i*2] = hex[hash[i] >> 4];
        out[i*2 + 1] = hex[hash[i] & 0xf];
    }
    out[64] = '\0';
    return out;
}

static char* create_object_path(const char* root, const char* hex_hash) {
    // Format: <root>/objects/xx/xxxxxx...
    size_t len = strlen(root) + 64;
    char* path = malloc(len);
    if (!path) return NULL;
    
    snprintf(path, len, "%s/objects/%.2s/%s", root, hex_hash, hex_hash + 2);
    return path;
}

static eb_status_t check_directories(const char* root) {
    char path[4096];
    struct stat st;
    
    // Main .eb directory
    snprintf(path, sizeof(path), "%s/.eb", root);
    DEBUG_PRINT("Checking main .eb directory: %s\n", path);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        DEBUG_PRINT("Main .eb directory check failed: %s\n", strerror(errno));
        return EB_ERROR_NOT_INITIALIZED;
    }
    
    // Required subdirectories
    const char* dirs[] = {
        "/objects",
        "/objects/temp",
        "/metadata",
        "/metadata/files",
        "/metadata/models",
        "/metadata/versions"
    };
    
    for (size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/.eb%s", root, dirs[i]);
        DEBUG_PRINT("Checking directory: %s\n", path);
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            DEBUG_PRINT("Directory check failed: %s - %s\n", path, strerror(errno));
            return EB_ERROR_NOT_INITIALIZED;
        }
    }
    
    DEBUG_PRINT("All directory checks passed\n");
    return EB_SUCCESS;
}

/* Core storage functions */

eb_status_t eb_store_init(const eb_store_config_t* config, eb_store_t** out) {
    if (!config || !out) {
        DEBUG_PRINT("Invalid input to eb_store_init: config=%p, out=%p\n", 
                (void*)config, (void*)out);
        return EB_ERROR_INVALID_INPUT;
    }
    
    DEBUG_PRINT("Initializing store at path: %s\n", config->root_path);
    
    // Special case for memory store
    if (strcmp(config->root_path, ":memory:") == 0) {
        DEBUG_PRINT("DEBUG: Using memory store\n");
        #ifdef EB_ENABLE_MEMORY_STORE
        return eb_store_init_memory(out);
        #else
        return EB_ERROR_INVALID_INPUT;
        #endif
    }
    
    // Check if directory structure exists
    eb_status_t status = check_directories(config->root_path);
    if (status != EB_SUCCESS) {
        DEBUG_PRINT("DEBUG: Directory check failed with status: %d\n", status);
        return status;
    }
    
    // Create store structure
    eb_store_t* store = malloc(sizeof(eb_store_t));
    if (!store) {
        DEBUG_PRINT("DEBUG: Failed to allocate store structure\n");
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    store->storage_path = strdup(config->root_path);
    if (!store->storage_path) {
        DEBUG_PRINT("DEBUG: Failed to duplicate storage path\n");
        free(store);
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    // Initialize hash table
    store->vectors = calloc(HASH_TABLE_SIZE, sizeof(eb_stored_vector_t));
    if (!store->vectors) {
        DEBUG_PRINT("DEBUG: Failed to allocate hash table\n");
        free(store->storage_path);
        free(store);
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    store->vector_count = 0;
    *out = store;
    DEBUG_PRINT("DEBUG: Store initialized successfully\n");
    return EB_SUCCESS;
}

eb_status_t eb_store_destroy(eb_store_t* store) {
    if (!store) return EB_SUCCESS;
    
    // Free all stored vectors
    for (size_t i = 0; i < HASH_TABLE_SIZE; i++) {
        eb_stored_vector_t* current = &store->vectors[i];
        while (current) {
            eb_stored_vector_t* next = current->next;
            eb_destroy_embedding(current->embedding);
            
            // Free metadata
            eb_metadata_t* meta = current->metadata;
            while (meta) {
                eb_metadata_t* next_meta = meta->next;
                free(meta->key);
                free(meta->value);
                free(meta);
                meta = next_meta;
            }
            
            free(current->model_version);
            if (current != &store->vectors[i]) {
                free(current);
            }
            current = next;
        }
    }
    
    free(store->storage_path);
    free(store->vectors);
    free(store);
    return EB_SUCCESS;
}

void eb_destroy_stored_vectors(eb_stored_vector_t* versions, size_t count) {
    if (!versions) return;
    
    for (size_t i = 0; i < count; i++) {
        eb_stored_vector_t* version = &versions[i];
        
        // Don't free the embedding or metadata as they're owned by the store
        // Just free strings that were duplicated during retrieval
        free(version->model_version);
    }
    
    free(versions);
}

/* Write object to temporary file, then move to final location */
static eb_status_t write_object(
    eb_store_t* store,
    const void* data,
    size_t size,
    uint32_t obj_type,
    uint32_t flags,
    char out_hash[65]
) {
    uint8_t hash[32];
    hash_data((const float*)data, size, hash);
    hash_to_hex(hash, out_hash);
    
    // Create temporary file path
    char temp_path[4096];
    snprintf(temp_path, sizeof(temp_path), "%s/.eb/objects/temp/tmp-%s",
             store->storage_path, out_hash);
             
    // Create final object path
    char* obj_path = create_object_path(store->storage_path, out_hash);
    if (!obj_path) return EB_ERROR_MEMORY_ALLOCATION;
    
    // Check if object already exists
    struct stat st;
    if (stat(obj_path, &st) == 0) {
        free(obj_path);
        return EB_SUCCESS;  // Object already exists
    }
    
    // Create object header
    eb_object_header_t header = {
        .magic = EB_VECTOR_MAGIC,
        .version = EB_VERSION,
        .obj_type = obj_type,
        .flags = flags,
        .size = size
    };
    memcpy(header.hash, hash, 32);
    
    // Write to temporary file
    FILE* fp = fopen(temp_path, "wb");
    if (!fp) {
        free(obj_path);
        return EB_ERROR_FILE_IO;
    }
    
    // Write header and data
    if (fwrite(&header, sizeof(header), 1, fp) != 1 ||
        fwrite(data, size, 1, fp) != 1) {
        fclose(fp);
        unlink(temp_path);
        free(obj_path);
        return EB_ERROR_FILE_IO;
    }
    
    fclose(fp);
    
    // Create directory if needed
    char dir_path[4096];
    snprintf(dir_path, sizeof(dir_path), "%s/.eb/objects/%.2s",
             store->storage_path, out_hash);
             
    #ifdef _WIN32
    _mkdir(dir_path);
    #else
    mkdir(dir_path, 0755);
    #endif
    
    // Move to final location (atomic operation)
    if (rename(temp_path, obj_path) != 0) {
        unlink(temp_path);
        free(obj_path);
        return EB_ERROR_FILE_IO;
    }
    
    free(obj_path);
    return EB_SUCCESS;
}

/* Add this function if it doesn't exist */
static eb_status_t append_to_history(const char* root, const char* source, const char* hash) {
    char history_path[PATH_MAX];
    snprintf(history_path, sizeof(history_path), "%s/.eb/history", root);
    
    // Create history file if it doesn't exist
    FILE* f = fopen(history_path, "a+");
    if (!f) {
        return EB_ERROR_FILE_IO;
    }

    // Get current time in ISO 8601 format
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    // Write entry with timestamp and provider
    fprintf(f, "%s %s %s openai\n", source, hash, timestamp);
    fclose(f);

    return EB_SUCCESS;
}

/* Store vector in content-addressable storage */
eb_status_t eb_store_vector(
    eb_store_t* store,
    const eb_embedding_t* embedding,
    const eb_metadata_t* metadata,
    const char* model_version,
    uint64_t* out_id
) {
    if (!store || !embedding || !model_version || !out_id) {
        return EB_ERROR_INVALID_INPUT;
    }
    
    // Special case for memory store
    if (strcmp(store->storage_path, ":memory:") == 0) {
        #ifdef EB_ENABLE_MEMORY_STORE
        return eb_store_vector_memory(store, embedding, metadata, model_version, out_id);
        #else
        return EB_ERROR_INVALID_INPUT;
        #endif
    }
    
    // Calculate total size
    size_t data_size = embedding->dimensions * sizeof(float);
    
    // Generate ID from data
    uint8_t hash[32];
    hash_data(embedding->values, data_size, hash);
    *out_id = *(uint64_t*)hash;  // Use first 8 bytes as ID
    
    // Write vector object
    char hex_hash[65];
    eb_status_t status = write_object(
        store,
        embedding->values,
        data_size,
        EB_OBJ_VECTOR,
        embedding->normalize ? 0x01 : 0x00,
        hex_hash
    );
    if (status != EB_SUCCESS) return status;
    
    // Find source file from metadata
    const char* source_file = NULL;
    const eb_metadata_t* meta = metadata;
    while (meta) {
        if (strcmp(meta->key, "source") == 0) {
            source_file = meta->value;
            break;
        }
        meta = meta->next;
    }

    // Create new metadata with source if not present
    eb_metadata_t* new_metadata = NULL;
    if (metadata) {
        // Copy existing metadata
        const eb_metadata_t* curr = metadata;
        eb_metadata_t* last = NULL;
        while (curr) {
            eb_metadata_t* copy;
            status = eb_metadata_create(curr->key, curr->value, &copy);
            if (status != EB_SUCCESS) {
                eb_metadata_destroy(new_metadata);
                return status;
            }
            if (!new_metadata) {
                new_metadata = copy;
            } else {
                last->next = copy;
            }
            last = copy;
            curr = curr->next;
        }
    }

    // Add source metadata if not present
    if (!source_file && metadata) {
        source_file = metadata->value; // Use first metadata value as source
        eb_metadata_t* source_meta;
        status = eb_metadata_create("source", source_file, &source_meta);
        if (status != EB_SUCCESS) {
            eb_metadata_destroy(new_metadata);
            return status;
        }
        source_meta->next = new_metadata;
        new_metadata = source_meta;
    }
    
    // Store metadata
    char meta_hash[65] = {0};
    if (new_metadata) {
        status = eb_store_metadata(store, new_metadata, meta_hash);
        if (status != EB_SUCCESS) {
            eb_metadata_destroy(new_metadata);
            return status;
        }
        
        // Create reference linking vector to metadata
        status = eb_update_refs(store, hex_hash, meta_hash, model_version);
        if (status != EB_SUCCESS) {
            eb_metadata_destroy(new_metadata);
            return status;
        }
    }
    
    if (source_file) {
        // First append to history
        status = append_to_history(store->storage_path, source_file, hex_hash);
        if (status != EB_SUCCESS) {
            fprintf(stderr, "warning: failed to record history entry\n");
        }
        
        // Then update index (overwrite mode to keep only latest)
        char index_path[PATH_MAX];
        snprintf(index_path, sizeof(index_path), "%s/.eb/index", store->storage_path);
        
        // Read current index
        FILE* f = fopen(index_path, "r");
        char** lines = NULL;
        size_t line_count = 0;
        if (f) {
            char line[PATH_MAX + 65];
            while (fgets(line, sizeof(line), f)) {
                // Skip the line with our source file
                if (strstr(line, source_file) == NULL) {
                    lines = realloc(lines, (line_count + 1) * sizeof(char*));
                    lines[line_count] = strdup(line);
                    line_count++;
                }
            }
            fclose(f);
        }
        
        // Write updated index
        f = fopen(index_path, "w");
        if (f) {
            // Write existing lines
            for (size_t i = 0; i < line_count; i++) {
                fprintf(f, "%s", lines[i]);
                free(lines[i]);
            }
            free(lines);
            
            // Write new entry
            fprintf(f, "%s %s\n", source_file, hex_hash);
            fclose(f);
        }
    }
    
    eb_metadata_destroy(new_metadata);
    return EB_SUCCESS;
}

/* Read object from content-addressable storage */
eb_status_t read_object(
    eb_store_t* store,
    const char* hash,
    void** out_data,
    size_t* out_size,
    eb_object_header_t* out_header
) {
    char* obj_path = create_object_path(store->storage_path, hash);
    if (!obj_path) return EB_ERROR_MEMORY_ALLOCATION;
    
    FILE* fp = fopen(obj_path, "rb");
    if (!fp) {
        free(obj_path);
        return EB_ERROR_NOT_FOUND;
    }
    
    // Read and validate header
    eb_object_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        free(obj_path);
        return EB_ERROR_FILE_IO;
    }
    
    // Validate magic and version
    if (header.magic != EB_VECTOR_MAGIC || header.version > EB_VERSION) {
        fclose(fp);
        free(obj_path);
        return EB_ERROR_INVALID_INPUT;
    }
    
    // Allocate and read data
    void* data = malloc(header.size);
    if (!data) {
        fclose(fp);
        free(obj_path);
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    if (fread(data, header.size, 1, fp) != 1) {
        free(data);
        fclose(fp);
        free(obj_path);
        return EB_ERROR_FILE_IO;
    }
    
    fclose(fp);
    free(obj_path);
    
    // Verify hash
    uint8_t computed_hash[32];
    hash_data((const float*)data, header.size, computed_hash);
    if (memcmp(computed_hash, header.hash, 32) != 0) {
        free(data);
        return (eb_status_t)EB_ERROR_HASH_MISMATCH;  // Explicit cast to silence warning
    }
    
    *out_data = data;
    *out_size = header.size;
    *out_header = header;
    
    return EB_SUCCESS;
}

/* Retrieve vector by hash */
eb_status_t eb_get_vector(
    eb_store_t* store,
    uint64_t vector_id,
    eb_embedding_t** out_embedding,
    eb_metadata_t** out_metadata
) {
    if (!store || !out_embedding) {
        return EB_ERROR_INVALID_INPUT;
    }
    
    // Special case for memory store
    if (strcmp(store->storage_path, ":memory:") == 0) {
        #ifdef EB_ENABLE_MEMORY_STORE
        return eb_get_vector_memory(store, vector_id, out_embedding, out_metadata);
        #else
        return EB_ERROR_INVALID_INPUT;
        #endif
    }
    
    // Convert ID to hex hash
    char hex_hash[65];
    snprintf(hex_hash, sizeof(hex_hash), "%016lx", vector_id);
    
    // Read vector object
    void* data;
    size_t size;
    eb_object_header_t header;
    
    eb_status_t status = read_object(store, hex_hash, &data, &size, &header);
    if (status != EB_SUCCESS) return status;
    
    // Create embedding from data
    status = eb_create_embedding(
        data,
        size / sizeof(float),  // Calculate dimensions from size
        1,  // Single vector
        EB_FLOAT32,  // Always stored as float32
        header.flags & 0x01,  // Normalize flag
        out_embedding
    );
    
    free(data);
    
    if (status != EB_SUCCESS) return status;
    
    // Load metadata if requested
    if (out_metadata) {
        char meta_hash[65];
        status = eb_get_ref(store, hex_hash, meta_hash);
        if (status == EB_SUCCESS) {
            status = eb_get_metadata(store, meta_hash, out_metadata);
        }
    }
    
    return status;
}

eb_status_t eb_get_vector_evolution(
    eb_store_t* store,
    uint64_t vector_id,
    uint64_t from_time,
    uint64_t to_time,
    eb_stored_vector_t** out_versions,
    size_t* out_count
) {
    if (!store || !out_versions || !out_count) {
        return EB_ERROR_INVALID_INPUT;
    }
    
    size_t index = vector_id % HASH_TABLE_SIZE;
    eb_stored_vector_t* current = &store->vectors[index];
    
    // Find the vector
    while (current && current->id != vector_id) {
        current = current->next;
    }
    
    if (!current) return EB_ERROR_NOT_FOUND;
    
    // Count versions in time range
    size_t version_count = 0;
    eb_stored_vector_t* version = current;
    while (version) {
        if (version->timestamp >= from_time && 
            version->timestamp <= to_time) {
            version_count++;
        }
        version = version->next;
    }
    
    if (version_count == 0) {
        *out_versions = NULL;
        *out_count = 0;
        return EB_SUCCESS;
    }
    
    // Allocate array for versions
    eb_stored_vector_t* versions = malloc(version_count * sizeof(eb_stored_vector_t));
    if (!versions) return EB_ERROR_MEMORY_ALLOCATION;
    
    // Copy versions in time range
    size_t i = 0;
    version = current;
    while (version && i < version_count) {
        if (version->timestamp >= from_time && 
            version->timestamp <= to_time) {
            versions[i++] = *version;
        }
        version = version->next;
    }
    
    *out_versions = versions;
    *out_count = version_count;
    return EB_SUCCESS;
}

eb_status_t eb_metadata_create(const char* key, const char* value, eb_metadata_t** out) {
    if (!key || !value || !out) return EB_ERROR_INVALID_INPUT;
    
    eb_metadata_t* meta = malloc(sizeof(eb_metadata_t));
    if (!meta) return EB_ERROR_MEMORY_ALLOCATION;
    
    meta->key = strdup(key);
    meta->value = strdup(value);
    meta->next = NULL;
    
    if (!meta->key || !meta->value) {
        free(meta->key);
        free(meta->value);
        free(meta);
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    *out = meta;
    return EB_SUCCESS;
}

eb_status_t eb_format_evolution(
    const eb_stored_vector_t* versions,
    size_t version_count,
    const eb_comparison_result_t* changes,
    size_t change_count,
    char** out_formatted,
    size_t* out_length
) {
    if (!versions || !out_formatted || !out_length || version_count == 0) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Initial buffer size (can be adjusted based on expected output size)
    size_t buffer_size = 4096;
    char* buffer = malloc(buffer_size);
    if (!buffer) return EB_ERROR_MEMORY_ALLOCATION;

    size_t current_pos = 0;
    
    // Helper function to safely append to buffer
    #define APPEND_STR(str) do { \
        size_t len = strlen(str); \
        if (current_pos + len >= buffer_size) { \
            buffer_size *= 2; \
            char* new_buffer = realloc(buffer, buffer_size); \
            if (!new_buffer) { \
                free(buffer); \
                return EB_ERROR_MEMORY_ALLOCATION; \
            } \
            buffer = new_buffer; \
        } \
        strcpy(buffer + current_pos, str); \
        current_pos += len; \
    } while(0)

    // Format each version
    for (size_t i = 0; i < version_count; i++) {
        const eb_stored_vector_t* version = &versions[i];
        char temp[256];  // Temporary buffer for formatting

        // Format version marker and ID
        snprintf(temp, sizeof(temp), "%c Vector ID: %lu\n", 
                (i > 0 ? '*' : ' '), version->id);
        APPEND_STR(temp);

        // Format model and timestamp
        time_t ts = (time_t)version->timestamp;
        struct tm* tm_info = localtime(&ts);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

        snprintf(temp, sizeof(temp), "%c  Model: %s\n", 
                (i < version_count-1 ? '|' : ' '), version->model_version);
        APPEND_STR(temp);

        // Format metadata
        APPEND_STR(i < version_count-1 ? "|  Metadata: {" : "   Metadata: {");
        eb_metadata_t* meta = version->metadata;
        bool first = true;
        while (meta) {
            snprintf(temp, sizeof(temp), "%s\"%s\": \"%s\"",
                    first ? "" : ", ", meta->key, meta->value);
            APPEND_STR(temp);
            first = false;
            meta = meta->next;
        }
        APPEND_STR("}\n");

        snprintf(temp, sizeof(temp), "%c  Timestamp: %s\n",
                (i < version_count-1 ? '|' : ' '), time_str);
        APPEND_STR(temp);

        // Add comparison metrics if this isn't the first version
        if (i > 0 && i-1 < change_count) {
            const eb_comparison_result_t* change = &changes[i-1];
            APPEND_STR("|\n");
            
            snprintf(temp, sizeof(temp), 
                    "|  Semantic Preservation: %.0f%%\n",
                    change->semantic_preservation * 100);
            APPEND_STR(temp);

            snprintf(temp, sizeof(temp),
                    "|  Cosine Similarity: %.2f\n",
                    change->cosine_similarity);
            APPEND_STR(temp);

            APPEND_STR("|\n");
        }
    }

    #undef APPEND_STR

    *out_formatted = buffer;
    *out_length = current_pos;
    return EB_SUCCESS;
}

eb_status_t eb_store_memory(
    eb_store_t* store,
    const eb_embedding_t* embedding,
    const eb_metadata_t* metadata,
    const char* model_version,
    uint64_t* out_id
) {
    if (!store || !embedding || !model_version || !out_id) {
        DEBUG_PRINT("DEBUG: Invalid input check failed\n");
        DEBUG_PRINT("DEBUG: store=%p, embedding=%p, model_version=%p, out_id=%p\n",
               (void*)store, (void*)embedding, (void*)model_version, (void*)out_id);
        return EB_ERROR_INVALID_INPUT;
    }

    // Create a copy of the embedding
    eb_embedding_t* embedding_copy;
    eb_status_t status = eb_create_embedding(
        embedding->values,
        embedding->dimensions,
        1,  // Single vector
        EB_FLOAT32,
        embedding->normalize,
        &embedding_copy
    );
    if (status != EB_SUCCESS) {
        return status;
    }

    // Create a copy of the metadata
    eb_metadata_t* metadata_copy = NULL;
    eb_metadata_t* last_meta = NULL;
    const eb_metadata_t* meta = metadata;
    
    DEBUG_PRINT("DEBUG: Creating metadata copy\n");
    DEBUG_PRINT("DEBUG: Initial metadata pointer: %p\n", (void*)metadata);
    
    while (meta) {
        DEBUG_PRINT("DEBUG: Processing metadata key: %s, value: %s\n", meta->key, meta->value);
        eb_metadata_t* new_meta;
        status = eb_metadata_create(meta->key, meta->value, &new_meta);
        if (status != EB_SUCCESS) {
            DEBUG_PRINT("DEBUG: Failed to create metadata copy (status=%d)\n", status);
            eb_destroy_embedding(embedding_copy);
            eb_metadata_destroy(metadata_copy);
            return status;
        }
        
        if (!metadata_copy) {
            metadata_copy = new_meta;
        } else {
            last_meta->next = new_meta;
        }
        last_meta = new_meta;
        meta = meta->next;
    }

    // Generate vector ID based on metadata and model version
    // This ensures same text gets same ID across versions
    size_t hash_data_size = 0;
    meta = metadata;
    bool found_text = false;
    
    DEBUG_PRINT("DEBUG: Searching for text metadata\n");
    while (meta) {
        DEBUG_PRINT("DEBUG: Checking metadata key: %s\n", meta->key);
        if (strcmp(meta->key, "text") == 0) {  // Only use text for ID
            hash_data_size = strlen(meta->value) + 1;
            found_text = true;
            DEBUG_PRINT("DEBUG: Found text metadata with length %zu\n", hash_data_size - 1);
            break;
        }
        meta = meta->next;
    }

    if (!found_text) {
        DEBUG_PRINT("DEBUG: No text metadata found in keys: ");
        meta = metadata;
        while (meta) {
            DEBUG_PRINT("%s, ", meta->key);
            meta = meta->next;
        }
        DEBUG_PRINT("\n");
        eb_destroy_embedding(embedding_copy);
        eb_metadata_destroy(metadata_copy);
        return EB_ERROR_INVALID_INPUT;
    }

    char* hash_data = malloc(hash_data_size);
    if (!hash_data) {
        DEBUG_PRINT("DEBUG: Failed to allocate hash data\n");
        eb_destroy_embedding(embedding_copy);
        eb_metadata_destroy(metadata_copy);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Use only the text for hashing to ensure same text gets same ID
    meta = metadata;
    while (meta) {
        if (strcmp(meta->key, "text") == 0) {
            strcpy(hash_data, meta->value);
            break;
        }
        meta = meta->next;
    }

    // Generate ID from text only
    *out_id = generate_id(hash_data, hash_data_size - 1);  // Exclude null terminator
    free(hash_data);

    DEBUG_PRINT("DEBUG: Generated ID: %lu for text metadata\n", *out_id);

    // Store using the regular function
    DEBUG_PRINT("DEBUG: Storing vector\n");
    status = eb_store_vector(
        store,
        embedding_copy,
        metadata_copy,
        model_version,
        out_id
    );
    DEBUG_PRINT("DEBUG: Store vector result: %d\n", status);

    return status;
}

static __attribute__((unused)) size_t count_version_chain(eb_stored_vector_t* vector) {
    size_t count = 0;
    eb_stored_vector_t* current = vector;
    
    while (current) {
        if (current->embedding) count++;
        current = current->next;
    }
    return count;
}

eb_status_t eb_get_memory_evolution_with_changes(
    eb_store_t* store,
    uint64_t vector_id,
    uint64_t from_time,
    uint64_t to_time,
    eb_stored_vector_t** out_versions,
    size_t* out_version_count,
    eb_comparison_result_t** out_changes,
    size_t* out_change_count
) {
    if (!store || !out_versions || !out_version_count || 
        !out_changes || !out_change_count) {
        DEBUG_PRINT("DEBUG: Invalid input check failed in eb_get_memory_evolution_with_changes\n");
        return EB_ERROR_INVALID_INPUT;
    }

    DEBUG_PRINT("DEBUG: Looking for vector ID: %lu\n", vector_id);

    // Find all related vectors in the chain
    size_t max_versions = 32;  // Reasonable limit
    eb_stored_vector_t** chain = malloc(max_versions * sizeof(eb_stored_vector_t*));
    size_t chain_length = 0;

    // First, find the vector with the given ID
    size_t index = vector_id % HASH_TABLE_SIZE;
    eb_stored_vector_t* current = &store->vectors[index];
    eb_stored_vector_t* target = NULL;

    // Find the target vector
    while (current) {
        if (current->id == vector_id) {
            target = current;
            break;
        }
        current = current->next;
    }

    if (!target) {
        free(chain);
        return EB_ERROR_NOT_FOUND;
    }

    // Add target to chain
    chain[chain_length++] = target;

    // Find parent versions
    current = target;
    while (current && current->parent_id != 0 && chain_length < max_versions) {
        // Find parent
        size_t parent_index = current->parent_id % HASH_TABLE_SIZE;
        eb_stored_vector_t* parent = &store->vectors[parent_index];
        
        while (parent) {
            if (parent->id == current->parent_id) {
                chain[chain_length++] = parent;
                current = parent;
                break;
            }
            parent = parent->next;
        }
        
        if (!parent) break;  // Parent not found
    }

    // Find child versions
    for (size_t i = 0; i < HASH_TABLE_SIZE && chain_length < max_versions; i++) {
        current = &store->vectors[i];
        while (current) {
            if (current->parent_id == vector_id) {
                chain[chain_length++] = current;
            }
            current = current->next;
        }
    }

    // Sort chain by timestamp
    for (size_t i = 0; i < chain_length - 1; i++) {
        for (size_t j = 0; j < chain_length - i - 1; j++) {
            if (chain[j]->timestamp > chain[j + 1]->timestamp) {
                eb_stored_vector_t* temp = chain[j];
                chain[j] = chain[j + 1];
                chain[j + 1] = temp;
            }
        }
    }

    // Copy versions within time range
    size_t version_count = 0;
    for (size_t i = 0; i < chain_length; i++) {
        if (chain[i]->timestamp >= from_time && chain[i]->timestamp <= to_time) {
            version_count++;
        }
    }

    if (version_count == 0) {
        free(chain);
        *out_versions = NULL;
        *out_version_count = 0;
        *out_changes = NULL;
        *out_change_count = 0;
        return EB_SUCCESS;
    }

    // Allocate and copy versions
    eb_stored_vector_t* versions = malloc(version_count * sizeof(eb_stored_vector_t));
    if (!versions) {
        free(chain);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    size_t version_idx = 0;
    for (size_t i = 0; i < chain_length; i++) {
        if (chain[i]->timestamp >= from_time && chain[i]->timestamp <= to_time) {
            versions[version_idx] = *chain[i];
            versions[version_idx].model_version = strdup(chain[i]->model_version);
            version_idx++;
        }
    }

    // Compute changes between consecutive versions
    size_t change_count = version_count > 1 ? version_count - 1 : 0;
    eb_comparison_result_t* changes = NULL;

    if (change_count > 0) {
        changes = malloc(change_count * sizeof(eb_comparison_result_t));
        if (!changes) {
            free(versions);
            free(chain);
            return EB_ERROR_MEMORY_ALLOCATION;
        }

        for (size_t i = 0; i < change_count; i++) {
            eb_status_t status = eb_compare_memory_versions(
                &versions[i],
                &versions[i + 1],
                &changes[i]
            );
            
            if (status != EB_SUCCESS) {
                changes[i].cosine_similarity = 0.0;
                changes[i].semantic_preservation = 0.0;
                changes[i].method_used = EB_COMPARE_PROJECTION;
            }
        }
    }

    free(chain);
    *out_versions = versions;
    *out_version_count = version_count;
    *out_changes = changes;
    *out_change_count = change_count;

    return EB_SUCCESS;
}

void eb_metadata_append(eb_metadata_t* metadata, eb_metadata_t* next) {
    if (!metadata) return;
    metadata->next = next;
}

/* Store metadata object */
eb_status_t eb_store_metadata(
    eb_store_t* store,
    const eb_metadata_t* metadata,
    char out_hash[65]
) {
    if (!store || !metadata || !out_hash) return EB_ERROR_INVALID_INPUT;
    
    // Calculate total size needed
    size_t total_size = 0;
    const eb_metadata_t* meta = metadata;
    uint32_t entry_count = 0;
    
    while (meta) {
        total_size += strlen(meta->key) + 1;  // Include null terminator
        total_size += strlen(meta->value) + 1;
        entry_count++;
        meta = meta->next;
    }
    
    // Create metadata buffer
    char* buffer = malloc(total_size);
    if (!buffer) return EB_ERROR_MEMORY_ALLOCATION;
    
    // Pack metadata into buffer
    char* ptr = buffer;
    meta = metadata;
    while (meta) {
        size_t key_len = strlen(meta->key) + 1;
        size_t value_len = strlen(meta->value) + 1;
        
        memcpy(ptr, meta->key, key_len);
        ptr += key_len;
        memcpy(ptr, meta->value, value_len);
        ptr += value_len;
        
        meta = meta->next;
    }
    
    // Store as object
    eb_status_t status = write_object(
        store,
        buffer,
        total_size,
        EB_OBJ_META,
        entry_count,  // Store count in flags
        out_hash
    );
    
    free(buffer);
    return status;
}

/* Retrieve metadata object */
eb_status_t eb_get_metadata(
    eb_store_t* store,
    const char* hash,
    eb_metadata_t** out_metadata
) {
    if (!store || !hash || !out_metadata) return EB_ERROR_INVALID_INPUT;
    
    // Read object
    void* data;
    size_t size;
    eb_object_header_t header;
    
    eb_status_t status = read_object(store, hash, &data, &size, &header);
    if (status != EB_SUCCESS) return status;
    
    // Verify object type
    if (header.obj_type != EB_OBJ_META) {
        free(data);
        return EB_ERROR_INVALID_INPUT;
    }
    
    // Parse metadata entries
    char* ptr = data;
    char* end = ptr + size;
    eb_metadata_t* head = NULL;
    eb_metadata_t* current = NULL;
    uint32_t entry_count = header.flags;
    
    for (uint32_t i = 0; i < entry_count && ptr < end; i++) {
        const char* key = ptr;
        ptr += strlen(key) + 1;
        
        if (ptr >= end) break;
        
        const char* value = ptr;
        ptr += strlen(value) + 1;
        
        // Create metadata entry
        eb_metadata_t* meta;
        status = eb_metadata_create(key, value, &meta);
        if (status != EB_SUCCESS) {
            eb_metadata_destroy(head);
            free(data);
            return status;
        }
        
        if (!head) {
            head = meta;
            current = meta;
        } else {
            current->next = meta;
            current = meta;
        }
    }
    
    free(data);
    *out_metadata = head;
    return EB_SUCCESS;
}

/* Update reference mapping */
eb_status_t eb_update_refs(
    eb_store_t* store,
    const char* vector_hash,
    const char* meta_hash,
    const char* model_version
) {
    if (!store || !vector_hash || !meta_hash || !model_version) {
        return EB_ERROR_INVALID_INPUT;
    }
    
    // Create reference file path
    char ref_path[4096];
    snprintf(ref_path, sizeof(ref_path), "%s/.eb/metadata/files/%s.ref",
             store->storage_path, vector_hash);
             
    // Write reference data
    FILE* fp = fopen(ref_path, "wb");
    if (!fp) return EB_ERROR_FILE_IO;
    
    // Write metadata hash
    if (fwrite(meta_hash, 64, 1, fp) != 1) {
        fclose(fp);
        return EB_ERROR_FILE_IO;
    }
    
    // Write model version
    size_t version_len = strlen(model_version);
    if (fwrite(&version_len, sizeof(size_t), 1, fp) != 1 ||
        fwrite(model_version, version_len, 1, fp) != 1) {
        fclose(fp);
        return EB_ERROR_FILE_IO;
    }
    
    fclose(fp);
    return EB_SUCCESS;
}

/* Get reference mapping */
eb_status_t eb_get_ref(
    eb_store_t* store,
    const char* vector_hash,
    char meta_hash[65]  // Output buffer for metadata hash
) {
    if (!store || !vector_hash || !meta_hash) return EB_ERROR_INVALID_INPUT;
    
    // Open reference file
    char ref_path[4096];
    snprintf(ref_path, sizeof(ref_path), "%s/.eb/metadata/files/%s.ref",
             store->storage_path, vector_hash);
             
    FILE* fp = fopen(ref_path, "rb");
    if (!fp) return EB_ERROR_NOT_FOUND;
    
    // Read metadata hash
    char hash[64];
    if (fread(hash, 64, 1, fp) != 1) {
        fclose(fp);
        return EB_ERROR_FILE_IO;
    }
    
    // Copy to output buffer with null terminator
    memcpy(meta_hash, hash, 64);
    meta_hash[64] = '\0';
    
    fclose(fp);
    return EB_SUCCESS;
}

#ifdef EB_ENABLE_MEMORY_STORE

/* Initialize memory store */
eb_status_t eb_store_init_memory(eb_store_t** out) {
    eb_store_t* store = malloc(sizeof(eb_store_t));
    if (!store) return EB_ERROR_MEMORY_ALLOCATION;
    
    // Initialize hash table
    store->vectors = calloc(HASH_TABLE_SIZE, sizeof(eb_stored_vector_t));
    if (!store->vectors) {
        free(store);
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    store->storage_path = strdup(":memory:");
    if (!store->storage_path) {
        free(store->vectors);
        free(store);
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    store->vector_count = 0;
    *out = store;
    return EB_SUCCESS;
}

static eb_stored_vector_t* find_vector_by_text(eb_store_t* store, const char* text) {
    DEBUG_PRINT("DEBUG: Searching for vector with text: %.30s...\n", text);
    
    // Find existing vector with same text content
    for (size_t i = 0; i < HASH_TABLE_SIZE; i++) {
        eb_stored_vector_t* current = &store->vectors[i];
        while (current && current->metadata) {
            eb_metadata_t* meta = current->metadata;
            while (meta) {
                if (strcmp(meta->key, "text") == 0) {
                    DEBUG_PRINT("DEBUG: Found text metadata: %.30s...\n", meta->value);
                    if (strcmp(meta->value, text) == 0) {
                        DEBUG_PRINT("DEBUG: Found matching vector at index %zu with ID %lu\n", 
                               i, current->id);
                        return current;
                    }
                }
                meta = meta->next;
            }
            current = current->next;
        }
    }
    DEBUG_PRINT("DEBUG: No existing vector found with matching text\n");
    return NULL;
}

/* Store vector in memory */
eb_status_t eb_store_vector_memory(
    eb_store_t* store,
    const eb_embedding_t* embedding,
    const eb_metadata_t* metadata,
    const char* model_version,
    uint64_t* out_id
) {
    if (!store || !embedding || !model_version || !out_id) {
        DEBUG_PRINT("DEBUG: Invalid input check failed in eb_store_vector_memory\n");
        DEBUG_PRINT("DEBUG: store=%p, embedding=%p, model_version=%p, out_id=%p\n",
               (void*)store, (void*)embedding, (void*)model_version, (void*)out_id);
        return EB_ERROR_INVALID_INPUT;
    }
    
    // Find text in metadata if present
    const char* text = NULL;
    uint64_t parent_id = 0;  // Track parent ID from metadata
    const eb_metadata_t* meta = metadata;
    while (meta) {
        if (strcmp(meta->key, "text") == 0) {
            text = meta->value;
            DEBUG_PRINT("DEBUG: Found text in metadata: %.30s...\n", text);
        } else if (strcmp(meta->key, "parent_id") == 0) {
            parent_id = strtoull(meta->value, NULL, 10);
            DEBUG_PRINT("DEBUG: Found parent_id in metadata: %lu\n", parent_id);
        }
        meta = meta->next;
    }
    
    // Generate ID from embedding data if no text
    if (!text) {
        DEBUG_PRINT("DEBUG: No text found in metadata, using embedding data for ID\n");
        *out_id = generate_id(embedding->values, embedding->dimensions * sizeof(float));
    } else {
        // If we have a parent_id, use it directly
        if (parent_id != 0) {
            *out_id = generate_id(text, strlen(text));
        } else {
            // Find existing vector with same text
            eb_stored_vector_t* existing = find_vector_by_text(store, text);
            if (existing) {
                parent_id = existing->id;
            }
            *out_id = generate_id(text, strlen(text));
        }
    }
    
    DEBUG_PRINT("DEBUG: Generated ID %lu with parent_id %lu\n", *out_id, parent_id);
    
    // Find slot in hash table
    size_t index = *out_id % HASH_TABLE_SIZE;
    DEBUG_PRINT("DEBUG: Using hash table index %zu\n", index);
    
    eb_stored_vector_t* slot = &store->vectors[index];
    DEBUG_PRINT("DEBUG: Slot at index %zu: embedding=%p, id=%lu, parent_id=%lu\n",
           index, (void*)slot->embedding, slot->id, slot->parent_id);
    
    // Create a copy of the embedding
    eb_embedding_t* embedding_copy;
    eb_status_t status = eb_create_embedding(
        embedding->values,
        embedding->dimensions,
        1,  // Single vector
        EB_FLOAT32,
        embedding->normalize,
        &embedding_copy
    );
    if (status != EB_SUCCESS) {
        return status;
    }

    // Create a new stored vector
    eb_stored_vector_t* new_vector = malloc(sizeof(eb_stored_vector_t));
    if (!new_vector) {
        eb_destroy_embedding(embedding_copy);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize the new vector
    new_vector->id = *out_id;
    new_vector->embedding = embedding_copy;
    new_vector->metadata = NULL;
    new_vector->model_version = strdup(model_version);
    new_vector->timestamp = time(NULL);
    new_vector->parent_id = parent_id;
    new_vector->next = NULL;
    
    DEBUG_PRINT("DEBUG: Created new vector entry:\n");
    DEBUG_PRINT("  - ID: %lu\n", new_vector->id);
    DEBUG_PRINT("  - Parent ID: %lu\n", new_vector->parent_id);
    DEBUG_PRINT("  - Model: %s\n", new_vector->model_version);
    DEBUG_PRINT("  - Timestamp: %lu\n", new_vector->timestamp);
    
    // Copy metadata if present
    if (metadata) {
        eb_metadata_t* last_meta = NULL;
        meta = metadata;
        while (meta) {
            eb_metadata_t* new_meta;
            status = eb_metadata_create(meta->key, meta->value, &new_meta);
            if (status != EB_SUCCESS) {
                eb_destroy_embedding(embedding_copy);
                free(new_vector->model_version);
                free(new_vector);
                return status;
            }
            
            if (!new_vector->metadata) {
                new_vector->metadata = new_meta;
            } else {
                last_meta->next = new_meta;
            }
            last_meta = new_meta;
            meta = meta->next;
        }
    }
    
    // Add to hash table
    if (slot->embedding) {
        // Slot occupied, chain to next
        new_vector->next = slot->next;
        slot->next = new_vector;
    } else {
        // Empty slot
        *slot = *new_vector;
        free(new_vector);  // Contents copied to slot
    }
    
    store->vector_count++;
    return EB_SUCCESS;
}

/* Retrieve vector from memory */
eb_status_t eb_get_vector_memory(
    eb_store_t* store,
    uint64_t vector_id,
    eb_embedding_t** out_embedding,
    eb_metadata_t** out_metadata
) {
    if (!store || !out_embedding) return EB_ERROR_INVALID_INPUT;
    
    // Find vector in hash table
    size_t index = vector_id % HASH_TABLE_SIZE;
    eb_stored_vector_t* current = &store->vectors[index];
    
    while (current) {
        if (current->id == vector_id) {
            // Copy embedding
            eb_embedding_t* embedding_copy;
            eb_status_t status = eb_create_embedding(
                current->embedding->values,
                current->embedding->dimensions,
                1,  // Single vector
                EB_FLOAT32,
                current->embedding->normalize,
                &embedding_copy
            );
            if (status != EB_SUCCESS) return status;
            
            // Copy metadata if requested
            if (out_metadata && current->metadata) {
                const eb_metadata_t* meta = current->metadata;
                eb_metadata_t* last_meta = NULL;
                
                while (meta) {
                    eb_metadata_t* new_meta;
                    status = eb_metadata_create(meta->key, meta->value, &new_meta);
                    if (status != EB_SUCCESS) {
                        eb_destroy_embedding(embedding_copy);
                        eb_metadata_destroy(*out_metadata);
                        return status;
                    }
                    
                    if (!*out_metadata) {
                        *out_metadata = new_meta;
                    } else {
                        last_meta->next = new_meta;
                    }
                    last_meta = new_meta;
                    meta = meta->next;
                }
            }
            
            return EB_SUCCESS;
        }
        current = current->next;
    }
    
    return EB_ERROR_NOT_FOUND;
}

#endif /* EB_ENABLE_MEMORY_STORE */

eb_status_t eb_store_get_latest(eb_store_t* store, const char* file, eb_stored_vector_t** vectors) {
    if (!store || !file || !vectors) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Get current hash
    char current_hash[65];
    eb_status_t status = get_current_hash(store->storage_path, file, current_hash, sizeof(current_hash));
    if (status != EB_SUCCESS) {
        return status;
    }

    // Get version history
    size_t version_count;
    eb_stored_vector_t* history_versions;
    status = get_version_history(store->storage_path, file, &history_versions, &version_count);
    if (status != EB_SUCCESS) {
        return status;
    }

    // Create result with current version and history
    *vectors = malloc(sizeof(eb_stored_vector_t));
    if (!*vectors) {
        eb_destroy_stored_vectors(history_versions, version_count);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Set up current version
    (*vectors)->id = version_count + 1;
    (*vectors)->timestamp = time(NULL);
    (*vectors)->embedding = NULL;
    (*vectors)->model_version = strdup("openai");
    (*vectors)->next = history_versions; // Link to history

    // Add hash metadata
    eb_metadata_t* hash_meta;
    eb_metadata_create("hash", current_hash, &hash_meta);
    (*vectors)->metadata = hash_meta;

    return EB_SUCCESS;
}

/* Get the path to a stored embedding file */
eb_status_t eb_store_get_path(eb_store_t *store,
                             const char *hash,
                             char *path_out,
                             size_t path_size)
{
        if (!store || !hash || !path_out || path_size == 0)
                return EB_ERROR_INVALID_INPUT;

        /* Construct path to embedding file */
        if (snprintf(path_out, path_size, "%s/.eb/objects/%s.bin",
                    store->storage_path, hash) >= (int)path_size) {
                return EB_ERROR_PATH_TOO_LONG;
        }

        /* Check if file exists */
        if (access(path_out, F_OK) != 0) {
                return EB_ERROR_NOT_FOUND;
        }

        return EB_SUCCESS;
}

/* Resolve partial hash to full hash */
eb_status_t eb_store_resolve_hash(
    eb_store_t* store,
    const char* partial_hash,
    char full_hash[65],
    size_t buf_size
) {
    if (!store || !partial_hash || !full_hash || buf_size < 65) {
        DEBUG_PRINT("Invalid input to eb_store_resolve_hash\n");
        return EB_ERROR_INVALID_INPUT;
    }

    size_t hash_len = strlen(partial_hash);
    if (hash_len < 4) {
        DEBUG_PRINT("Hash too short: %s (len=%zu)\n", 
                partial_hash, hash_len);
        return EB_ERROR_INVALID_INPUT;
    }

    /* If hash is already full length, just copy it */
    if (hash_len == 64) {
        strncpy(full_hash, partial_hash, 64);
        full_hash[64] = '\0';
        return EB_SUCCESS;
    }

    /* Check if file exists directly first */
    char direct_path[4096];
    snprintf(direct_path, sizeof(direct_path), "%s/.eb/objects/%s.raw", 
             store->storage_path, partial_hash);
    
    if (access(direct_path, F_OK) == 0) {
        strncpy(full_hash, partial_hash, 64);
        full_hash[64] = '\0';
        return EB_SUCCESS;
    }

    /* Search in objects directory */
    char objects_dir[4096];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.eb/objects", store->storage_path);
    
    DIR* dir = opendir(objects_dir);
    if (!dir) {
        DEBUG_PRINT("Failed to open objects directory: %s\n", objects_dir);
        return EB_ERROR_NOT_FOUND;
    }

    struct dirent* entry;
    char* match = NULL;
    int match_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;  // Skip non-regular files
        
        size_t name_len = strlen(entry->d_name);
        if (name_len < 4 || strcmp(entry->d_name + name_len - 4, ".raw") != 0) {
            continue;  // Skip non-.raw files
        }

        // Remove .raw extension
        char hash[65];
        strncpy(hash, entry->d_name, name_len - 4);
        hash[name_len - 4] = '\0';

        if (strncmp(hash, partial_hash, hash_len) == 0) {
            DEBUG_PRINT("Found matching hash: %s\n", hash);
            if (match) {
                DEBUG_PRINT("Multiple matches found - hash is ambiguous\n");
                free(match);
                closedir(dir);
                return EB_ERROR_HASH_AMBIGUOUS;
            }
            match = strdup(hash);
            match_count++;
        }
    }

    closedir(dir);

    if (!match) {
        DEBUG_PRINT("No matching hash found for %s\n", partial_hash);
        return EB_ERROR_NOT_FOUND;
    }

    strncpy(full_hash, match, 64);
    full_hash[64] = '\0';
    free(match);

    DEBUG_PRINT("Successfully resolved %s to %s\n", 
            partial_hash, full_hash);
    return EB_SUCCESS;
}

eb_status_t get_current_hash(const char* root, const char* source, char* hash_out, size_t hash_size) {
    if (!root || !source || !hash_out || hash_size < 65) {
        return EB_ERROR_INVALID_INPUT;
    }

    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/.eb/index", root);

    FILE* fp = fopen(index_path, "r");
    if (!fp) {
        return EB_ERROR_NOT_FOUND;
    }

    char line[2048];
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        char file[PATH_MAX], hash[65];
        line[strcspn(line, "\n")] = 0;

        DEBUG_PRINT("get_current_hash: Processing line: %s\n", line);
        
        // Parse line format "hash file"
        if (sscanf(line, "%s %s", hash, file) == 2) {
            DEBUG_PRINT("get_current_hash: Comparing [%s] with [%s]\n", file, source);
            
            if (strcmp(file, source) == 0) {
                strncpy(hash_out, hash, hash_size - 1);
                hash_out[hash_size - 1] = '\0';
                found = true;
                //break; continue to find last entry
            }
        }
    }

    fclose(fp);
    return found ? EB_SUCCESS : EB_ERROR_NOT_FOUND;
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;
    int ret;

    if (!path || !*path) return -1;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            ret = mkdir(tmp, 0755);
            if (ret != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    ret = mkdir(tmp, 0755);
    return (ret != 0 && errno != EEXIST) ? -1 : 0;
}

eb_status_t store_embedding_file(const char* embedding_path, const char* source_file,
                                    const char* base_dir) {
    if (!embedding_path || !source_file || !base_dir) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Create objects directory if it doesn't exist
    char objects_dir[PATH_MAX];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.eb/objects", base_dir);
    if (mkdir_p(objects_dir) != 0) {
        return EB_ERROR_FILE_IO;
    }

    printf("Storing embedding file: %s\n", embedding_path);
    printf("Source file: %s\n", source_file);
    printf("Base directory: %s\n", base_dir);

    // Read the raw file bytes
    FILE* fp = fopen(embedding_path, "rb");
    if (!fp) {
        return EB_ERROR_FILE_IO;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Read file content
    unsigned char* file_content = malloc(file_size);
    if (!file_content) {
        fclose(fp);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    size_t bytes_read = fread(file_content, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        free(file_content);
        return EB_ERROR_FILE_IO;
    }

    // Calculate hash of file content
    unsigned char hash[32];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, file_content, file_size);
    SHA256_Final(hash, &sha256);

    char hash_str[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hash_str[i * 2], "%02x", hash[i]);
    }
    hash_str[64] = '\0';

    printf("Generated hash: %s\n", hash_str);

    // Store raw file
    char raw_path[PATH_MAX];
    snprintf(raw_path, sizeof(raw_path), "%s/%s.raw", objects_dir, hash_str);
    fp = fopen(raw_path, "wb");
    if (!fp) {
        free(file_content);
        return EB_ERROR_FILE_IO;
    }

    if (fwrite(file_content, 1, file_size, fp) != (size_t)file_size) {
        fclose(fp);
        free(file_content);
        return EB_ERROR_FILE_IO;
    }

    fclose(fp);
    free(file_content);

    // Store metadata
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", objects_dir, hash_str);
    fp = fopen(meta_path, "w");
    if (!fp) {
        return EB_ERROR_FILE_IO;
    }

    time_t now = time(NULL);
    fprintf(fp, "source_file=%s\n", source_file);
    fprintf(fp, "timestamp=%ld\n", now);
    fprintf(fp, "file_type=%s\n", strrchr(embedding_path, '.') + 1);
    fprintf(fp, "provider=openai\n");  // TODO: Make this configurable
    fclose(fp);

    // Update index
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/.eb/index", base_dir);
    fp = fopen(index_path, "a");
    if (!fp) {
        return EB_ERROR_FILE_IO;
    }
    fprintf(fp, "%s %s\n", hash_str, source_file);
    fclose(fp);

    // Update history
    char history_path[PATH_MAX];
    snprintf(history_path, sizeof(history_path), "%s/.eb/history", base_dir);
    fp = fopen(history_path, "a");
    if (!fp) {
        return EB_ERROR_FILE_IO;
    }
    fprintf(fp, "%ld %s %s\n", now, hash_str, source_file);
    fclose(fp);

    printf("Successfully stored embedding with hash: %s\n", hash_str);

    return EB_SUCCESS;
}

static eb_status_t calculate_file_hash(const char* file_path, char* hash_out, size_t hash_size) {
    if (!file_path || !hash_out || hash_size < 65) {
        return EB_ERROR_INVALID_INPUT;
    }

    FILE* f = fopen(file_path, "rb");
    if (!f) {
        return EB_ERROR_FILE_IO;
    }

    // Initialize EVP context
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        fclose(f);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    const EVP_MD* md = EVP_sha256();
    if (!md) {
        EVP_MD_CTX_free(ctx);
        fclose(f);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        fclose(f);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Read and hash the entire file content without special handling
    unsigned char buffer[8192];
    size_t bytes_read;
    size_t total_bytes = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, bytes_read) != 1) {
            DEBUG_PRINT("calculate_file_hash: Failed to update hash\n");
            EVP_MD_CTX_free(ctx);
            fclose(f);
            return EB_ERROR_FILE_IO;
        }
        total_bytes += bytes_read;
    }
    DEBUG_PRINT("calculate_file_hash: Hashed %zu bytes of data\n", total_bytes);

    // Get final hash
    unsigned char hash_result[32];
    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, hash_result, &hash_len) != 1) {
        DEBUG_PRINT("calculate_file_hash: Failed to finalize hash\n");
        EVP_MD_CTX_free(ctx);
        fclose(f);
        return EB_ERROR_FILE_IO;
    }

    EVP_MD_CTX_free(ctx);
    fclose(f);

    // Convert to hex string
    hash_to_hex(hash_result, hash_out);
    DEBUG_PRINT("calculate_file_hash: Generated hash %s\n", hash_out);
    return EB_SUCCESS;
}

static eb_status_t copy_file(const char* src, const char* dst) {
    FILE *fsrc = fopen(src, "rb");
    if (!fsrc) {
        return EB_ERROR_FILE_IO;
    }

    FILE *fdst = fopen(dst, "wb");
    if (!fdst) {
        fclose(fsrc);
        return EB_ERROR_FILE_IO;
    }

    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fsrc)) > 0) {
        if (fwrite(buffer, 1, n, fdst) != n) {
            fclose(fsrc);
            fclose(fdst);
            return EB_ERROR_FILE_IO;
        }
    }

    fclose(fsrc);
    fclose(fdst);
    return EB_SUCCESS;
}

/* Get version history for a file */
eb_status_t get_version_history(const char* root, const char* source, 
                              eb_stored_vector_t** out_versions, size_t* out_count) {
    char history_path[PATH_MAX];
    snprintf(history_path, sizeof(history_path), "%s/.eb/history", root);
    
    FILE* f = fopen(history_path, "r");
    if (!f) {
        *out_versions = NULL;
        *out_count = 0;
        return EB_SUCCESS; // No history is not an error
    }

    // First pass: count entries for this source
    char line[PATH_MAX + 128]; // Path + hash + timestamp + provider
    size_t version_count = 0;
    
    while (fgets(line, sizeof(line), f)) {
        char file_path[PATH_MAX];
        char hash[65];
        char timestamp[32];
        char provider[32];
        
        if (sscanf(line, "%s %s %s %s", file_path, hash, timestamp, provider) == 4) {
            if (strcmp(file_path, source) == 0) {
                version_count++;
            }
        }
    }

    if (version_count == 0) {
        fclose(f);
        *out_versions = NULL;
        *out_count = 0;
        return EB_SUCCESS;
    }

    // Allocate array for versions
    eb_stored_vector_t* versions = malloc(version_count * sizeof(eb_stored_vector_t));
    if (!versions) {
        fclose(f);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Second pass: read entries
    rewind(f);
    size_t idx = 0;
    
    while (fgets(line, sizeof(line), f) && idx < version_count) {
        char file_path[PATH_MAX];
        char hash[65];
        char timestamp[32];
        char provider[32];
        
        if (sscanf(line, "%s %s %s %s", file_path, hash, timestamp, provider) == 4) {
            if (strcmp(file_path, source) == 0) {
                // Initialize version entry
                versions[idx].id = idx + 1; // Simple sequential ID
                
                // Parse timestamp
                struct tm tm = {0};
                strptime(timestamp, "%Y-%m-%dT%H:%M:%SZ", &tm);
                versions[idx].timestamp = mktime(&tm);
                
                versions[idx].embedding = NULL; // Don't load embedding for history
                versions[idx].metadata = NULL;
                versions[idx].model_version = strdup(provider);
                versions[idx].next = (idx < version_count - 1) ? &versions[idx + 1] : NULL;
                
                // Store hash and metadata
                eb_metadata_t* hash_meta;
                eb_metadata_create("hash", hash, &hash_meta);
                eb_metadata_t* ts_meta;
                eb_metadata_create("timestamp", timestamp, &ts_meta);
                eb_metadata_t* provider_meta;
                eb_metadata_create("provider", provider, &provider_meta);
                
                hash_meta->next = ts_meta;
                ts_meta->next = provider_meta;
                versions[idx].metadata = hash_meta;
                
                idx++;
            }
        }
    }

    fclose(f);
    *out_versions = versions;
    *out_count = version_count;
    return EB_SUCCESS;
}

static eb_status_t create_binary_delta(const char* base_path, const char* new_path, const char* delta_path) {
    DEBUG_PRINT("create_binary_delta: Creating delta between %s and %s\n", base_path, new_path);
    
    FILE* base = fopen(base_path, "rb");
    FILE* new_file = fopen(new_path, "rb");
    FILE* delta = fopen(delta_path, "wb");
    
    if (!base || !new_file || !delta) {
        if (base) fclose(base);
        if (new_file) fclose(new_file);
        if (delta) fclose(delta);
        DEBUG_PRINT("create_binary_delta: Failed to open files\n");
        return EB_ERROR_FILE_IO;
    }

    // Write delta header
    uint32_t magic = 0x3E42444C;  // "EBD>" magic number
    uint32_t version = 1;
    if (fwrite(&magic, sizeof(magic), 1, delta) != 1 ||
        fwrite(&version, sizeof(version), 1, delta) != 1) {
        DEBUG_PRINT("create_binary_delta: Failed to write header\n");
        goto cleanup;
    }

    // Read files in chunks and find differences
    unsigned char base_buf[8192];
    unsigned char new_buf[8192];
    size_t base_read, new_read;
    uint64_t offset = 0;

    while (1) {
        base_read = fread(base_buf, 1, sizeof(base_buf), base);
        new_read = fread(new_buf, 1, sizeof(new_buf), new_file);

        if (base_read == 0 && new_read == 0) break;

        // Compare chunks
        size_t min_size = base_read < new_read ? base_read : new_read;
        size_t i = 0;
        
        while (i < min_size) {
            // Find start of difference
            size_t start = i;
            while (i < min_size && base_buf[i] == new_buf[i]) i++;
            
            // If we found a difference or reached end
            if (i < min_size || base_read != new_read) {
                // Find end of difference
                size_t end = i;
                while (end < min_size && end - i < 255 && base_buf[end] != new_buf[end]) end++;
                
                // Write delta command
                uint64_t cmd_offset = offset + start;
                uint8_t cmd_size = end - i;
                if (fwrite(&cmd_offset, sizeof(cmd_offset), 1, delta) != 1 ||
                    fwrite(&cmd_size, sizeof(cmd_size), 1, delta) != 1 ||
                    fwrite(new_buf + i, 1, cmd_size, delta) != 1) {
                    DEBUG_PRINT("create_binary_delta: Failed to write delta command\n");
                    goto cleanup;
                }
                
                i = end;
            }
        }
        
        offset += min_size;
    }

    // Handle remaining data in new file
    if (new_read > base_read) {
        uint64_t cmd_offset = offset + base_read;
        uint8_t cmd_size = new_read - base_read;
        if (fwrite(&cmd_offset, sizeof(cmd_offset), 1, delta) != 1 ||
            fwrite(&cmd_size, sizeof(cmd_size), 1, delta) != 1 ||
            fwrite(new_buf + base_read, 1, cmd_size, delta) != 1) {
            DEBUG_PRINT("create_binary_delta: Failed to write remaining data\n");
            goto cleanup;
        }
    }

    fclose(base);
    fclose(new_file);
    fclose(delta);
    return EB_SUCCESS;

cleanup:
    fclose(base);
    fclose(new_file);
    fclose(delta);
    return EB_ERROR_FILE_IO;
}

static eb_status_t apply_binary_delta(const char* base_path, const char* delta_path, const char* output_path) {
    DEBUG_PRINT("apply_binary_delta: Applying delta from %s using base %s\n", delta_path, base_path);
    
    FILE* base = fopen(base_path, "rb");
    FILE* delta = fopen(delta_path, "rb");
    FILE* output = fopen(output_path, "wb");
    
    if (!base || !delta || !output) {
        if (base) fclose(base);
        if (delta) fclose(delta);
        if (output) fclose(output);
        DEBUG_PRINT("apply_binary_delta: Failed to open files\n");
        return EB_ERROR_FILE_IO;
    }

    // Read and verify delta header
    uint32_t magic, version;
    if (fread(&magic, sizeof(magic), 1, delta) != 1 ||
        fread(&version, sizeof(version), 1, delta) != 1 ||
        magic != 0x3E42444C || version != 1) {
        DEBUG_PRINT("apply_binary_delta: Invalid delta header\n");
        goto cleanup;
    }

    // Copy base file to output
    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), base)) > 0) {
        if (fwrite(buffer, 1, n, output) != n) {
            DEBUG_PRINT("apply_binary_delta: Failed to copy base file\n");
            goto cleanup;
        }
    }

    // Apply delta commands
    uint64_t cmd_offset;
    uint8_t cmd_size;
    
    while (fread(&cmd_offset, sizeof(cmd_offset), 1, delta) == 1) {
        if (fread(&cmd_size, sizeof(cmd_size), 1, delta) != 1) {
            DEBUG_PRINT("apply_binary_delta: Failed to read command size\n");
            goto cleanup;
        }

        // Seek to position in output file
        if (fseek(output, cmd_offset, SEEK_SET) != 0) {
            DEBUG_PRINT("apply_binary_delta: Failed to seek in output file\n");
            goto cleanup;
        }

        // Read and write modified data
        unsigned char mod_data[255];  // Max cmd_size is 255
        if (fread(mod_data, 1, cmd_size, delta) != cmd_size ||
            fwrite(mod_data, 1, cmd_size, output) != cmd_size) {
            DEBUG_PRINT("apply_binary_delta: Failed to apply modification\n");
            goto cleanup;
        }
    }

    fclose(base);
    fclose(delta);
    fclose(output);
    return EB_SUCCESS;

cleanup:
    fclose(base);
    fclose(delta);
    fclose(output);
    return EB_ERROR_FILE_IO;
}