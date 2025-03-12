#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <stdbool.h>
#include <limits.h>

// Maximum path depth to prevent infinite loops
#define MAX_PATH_DEPTH 100

// Find the .eb repository root directory
char* find_repo_root(const char* start_path);

// Convert path to be relative to repository root
char* get_relative_path(const char* abs_path, const char* repo_root);

// Get absolute path from relative path and repo root
char* get_absolute_path(const char* rel_path, const char* repo_root);

// Get the path to the current repository
char* get_repository_path(void);

#endif 