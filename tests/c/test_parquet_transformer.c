/**
 * EmbeddingBridge - Test for Parquet Transformer
 * Copyright (C) 2024
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>

#include "transformer.h"
#include "status.h"

/* Forward declarations */
extern eb_transformer_t* eb_parquet_transformer_create(int compression_level);
extern eb_status_t eb_register_parquet_transformer(void);
extern eb_status_t eb_transform(
    eb_transformer_t *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);
extern eb_status_t eb_inverse_transform(
    eb_transformer_t *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);
extern void eb_transformer_free(eb_transformer_t *transformer);

#define TEST_DATA_SIZE 100
#define NUMPY_HEADER_SIZE 128

/**
 * Create a test NumPy file in memory
 */
void* create_numpy_file(size_t* size_out) {
    // Create data array
    float* data = (float*)malloc(TEST_DATA_SIZE * sizeof(float));
    for (int i = 0; i < TEST_DATA_SIZE; i++) {
        data[i] = (float)i;
    }
    
    // Create header (simplified)
    const char magic[] = "\x93NUMPY\x01\x00";
    uint16_t header_len = NUMPY_HEADER_SIZE;
    
    // Calculate total size
    *size_out = 10 + header_len + (TEST_DATA_SIZE * sizeof(float));
    
    // Allocate memory for the .npy file
    void* npy_data = malloc(*size_out);
    if (!npy_data) {
        free(data);
        return NULL;
    }
    
    // Copy magic string and version
    memcpy(npy_data, magic, 8);
    
    // Write header length
    uint16_t* header_len_ptr = (uint16_t*)((uint8_t*)npy_data + 8);
    *header_len_ptr = header_len;
    
    // Create and write header content
    char header[NUMPY_HEADER_SIZE];
    snprintf(header, sizeof(header),
            "{'descr': '<f4', 'fortran_order': False, 'shape': (%d,), }", 
            TEST_DATA_SIZE);
    
    // Pad header with spaces
    size_t header_text_len = strlen(header);
    for (size_t i = header_text_len; i < header_len - 1; i++) {
        header[i] = ' ';
    }
    header[header_len - 1] = '\n';
    
    // Copy header to file
    memcpy((uint8_t*)npy_data + 10, header, header_len);
    
    // Copy data to file
    memcpy((uint8_t*)npy_data + 10 + header_len, data, TEST_DATA_SIZE * sizeof(float));
    
    // Free temporary data
    free(data);
    
    return npy_data;
}

int main(void) {
    printf("Testing Parquet transformer...\n");
    
    // Test 1: Binary data roundtrip
    printf("\n=== Test 1: Binary data roundtrip ===\n");
    
    // Create test data
    float test_data[TEST_DATA_SIZE];
    for (int i = 0; i < TEST_DATA_SIZE; i++) {
        test_data[i] = (float)i;
    }
    
    // Create transformer
    eb_transformer_t* transformer = eb_parquet_transformer_create(9);
    if (!transformer) {
        printf("ERROR: Failed to create Parquet transformer\n");
        return 1;
    }
    
    // Transform the data (binary mode)
    void* compressed = NULL;
    size_t compressed_size = 0;
    eb_status_t status = eb_transform(
        transformer, 
        test_data, 
        TEST_DATA_SIZE * sizeof(float),
        &compressed, 
        &compressed_size);
        
    if (status != EB_SUCCESS) {
        printf("ERROR: Parquet transform failed: %d\n", status);
        eb_transformer_free(transformer);
        return 1;
    }
    
    printf("Compressed %zu bytes to %zu bytes\n", 
           TEST_DATA_SIZE * sizeof(float), compressed_size);
    
    // Inverse transform the data
    void* decompressed = NULL;
    size_t decompressed_size = 0;
    status = eb_inverse_transform(
        transformer,
        compressed,
        compressed_size,
        &decompressed,
        &decompressed_size);
        
    if (status != EB_SUCCESS) {
        printf("ERROR: Parquet inverse transform failed: %d\n", status);
        free(compressed);
        eb_transformer_free(transformer);
        return 1;
    }
    
    printf("Decompressed %zu bytes back to %zu bytes\n", 
           compressed_size, decompressed_size);
    
    // Verify data integrity
    if (decompressed_size != TEST_DATA_SIZE * sizeof(float)) {
        printf("ERROR: Decompressed size (%zu) doesn't match original size (%zu)\n",
               decompressed_size, TEST_DATA_SIZE * sizeof(float));
        free(compressed);
        free(decompressed);
        eb_transformer_free(transformer);
        return 1;
    }
    
    float* decompressed_data = (float*)decompressed;
    bool data_matches = true;
    for (int i = 0; i < TEST_DATA_SIZE; i++) {
        if (fabsf(decompressed_data[i] - test_data[i]) > 0.0001f) {
            printf("ERROR: Data mismatch at index %d: %f != %f\n", 
                   i, decompressed_data[i], test_data[i]);
            data_matches = false;
            break;
        }
    }
    
    free(compressed);
    free(decompressed);
    
    if (!data_matches) {
        eb_transformer_free(transformer);
        return 1;
    }
    
    // Test 2: NumPy file roundtrip
    printf("\n=== Test 2: NumPy file roundtrip ===\n");
    
    // Create a NumPy file in memory
    size_t npy_size = 0;
    void* npy_data = create_numpy_file(&npy_size);
    if (!npy_data) {
        printf("ERROR: Failed to create NumPy file in memory\n");
        eb_transformer_free(transformer);
        return 1;
    }
    
    printf("Created NumPy file of size %zu bytes\n", npy_size);
    
    // Transform the NumPy file
    void* compressed_npy = NULL;
    size_t compressed_npy_size = 0;
    status = eb_transform(
        transformer, 
        npy_data, 
        npy_size,
        &compressed_npy, 
        &compressed_npy_size);
        
    if (status != EB_SUCCESS) {
        printf("ERROR: NumPy Parquet transform failed: %d\n", status);
        free(npy_data);
        eb_transformer_free(transformer);
        return 1;
    }
    
    printf("Compressed NumPy %zu bytes to %zu bytes\n", 
           npy_size, compressed_npy_size);
    
    // Inverse transform the NumPy file
    void* decompressed_npy = NULL;
    size_t decompressed_npy_size = 0;
    status = eb_inverse_transform(
        transformer,
        compressed_npy,
        compressed_npy_size,
        &decompressed_npy,
        &decompressed_npy_size);
        
    if (status != EB_SUCCESS) {
        printf("ERROR: NumPy Parquet inverse transform failed: %d\n", status);
        free(npy_data);
        free(compressed_npy);
        eb_transformer_free(transformer);
        return 1;
    }
    
    printf("Decompressed NumPy %zu bytes back to %zu bytes\n", 
           compressed_npy_size, decompressed_npy_size);
    
    // Verify NumPy header is present
    if (decompressed_npy_size < 10) {
        printf("ERROR: Decompressed NumPy file too small\n");
        free(npy_data);
        free(compressed_npy);
        free(decompressed_npy);
        eb_transformer_free(transformer);
        return 1;
    }
    
    if (memcmp(decompressed_npy, "\x93NUMPY", 6) != 0) {
        printf("ERROR: NumPy magic number not found in decompressed data\n");
        free(npy_data);
        free(compressed_npy);
        free(decompressed_npy);
        eb_transformer_free(transformer);
        return 1;
    }
    
    printf("NumPy magic number verified in decompressed data\n");
    
    // Check data integrity by extracting the array data
    uint16_t header_size = *((uint16_t*)((uint8_t*)decompressed_npy + 8));
    size_t data_offset = 10 + header_size;
    
    if (decompressed_npy_size < data_offset + (TEST_DATA_SIZE * sizeof(float))) {
        printf("ERROR: Decompressed NumPy file truncated\n");
        free(npy_data);
        free(compressed_npy);
        free(decompressed_npy);
        eb_transformer_free(transformer);
        return 1;
    }
    
    float* npy_float_data = (float*)((uint8_t*)decompressed_npy + data_offset);
    data_matches = true;
    for (int i = 0; i < TEST_DATA_SIZE; i++) {
        if (fabsf(npy_float_data[i] - (float)i) > 0.0001f) {
            printf("ERROR: NumPy data mismatch at index %d: %f != %f\n", 
                   i, npy_float_data[i], (float)i);
            data_matches = false;
            break;
        }
    }
    
    free(npy_data);
    free(compressed_npy);
    free(decompressed_npy);
    
    if (!data_matches) {
        eb_transformer_free(transformer);
        return 1;
    }
    
    // Clean up transformer
    eb_transformer_free(transformer);
    
    // Test 3: Registration
    printf("\n=== Test 3: Registration ===\n");
    status = eb_register_parquet_transformer();
    if (status != EB_SUCCESS) {
        printf("ERROR: Failed to register Parquet transformer: %d\n", status);
        return 1;
    }
    
    printf("Parquet transformer registered successfully\n");
    
    printf("\nAll tests passed.\n");
    return 0;
} 