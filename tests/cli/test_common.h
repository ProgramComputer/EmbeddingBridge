#ifndef EB_TEST_COMMON_H
#define EB_TEST_COMMON_H

#include <stdbool.h>
#include <embedding_bridge/core.h>

// Test environment setup/teardown
void setup_test_env(void);
void teardown_test_env(void);

// Test utilities
bool embedding_exists(const char* file_path);
bool file_exists(const char* path);
void create_test_file(const char* path, const char* content);

// Test data
extern const char* TEST_DOCUMENT;
extern const char* TEST_DOCUMENT_MODIFIED;

#endif // EB_TEST_COMMON_H 