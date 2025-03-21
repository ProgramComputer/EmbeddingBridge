/*
 * EmbeddingBridge - S3 Remote Operations Test
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
#include "transport.h"
#include "debug.h"
#include "status.h"

#define TEST_REMOTE_NAME "test-s3"
#define TEST_S3_URL "s3://embeddingbridge-test/test-data"
#define TEST_REGION "us-west-2"
#define TEST_DATA "Hello, S3 remote storage!"
#define TEST_SIZE (sizeof(TEST_DATA) - 1)

/* Test S3 remote configuration */
static void test_s3_remote_config(void) {
    printf("Testing S3 remote configuration...\n");
    
    /* Initialize remote subsystem */
    eb_status_t status = eb_remote_init();
    assert(status == EB_SUCCESS);
    
    /* Add an S3 remote with region specified */
    char url_with_region[512];
    snprintf(url_with_region, sizeof(url_with_region), "%s?region=%s", TEST_S3_URL, TEST_REGION);
    status = eb_remote_add(TEST_REMOTE_NAME, url_with_region, NULL, 9, true, "json");
    assert(status == EB_SUCCESS);
    
    /* Verify remote info */
    char url[256];
    int timeout;
    bool verify_ssl;
    char transformer[32];
    
    status = eb_remote_info(TEST_REMOTE_NAME, url, sizeof(url), 
                           &timeout, &verify_ssl, transformer, sizeof(transformer));
    assert(status == EB_SUCCESS);
    assert(strstr(url, TEST_S3_URL) != NULL);
    assert(timeout == 9);
    assert(verify_ssl == true);
    assert(strcmp(transformer, "json") == 0);
    
    /* Clean up */
    status = eb_remote_remove(TEST_REMOTE_NAME);
    assert(status == EB_SUCCESS);
    
    eb_remote_shutdown();
    
    printf("S3 remote configuration tests passed!\n");
}

/* Test basic S3 transport */
static void test_s3_transport_basic(void) {
    printf("Testing basic S3 transport...\n");
    
    /* Try to open an S3 transport with region specified */
    char url_with_region[512];
    snprintf(url_with_region, sizeof(url_with_region), "%s?region=%s", TEST_S3_URL, TEST_REGION);
    eb_transport_t *transport = transport_open(url_with_region);
    
    if (transport) {
        printf("Successfully created S3 transport\n");
        assert(transport->type == TRANSPORT_S3);
        
        /* Try to connect to S3 */
        int status = transport_connect(transport);
        if (status == EB_SUCCESS) {
            printf("Successfully connected to S3\n");
            
            /* Simple data send/receive test */
            const char *test_message = "S3 transport test message";
            printf("Sending test data: %s\n", test_message);
            status = transport_send_data(transport, test_message, strlen(test_message));
            
            if (status == EB_SUCCESS) {
                printf("Data sent successfully\n");
                
                /* Try to receive data */
                char buffer[256];
                size_t received = 0;
                status = transport_receive_data(transport, buffer, sizeof(buffer), &received);
                
                if (status == EB_SUCCESS) {
                    buffer[received] = '\0';
                    printf("Received data: %s\n", buffer);
                } else {
                    printf("Failed to receive data: %s\n", transport_get_error(transport));
                }
            } else {
                printf("Failed to send data: %s\n", transport_get_error(transport));
            }
            
            transport_disconnect(transport);
            printf("Disconnected from S3\n");
        } else {
            printf("Could not connect to S3: %s\n", transport_get_error(transport));
        }
        
        transport_close(transport);
        printf("S3 transport closed\n");
    } else {
        printf("S3 transport creation failed\n");
    }
    
    printf("Basic S3 transport test completed\n");
}

/* Test dataset with S3 remote */
static void test_s3_dataset(void) {
    printf("Testing dataset with S3 remote...\n");
    
    /* Initialize remote subsystem */
    eb_status_t status = eb_remote_init();
    assert(status == EB_SUCCESS);
    
    /* Add S3 remote for dataset with region specified */
    char url_with_region[512];
    snprintf(url_with_region, sizeof(url_with_region), "%s?region=%s", TEST_S3_URL, TEST_REGION);
    status = eb_remote_add(TEST_REMOTE_NAME, url_with_region, NULL, 9, true, "json");
    assert(status == EB_SUCCESS);
    
    /* Add a dataset using the S3 remote */
    status = eb_dataset_add("s3-dataset", TEST_REMOTE_NAME, "/embeddings/test");
    assert(status == EB_SUCCESS);
    
    /* Verify dataset info */
    char remote_name[64];
    char path[256];
    bool has_documents;
    bool has_queries;
    
    status = eb_dataset_info("s3-dataset", remote_name, sizeof(remote_name),
                           path, sizeof(path), &has_documents, &has_queries);
    assert(status == EB_SUCCESS);
    assert(strcmp(remote_name, TEST_REMOTE_NAME) == 0);
    
    /* Clean up */
    status = eb_dataset_remove("s3-dataset");
    assert(status == EB_SUCCESS);
    
    status = eb_remote_remove(TEST_REMOTE_NAME);
    assert(status == EB_SUCCESS);
    
    eb_remote_shutdown();
    
    printf("S3 dataset tests passed!\n");
}

int main(void) {
    printf("=== S3 Remote Operations Test ===\n");
    
    /* Enable debug output */
    eb_debug_set_level(EB_DEBUG_TRACE);
    
    /* Set the AWS region environment variable */
    setenv("AWS_REGION", TEST_REGION, 1);
    printf("Set AWS_REGION to %s\n", TEST_REGION);
    
    /* Check for AWS credentials */
    char *aws_key = getenv("AWS_ACCESS_KEY_ID");
    char *aws_secret = getenv("AWS_SECRET_ACCESS_KEY");
    bool have_credentials = (aws_key && aws_secret);
    
    /* Try direct S3 transport open */
    printf("Testing direct S3 transport creation...\n");
    char url_with_region[512];
    snprintf(url_with_region, sizeof(url_with_region), "%s?region=%s", TEST_S3_URL, TEST_REGION);
    eb_transport_t *transport = transport_open(url_with_region);
    
    if (transport) {
        printf("Direct S3 transport creation succeeded\n");
        transport_close(transport);
    } else {
        printf("Direct S3 transport creation failed\n");
        if (!have_credentials) {
            printf("This may be due to missing AWS credentials\n");
        }
    }
    
    /* Run tests based on credential availability */
    if (!have_credentials) {
        printf("Warning: AWS credentials not found in environment variables.\n");
        printf("Set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environment variables.\n");
        printf("Will run configuration tests only.\n");
        
        /* Run only config tests that don't require actual S3 connection */
        test_s3_remote_config();
        test_s3_dataset();
    } else {
        printf("AWS credentials found in environment variables.\n");
        printf("Will run all S3 remote tests.\n");
        
        /* Run all tests */
        test_s3_remote_config();
        test_s3_transport_basic();
        test_s3_dataset();
    }
    
    printf("All S3 remote tests completed!\n");
    return 0;
} 