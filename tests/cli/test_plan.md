# EmbeddingBridge CLI Test Plan

## Command Structure Tests

### Basic Command Tests
```c
void test_cli_basic(void) {
    // Test help
    assert(system("eb --help") == 0);
    assert(system("eb") == 1);  // No command should fail
    assert(system("eb invalid-command") == 1);
}
```

### Store Command Tests
```c
void test_store_command(void) {
    // Basic storage
    assert(system("eb store test.txt") == 0);
    
    // Options
    assert(system("eb store --no-git test.txt") == 0);
    assert(system("eb store --model openai-3 test.txt") == 0);
    
    // Errors
    assert(system("eb store nonexistent.txt") == 1);
    assert(system("eb store --model invalid test.txt") == 1);
}
```

### Diff Command Tests
```c
void test_diff_command(void) {
    // Basic diff
    assert(system("eb diff file1.txt file2.txt") == 0);
    
    // Git integration
    assert(system("eb diff HEAD HEAD~1 file.txt") == 0);
    
    // Options
    assert(system("eb diff --threshold 0.8 file1.txt file2.txt") == 0);
    
    // Errors
    assert(system("eb diff nonexistent.txt other.txt") == 1);
}
```

### Query Command Tests
```c
void test_query_command(void) {
    // Basic query
    assert(system("eb query 'test query'") == 0);
    
    // Options
    assert(system("eb query --limit 5 'test query'") == 0);
    assert(system("eb query --model openai-3 'test query'") == 0);
    
    // Errors
    assert(system("eb query") == 1);  // Missing query
}
```

### Git Integration Tests
```c
void test_git_integration(void) {
    // Hook installation
    assert(system("eb hooks install") == 0);
    assert(file_exists(".git/hooks/pre-commit"));
    
    // Hook execution
    assert(system("git add test.txt") == 0);
    assert(system("git commit -m 'test'") == 0);
    assert(embedding_exists("test.txt"));
}
```

## Required Test Files

### test.txt
Basic test file for embedding generation:
```
This is a test document.
It contains multiple lines
to test embedding generation.
```

### file1.txt and file2.txt
Similar files with known differences for diff testing:
```
// file1.txt
This is the original document.
It talks about machine learning.
```

```
// file2.txt
This is the modified document.
It talks about artificial intelligence.
```

## Test Environment Setup
```c
void setup_test_env(void) {
    // Create test directory
    system("rm -rf test_dir");
    system("mkdir test_dir");
    system("cd test_dir");
    
    // Initialize git
    system("git init");
    
    // Create test files
    create_test_files();
}

void teardown_test_env(void) {
    system("cd ..");
    system("rm -rf test_dir");
}
```

## Test Execution Order
1. Basic command structure
2. Store functionality
3. Diff functionality
4. Query functionality
5. Git integration

## Success Criteria
- All commands return expected status codes
- Embeddings are correctly stored and retrieved
- Git hooks function as expected
- Error cases are properly handled
- Memory management is clean (no leaks) 