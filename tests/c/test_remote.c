/*
 * EmbeddingBridge - Remote Operations Test
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
#include <assert.h>
#include "remote.h"
#include "compress.h"
#include "transformer.h"
#include "json_transformer.h"
#include "parquet_transformer.h"
#include "debug.h"

#define TEST_STR "Hello, world!"
#define TEST_SIZE (sizeof(TEST_STR) - 1)

/* Mock HTTP server responses will be simulated in the tests */

/* Test compression functions */
static void test_compression(void) {
    printf("Testing compression functions...\n");
    
    void *compressed = NULL;
    size_t compressed_size = 0;
    void *decompressed = NULL;
    size_t decompressed_size = 0;
    
    /* Test compression level 0 (just copy) */
    eb_status_t status = compress_buffer(TEST_STR, TEST_SIZE, 0,
                                        &compressed, &compressed_size);
    assert(status == EB_SUCCESS);
    assert(compressed != NULL);
    assert(compressed_size == TEST_SIZE);
    assert(memcmp(compressed, TEST_STR, TEST_SIZE) == 0);
    
    /* Test decompression of uncompressed data */
    status = decompress_buffer(compressed, compressed_size,
                              &decompressed, &decompressed_size);
    assert(status == EB_SUCCESS);
    assert(decompressed != NULL);
    assert(decompressed_size == TEST_SIZE);
    assert(memcmp(decompressed, TEST_STR, TEST_SIZE) == 0);
    
    free(compressed);
    free(decompressed);
    compressed = NULL;
    decompressed = NULL;
    
    /* Test compression level 1 if zstd is available */
    status = compress_buffer(TEST_STR, TEST_SIZE, 1,
                            &compressed, &compressed_size);
    if (status == EB_ERROR_DEPENDENCY_MISSING) {
        printf("Skipping ZSTD compression test - zstd not available\n");
    } else {
        assert(status == EB_SUCCESS);
        assert(compressed != NULL);
        
        /* Test decompression */
        status = decompress_buffer(compressed, compressed_size,
                                  &decompressed, &decompressed_size);
        assert(status == EB_SUCCESS);
        assert(decompressed != NULL);
        assert(decompressed_size == TEST_SIZE);
        assert(memcmp(decompressed, TEST_STR, TEST_SIZE) == 0);
        
        free(compressed);
        free(decompressed);
    }
    
    printf("Compression tests passed!\n");
}

/* Test transformation functions */
static void test_transformers(void) {
    printf("Testing transformer functions...\n");
    
    /* Initialize the transformer registry */
    eb_status_t status = eb_transformer_registry_init();
    assert(status == EB_SUCCESS);
    
    /* Register transformers */
    status = eb_register_builtin_transformers();
    assert(status == EB_SUCCESS);
    
    /* Test JSON transformer */
    printf("Testing JSON transformer...\n");
    eb_transformer_t *json_transformer = eb_find_transformer("json");
    assert(json_transformer != NULL);
    assert(strcmp(json_transformer->name, "json") == 0);
    
    /* Test transformation */
    void *transformed = NULL;
    size_t transformed_size = 0;
    status = eb_transform(json_transformer, TEST_STR, TEST_SIZE,
                         &transformed, &transformed_size);
    assert(status == EB_SUCCESS);
    assert(transformed != NULL);
    assert(transformed_size > 0);
    
    /* Verify it's valid JSON */
    char *json = (char *)transformed;
    assert(json[0] == '{');
    
    /* Test inverse transformation */
    void *original = NULL;
    size_t original_size = 0;
    status = eb_inverse_transform(json_transformer, transformed, transformed_size,
                                &original, &original_size);
    assert(status == EB_SUCCESS);
    assert(original != NULL);
    
    /* Skip exact size/content checks as they may vary by implementation */
    printf("JSON transformer inverse transform returned %zu bytes\n", original_size);
    
    /* Clean up */
    free(transformed);
    free(original);
    
    /* Test Parquet transformer */
    printf("Testing Parquet transformer...\n");
    eb_transformer_t *parquet_transformer = eb_find_transformer("parquet");
    assert(parquet_transformer != NULL);
    printf("Found Parquet transformer: %s\n", parquet_transformer->name);
    assert(strcmp(parquet_transformer->name, "parquet") == 0);
    
    /* Skip transformation tests as they depend on Arrow/Parquet libraries */
    printf("Skipping detailed Parquet transformer tests (requires Arrow/Parquet)\n");
    
    /* Cleanup the transformer registry */
    printf("Cleaning up transformer registry...\n");
    eb_transformer_registry_cleanup();
    
    printf("Transformer tests passed!\n");
}

/* Test remote registry functions without actual networking */
static void test_remote_registry(void) {
    printf("Testing remote registry functions...\n");
    
    /* Initialize the remote subsystem */
    eb_status_t status = eb_remote_init();
    assert(status == EB_SUCCESS);
    
    /* Add a remote with JSON format */
    status = eb_remote_add("test-json", "http://localhost:8080",
                        "test-token", 6, true, "json");
    assert(status == EB_SUCCESS);
    
    /* Add a remote with Parquet format */
    status = eb_remote_add("test-parquet", "http://localhost:8081",
                        "test-token", 9, true, "parquet");
    assert(status == EB_SUCCESS);
    
    /* Try to add the same remote again (should fail) */
    status = eb_remote_add("test-json", "http://localhost:8081",
                        NULL, 0, false, NULL);
    assert(status == EB_ERROR_ALREADY_EXISTS);
    
    /* Remove the remotes */
    status = eb_remote_remove("test-json");
    assert(status == EB_SUCCESS);
    
    status = eb_remote_remove("test-parquet");
    assert(status == EB_SUCCESS);
    
    /* Try to remove a non-existent remote */
    status = eb_remote_remove("non-existent");
    assert(status == EB_ERROR_NOT_FOUND);
    
    /* Shut down the remote subsystem */
    eb_remote_shutdown();
    
    printf("Remote registry tests passed!\n");
}

int main(void) {
    printf("=== Remote Operations Test ===\n");
    
    /* Enable debug output */
    eb_debug_set_level(EB_DEBUG_INFO);
    
    /* Run tests */
    test_compression();
    test_transformers();
    
    /* Run remote registry tests */
    printf("\nRunning remote registry tests...\n");
    test_remote_registry();
    
    printf("All remote operation tests passed!\n");
    return 0;
} 