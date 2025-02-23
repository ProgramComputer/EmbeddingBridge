#ifndef EMBEDDING_BRIDGE_GIT_TYPES_H
#define EMBEDDING_BRIDGE_GIT_TYPES_H

#include "types.h"
#include <stdint.h>
#include <stdbool.h>

// Git-specific error codes
#define EB_ERROR_NOT_GIT_REPO (EB_ERROR_NOT_FOUND + 1)
#define EB_ERROR_GIT_OPERATION (EB_ERROR_NOT_GIT_REPO + 1)

// Git hash and branch size constants
#define EB_GIT_HASH_SIZE 41    // SHA-1 hash (40 chars + null terminator)
#define EB_GIT_BRANCH_SIZE 100 // Branch name max length

// Git metadata structure
typedef struct {
    char commit_id[EB_GIT_HASH_SIZE];     // SHA-1 hash
    char author[100];                      // Author name and email
    uint64_t commit_time;                  // Unix timestamp of commit
    char branch[EB_GIT_BRANCH_SIZE];      // Current branch
    bool is_modified;                      // Whether file is modified
    bool is_tracked;                       // Whether file is tracked
} eb_git_metadata_t;

// Git hook status structure
typedef struct {
    char name[32];       // Hook name (pre-commit, post-commit, etc.)
    bool installed;      // Whether hook is installed
    bool enabled;        // Whether hook is enabled in config
    bool verbose;        // Whether hook is in verbose mode
    bool has_backup;     // Whether original hook was backed up
} eb_git_hook_status_t;

// Git functions
bool eb_git_is_repo(void);

// Git hook management functions
eb_status_t eb_git_install_hooks(bool force);
eb_status_t eb_git_uninstall_hooks(bool force);
eb_status_t eb_git_get_hook_status(eb_git_hook_status_t** out_status, size_t* out_count);
void eb_git_free_hook_status(eb_git_hook_status_t* status, size_t count);

#endif // EMBEDDING_BRIDGE_GIT_TYPES_H