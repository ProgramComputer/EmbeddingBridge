/*
 * EmbeddingBridge - Embedding Comparison Implementation
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
#include <math.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include <linux/limits.h>  // For PATH_MAX on Linux
#include <npy_array.h>
#include <ctype.h>

/* Core includes - order matters */
#include "../core/types.h"
#include "../core/error.h"
#include "../core/debug.h"
#include "../core/embedding.h"
#include "../core/store.h"
#include "../core/path_utils.h"

/* CLI includes */
#include "cli.h"
#include "colors.h"
#include "debug.h"

/* Function declarations */
void cli_info(const char* format, ...);
static bool is_hex_string(const char* str);
static bool has_multiple_models(const char* repo_root, const char* file_path);
static char* get_available_models(const char* repo_root, const char* file_path);
static const char* get_default_model_for_file(const char* repo_root, const char* file_path);

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Forward declarations */
static float* load_npy_embedding(const char* filepath, size_t* dims);
static float* load_bin_embedding(const char* filepath, size_t* dims);
static float* load_stored_embedding(const char* hash, size_t* dims);
static float* load_embedding(const char* path_or_hash, size_t* dims);
static char* resolve_hash(const char* input_hash);
static bool is_valid_hash(const char* str);
static int ends_with(const char* str, const char* suffix);
static int check_invalid_values(const float* embedding, size_t dims);
static float cosine_similarity(const float* vec1, const float* vec2, size_t dims);
static float euclidean_distance(const float* vec1, const float* vec2, size_t dims);
static float euclidean_similarity(const float* vec1, const float* vec2, size_t dims);

static const char* DIFF_USAGE = 
    "Usage: eb diff [options] <input1> <input2>\n"
    "\n"
    "Compare two embeddings and show their similarity\n"
    "\n"
    "Arguments:\n"
    "  <input1>    First embedding (hash, file, or source file)\n"
    "  <input2>    Second embedding (hash, file, or source file)\n"
    "\n"
    "Options:\n"
    "  --models <model1>[,<model2>]  Specify models to use (required for multi-model repos)\n"
    "  --model <model>               Shorthand to use the same model for both inputs\n"
    "\n"
    "Examples:\n"
    "  eb diff 7d39a15 9f3e8c2               # Compare using short hashes (7 chars)\n"
    "  eb diff 7d39a15cb74e02f1a0a4e5842b1b1d5c3e2a98765434abcdef 9f3e8c2a3b4c5d6e\n" 
    "                                       # Compare using full or partial hashes\n"
    "  eb diff file1.npy file2.npy           # Compare two .npy files\n"
    "  eb diff file1.bin file2.bin           # Compare two binary files\n"
    "  eb diff doc1.txt doc2.txt             # Compare source files (looks for associated files)\n"
    "  eb diff --model voyage-2 file.txt      # Compare latest vs. previous for voyage-2\n"
    "  eb diff --models openai-3,voyage-2 file1.txt file2.txt\n"
    "                                       # Compare file1 with openai-3 and file2 with voyage-2\n";

/* Calculate cosine similarity between two float vectors */
static float cosine_similarity(const float *vec1, const float *vec2, size_t dims)
{
    double dot_product = 0.0;
    double norm1 = 0.0;
    double norm2 = 0.0;
    
    DEBUG_PRINT("Calculating similarity for %zu dimensions", dims);
    
    // Calculate dot product and norms in one pass
    for (size_t i = 0; i < dims; i++) {
        // Check for invalid values
        if (isnan(vec1[i]) || isnan(vec2[i]) || 
            isinf(vec1[i]) || isinf(vec2[i])) {
            DEBUG_PRINT("Invalid value detected at index %zu: vec1=%f, vec2=%f",
                       i, vec1[i], vec2[i]);
            return 0.0f;
        }
        
        dot_product += (double)vec1[i] * (double)vec2[i];
        norm1 += (double)vec1[i] * (double)vec1[i];
        norm2 += (double)vec2[i] * (double)vec2[i];
    }
    
    DEBUG_PRINT("Raw calculations: dot_product=%f, norm1=%f, norm2=%f",
                dot_product, norm1, norm2);
    
    if (norm1 <= 0.0 || norm2 <= 0.0) {
        DEBUG_PRINT("Zero norm detected: norm1=%f, norm2=%f", norm1, norm2);
        return 0.0f;
    }
    
    // Calculate cosine similarity
    float similarity = (float)(dot_product / (sqrt(norm1) * sqrt(norm2)));
    
    DEBUG_PRINT("Calculated cosine similarity: %f", similarity);
    
    return similarity;
}

/* Calculate Euclidean distance between two float vectors */
static float euclidean_distance(const float *vec1, const float *vec2, size_t dims)
{
    double sum = 0.0;
    
    DEBUG_PRINT("Calculating Euclidean distance for %zu dimensions", dims);
    
    for (size_t i = 0; i < dims; i++) {
        // Check for invalid values
        if (isnan(vec1[i]) || isnan(vec2[i]) || 
            isinf(vec1[i]) || isinf(vec2[i])) {
            DEBUG_PRINT("Invalid value detected at index %zu: vec1=%f, vec2=%f",
                       i, vec1[i], vec2[i]);
            return INFINITY;
        }
        
        double diff = (double)vec1[i] - (double)vec2[i];
        sum += diff * diff;
    }
    
    DEBUG_PRINT("Euclidean distance squared: %f", sum);
    
    return (float)sqrt(sum);
}

/* Calculate normalized Euclidean similarity (0 to 1 scale, where 1 is identical) */
static float euclidean_similarity(const float *vec1, const float *vec2, size_t dims)
{
    float distance = euclidean_distance(vec1, vec2, dims);
    
    if (isinf(distance) || isnan(distance)) {
        return 0.0f;
    }
    
    // Normalize to [0,1] range where 1 means identical
    // Using a common approach: 1 / (1 + distance)
    float similarity = 1.0f / (1.0f + distance);
    
    DEBUG_PRINT("Calculated normalized Euclidean similarity: %f", similarity);
    
    return similarity;
}

/* Load embedding from .npy file */
static float* load_npy_embedding(const char *filepath, size_t *dims) 
{
    npy_array_t *arr = npy_array_load(filepath);
    if (!arr) {
        cli_error("Cannot read NumPy file: %s", filepath);
        return NULL;
    }

    DEBUG_PRINT("Loading .npy file with %d dimensions", arr->ndim);
    DEBUG_PRINT("NPY array info:");
    DEBUG_PRINT("  Type: %c", arr->typechar);
    DEBUG_PRINT("  Dimensions: %d", arr->ndim);
    for (int i = 0; i < arr->ndim; i++) {
        DEBUG_PRINT("  Shape[%d]: %zu", i, arr->shape[i]);
    }
    DEBUG_PRINT("  Element size: %zu", arr->elem_size);
    DEBUG_PRINT("  Fortran order: %s", arr->fortran_order ? "true" : "false");

    // Verify it's a float array (float32 or float64)
    if (arr->typechar != 'f') {
        cli_error("Invalid .npy format - expected float32/float64 array, got type '%c'", arr->typechar);
        npy_array_free(arr);
        return NULL;
    }

    // Get total size
    size_t total_elements = 1;
    for (int i = 0; i < arr->ndim; i++) {
        total_elements *= arr->shape[i];
    }
    *dims = total_elements;

    // Allocate and copy the data
    float *data = malloc(total_elements * sizeof(float));
    if (!data) {
        cli_error("Out of memory");
        npy_array_free(arr);
        return NULL;
    }

    // If float64, we need to convert to float32
    if (arr->elem_size == 8) {
        double *src = (double*)arr->data;
        for (size_t i = 0; i < total_elements; i++) {
            data[i] = (float)src[i];
        }
    } else {
        memcpy(data, arr->data, total_elements * sizeof(float));
    }

    DEBUG_PRINT("First 5 values from .npy file:");
    for (size_t i = 0; i < 5 && i < total_elements; i++) {
        DEBUG_PRINT("[%zu]: %f", i, data[i]);
    }

    npy_array_free(arr);
    return data;
}

/* Helper function to check if string ends with suffix */
static int ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (str_len < suffix_len) return 0;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/* Load embedding from binary file */
static float* load_bin_embedding(const char *filepath, size_t *dims) 
{
    struct stat st;
    FILE *f;
    float *data = NULL;
    
    if (stat(filepath, &st) != 0) {
        cli_error("Cannot access binary file: %s", filepath);
        return NULL;
    }
    
    *dims = st.st_size / sizeof(float);
    
    f = fopen(filepath, "rb");
    if (!f) {
        cli_error("Cannot open binary file: %s", filepath);
        return NULL;
    }
    
    data = malloc(st.st_size);
    if (!data) {
        cli_error("Out of memory");
        fclose(f);
        return NULL;
    }
    
    if (fread(data, 1, (size_t)st.st_size, f) != (size_t)st.st_size) {
        cli_error("Failed to read binary file: %s", filepath);
        free(data);
        fclose(f);
        return NULL;
    }

    DEBUG_PRINT("First 5 values from binary file:");
    for (size_t i = 0; i < 5 && i < *dims; i++) {
        DEBUG_PRINT("[%zu]: %f", i, data[i]);
    }
    
    fclose(f);
    return data;
}

static bool is_valid_hash(const char* str) {
    if (!str || strlen(str) != 64) return false;
    for (int i = 0; str[i]; i++) {
        if (!isxdigit(str[i])) return false;
    }
    return true;
}

/* Helper function to resolve hash */
static char* resolve_hash(const char* input_hash) {
    DEBUG_PRINT("resolve_hash: Starting resolution for hash: %s\n", input_hash);
    
    char* repo_root = find_repo_root(".");
    if (!repo_root) {
        DEBUG_PRINT("resolve_hash: Failed to find repository root\n");
        cli_error("Not in an eb repository");
        return NULL;
    }
    DEBUG_PRINT("resolve_hash: Found repository root: %s\n", repo_root);

    eb_store_t* store;
    eb_store_config_t config = { .root_path = repo_root };
    if (eb_store_init(&config, &store) != EB_SUCCESS) {
        DEBUG_PRINT("resolve_hash: Failed to initialize store\n");
        free(repo_root);
        return NULL;
    }
    DEBUG_PRINT("resolve_hash: Successfully initialized store\n");

    char* full_hash = malloc(65); // 64 chars + null terminator
    if (!full_hash) {
        DEBUG_PRINT("resolve_hash: Failed to allocate memory for full hash\n");
        eb_store_destroy(store);
        free(repo_root);
        return NULL;
    }

    eb_status_t status = eb_store_resolve_hash(store, input_hash, full_hash, 65);
    DEBUG_PRINT("resolve_hash: Resolution status: %d\n", status);
    
    eb_store_destroy(store);
    free(repo_root);

    if (status != EB_SUCCESS) {
        DEBUG_PRINT("resolve_hash: Resolution failed, freeing memory\n");
        free(full_hash);
        return NULL;
    }

    DEBUG_PRINT("resolve_hash: Successfully resolved to: %s\n", full_hash);
    return full_hash;
}

/* Modified load_stored_embedding to handle multiple file types */
static float* load_embedding(const char* path_or_hash, size_t *dims) 
{
    DEBUG_PRINT("Attempting to load: %s\n", path_or_hash);
    
    // First try to resolve if it looks like a hash (4-64 hex chars)
    if (strlen(path_or_hash) >= 4 && strlen(path_or_hash) <= 64) {
        bool looks_like_hash = true;
        for (const char* p = path_or_hash; *p; p++) {
            if (!isxdigit(*p)) {
                looks_like_hash = false;
                DEBUG_PRINT("Input contains non-hex character: %c\n", *p);
                break;
            }
        }
        
        DEBUG_PRINT("Input %s looks like a hash: %s\n", path_or_hash, looks_like_hash ? "yes" : "no");
        
        if (looks_like_hash) {
            DEBUG_PRINT("Attempting to resolve hash: %s\n", path_or_hash);
            char* resolved = resolve_hash(path_or_hash);
            if (resolved) {
                DEBUG_PRINT("Successfully resolved hash %s to %s\n", path_or_hash, resolved);
                float* result = load_stored_embedding(resolved, dims);
                free(resolved);
                return result;
            } else {
                DEBUG_PRINT("Failed to resolve hash: %s\n", path_or_hash);
            }
        }
    } else {
        DEBUG_PRINT("Input length %zu is outside hash length range (4-64)\n", strlen(path_or_hash));
    }
    
    // If not a hash or hash resolution failed, try as direct file
    if (strstr(path_or_hash, ".npy")) {
        DEBUG_PRINT("Loading direct .npy file: %s\n", path_or_hash);
        return load_npy_embedding(path_or_hash, dims);
    }
    
    if (strstr(path_or_hash, ".bin")) {
        DEBUG_PRINT("Loading direct .bin file: %s\n", path_or_hash);
        return load_bin_embedding(path_or_hash, dims);
    }
    
    cli_error("Unsupported file format or invalid hash: %s", path_or_hash);
    return NULL;
}

static int check_invalid_values(const float *embedding, size_t dims) 
{
    DEBUG_PRINT("Checking %zu dimensions for invalid values", dims);
    
    for (size_t i = 0; i < dims; i++) {
        if (isnan(embedding[i]) || isinf(embedding[i])) {
            DEBUG_PRINT("Found invalid value at index %zu", i);
            return 1;
        }
    }
    
    return 0;
}

static float* load_stored_embedding(const char* hash, size_t *dims) 
{
    DEBUG_PRINT("Loading stored embedding with hash: %s\n", hash);
    
    // Find repository root
    char *repo_root = find_repo_root(".");
    if (!repo_root) {
        cli_error("Not in an eb repository");
        return NULL;
    }

    // Try using the store API first, which handles decompression properly
    eb_store_t *store = NULL;
    eb_store_config_t config = { .root_path = repo_root };
    if (eb_store_init(&config, &store) == EB_SUCCESS) {
        void *decompressed_data = NULL;
        size_t decompressed_size = 0;
        eb_object_header_t header;
        
        eb_status_t status = read_object(store, hash, &decompressed_data, &decompressed_size, &header);
        
        if (status == EB_SUCCESS && decompressed_data) {
            // Dump the first bytes to help debug format
            DEBUG_PRINT("First bytes in hex:");
            char debug_buf[100] = {0};
            for (size_t i = 0; i < 16 && i < decompressed_size; i++) {
                sprintf(debug_buf + (i*3), "%02x ", ((unsigned char*)decompressed_data)[i]);
            }
            DEBUG_PRINT("%s", debug_buf);
            
            // Check specifically for binary format with dimension header
            if (decompressed_size >= 4) {
                uint32_t dim_header = 0;
                memcpy(&dim_header, decompressed_data, sizeof(uint32_t));
                
                DEBUG_PRINT("Possible dimension header: %u (0x%08x)", dim_header, dim_header);
                
                // If this looks like a valid dimension count (1536 for OpenAI embeddings)
                if (dim_header == 1536 || (dim_header > 100 && dim_header < 10000)) {
                    DEBUG_PRINT("Found valid dimension header: %u", dim_header);
                    
                    // Set dimensions from header
                    *dims = dim_header;
                    
                    // Skip the header and copy only the actual float data
                    size_t data_size = dim_header * sizeof(float);
                    float *data_copy = malloc(data_size);
                    if (!data_copy) {
                        cli_error("Out of memory");
                        free(decompressed_data);
                        eb_store_destroy(store);
                        free(repo_root);
                        return NULL;
                    }
                    
                    // Copy data, skipping the 4-byte header
                    memcpy(data_copy, (uint8_t*)decompressed_data + sizeof(uint32_t), data_size);
                    
                    DEBUG_PRINT("Successfully extracted %u-dimension embedding", dim_header);
                    
                    free(decompressed_data);
                    eb_store_destroy(store);
                    free(repo_root);
                    return data_copy;
                }
            }
            
            // If no valid header found, use the original approach
            // Determine the number of dimensions
            *dims = decompressed_size / sizeof(float);
            
            DEBUG_PRINT("Successfully read and decompressed embedding: %zu bytes, %zu dimensions",
                      decompressed_size, *dims);
                      
            // Dump the first bytes to help debug format
            DEBUG_PRINT("First bytes in hex:");
            for (size_t i = 0; i < 32 && i < decompressed_size; i++) {
                DEBUG_PRINT("%02x ", ((unsigned char*)decompressed_data)[i]);
            }
            
            // Make a copy of the decompressed data to return
            float *data_copy = malloc(decompressed_size);
            if (!data_copy) {
                cli_error("Out of memory");
                free(decompressed_data);
                eb_store_destroy(store);
                free(repo_root);
                return NULL;
            }
            
            memcpy(data_copy, decompressed_data, decompressed_size);
            
            // Debug first few values
            DEBUG_PRINT("First 5 values from decompressed data:");
            for (size_t i = 0; i < 5 && i < *dims; i++) {
                DEBUG_PRINT("[%zu]: %f", i, data_copy[i]);
            }
            
            free(decompressed_data);
            eb_store_destroy(store);
            free(repo_root);
            return data_copy;
        } else {
            DEBUG_PRINT("Failed to read object using store API: %d", status);
            
            // Even if the store API fails, we can try to read and decompress the file directly
            // Construct path to raw file
            char raw_path[PATH_MAX];
            snprintf(raw_path, sizeof(raw_path), "%s/.eb/objects/%s.raw", repo_root, hash);
            
            // Try to read and decompress the file directly
            FILE *f = fopen(raw_path, "rb");
            if (f) {
                DEBUG_PRINT("Trying to read the object file directly: %s", raw_path);
                
                // Read the object header
                eb_object_header_t header;
                if (fread(&header, sizeof(header), 1, f) == 1) {
                    DEBUG_PRINT("Read object header: magic=0x%08x, version=%u, flags=0x%08x, size=%u",
                               header.magic, header.version, header.flags, header.size);
                    
                    // Get data size
                    fseek(f, 0, SEEK_END);
                    long file_size = ftell(f);
                    size_t data_size = file_size - sizeof(header);
                    fseek(f, sizeof(header), SEEK_SET);
                    
                    // Read the compressed data
                    void *compressed_data = malloc(data_size);
                    if (compressed_data && fread(compressed_data, 1, data_size, f) == data_size) {
                        // Try to decompress it using ZSTD
                        void *decompressed_data = NULL;
                        size_t decompressed_size = 0;
                        
                        if (eb_decompress_zstd(compressed_data, data_size, 
                                            &decompressed_data, &decompressed_size) == EB_SUCCESS) {
                            DEBUG_PRINT("Successfully decompressed data directly: %zu bytes", decompressed_size);
                            
                            // Check if it has the dimension header format
                            if (decompressed_size >= 4) {
                                uint32_t dim_header = 0;
                                memcpy(&dim_header, decompressed_data, sizeof(uint32_t));
                                
                                DEBUG_PRINT("Possible dimension header: %u (0x%08x)", dim_header, dim_header);
                                
                                // If this looks like a valid dimension count (1536 for OpenAI embeddings)
                                if (dim_header == 1536 || (dim_header > 100 && dim_header < 10000)) {
                                    DEBUG_PRINT("Found valid dimension header: %u", dim_header);
                                    
                                    // Set dimensions from header
                                    *dims = dim_header;
                                    
                                    // Skip the header and copy only the actual float data
                                    size_t data_size = dim_header * sizeof(float);
                                    float *data_copy = malloc(data_size);
                                    if (data_copy) {
                                        // Copy data, skipping the 4-byte header
                                        memcpy(data_copy, (uint8_t*)decompressed_data + sizeof(uint32_t), data_size);
                                        
                                        DEBUG_PRINT("Successfully extracted %u-dimension embedding from direct file", dim_header);
                                        
                                        free(decompressed_data);
                                        free(compressed_data);
                                        fclose(f);
                                        eb_store_destroy(store);
                                        free(repo_root);
                                        return data_copy;
                                    }
                                }
                            }
                            
                            free(decompressed_data);
                        }
                        
                        free(compressed_data);
                    }
                }
                
                fclose(f);
            }
        }
    } else {
        DEBUG_PRINT("Failed to initialize store, falling back to direct file reading");
    }
    
    // Original implementation as fallback
    // Construct path to raw file
    char raw_path[PATH_MAX];
    snprintf(raw_path, sizeof(raw_path), "%s/.eb/objects/%s.raw", repo_root, hash);
    
    // Try loading as npy file first
    npy_array_t *arr = npy_array_load(raw_path);
    if (arr) {
        DEBUG_PRINT("Successfully loaded as .npy file");
        DEBUG_PRINT("NPY array info:");
        DEBUG_PRINT("  Type: %c", arr->typechar);
        DEBUG_PRINT("  Dimensions: %d", arr->ndim);
        for (int i = 0; i < arr->ndim; i++) {
            DEBUG_PRINT("  Shape[%d]: %zu", i, arr->shape[i]);
        }
        DEBUG_PRINT("  Element size: %zu", arr->elem_size);
        DEBUG_PRINT("  Fortran order: %s", arr->fortran_order ? "true" : "false");

        // Get dimensions and allocate buffer
        *dims = arr->shape[0];
        float *data = malloc(*dims * sizeof(float));
        if (!data) {
            cli_error("Out of memory");
            npy_array_free(arr);
            free(repo_root);
            return NULL;
        }
        
        // Copy data
        memcpy(data, arr->data, *dims * sizeof(float));
        
        // Debug first few values
        DEBUG_PRINT("First 5 values:");
        for (size_t i = 0; i < 5 && i < *dims; i++) {
            DEBUG_PRINT("[%zu]: %f", i, data[i]);
        }
        
        npy_array_free(arr);
        free(repo_root);
        return data;
    }
    
    DEBUG_PRINT("Not a .npy file, trying as raw float data");
    
    // Open and read file as raw float data
    FILE *f = fopen(raw_path, "rb");
    if (!f) {
        cli_error("Cannot open raw file: %s", raw_path);
        free(repo_root);
        return NULL;
    }
    
    // Get file size for raw float data
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Calculate number of floats
    *dims = size / sizeof(float);
    
    // Allocate buffer
    float *data = malloc(size);
    if (!data) {
        cli_error("Out of memory");
        fclose(f);
        free(repo_root);
        return NULL;
    }
    
    // Read data
    if (fread(data, 1, size, f) != (size_t)size) {
        cli_error("Failed to read raw file");
        free(data);
        fclose(f);
        free(repo_root);
        return NULL;
    }
    
    // Debug first few values
    DEBUG_PRINT("First 5 values from raw data:");
    for (size_t i = 0; i < 5 && i < *dims; i++) {
        DEBUG_PRINT("[%zu]: %f", i, data[i]);
    }
    
    fclose(f);
    free(repo_root);
    return data;
}

/* Load embedding with a specific model */
static float* load_embedding_with_model(const char* path_or_hash, const char* model, size_t *dims) 
{
    DEBUG_PRINT("Attempting to load with model %s: %s\n", model ? model : "NULL", path_or_hash);
    
    // First try to resolve if it looks like a hash (4-64 hex chars)
    if (strlen(path_or_hash) >= 4 && strlen(path_or_hash) <= 64) {
        bool looks_like_hash = true;
        for (const char* p = path_or_hash; *p; p++) {
            if (!isxdigit(*p)) {
                looks_like_hash = false;
                DEBUG_PRINT("Input contains non-hex character: %c\n", *p);
                break;
            }
        }
        
        DEBUG_PRINT("Input %s looks like a hash: %s\n", path_or_hash, looks_like_hash ? "yes" : "no");
        
        if (looks_like_hash) {
            DEBUG_PRINT("Attempting to resolve hash: %s\n", path_or_hash);
            char* resolved = resolve_hash(path_or_hash);
            if (resolved) {
                DEBUG_PRINT("Successfully resolved hash %s to %s\n", path_or_hash, resolved);
                float* result = load_stored_embedding(resolved, dims);
                free(resolved);
                return result;
            } else {
                DEBUG_PRINT("Failed to resolve hash: %s\n", path_or_hash);
            }
        }
    } else {
        DEBUG_PRINT("Input length %zu is outside hash length range (4-64)\n", strlen(path_or_hash));
    }
    
    // If not a hash or hash resolution failed, try as direct file
    if (strstr(path_or_hash, ".npy")) {
        DEBUG_PRINT("Loading direct .npy file: %s\n", path_or_hash);
        return load_npy_embedding(path_or_hash, dims);
    }
    
    if (strstr(path_or_hash, ".bin")) {
        DEBUG_PRINT("Loading direct .bin file: %s\n", path_or_hash);
        return load_bin_embedding(path_or_hash, dims);
    }
    
    // If not a direct file, try to get the hash for the file and model
    char repo_root[PATH_MAX];
    char* root_dir = find_repo_root(".");
    if (root_dir) {
        strncpy(repo_root, root_dir, sizeof(repo_root) - 1);
        repo_root[sizeof(repo_root) - 1] = '\0';
        free(root_dir);
        
        char hash[65];
        char rel_path[PATH_MAX];
        
        // Get relative path if needed
        if (path_or_hash[0] == '/') {
            char* relative = get_relative_path(path_or_hash, repo_root);
            if (relative) {
                strncpy(rel_path, relative, sizeof(rel_path) - 1);
                rel_path[sizeof(rel_path) - 1] = '\0';
                free(relative);
            } else {
                strncpy(rel_path, path_or_hash, sizeof(rel_path) - 1);
                rel_path[sizeof(rel_path) - 1] = '\0';
            }
        } else {
            strncpy(rel_path, path_or_hash, sizeof(rel_path) - 1);
            rel_path[sizeof(rel_path) - 1] = '\0';
        }
        
        // For file paths, be explicit about model requirements
        if (!model) {
            // Check if multiple models exist for this file
            if (has_multiple_models(repo_root, rel_path)) {
                cli_error("Multiple models exist for '%s'. Please specify a model with --models", 
                         rel_path);
                cli_info("Available models: %s", get_available_models(repo_root, rel_path));
                return NULL;
            }
            // If only one model exists, use it (with a debug message)
            model = get_default_model_for_file(repo_root, rel_path);
            DEBUG_PRINT("Using default model '%s' for file %s", model ? model : "NULL", rel_path);
        }
        
        DEBUG_PRINT("Looking for hash with model %s for file: %s\n", model ? model : "NULL", rel_path);
        
        if (model && get_current_hash_with_model(repo_root, rel_path, model, hash, sizeof(hash)) == EB_SUCCESS) {
            // For diff command, we want to use the current hash from the model ref file or index,
            // which has already been properly updated by rollback and other commands.
            // This ensures we're comparing the correct version that the user intends to use,
            // not necessarily the most recent one in the log.
            
            DEBUG_PRINT("Using hash %s for file %s with model %s\n", hash, rel_path, model);
            char* resolved = resolve_hash(hash);
            if (resolved) {
                DEBUG_PRINT("Successfully resolved hash %s to %s\n", hash, resolved);
                float* result = load_stored_embedding(resolved, dims);
                free(resolved);
                return result;
            } else {
                DEBUG_PRINT("Failed to resolve hash: %s\n", hash);
            }
        } else {
            DEBUG_PRINT("No hash found for file %s with model %s\n", rel_path, model ? model : "NULL");
            
            // Provide a more helpful error message
            if (model) {
                cli_error("No embedding found for '%s' with model '%s'", rel_path, model);
                cli_info("Try using 'eb store --model %s %s' first", model, rel_path);
            } else {
                cli_error("No embedding found for '%s'", rel_path);
                cli_info("Try using 'eb store' to create an embedding first");
            }
            return NULL;
        }
    } else {
        DEBUG_PRINT("Failed to get repo root\n");
    }
    
    // Provide more specific error messages based on context
    if (strlen(path_or_hash) >= 4 && strlen(path_or_hash) <= 64 && is_hex_string(path_or_hash)) {
        cli_error("Invalid hash: '%s'", path_or_hash);
    } else {
        cli_error("Unsupported file format or invalid hash: %s", path_or_hash);
        cli_info("Supported formats: .npy, .bin, or tracked files");
    }
    return NULL;
}

/* Check if a string contains only hexadecimal characters */
static bool is_hex_string(const char* str) {
    if (!str) return false;
    for (const char* p = str; *p; p++) {
        if (!isxdigit(*p)) return false;
    }
    return true;
}

/* Check if a file has embeddings from multiple models */
static bool has_multiple_models(const char* repo_root, const char* file_path) {
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s/.eb/log", repo_root);
    
    FILE* fp = fopen(log_path, "r");
    if (!fp) return false;
    
    char line[1024];
    int model_count = 0;
    char models[10][64] = {0}; // Store up to 10 different models
    
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Parse line to extract file path and model
        char* line_file_path = NULL;
        char* model = NULL;
        
        // Check for newer format (timestamp hash file model)
        char* token = strtok(line, " ");
        if (token && (isdigit(token[0]) || token[0] == '-')) {
            // Skip timestamp and hash
            token = strtok(NULL, " "); // hash
            if (!token) continue;
            
            token = strtok(NULL, " "); // file path
            if (!token) continue;
            line_file_path = token;
            
            token = strtok(NULL, " "); // model
            if (!token) continue;
            model = token;
        } else {
            // Older format (file hash timestamp model)
            line_file_path = token;
            
            // Skip hash and timestamp
            token = strtok(NULL, " "); // hash
            if (!token) continue;
            
            token = strtok(NULL, " "); // timestamp
            if (!token) continue;
            
            token = strtok(NULL, " "); // model
            if (!token) continue;
            model = token;
        }
        
        // Check if this is for our file
        if (line_file_path && strcmp(line_file_path, file_path) == 0) {
            // Check if we've seen this model before
            bool found = false;
            for (int i = 0; i < model_count; i++) {
                if (strcmp(models[i], model) == 0) {
                    found = true;
                    break;
                }
            }
            
            // If not, add it to our list
            if (!found && model_count < 10) {
                strncpy(models[model_count], model, 63);
                models[model_count][63] = '\0';
                model_count++;
            }
        }
    }
    
    fclose(fp);
    return model_count > 1;
}

/* Get a comma-separated list of available models for a file */
static char* get_available_models(const char* repo_root, const char* file_path) {
    static char model_list[512] = {0};
    model_list[0] = '\0';
    
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s/.eb/log", repo_root);
    
    FILE* fp = fopen(log_path, "r");
    if (!fp) return model_list;
    
    char line[1024];
    int model_count = 0;
    char models[10][64] = {0}; // Store up to 10 different models
    
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Parse line to extract file path and model
        char* line_file_path = NULL;
        char* model = NULL;
        
        // Check for newer format (timestamp hash file model)
        char* token = strtok(line, " ");
        if (token && (isdigit(token[0]) || token[0] == '-')) {
            // Skip timestamp and hash
            token = strtok(NULL, " "); // hash
            if (!token) continue;
            
            token = strtok(NULL, " "); // file path
            if (!token) continue;
            line_file_path = token;
            
            token = strtok(NULL, " "); // model
            if (!token) continue;
            model = token;
        } else {
            // Older format (file hash timestamp model)
            line_file_path = token;
            
            // Skip hash and timestamp
            token = strtok(NULL, " "); // hash
            if (!token) continue;
            
            token = strtok(NULL, " "); // timestamp
            if (!token) continue;
            
            token = strtok(NULL, " "); // model
            if (!token) continue;
            model = token;
        }
        
        // Check if this is for our file
        if (line_file_path && strcmp(line_file_path, file_path) == 0) {
            // Check if we've seen this model before
            bool found = false;
            for (int i = 0; i < model_count; i++) {
                if (strcmp(models[i], model) == 0) {
                    found = true;
                    break;
                }
            }
            
            // If not, add it to our list
            if (!found && model_count < 10) {
                strncpy(models[model_count], model, 63);
                models[model_count][63] = '\0';
                model_count++;
            }
        }
    }
    
    fclose(fp);
    
    // Build comma-separated list
    for (int i = 0; i < model_count; i++) {
        if (i > 0) strcat(model_list, ", ");
        strcat(model_list, models[i]);
    }
    
    return model_list;
}

/* Get the default model for a file (the only model, or NULL if multiple) */
static const char* get_default_model_for_file(const char* repo_root, const char* file_path) {
    static char default_model[64] = {0};
    default_model[0] = '\0';
    
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s/.eb/log", repo_root);
    
    FILE* fp = fopen(log_path, "r");
    if (!fp) return NULL;
    
    char line[1024];
    int model_count = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Parse line to extract file path and model
        char* line_file_path = NULL;
        char* model = NULL;
        
        // Check for newer format (timestamp hash file model)
        char* token = strtok(line, " ");
        if (token && (isdigit(token[0]) || token[0] == '-')) {
            // Skip timestamp and hash
            token = strtok(NULL, " "); // hash
            if (!token) continue;
            
            token = strtok(NULL, " "); // file path
            if (!token) continue;
            line_file_path = token;
            
            token = strtok(NULL, " "); // model
            if (!token) continue;
            model = token;
        } else {
            // Older format (file hash timestamp model)
            line_file_path = token;
            
            // Skip hash and timestamp
            token = strtok(NULL, " "); // hash
            if (!token) continue;
            
            token = strtok(NULL, " "); // timestamp
            if (!token) continue;
            
            token = strtok(NULL, " "); // model
            if (!token) continue;
            model = token;
        }
        
        // Check if this is for our file
        if (line_file_path && strcmp(line_file_path, file_path) == 0) {
            // If we already found a different model, return NULL (multiple models)
            if (model_count > 0 && strcmp(default_model, model) != 0) {
                fclose(fp);
                return NULL;
            }
            
            // Store this model
            strncpy(default_model, model, 63);
            default_model[63] = '\0';
            model_count++;
        }
    }
    
    fclose(fp);
    
    // Return the model if we found exactly one
    return model_count == 1 ? default_model : NULL;
}

/* Get the default model from config */
static char* get_default_model() {
    static char default_model[64] = {0};
    default_model[0] = '\0';
    
    // Try to find repo root
    char* repo_root = find_repo_root(".");
    if (!repo_root) {
        return NULL;
    }
    
    // Try to read config file
    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/.eb/config", repo_root);
    
    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        free(repo_root);
        return NULL;
    }
    
    // Read file line by line looking for model.default
    char line[1024];
    bool in_model_section = false;
    
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        
        // Skip empty lines and comments
        if (len == 0 || line[0] == '#')
            continue;
        
        // Check for section headers
        if (line[0] == '[') {
            in_model_section = (strncmp(line, "[model]", 7) == 0);
            continue;
        }
        
        // If we're in the model section, look for default setting
        if (in_model_section) {
            char* eq = strchr(line, '=');
            if (eq) {
                // Extract key (trim whitespace)
                char key[64] = {0};
                char* k = line;
                char* k_end = eq - 1;
                
                // Trim leading whitespace from key
                while (k <= k_end && isspace(*k)) k++;
                
                // Trim trailing whitespace from key
                while (k_end >= k && isspace(*k_end)) k_end--;
                
                // Copy key
                if (k_end >= k) {
                    size_t key_len = k_end - k + 1;
                    if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
                    strncpy(key, k, key_len);
                    key[key_len] = '\0';
                }
                
                // If this is the default key, extract value
                if (strcmp(key, "default") == 0) {
                    // Extract value (trim whitespace)
                    char* v = eq + 1;
                    char* v_end = line + len - 1;
                    
                    // Trim leading whitespace from value
                    while (v <= v_end && isspace(*v)) v++;
                    
                    // Trim trailing whitespace from value
                    while (v_end >= v && isspace(*v_end)) v_end--;
                    
                    // Copy value
                    if (v_end >= v) {
                        size_t val_len = v_end - v + 1;
                        if (val_len >= sizeof(default_model)) val_len = sizeof(default_model) - 1;
                        strncpy(default_model, v, val_len);
                        default_model[val_len] = '\0';
                        break;
                    }
                }
            }
        }
    }
    
    fclose(fp);
    free(repo_root);
    
    return *default_model ? default_model : NULL;
}

int cmd_diff(int argc, char** argv) {
    const char *hash1, *hash2;
    float *emb1 = NULL, *emb2 = NULL;
    size_t dims1 = 0, dims2 = 0;
    float cos_similarity, euc_distance, euc_similarity;
    int ret = 1;
    bool is_test = getenv("EB_TEST_MODE") != NULL;
    eb_cli_options_t opts = {0};
    
    DEBUG_PRINT("Starting diff command with %d arguments", argc);
    
    // Check for help option
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", DIFF_USAGE);
        return (argc < 2) ? 1 : 0;
    }
    
    // Parse --models flag
    const char* models_str = get_option_value(argc, argv, NULL, "--models");
    // Parse --model flag (shorthand for single model use)
    const char* model_str = get_option_value(argc, argv, NULL, "--model");
    
    char* models_copy = NULL;
    char* model1 = NULL;
    char* model2 = NULL;
    
    // If both options are provided, prioritize --models and warn the user
    if (models_str && model_str) {
        cli_warning("Both --models and --model specified. Using --models.");
        model_str = NULL;
    }
    
    if (models_str) {
        DEBUG_PRINT("Models specified: %s", models_str);
        
        // Parse comma-separated models
        models_copy = strdup(models_str);
        if (!models_copy) {
            cli_error("Memory allocation failed");
            return 1;
        }
        
        model1 = strtok(models_copy, ",");
        model2 = strtok(NULL, ",");
        
        // If only one model specified, use it for both inputs
        if (!model2) {
            model2 = model1;
        }
        
        DEBUG_PRINT("Using models: %s and %s", model1, model2 ? model2 : model1);
        
        // Store models in options
        opts.model = model1;
        opts.models = models_str; // Keep the original string
        
        // Store the second model separately for later use
        char* second_model = model2 ? strdup(model2) : NULL;
        if (model2 && !second_model) {
            cli_error("Memory allocation failed");
            free(models_copy);
            return 1;
        }
        opts.second_model = second_model;
    } else if (model_str) {
        // Single model specified with --model
        DEBUG_PRINT("Single model specified: %s", model_str);
        
        // Use the same model for both inputs
        model1 = model2 = (char*)model_str;
        
        DEBUG_PRINT("Using model %s for both inputs", model_str);
        
        // Store models in options
        opts.model = model_str;
        opts.models = NULL;
        opts.second_model = NULL;
    } else {
        // Try to get default model from config
        char* default_model = get_default_model();
        if (default_model) {
            model1 = model2 = default_model;
            DEBUG_PRINT("Using default model from config: %s", default_model);
        } else {
            DEBUG_PRINT("No model specified and no default model found");
        }
    }
    
    // Check if we have enough arguments
    if (argc < 2) {
        printf("%s", DIFF_USAGE);
        free(models_copy);
        return 1;
    }
    
    // Determine if we're comparing two files or one file against history
    if (argc >= 3 && argv[argc-2][0] != '-' && argv[argc-1][0] != '-') {
        DEBUG_PRINT("Comparing inputs: %s and %s", argv[argc-2], argv[argc-1]);
        hash1 = argv[argc-2];
        hash2 = argv[argc-1];
    } else if (argc >= 2 && argv[argc-1][0] != '-') {
        DEBUG_PRINT("Attempting to load: %s", argv[argc-1]);
        hash1 = argv[argc-1];
        hash2 = NULL;  // Historical comparison
    } else {
        cli_error("No valid input files specified");
        free(models_copy);
        return 1;
    }
    
    // Check if inputs are hashes or files
    bool hash1_is_hash = is_valid_hash(hash1);
    bool hash2_is_hash = hash2 ? is_valid_hash(hash2) : false;
    
    DEBUG_PRINT("Input %s looks like a hash: %s", hash1, hash1_is_hash ? "yes" : "no");
    if (hash2) {
        DEBUG_PRINT("Input %s looks like a hash: %s", hash2, hash2_is_hash ? "yes" : "no");
    }
    
    /* Load embeddings */
    emb1 = load_embedding_with_model(hash1, model1, &dims1);
    if (!emb1) {
        cli_error("Failed to load embedding for %s", hash1);
        free(models_copy);
        if (opts.second_model) {
            free((void*)opts.second_model);
        }
        return 1;
    }
    
    if (hash2) {
        emb2 = load_embedding_with_model(hash2, model2, &dims2);
    } else {
        // TODO: Implement historical comparison with model
        cli_error("Historical comparison not yet implemented");
        free(emb1);
        free(models_copy);
        if (opts.second_model) {
            free((void*)opts.second_model);
        }
        return 1;
    }
    
    if (!emb2) {
        cli_error("Failed to load embedding for %s", hash2);
        free(emb1);
        free(models_copy);
        if (opts.second_model) {
            free((void*)opts.second_model);
        }
        return 1;
    }
    
    // Check dimensions match
    if (dims1 != dims2) {
        cli_error("Embedding dimensions do not match: %zu != %zu", dims1, dims2);
        cli_info("This can happen when comparing embeddings from different models");
        if (model1 && model2 && strcmp(model1, model2) != 0) {
            cli_info("You're comparing %s (%zu dims) with %s (%zu dims)", 
                    model1, dims1, model2, dims2);
        }
        free(emb1);
        free(emb2);
        free(models_copy);
        if (opts.second_model) {
            free((void*)opts.second_model);
        }
        return 1;
    }
    
    // Check for invalid values
    if (check_invalid_values(emb1, dims1) || check_invalid_values(emb2, dims2)) {
        cli_error("Invalid embedding values detected");
        free(emb1);
        free(emb2);
        free(models_copy);
        if (opts.second_model) {
            free((void*)opts.second_model);
        }
        return 1;
    }
    
    // Calculate similarity metrics
    cos_similarity = cosine_similarity(emb1, emb2, dims1);
    euc_distance = euclidean_distance(emb1, emb2, dims1);
    euc_similarity = 1.0f / (1.0f + euc_distance);  // Convert to similarity
    
    // Print results
    if (is_test) {
        // Machine-readable output for tests
        printf("%.6f,%.6f,%.6f\n", cos_similarity, euc_distance, euc_similarity);
    } else {
        // Human-readable output
        printf("Cosine similarity: %.4f\n", cos_similarity);
        printf("Euclidean distance: %.4f\n", euc_distance);
        printf("Euclidean similarity: %.4f\n", euc_similarity);
        
        // Interpretation
        printf("\nInterpretation: ");
        if (cos_similarity > 0.95f) {
            printf("Embeddings are %svery similar%s (>95%%)\n", 
                   COLOR_GREEN, COLOR_RESET);
        } else if (cos_similarity > 0.85f) {
            printf("Embeddings are %ssimilar%s (85-95%%)\n", 
                   COLOR_GREEN, COLOR_RESET);
        } else if (cos_similarity > 0.70f) {
            printf("Embeddings are %smoderately similar%s (70-85%%)\n", 
                   COLOR_YELLOW, COLOR_RESET);
        } else if (cos_similarity > 0.50f) {
            printf("Embeddings have %ssome similarity%s (50-70%%)\n", 
                   COLOR_YELLOW, COLOR_RESET);
        } else {
            printf("Embeddings are %ssignificantly different%s (<50%%)\n", 
                   COLOR_RED, COLOR_RESET);
        }
    }
    
    ret = 0;  // Success
    
    // Cleanup
    free(emb1);
    free(emb2);
    free(models_copy);
    if (opts.second_model) {
        free((void*)opts.second_model);
    }
    
    return ret;
} 