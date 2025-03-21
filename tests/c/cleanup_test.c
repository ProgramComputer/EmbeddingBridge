/*
 * EmbeddingBridge - Transformer Registry Cleanup Test
 * Copyright (C) 2024 ProgramComputer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transformer.h"
#include "json_transformer.h"
#include "parquet_transformer.h"
#include "debug.h"

int main(void) {
    printf("=== Transformer Registry Cleanup Test ===\n");
    
    /* Enable debug output */
    eb_debug_set_level(EB_DEBUG_INFO);
    
    printf("Step 1: Initializing transformer registry...\n");
    eb_status_t status = eb_transformer_registry_init();
    if (status != EB_SUCCESS) {
        printf("Failed to initialize registry: %d\n", status);
        return 1;
    }
    
    printf("Step 2: Registering JSON transformer...\n");
    status = eb_register_json_transformer();
    if (status != EB_SUCCESS) {
        printf("Failed to register JSON transformer: %d\n", status);
        return 1;
    }
    
    printf("Step 3: Looking up JSON transformer...\n");
    eb_transformer_t *json_transformer = eb_find_transformer("json");
    if (json_transformer == NULL) {
        printf("Failed to find JSON transformer\n");
        return 1;
    }
    printf("JSON transformer found: %s\n", json_transformer->name);
    
    printf("Step 4: Registering Parquet transformer...\n");
    status = eb_register_parquet_transformer();
    if (status != EB_SUCCESS) {
        printf("Failed to register Parquet transformer: %d\n", status);
        return 1;
    }
    
    printf("Step 5: Looking up Parquet transformer...\n");
    eb_transformer_t *parquet_transformer = eb_find_transformer("parquet");
    if (parquet_transformer == NULL) {
        printf("Failed to find Parquet transformer\n");
        return 1;
    }
    printf("Parquet transformer found: %s\n", parquet_transformer->name);
    
    printf("Step 6: Cleaning up transformer registry...\n");
    fflush(stdout); /* Ensure output is visible before potential crash */
    eb_transformer_registry_cleanup();
    
    printf("Step 7: Cleanup completed successfully\n");
    
    return 0;
} 