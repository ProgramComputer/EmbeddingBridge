/*
 * EmbeddingBridge - Dataset Operations Tests
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
#include "debug.h"

#define TEST_DATASET_NAME "test-dataset"
#define TEST_REMOTE_NAME "test-remote"
#define TEST_DATASET_PATH "/datasets/test"

/* Test dataset registry functionality */
static void test_dataset_registry(void) {
    printf("Testing dataset registry functions...\n");
    
    eb_status_t status;
    
    /* Initialize remote subsystem */
    status = eb_remote_init();
    assert(status == EB_SUCCESS);
    
    /* Add a test remote to use with datasets */
    status = eb_remote_add(TEST_REMOTE_NAME, "http://localhost:8080",
                          "test-token", 6, true, "json");
    assert(status == EB_SUCCESS);
    
    /* Add a dataset */
    status = eb_dataset_add(TEST_DATASET_NAME, TEST_REMOTE_NAME, TEST_DATASET_PATH);
    assert(status == EB_SUCCESS);
    
    /* Try to add the same dataset again (should fail) */
    status = eb_dataset_add(TEST_DATASET_NAME, TEST_REMOTE_NAME, TEST_DATASET_PATH);
    assert(status == EB_ERROR_ALREADY_EXISTS);
    
    /* Get dataset info */
    char remote_name[64];
    char path[256];
    bool has_documents;
    bool has_queries;
    
    status = eb_dataset_info(TEST_DATASET_NAME, remote_name, sizeof(remote_name),
                           path, sizeof(path), &has_documents, &has_queries);
    assert(status == EB_SUCCESS);
    
    /* Verify dataset info */
    assert(strcmp(remote_name, TEST_REMOTE_NAME) == 0);
    assert(strcmp(path, TEST_DATASET_PATH) == 0);
    assert(has_documents == false);  /* Default is false */
    assert(has_queries == false);    /* Default is false */
    
    /* Update dataset metadata */
    status = eb_dataset_update_metadata(TEST_DATASET_NAME, 
                                       "2024-03-17T12:00:00Z",
                                       "synthetic", 
                                       "similarity-search",
                                       "bert-base-uncased", 
                                       "bm25");
    assert(status == EB_SUCCESS);
    
    /* Set availability flags */
    status = eb_dataset_set_availability(TEST_DATASET_NAME, true, true);
    assert(status == EB_SUCCESS);
    
    /* Get dataset info again to verify updates */
    status = eb_dataset_info(TEST_DATASET_NAME, remote_name, sizeof(remote_name),
                           path, sizeof(path), &has_documents, &has_queries);
    assert(status == EB_SUCCESS);
    
    /* Verify updated availability flags */
    assert(has_documents == true);
    assert(has_queries == true);
    
    /* List all datasets */
    char **datasets;
    int count;
    
    status = eb_dataset_list(&datasets, &count);
    assert(status == EB_SUCCESS);
    assert(count >= 1);  /* Should have at least our test dataset */
    
    /* Print and free dataset names */
    printf("Found %d datasets:\n", count);
    for (int i = 0; i < count; i++) {
        printf("  %s\n", datasets[i]);
        free(datasets[i]);
    }
    free(datasets);
    
    /* Remove the dataset */
    status = eb_dataset_remove(TEST_DATASET_NAME);
    assert(status == EB_SUCCESS);
    
    /* Try to get info for the removed dataset (should fail) */
    status = eb_dataset_info(TEST_DATASET_NAME, remote_name, sizeof(remote_name),
                           path, sizeof(path), &has_documents, &has_queries);
    assert(status == EB_ERROR_NOT_FOUND);
    
    /* Clean up */
    status = eb_remote_remove(TEST_REMOTE_NAME);
    assert(status == EB_SUCCESS);
    
    eb_remote_shutdown();
    
    printf("Dataset registry tests passed!\n");
}

int main(void) {
    printf("=== Dataset Operations Tests ===\n");
    
    /* Enable debug output */
    eb_debug_set_level(EB_DEBUG_INFO);
    
    /* Run tests */
    test_dataset_registry();
    
    printf("All dataset operations tests passed!\n");
    return 0;
} 