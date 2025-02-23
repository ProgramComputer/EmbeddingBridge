#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../src/core/types.h"

// Test helpers
static void setup_git_env(void) {
    system("rm -rf test_repo");
    system("mkdir test_repo");
    system("cd test_repo && git init && git config user.email 'test@example.com' && git config user.name 'Test User'");
}

static void cleanup_git_env(void) {
    system("rm -rf test_repo");
}

// Test Git metadata retrieval
static void test_git_metadata(void) {
    printf("Testing Git metadata retrieval...\n");
    
    // Test in non-Git directory
    eb_git_metadata_t meta;
    eb_status_t status = eb_git_get_metadata(&meta);
    assert(status == EB_ERROR_GIT_OPERATION);
    
    // Change to test repo
    assert(chdir("test_repo") == 0);
    
    // Create a commit
    system("touch test.txt");
    system("git add test.txt");
    system("git commit -m 'test commit'");
    
    // Now test metadata retrieval
    status = eb_git_get_metadata(&meta);
    assert(status == EB_SUCCESS);
    assert(strlen(meta.commit_id) == 40);  // SHA-1 hash length
    assert(strcmp(meta.branch, "main") == 0 || strcmp(meta.branch, "master") == 0);
    
    // Return to original directory
    assert(chdir("..") == 0);
    
    printf("✓ Git metadata retrieval works correctly\n");
}

// Test Git repository detection
static void test_git_repo_detection(void) {
    printf("Testing Git repository detection...\n");
    
    // Test in non-Git directory
    assert(!eb_git_is_repo());
    
    // Test in Git directory
    assert(chdir("test_repo") == 0);
    assert(eb_git_is_repo());
    assert(chdir("..") == 0);
    
    printf("✓ Git repository detection works correctly\n");
}

// Test Git reference validation
static void test_git_ref_validation(void) {
    printf("Testing Git reference validation...\n");
    
    assert(chdir("test_repo") == 0);
    
    // Test invalid refs
    assert(!eb_git_is_valid_ref(NULL));
    assert(!eb_git_is_valid_ref("nonexistent-branch"));
    
    // Test valid refs
    assert(eb_git_is_valid_ref("HEAD"));
    assert(eb_git_is_valid_ref("main") || eb_git_is_valid_ref("master"));
    
    assert(chdir("..") == 0);
    
    printf("✓ Git reference validation works correctly\n");
}

// Test file content retrieval
static void test_git_file_retrieval(void) {
    printf("Testing Git file retrieval...\n");
    
    assert(chdir("test_repo") == 0);
    
    // Create a file with known content
    system("echo 'test content' > test.txt");
    system("git add test.txt");
    system("git commit -m 'add test content'");
    
    // Test retrieval
    char* content;
    size_t length;
    eb_status_t status = eb_git_get_file_at_ref("HEAD", "test.txt", &content, &length);
    
    assert(status == EB_SUCCESS);
    assert(content != NULL);
    assert(strncmp(content, "test content\n", length) == 0);
    
    free(content);
    assert(chdir("..") == 0);
    
    printf("✓ Git file retrieval works correctly\n");
}

int main(void) {
    printf("Running Git integration tests...\n\n");
    
    setup_git_env();
    
    // Run tests
    test_git_metadata();
    test_git_repo_detection();
    test_git_ref_validation();
    test_git_file_retrieval();
    
    cleanup_git_env();
    
    printf("\nAll Git integration tests passed!\n");
    return 0;
} 