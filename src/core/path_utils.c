#include "path_utils.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>

char* find_repo_root(const char* start_path) {
    char current_path[PATH_MAX];
    char *absolute_path;
    
    DEBUG_PRINT("find_repo_root called with path: %s", start_path ? start_path : "(null)");
    
    // If start_path is empty or ".", use getcwd
    if (!start_path || !strlen(start_path) || strcmp(start_path, ".") == 0) {
        if (!getcwd(current_path, PATH_MAX)) {
            DEBUG_PRINT("Failed to get current working directory");
            return NULL;
        }
        DEBUG_PRINT("Using current directory: %s", current_path);
    } else {
        absolute_path = realpath(start_path, current_path);
        if (!absolute_path) {
            DEBUG_PRINT("Failed to resolve path: %s", start_path);
            return NULL;
        }
        DEBUG_PRINT("Resolved to absolute path: %s", current_path);
    }

    int depth = 0;
    while (depth < MAX_PATH_DEPTH) {
        char eb_path[PATH_MAX];
        snprintf(eb_path, PATH_MAX, "%s/.eb", current_path);
        DEBUG_PRINT("Checking for .eb at: %s", eb_path);
        
        struct stat st;
        if (stat(eb_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            DEBUG_PRINT("Found .eb repository at: %s", current_path);
            return strdup(current_path);
        }

        // Move up one directory
        char *last_slash = strrchr(current_path, '/');
        if (!last_slash || last_slash == current_path) {
            DEBUG_PRINT("Reached root directory without finding .eb");
            break;
        }
        *last_slash = '\0';
        depth++;
        DEBUG_PRINT("Moving up to parent: %s", current_path);
    }

    DEBUG_PRINT("No .eb repository found in parent directories");
    return NULL;
}

char* get_relative_path(const char* abs_path, const char* repo_root) {
    char real_abs[PATH_MAX];
    char real_root[PATH_MAX];
    
    if (!realpath(abs_path, real_abs) || !realpath(repo_root, real_root)) {
        return NULL;
    }

    if (strncmp(real_abs, real_root, strlen(real_root)) != 0) {
        return NULL;
    }

    const char* rel = real_abs + strlen(real_root);
    while (*rel == '/') rel++;
    
    return strdup(rel);
}

char* get_absolute_path(const char* rel_path, const char* repo_root) {
    char result[PATH_MAX];
    snprintf(result, PATH_MAX, "%s/%s", repo_root, rel_path);
    
    char* abs_path = realpath(result, NULL);
    if (!abs_path) {
        // If file doesn't exist yet, just concatenate
        return strdup(result);
    }
    return abs_path;
} 