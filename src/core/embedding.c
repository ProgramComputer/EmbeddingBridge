/*
 * EmbeddingBridge - Core Embedding Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "types.h"
#include "embedding.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <openssl/evp.h>
#include <openssl/sha.h>
#include "debug.h"

// Dynamic model registry - simplified design
#define MAX_MODELS 32
#define MAX_MODEL_NAME 64

// File paths for model registry
#define MODEL_REGISTRY_DIR ".eb/metadata/models"
#define MODEL_REGISTRY_FILE ".eb/metadata/models/registry.json"
#define EB_DIR_ENVIRONMENT "EB_DIR"
#define PATH_SEP '/'

// Cache for repository root
static char* cached_repo_root = NULL;
static pthread_mutex_t repo_root_lock = PTHREAD_MUTEX_INITIALIZER;

static void free_cached_repo_root(void) {
    pthread_mutex_lock(&repo_root_lock);
    free(cached_repo_root);
    cached_repo_root = NULL;
    pthread_mutex_unlock(&repo_root_lock);
}

static char* normalize_path(const char* path) {
    if (!path) return NULL;
    
    char* normalized = malloc(PATH_MAX);
    if (!normalized) return NULL;
    
    char* dst = normalized;
    const char* src = path;
    
    // Handle Windows paths
    #ifdef _WIN32
    while (*src) {
        if (*src == '\\') *dst = '/';
        else *dst = *src;
        src++;
        dst++;
    }
    #else
    strcpy(normalized, path);
    #endif
    
    // Remove trailing slashes
    size_t len = strlen(normalized);
    while (len > 1 && normalized[len-1] == '/') {
        normalized[--len] = '\0';
    }
    
    return normalized;
}

static char* resolve_symlink(const char* path) {
    #ifdef _WIN32
    // Windows doesn't have symbolic links in the same way
    return strdup(path);
    #else
    char* resolved = malloc(PATH_MAX);
    if (!resolved) return NULL;
    
    if (realpath(path, resolved) == NULL) {
        free(resolved);
        return strdup(path);  // Fall back to original path
    }
    return resolved;
    #endif
}

static char* find_repository_root(void) {
    // Check cache first
    pthread_mutex_lock(&repo_root_lock);
    if (cached_repo_root) {
        DEBUG_PRINT("Using cached repo root: %s\n", cached_repo_root);
        char* result = strdup(cached_repo_root);
        pthread_mutex_unlock(&repo_root_lock);
        return result;
    }
    pthread_mutex_unlock(&repo_root_lock);

    // Check EB_DIR environment variable first
    const char* eb_dir = getenv(EB_DIR_ENVIRONMENT);
    if (eb_dir) {
        DEBUG_PRINT("Found EB_DIR environment variable: %s\n", eb_dir);
        char* normalized = normalize_path(eb_dir);
        if (normalized) {
            struct stat st;
            if (stat(normalized, &st) == 0 && S_ISDIR(st.st_mode)) {
                pthread_mutex_lock(&repo_root_lock);
                cached_repo_root = normalized;
                char* result = strdup(normalized);
                pthread_mutex_unlock(&repo_root_lock);
                DEBUG_PRINT("Using EB_DIR as repo root: %s\n", result);
                return result;
            }
            free(normalized);
        }
    }

    // Get current working directory
    char* path = malloc(PATH_MAX);
    if (!path) {
        DEBUG_PRINT("Failed to allocate memory for path\n");
        return NULL;
    }

    if (!getcwd(path, PATH_MAX)) {
        DEBUG_PRINT("Failed to get current working directory: %s\n", strerror(errno));
        free(path);
        return NULL;
    }
    DEBUG_PRINT("Starting search from current directory: %s\n", path);

    char* normalized = normalize_path(path);
    free(path);
    if (!normalized) {
        DEBUG_PRINT("Failed to normalize path\n");
        return NULL;
    }

    // Walk up the directory tree
    char* current = normalized;
    while (strlen(current) > 1) {
        char eb_dir[PATH_MAX];
        snprintf(eb_dir, sizeof(eb_dir), "%s/.eb", current);
        DEBUG_PRINT("Checking for .eb directory at: %s\n", eb_dir);
        
        // Resolve any symbolic links
        char* resolved = resolve_symlink(eb_dir);
        if (!resolved) {
            DEBUG_PRINT("Failed to resolve symlink for: %s\n", eb_dir);
            free(normalized);
            return NULL;
        }
        
        struct stat st;
        if (stat(resolved, &st) == 0 && S_ISDIR(st.st_mode)) {
            DEBUG_PRINT("Found .eb directory at: %s\n", resolved);
            free(resolved);
            
            // Cache the result
            pthread_mutex_lock(&repo_root_lock);
            cached_repo_root = strdup(current);
            char* result = strdup(current);
            pthread_mutex_unlock(&repo_root_lock);
            
            DEBUG_PRINT("Setting repo root to: %s\n", result);
            free(normalized);
            return result;
        }
        free(resolved);

        // Go up one directory
        char* last_slash = strrchr(current, PATH_SEP);
        if (!last_slash) {
            DEBUG_PRINT("No more parent directories to check\n");
            break;
        }
        *last_slash = '\0';
        DEBUG_PRINT("Moving up to parent directory: %s\n", current);
    }

    DEBUG_PRINT("Failed to find repository root\n");
    free(normalized);
    return NULL;
}

typedef struct {
    char name[MAX_MODEL_NAME];
    size_t dimensions;
    bool normalize;
    char* version;
    char* description;
} eb_model_registry_entry_t;

static struct {
    eb_model_registry_entry_t models[MAX_MODELS];
    size_t count;
} model_registry = {
    .models = {{{0}}},
    .count = 0
};

static bool registry_initialized = false;

static eb_status_t ensure_registry_loaded(void) {
    DEBUG_PRINT("Entering ensure_registry_loaded, initialized=%d, count=%zu\n", 
            registry_initialized, model_registry.count);
    
    if (registry_initialized) {
        DEBUG_PRINT("Registry already initialized\n");
        return EB_SUCCESS;
    }

    // Reset registry state
    model_registry.count = 0;
    memset(model_registry.models, 0, sizeof(model_registry.models));

    // Find repository root
    char* repo_root = find_repository_root();
    if (!repo_root) {
        DEBUG_PRINT("Failed to find repository root\n");
        return EB_ERROR_NOT_INITIALIZED;
    }
    DEBUG_PRINT("Found repository root at: %s\n", repo_root);

    // Check if models directory exists
    char models_dir[1024];
    snprintf(models_dir, sizeof(models_dir), "%s/%s", repo_root, MODEL_REGISTRY_DIR);
    DEBUG_PRINT("Checking models directory at: %s\n", models_dir);
    
    struct stat st;
    if (stat(models_dir, &st) != 0) {
        DEBUG_PRINT("Creating models directory\n");
        if (mkdir(models_dir, 0755) != 0) {
            DEBUG_PRINT("Failed to create models directory: %s\n", strerror(errno));
            free(repo_root);
            return EB_ERROR_FILE_IO;
        }
    }

    // Try to read registry file
    char registry_path[1024];
    snprintf(registry_path, sizeof(registry_path), "%s/%s", repo_root, MODEL_REGISTRY_FILE);
    DEBUG_PRINT("Attempting to read registry file: %s\n", registry_path);
    free(repo_root);
    
    FILE* f = fopen(registry_path, "r");
    if (!f) {
        DEBUG_PRINT("No existing registry file found at %s: %s\n", 
                registry_path, strerror(errno));
        registry_initialized = true;
        return EB_SUCCESS;  // Not an error, just empty registry
    }

    // Read registry file
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char name[MAX_MODEL_NAME];
        size_t dimensions;
        int normalize;
        char version[256];
        char description[1024];

        // Parse line (tab-separated format)
        if (sscanf(line, "%63[^\t]\t%zu\t%d\t%255[^\t]\t%1023[^\n]",
                   name, &dimensions, &normalize, version, description) != 5) {
            DEBUG_PRINT("Skipping malformed line in registry: %s", line);
            continue;
        }

        // Add to in-memory registry
        size_t idx = model_registry.count;
        if (idx >= MAX_MODELS) {
            DEBUG_PRINT("Registry full (max %d models)\n", MAX_MODELS);
            fclose(f);
            return EB_ERROR_MEMORY_ALLOCATION;
        }

        strncpy(model_registry.models[idx].name, name, MAX_MODEL_NAME - 1);
        model_registry.models[idx].name[MAX_MODEL_NAME - 1] = '\0';
        model_registry.models[idx].dimensions = dimensions;
        model_registry.models[idx].normalize = normalize;
        
        model_registry.models[idx].version = strdup(version);
        model_registry.models[idx].description = strdup(description);
        
        if (!model_registry.models[idx].version || !model_registry.models[idx].description) {
            DEBUG_PRINT("Failed to allocate memory for model metadata\n");
            free(model_registry.models[idx].version);
            free(model_registry.models[idx].description);
            fclose(f);
            return EB_ERROR_MEMORY_ALLOCATION;
        }

        DEBUG_PRINT("Loaded model: %s (dimensions=%zu, normalize=%d)\n",
                name, dimensions, normalize);
        model_registry.count++;
    }

    fclose(f);
    registry_initialized = true;
    DEBUG_PRINT("Successfully loaded %zu models from registry\n", model_registry.count);
    return EB_SUCCESS;
}

static eb_status_t save_registry(void) {
    // Find repository root
    char* repo_root = find_repository_root();
    if (!repo_root) {
        DEBUG_PRINT("Failed to find repository root\n");
        return EB_ERROR_NOT_INITIALIZED;
    }

    // Open registry file
    char registry_path[1024];
    snprintf(registry_path, sizeof(registry_path), "%s/%s", repo_root, MODEL_REGISTRY_FILE);
    free(repo_root);
    
    FILE* f = fopen(registry_path, "w");
    if (!f) {
        DEBUG_PRINT("Failed to open registry file for writing at %s\n", registry_path);
        return EB_ERROR_FILE_IO;
    }

    // Write each model
    for (size_t i = 0; i < model_registry.count; i++) {
        fprintf(f, "%s\t%zu\t%d\t%s\t%s\n",
                model_registry.models[i].name,
                model_registry.models[i].dimensions,
                model_registry.models[i].normalize,
                model_registry.models[i].version,
                model_registry.models[i].description);
    }

    fclose(f);
    DEBUG_PRINT("Saved %zu models to registry\n", model_registry.count);
    return EB_SUCCESS;
}

eb_status_t eb_register_model(
    const char* name,
    size_t dimensions,
    bool normalize,
    const char* version,
    const char* description
) {
    DEBUG_PRINT("Entering eb_register_model\n");
    DEBUG_PRINT("Parameters - name: %s, dimensions: %zu, normalize: %d, version: %s\n", 
            name, dimensions, normalize, version ? version : "NULL");
    
    // Load existing registry
    eb_status_t status = ensure_registry_loaded();
    if (status != EB_SUCCESS)
        return status;

    if (!name || !version || !description || dimensions == 0) {
        DEBUG_PRINT("Invalid parameters detected\n");
        return EB_ERROR_INVALID_INPUT;
    }

    // Check if model already exists
    for (size_t i = 0; i < model_registry.count; i++) {
        if (strcmp(name, model_registry.models[i].name) == 0) {
            DEBUG_PRINT("Model %s already exists at index %zu\n", name, i);
            return EB_ERROR_INVALID_INPUT;
        }
    }

    // Check if registry is full
    if (model_registry.count >= MAX_MODELS) {
        DEBUG_PRINT("Registry full (count: %zu, max: %d)\n", model_registry.count, MAX_MODELS);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Add new model
    size_t idx = model_registry.count;
    strncpy(model_registry.models[idx].name, name, MAX_MODEL_NAME - 1);
    model_registry.models[idx].name[MAX_MODEL_NAME - 1] = '\0';
    
    model_registry.models[idx].version = strdup(version);
    if (!model_registry.models[idx].version)
        return EB_ERROR_MEMORY_ALLOCATION;
    
    model_registry.models[idx].description = strdup(description);
    if (!model_registry.models[idx].description) {
        free(model_registry.models[idx].version);
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    
    model_registry.models[idx].dimensions = dimensions;
    model_registry.models[idx].normalize = normalize;
    model_registry.count++;

    // Save updated registry
    status = save_registry();
    if (status != EB_SUCCESS) {
        // Rollback on save failure
        free(model_registry.models[idx].version);
        free(model_registry.models[idx].description);
        model_registry.count--;
        return status;
    }

    DEBUG_PRINT("Successfully registered model at index %zu, new count: %zu\n", idx, model_registry.count);
    return EB_SUCCESS;
}

bool eb_is_model_registered(const char* name) {
    eb_status_t status = ensure_registry_loaded();
    if (status != EB_SUCCESS || !name || !model_registry.count) {
        return false;
    }

    for (size_t i = 0; i < model_registry.count; i++) {
        if (strcmp(model_registry.models[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

eb_status_t eb_list_models(char*** out_names, size_t* out_count) {
    if (!out_names || !out_count)
        return EB_ERROR_INVALID_INPUT;

    // Load registry first
    eb_status_t status = ensure_registry_loaded();
    if (status != EB_SUCCESS)
        return status;

    *out_names = malloc(model_registry.count * sizeof(char*));
    if (!*out_names)
        return EB_ERROR_MEMORY_ALLOCATION;

    for (size_t i = 0; i < model_registry.count; i++) {
        (*out_names)[i] = strdup(model_registry.models[i].name);
        if (!(*out_names)[i]) {
            // Cleanup on failure
            for (size_t j = 0; j < i; j++)
                free((*out_names)[j]);
            free(*out_names);
            return EB_ERROR_MEMORY_ALLOCATION;
        }
    }

    *out_count = model_registry.count;
    return EB_SUCCESS;
}

void eb_unregister_model(const char* name) {
    if (!name || !model_registry.count) {
        return;
    }

    for (size_t i = 0; i < model_registry.count; i++) {
        if (strcmp(model_registry.models[i].name, name) == 0) {
            free(model_registry.models[i].version);
            free(model_registry.models[i].description);
            
            // Move last entry to this position if not last
            if (i < model_registry.count - 1) {
                model_registry.models[i] = model_registry.models[model_registry.count - 1];
            }
            
            model_registry.count--;
            break;
        }
    }
}

eb_status_t eb_get_model_info(const char* name, eb_model_info_t* out_info) {
    if (!name || !out_info) {
        return EB_ERROR_INVALID_INPUT;
    }

    for (size_t i = 0; i < model_registry.count; i++) {
        if (strcmp(name, model_registry.models[i].name) == 0) {
            out_info->dimensions = model_registry.models[i].dimensions;
            out_info->normalize_output = model_registry.models[i].normalize;
            
            out_info->version = strdup(model_registry.models[i].version);
            out_info->description = strdup(model_registry.models[i].description);
            
            if (!out_info->version || !out_info->description) {
                free(out_info->version);
                free(out_info->description);
                return EB_ERROR_MEMORY_ALLOCATION;
            }
            
            return EB_SUCCESS;
        }
    }

    return EB_ERROR_NOT_FOUND;
}

eb_status_t eb_generate_embedding(const char* text, const char* model_name, eb_embedding_t** out_embedding) {
    DEBUG_PRINT("Entering eb_generate_embedding with text: %.30s...\n", text);
    
    if (!text || !model_name || !out_embedding) {
        DEBUG_PRINT("Invalid input parameters\n");
        return EB_ERROR_INVALID_INPUT;
    }

    // Get model info
    eb_model_info_t model_info;
    eb_status_t status = eb_get_model_info(model_name, &model_info);
    if (status != EB_SUCCESS) {
        DEBUG_PRINT("Failed to get model info for %s: %d\n", model_name, status);
        return status;
    }
    DEBUG_PRINT("Got model info - dimensions: %zu, normalize: %d\n", 
            model_info.dimensions, model_info.normalize_output);

    // Allocate embedding
    *out_embedding = malloc(sizeof(eb_embedding_t));
    if (!*out_embedding) {
        DEBUG_PRINT("Failed to allocate embedding struct\n");
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Allocate values array
    (*out_embedding)->values = malloc(model_info.dimensions * sizeof(float));
    if (!(*out_embedding)->values) {
        DEBUG_PRINT("Failed to allocate values array\n");
        free(*out_embedding);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize embedding
    (*out_embedding)->dimensions = model_info.dimensions;
    (*out_embedding)->normalize = model_info.normalize_output;

    // Call the actual model to generate embeddings
    // For now, we'll use a simple hash-based approach that's deterministic
    // but produces more realistic looking embeddings than the previous version
    uint8_t hash[32];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        DEBUG_PRINT("Failed to create EVP context\n");
        free((*out_embedding)->values);
        free(*out_embedding);
        return EB_ERROR_COMPUTATION_FAILED;
    }

    const EVP_MD* md = EVP_sha256();
    if (!md || 
        EVP_DigestInit_ex(ctx, md, NULL) != 1 ||
        EVP_DigestUpdate(ctx, text, strlen(text)) != 1) {
        DEBUG_PRINT("Failed to initialize/update digest\n");
        EVP_MD_CTX_free(ctx);
        free((*out_embedding)->values);
        free(*out_embedding);
        return EB_ERROR_COMPUTATION_FAILED;
    }

    unsigned int hash_len;
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        DEBUG_PRINT("Failed to finalize digest\n");
        EVP_MD_CTX_free(ctx);
        free((*out_embedding)->values);
        free(*out_embedding);
        return EB_ERROR_COMPUTATION_FAILED;
    }
    EVP_MD_CTX_free(ctx);

    // Generate embedding values using the hash as a seed
    for (size_t i = 0; i < model_info.dimensions; i++) {
        // Use different bytes of the hash to seed each value
        uint8_t seed = hash[i % 32];
        // Generate a value between -1 and 1 using the seed
        float value = ((float)seed / 255.0f) * 2.0f - 1.0f;
        (*out_embedding)->values[i] = value;
    }

    // Normalize if required
    if (model_info.normalize_output) {
        DEBUG_PRINT("Normalizing embedding\n");
        status = eb_normalize_embedding(*out_embedding);
        if (status != EB_SUCCESS) {
            DEBUG_PRINT("Failed to normalize embedding\n");
            free((*out_embedding)->values);
            free(*out_embedding);
            *out_embedding = NULL;
            return status;
        }
    }

    DEBUG_PRINT("Successfully generated embedding\n");
    return EB_SUCCESS;
}

eb_status_t eb_create_embedding_from_file(const char* filepath,
                                     const char* model_name,
                                     eb_embedding_t** out_embedding) {
    if (!filepath || !model_name || !out_embedding) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Read file content
    FILE* file = fopen(filepath, "r");
    if (!file) {
        return EB_ERROR_FILE_IO;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate buffer for file content
    char* content = malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Read file content
    size_t bytes_read = fread(content, 1, file_size, file);
    fclose(file);
    content[bytes_read] = '\0';

    // Generate embedding - let eb_generate_embedding handle allocation
    eb_status_t status = eb_generate_embedding(content, model_name, out_embedding);
    free(content);

    return status;
}

void eb_destroy_embedding(eb_embedding_t* embedding) {
    if (embedding) {
        free(embedding->values);
        free(embedding);
    }
}

size_t eb_get_dtype_size(eb_dtype_t dtype) {
    switch (dtype) {
        case EB_FLOAT32: return 4;
        case EB_FLOAT64: return 8;
        case EB_INT32: return 4;
        case EB_INT64: return 8;
        default: return 0;
    }
}

eb_status_t eb_create_embedding(
    void* data,
    size_t dimensions,
    size_t count,
    eb_dtype_t dtype,
    bool normalize,
    eb_embedding_t** out_embedding
) {
    if (!data || !out_embedding || dimensions == 0) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Allocate embedding
    *out_embedding = malloc(sizeof(eb_embedding_t));
    if (!*out_embedding) {
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Allocate values array
    (*out_embedding)->values = malloc(dimensions * sizeof(float));
    if (!(*out_embedding)->values) {
        free(*out_embedding);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize embedding
    (*out_embedding)->dimensions = dimensions;
    (*out_embedding)->normalize = normalize;

    // Convert data based on dtype
    switch (dtype) {
        case EB_FLOAT32:
            memcpy((*out_embedding)->values, data, dimensions * sizeof(float));
            break;
        case EB_FLOAT64: {
            double* double_data = (double*)data;
            for (size_t i = 0; i < dimensions; i++) {
                (*out_embedding)->values[i] = (float)double_data[i];
            }
            break;
        }
        case EB_INT32: {
            int32_t* int32_data = (int32_t*)data;
            for (size_t i = 0; i < dimensions; i++) {
                (*out_embedding)->values[i] = (float)int32_data[i];
            }
            break;
        }
        case EB_INT64: {
            int64_t* int64_data = (int64_t*)data;
            for (size_t i = 0; i < dimensions; i++) {
                (*out_embedding)->values[i] = (float)int64_data[i];
            }
            break;
        }
        default:
            free((*out_embedding)->values);
            free(*out_embedding);
            return EB_ERROR_INVALID_INPUT;
    }

    // Normalize if required
    if (normalize) {
        eb_status_t status = eb_normalize_embedding(*out_embedding);
        if (status != EB_SUCCESS) {
            free((*out_embedding)->values);
            free(*out_embedding);
            *out_embedding = NULL;
            return status;
        }
    }

    return EB_SUCCESS;
}

void eb_metadata_destroy(eb_metadata_t* metadata) {
    if (!metadata) return;
    
    // Free all metadata in the linked list
    eb_metadata_t* current = metadata;
    while (current) {
        eb_metadata_t* next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }
}

eb_status_t eb_normalize_embedding(eb_embedding_t* embedding) {
    if (!embedding || !embedding->values || embedding->dimensions == 0) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Calculate L2 norm
    float norm = 0.0f;
    for (size_t i = 0; i < embedding->dimensions; i++) {
        norm += embedding->values[i] * embedding->values[i];
    }
    norm = sqrtf(norm);

    // Avoid division by zero
    if (norm < 1e-10f) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Normalize values
    for (size_t i = 0; i < embedding->dimensions; i++) {
        embedding->values[i] /= norm;
    }

    return EB_SUCCESS;
}

eb_status_t eb_get_model(const char* name, eb_model_registry_entry_t* out_model) {
    DEBUG_PRINT("Entering eb_get_model for model: %s\n", name ? name : "NULL");
    
    if (!name || !out_model) {
        DEBUG_PRINT("Invalid parameters in eb_get_model\n");
        return EB_ERROR_INVALID_INPUT;
    }

    for (size_t i = 0; i < model_registry.count; i++) {
        if (strcmp(model_registry.models[i].name, name) == 0) {
            memcpy(out_model, &model_registry.models[i], sizeof(eb_model_registry_entry_t));
            DEBUG_PRINT("Found model %s at index %zu\n", name, i);
            return EB_SUCCESS;
        }
    }

    DEBUG_PRINT("Model %s not found in registry\n", name);
    return EB_ERROR_NOT_FOUND;
}

/**
 * Calculate Levenshtein distance between two strings
 */
static size_t levenshtein_distance(const char* s1, const char* s2) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    size_t matrix[len1 + 1][len2 + 1];

    for (size_t i = 0; i <= len1; i++)
        matrix[i][0] = i;
    for (size_t j = 0; j <= len2; j++)
        matrix[0][j] = j;

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            size_t cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            size_t above = matrix[i-1][j] + 1;
            size_t left = matrix[i][j-1] + 1;
            size_t diag = matrix[i-1][j-1] + cost;
            matrix[i][j] = above < left ? (above < diag ? above : diag) 
                                      : (left < diag ? left : diag);
        }
    }
    return matrix[len1][len2];
}

eb_status_t eb_find_similar_models(const char* name, char*** similar_names, size_t* count) {
    if (!name || !similar_names || !count) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Get all registered models
    char** all_models;
    size_t model_count;
    eb_status_t status = eb_list_models(&all_models, &model_count);
    if (status != EB_SUCCESS) {
        return status;
    }

    // Allocate space for similar names (max 5 suggestions)
    const size_t max_suggestions = 5;
    *similar_names = malloc(sizeof(char*) * max_suggestions);
    if (!*similar_names) {
        for (size_t i = 0; i < model_count; i++) {
            free(all_models[i]);
        }
        free(all_models);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Find similar names using Levenshtein distance
    *count = 0;
    for (size_t i = 0; i < model_count && *count < max_suggestions; i++) {
        size_t distance = levenshtein_distance(name, all_models[i]);
        size_t max_len = strlen(name) > strlen(all_models[i]) ? 
                        strlen(name) : strlen(all_models[i]);
        
        // Consider names similar if distance is less than 50% of max length
        if (distance <= max_len / 2) {
            (*similar_names)[*count] = strdup(all_models[i]);
            if (!(*similar_names)[*count]) {
                // Clean up on error
                for (size_t j = 0; j < *count; j++) {
                    free((*similar_names)[j]);
                }
                free(*similar_names);
                for (size_t j = 0; j < model_count; j++) {
                    free(all_models[j]);
                }
                free(all_models);
                return EB_ERROR_MEMORY_ALLOCATION;
            }
            (*count)++;
        }
    }

    // Clean up model list
    for (size_t i = 0; i < model_count; i++) {
        free(all_models[i]);
    }
    free(all_models);

    return EB_SUCCESS;
}

void eb_cleanup_registry(void) {
    // Clean up model registry
    for (size_t i = 0; i < model_registry.count; i++) {
        free(model_registry.models[i].version);
        free(model_registry.models[i].description);
    }
    model_registry.count = 0;
    registry_initialized = false;

    // Clean up cached repo root
    free_cached_repo_root();
}

bool eb_calculate_file_hash(const char *file_path, char *hash_out)
{
        FILE *f = fopen(file_path, "rb");
        if (!f)
                return false;

        SHA256_CTX ctx;
        SHA256_Init(&ctx);

        unsigned char buf[8192];
        size_t n;

        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                SHA256_Update(&ctx, buf, n);
        }

        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_Final(hash, &ctx);
        fclose(f);

        /* Convert to hex string */
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                sprintf(hash_out + (i * 2), "%02x", hash[i]);
        }
        hash_out[64] = '\0';

        return true;
} 