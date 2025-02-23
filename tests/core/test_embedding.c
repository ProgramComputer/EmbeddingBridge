#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../src/core/types.h"

// Test fixtures - matches Python OpenAI embedding format
static const char* TEST_FIXTURES = "tests/fixtures";
static const char* OPENAI_SMALL_FIXTURE = "tests/fixtures/openai-3-small.bin";
static const char* OPENAI_LARGE_FIXTURE = "tests/fixtures/openai-3-large.bin";

// Test the embedding loading functionality
static void test_embedding_load(void) {
    printf("Testing embedding loading...\n");
    
    eb_embedding_t* embedding = NULL;
    eb_status_t status;
    
    // Test loading non-existent file
    status = eb_create_embedding_from_file("nonexistent.txt", "openai-3", &embedding);
    assert(status == EB_ERROR_FILE_NOT_FOUND);
    assert(embedding == NULL);
    
    // Test loading Python-generated OpenAI embedding
    status = eb_create_embedding_from_file(OPENAI_SMALL_FIXTURE, "openai-3", &embedding);
    assert(status == EB_SUCCESS);
    assert(embedding != NULL);
    assert(embedding->dimensions == 1536);  // OpenAI small model dimensions
    eb_destroy_embedding(embedding);
    
    printf("✓ Handles Python-generated embeddings correctly\n");
}

// Test embedding comparison across models
static void test_cross_model_comparison(void) {
    printf("Testing cross-model comparison...\n");
    
    eb_embedding_t *small = NULL, *large = NULL;
    eb_status_t status;
    
    // Load both model embeddings
    status = eb_create_embedding_from_file(OPENAI_SMALL_FIXTURE, "openai-3", &small);
    assert(status == EB_SUCCESS);
    
    status = eb_create_embedding_from_file(OPENAI_LARGE_FIXTURE, "openai-3", &large);
    assert(status == EB_SUCCESS);
    
    // Compare embeddings
    eb_comparison_result_t* result = NULL;
    status = eb_compare_embeddings(small, large, 5, &result);
    assert(status == EB_SUCCESS);
    assert(result != NULL);
    
    // Verify semantic preservation (should be high for same text)
    assert(result->cosine_similarity >= 0.7f);
    
    // Cleanup
    eb_destroy_embedding(small);
    eb_destroy_embedding(large);
    eb_destroy_comparison_result(result);
    
    printf("✓ Cross-model comparison works correctly\n");
}

// Test memory management
static void test_memory_management(void) {
    printf("Testing memory management...\n");
    
    // Test destroy with NULL
    eb_destroy_embedding(NULL);
    eb_destroy_comparison_result(NULL);
    
    printf("✓ Handles NULL destruction safely\n");
    
    // Test destroy with valid objects
    eb_embedding_t* embedding = calloc(1, sizeof(eb_embedding_t));
    embedding->data = malloc(100);  // Some dummy data
    eb_destroy_embedding(embedding);
    
    printf("✓ Cleans up valid objects correctly\n");
}

int main(void) {
    printf("Running embedding core tests...\n\n");
    
    // Run tests
    test_embedding_load();
    test_cross_model_comparison();
    test_memory_management();
    
    printf("\nAll embedding core tests passed!\n");
    return 0;
} 