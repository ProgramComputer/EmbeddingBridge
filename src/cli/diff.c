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
    "Usage: eb diff <input1> <input2>\n"
    "\n"
    "Compare two embeddings and show their similarity\n"
    "\n"
    "Arguments:\n"
    "  <input1>    First embedding (hash, file, or source file)\n"
    "  <input2>    Second embedding (hash, file, or source file)\n"
    "\n"
    "Examples:\n"
    "  eb diff abc123 def456              # Compare two stored embeddings\n"
    "  eb diff abc d34                    # Compare using short hashes\n"
    "  eb diff file1.npy file2.npy       # Compare two .npy files\n"
    "  eb diff file1.bin file2.bin       # Compare two binary files\n"
    "  eb diff doc1.txt doc2.txt         # Compare source files (looks for associated files)\n";

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

int cmd_diff(int argc, char *argv[])
{
    const char *hash1, *hash2;
    float *emb1 = NULL, *emb2 = NULL;
    size_t dims1, dims2;
    float cos_similarity, euc_distance, euc_similarity;
    int ret = 1;  // Initialize to error state
    bool is_test = getenv("EB_TEST_MODE") != NULL;
    
    DEBUG_PRINT("Starting diff command with %d arguments", argc);
    
    if (argc != 3) {
        fprintf(stderr, "%s", DIFF_USAGE);
        return 1;
    }
    
    hash1 = argv[1];
    hash2 = argv[2];
    DEBUG_PRINT("Comparing inputs: %s and %s", hash1, hash2);

    /* Quick check for identical inputs */
    if (strcmp(hash1, hash2) == 0) {
        if (is_test)
            printf("→ Cosine Similarity: 100%%\n→ Euclidean Distance: 0.00\n→ Euclidean Similarity: 100%%");
        else
            printf(COLOR_BOLD_GREEN "→ Cosine Similarity: 100%%" COLOR_RESET "\n"
                   COLOR_BOLD_GREEN "→ Euclidean Distance: 0.00" COLOR_RESET "\n"
                   COLOR_BOLD_GREEN "→ Euclidean Similarity: 100%%" COLOR_RESET "\n");
        return 0;
    }
    
    /* Load embeddings */
    emb1 = load_embedding(hash1, &dims1);
    if (!emb1)
        goto cleanup;
    
    emb2 = load_embedding(hash2, &dims2);
    if (!emb2)
        goto cleanup;
    
    /* Check dimensions match */
    if (dims1 != dims2) {
        cli_error("Embedding dimensions do not match: %zu != %zu", dims1, dims2);
        goto cleanup;
    }
    
    /* Check for invalid values */
    if (check_invalid_values(emb1, dims1) || check_invalid_values(emb2, dims2)) {
        cli_error("Invalid embedding values detected");
        goto cleanup;
    }

    /* Calculate similarities */
    cos_similarity = cosine_similarity(emb1, emb2, dims1);
    euc_distance = euclidean_distance(emb1, emb2, dims1);
    euc_similarity = euclidean_similarity(emb1, emb2, dims1);
    
    DEBUG_PRINT("Calculated raw cosine similarity: %f", cos_similarity);
    DEBUG_PRINT("Calculated raw Euclidean distance: %f", euc_distance);
    DEBUG_PRINT("Calculated raw Euclidean similarity: %f", euc_similarity);

    /* Print result */
    if (is_test) {
        printf("→ Cosine Similarity: %.0f%%\n→ Euclidean Distance: %.2f\n→ Euclidean Similarity: %.0f%%", 
               cos_similarity * 100, euc_distance, euc_similarity * 100);
    } else {
        printf(COLOR_BOLD_GREEN "→ Cosine Similarity: %.0f%%" COLOR_RESET "\n"
               COLOR_BOLD_GREEN "→ Euclidean Distance: %.2f" COLOR_RESET "\n"
               COLOR_BOLD_GREEN "→ Euclidean Similarity: %.0f%%" COLOR_RESET "\n", 
               cos_similarity * 100, euc_distance, euc_similarity * 100);
    }
    ret = 0;

cleanup:
    free(emb1);
    free(emb2);
    return ret;
} 