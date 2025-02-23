#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "test_common.h"

static void create_test_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f, "%s", content);
    fclose(f);
}

static void test_store_basic(void) {
    const char* test_content = "This is a test document.\n"
                              "It contains multiple lines\n"
                              "to test embedding generation.\n";
    
    create_test_file("test.txt", test_content);
    
    // Basic store command
    assert(system("../bin/eb store test.txt") == 0);
    assert(embedding_exists("test.txt"));
    
    // Store with model specification
    assert(system("../bin/eb store --model openai-3 test.txt") == 0);
    assert(embedding_exists("test.txt"));
    
    // Clean up
    remove("test.txt");
}

static void test_store_git_integration(void) {
    // Set up git repo
    assert(system("git init") == 0);
    
    const char* test_content = "Test content for git integration.\n";
    create_test_file("git_test.txt", test_content);
    
    // Store with git integration
    assert(system("../bin/eb store git_test.txt") == 0);
    assert(embedding_exists("git_test.txt"));
    
    // Verify git metadata
    eb_git_metadata_t meta;
    assert(eb_get_embedding_metadata("git_test.txt", &meta) == EB_OK);
    assert(meta.commit_id[0] != '\0');  // Should have commit ID
    
    // Store without git
    assert(system("../bin/eb store --no-git git_test.txt") == 0);
    
    // Clean up
    remove("git_test.txt");
    system("rm -rf .git");
}

static void test_store_errors(void) {
    // Non-existent file
    assert(system("../bin/eb store nonexistent.txt") != 0);
    
    // Invalid model
    create_test_file("error_test.txt", "test content\n");
    assert(system("../bin/eb store --model invalid-model error_test.txt") != 0);
    
    // Clean up
    remove("error_test.txt");
}

static void test_store_python_embedding(void) {
    printf("Testing store command with Python embeddings...\n");
    
    // Test storing Python-generated embedding
    assert(system("../bin/eb store --model openai-3 test.txt") == 0);
    assert(embedding_exists("test.txt"));
    
    // Test storing with different model
    assert(system("../bin/eb store --model openai-3-large test.txt") == 0);
    assert(embedding_exists("test.txt"));
    
    // Test version history
    assert(system("../bin/eb diff --model openai-3,openai-3-large test.txt") == 0);
    
    printf("✓ CLI handles Python embeddings correctly\n");
}

static void test_store_evolution(void) {
    printf("Testing embedding evolution tracking...\n");
    
    // Create test file with known content
    create_test_file("evolution.txt", "Test content for evolution tracking");
    
    // Store multiple versions
    assert(system("../bin/eb store --model openai-3 evolution.txt") == 0);
    assert(system("../bin/eb store --model openai-3-large evolution.txt") == 0);
    
    // Check evolution
    assert(system("../bin/eb log evolution.txt") == 0);
    
    // Clean up
    remove("evolution.txt");
    
    printf("✓ Evolution tracking works correctly\n");
}

static void test_store_metadata(void) {
    printf("Testing metadata storage...\n");
    
    // Store with metadata
    assert(system("../bin/eb store --model openai-3 --meta source=python test.txt") == 0);
    
    // Verify metadata
    assert(system("../bin/eb show test.txt") == 0);
    
    printf("✓ Metadata handling works correctly\n");
}

int main(void) {
    printf("Running store command tests...\n\n");
    
    setup_test_env();
    
    // Run tests
    test_store_basic();
    test_store_git_integration();
    test_store_errors();
    test_store_python_embedding();
    test_store_evolution();
    test_store_metadata();
    
    teardown_test_env();
    
    printf("\nAll store command tests passed!\n");
    return 0;
} 