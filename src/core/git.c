#include "types.h"
#include "git_types.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <git2.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

eb_status_t eb_git_get_metadata(const char* filepath, eb_git_metadata_t** out_metadata) {
    if (!filepath || !out_metadata) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Initialize libgit2
    git_libgit2_init();

    // Allocate metadata structure
    eb_git_metadata_t* metadata = (eb_git_metadata_t*)malloc(sizeof(eb_git_metadata_t));
    if (!metadata) {
        git_libgit2_shutdown();
        return EB_ERROR_MEMORY_ALLOCATION;
    }
    memset(metadata, 0, sizeof(eb_git_metadata_t));

    // Open repository
    git_repository* repo = NULL;
    if (git_repository_open_ext(&repo, ".", 0, NULL) != 0) {
        free(metadata);
        git_libgit2_shutdown();
        return EB_ERROR_NOT_GIT_REPO;
    }

    // Get HEAD commit
    git_oid head_oid;
    git_reference* head_ref = NULL;
    git_commit* head_commit = NULL;

    if (git_repository_head(&head_ref, repo) != 0) {
        git_repository_free(repo);
        free(metadata);
        git_libgit2_shutdown();
        return EB_ERROR_GIT_OPERATION;
    }

    git_reference_name_to_id(&head_oid, repo, git_reference_name(head_ref));
    git_commit_lookup(&head_commit, repo, &head_oid);

    // Fill metadata
    strncpy(metadata->commit_id, git_oid_tostr_s(&head_oid), EB_GIT_HASH_SIZE);
    strncpy(metadata->branch, git_reference_shorthand(head_ref), EB_GIT_BRANCH_SIZE);
    metadata->commit_time = (uint64_t)git_commit_time(head_commit);

    // Get file status
    git_status_options statusopt = GIT_STATUS_OPTIONS_INIT;
    statusopt.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED;
    statusopt.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;  // Fix missing initializer warning
    git_status_list* status = NULL;

    if (git_status_list_new(&status, repo, &statusopt) == 0) {
        size_t i, maxi = git_status_list_entrycount(status);
        for (i = 0; i < maxi; i++) {
            const git_status_entry* s = git_status_byindex(status, i);
            
            const char* path = s->head_to_index ? s->head_to_index->new_file.path :
                             s->index_to_workdir ? s->index_to_workdir->new_file.path : NULL;
            
            if (path && strcmp(path, filepath) == 0) {
                metadata->is_modified = (s->status & GIT_STATUS_WT_MODIFIED) != 0;
                metadata->is_tracked = (s->status & GIT_STATUS_WT_NEW) == 0;
                break;
            }
        }
        git_status_list_free(status);
    }

    // Clean up
    git_commit_free(head_commit);
    git_reference_free(head_ref);
    git_repository_free(repo);
    git_libgit2_shutdown();

    *out_metadata = metadata;
    return EB_SUCCESS;
}

bool eb_git_is_repo(void) {
    return system("git rev-parse --git-dir > /dev/null 2>&1") == 0;
}

bool eb_git_is_valid_ref(const char* ref) {
    if (!ref) return false;
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git rev-parse --verify %s > /dev/null 2>&1", ref);
    return system(cmd) == 0;
}

eb_status_t eb_git_get_file_at_ref(
    const char* ref,
    const char* file_path,
    char** out_content,
    size_t* out_length
) {
    if (!ref || !file_path || !out_content || !out_length) {
        return EB_ERROR_INVALID_INPUT;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), 
             "git show %s:%s 2>/dev/null",
             ref, file_path);

    FILE* fp = popen(cmd, "r");
    if (!fp) {
        return EB_ERROR_GIT_OPERATION;
    }

    // Read file content
    size_t capacity = 1024;
    size_t length = 0;
    char* content = malloc(capacity);
    
    if (!content) {
        pclose(fp);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t chunk_len = strlen(buffer);
        
        // Ensure capacity
        if (length + chunk_len >= capacity) {
            capacity *= 2;
            char* new_content = realloc(content, capacity);
            if (!new_content) {
                free(content);
                pclose(fp);
                return EB_ERROR_MEMORY_ALLOCATION;
            }
            content = new_content;
        }

        // Append chunk
        memcpy(content + length, buffer, chunk_len);
        length += chunk_len;
    }

    int status = pclose(fp);
    if (status != 0) {
        free(content);
        return EB_ERROR_GIT_OPERATION;
    }

    *out_content = content;
    *out_length = length;
    return EB_SUCCESS;
}

eb_status_t eb_git_install_hooks(bool force) {
    if (!eb_git_is_repo()) {
        return EB_ERROR_NOT_GIT_REPO;
    }

    char git_dir[1024];
    if (system("git rev-parse --git-dir > /dev/null 2>&1") != 0) {
        return EB_ERROR_GIT_OPERATION;
    }

    // Get Git hooks directory
    FILE* fp = popen("git rev-parse --git-dir", "r");
    if (!fp) {
        return EB_ERROR_GIT_OPERATION;
    }
    if (!fgets(git_dir, sizeof(git_dir), fp)) {
        pclose(fp);
        return EB_ERROR_GIT_OPERATION;
    }
    pclose(fp);

    // Remove newline if present
    char* newline = strchr(git_dir, '\n');
    if (newline) *newline = '\0';

    // Create hooks directory path
    char hooks_dir[1024];
    snprintf(hooks_dir, sizeof(hooks_dir), "%s/hooks", git_dir);

    // Create hooks directory if it doesn't exist
    struct stat st;
    if (stat(hooks_dir, &st) != 0) {
        if (mkdir(hooks_dir, 0755) != 0) {
            return EB_ERROR_GIT_OPERATION;
        }
    }

    // Install each hook
    const char* hooks[] = {"pre-commit", "post-commit", "pre-push", "post-merge", NULL};
    for (const char** hook = hooks; *hook; hook++) {
        char hook_path[1024];
        snprintf(hook_path, sizeof(hook_path), "%s/%s", hooks_dir, *hook);

        // Check if hook already exists
        if (access(hook_path, F_OK) == 0 && !force) {
            // Backup existing hook
            char backup_path[1024];
            snprintf(backup_path, sizeof(backup_path), "%s.pre-eb", hook_path);
            if (rename(hook_path, backup_path) != 0) {
                return EB_ERROR_GIT_OPERATION;
            }
        }

        // Create new hook
        int fd = open(hook_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (fd == -1) {
            return EB_ERROR_GIT_OPERATION;
        }

        // Write hook content
        const char* content = 
            "#!/bin/sh\n"
            "# eb hook: This is a managed hook. Edit with caution.\n"
            "\n"
            "# Check if hook is enabled\n"
            "if ! embr config get git.hooks.%s.enabled >/dev/null 2>&1 || \\\n"
            "   [ \"$(embr config get git.hooks.%s.enabled)\" = \"false\" ]; then\n"
            "    exit 0  # Hook disabled, skip silently\n"
            "fi\n"
            "\n"
            "# Get verbosity setting\n"
            "verbose=$(embr config get git.hooks.%s.verbose 2>/dev/null)\n"
            "\n"
            "# Run eb hook command\n"
            "[ \"$verbose\" = \"true\" ] && echo \"embr: Running %s hook\"\n"
            "eb hooks run %s \"$@\" || {\n"
            "    echo \"embr: %s hook failed\"\n"
            "    echo \"hint: Use 'embr config set git.hooks.%s.enabled false' to disable this hook\"\n"
            "    exit 1\n"
            "}\n"
            "exit 0\n";

        char hook_content[2048];
        snprintf(hook_content, sizeof(hook_content), content, 
                *hook, *hook, *hook, *hook, *hook, *hook, *hook);

        if (write(fd, hook_content, strlen(hook_content)) == -1) {
            close(fd);
            return EB_ERROR_GIT_OPERATION;
        }

        close(fd);
    }

    return EB_SUCCESS;
}

eb_status_t eb_git_uninstall_hooks(bool force) {
    if (!eb_git_is_repo()) {
        return EB_ERROR_NOT_GIT_REPO;
    }

    char git_dir[1024];
    FILE* fp = popen("git rev-parse --git-dir", "r");
    if (!fp) {
        return EB_ERROR_GIT_OPERATION;
    }
    if (!fgets(git_dir, sizeof(git_dir), fp)) {
        pclose(fp);
        return EB_ERROR_GIT_OPERATION;
    }
    pclose(fp);

    // Remove newline if present
    char* newline = strchr(git_dir, '\n');
    if (newline) *newline = '\0';

    // Process each hook
    const char* hooks[] = {"pre-commit", "post-commit", "pre-push", "post-merge", NULL};
    for (const char** hook = hooks; *hook; hook++) {
        char hook_path[1024];
        snprintf(hook_path, sizeof(hook_path), "%s/hooks/%s", git_dir, *hook);

        // Check if hook exists
        if (access(hook_path, F_OK) == 0) {
            // Check if it's our hook
            FILE* f = fopen(hook_path, "r");
            if (f) {
                char line[256];
                bool is_eb_hook = false;
                if (fgets(line, sizeof(line), f)) {
                    is_eb_hook = strstr(line, "# eb hook") != NULL;
                }
                fclose(f);

                if (is_eb_hook || force) {
                    // Check for backup
                    char backup_path[1024];
                    snprintf(backup_path, sizeof(backup_path), "%s.pre-eb", hook_path);
                    if (access(backup_path, F_OK) == 0) {
                        // Restore backup
                        if (rename(backup_path, hook_path) != 0) {
                            return EB_ERROR_GIT_OPERATION;
                        }
                    } else {
                        // No backup, just remove the hook
                        if (unlink(hook_path) != 0) {
                            return EB_ERROR_GIT_OPERATION;
                        }
                    }
                }
            }
        }
    }

    return EB_SUCCESS;
}

eb_status_t eb_git_get_hook_status(eb_git_hook_status_t** out_status, size_t* out_count) {
    if (!out_status || !out_count) {
        return EB_ERROR_INVALID_INPUT;
    }

    if (!eb_git_is_repo()) {
        return EB_ERROR_NOT_GIT_REPO;
    }

    // Get Git hooks directory
    char git_dir[1024];
    FILE* fp = popen("git rev-parse --git-dir", "r");
    if (!fp) {
        return EB_ERROR_GIT_OPERATION;
    }
    if (!fgets(git_dir, sizeof(git_dir), fp)) {
        pclose(fp);
        return EB_ERROR_GIT_OPERATION;
    }
    pclose(fp);

    // Remove newline if present
    char* newline = strchr(git_dir, '\n');
    if (newline) *newline = '\0';

    // Allocate status array
    const char* hooks[] = {"pre-commit", "post-commit", "pre-push", "post-merge", NULL};
    size_t hook_count = 0;
    while (hooks[hook_count]) hook_count++;

    eb_git_hook_status_t* status = malloc(hook_count * sizeof(eb_git_hook_status_t));
    if (!status) {
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Check each hook
    for (size_t i = 0; i < hook_count; i++) {
        strncpy(status[i].name, hooks[i], sizeof(status[i].name) - 1);
        status[i].name[sizeof(status[i].name) - 1] = '\0';

        char hook_path[1024];
        snprintf(hook_path, sizeof(hook_path), "%s/hooks/%s", git_dir, hooks[i]);

        // Check if hook exists and is executable
        status[i].installed = access(hook_path, X_OK) == 0;

        // Check if it's our hook
        if (status[i].installed) {
            FILE* f = fopen(hook_path, "r");
            if (f) {
                char line[256];
                if (fgets(line, sizeof(line), f)) {
                    status[i].installed = strstr(line, "# eb hook") != NULL;
                }
                fclose(f);
            }
        }

        // Check for backup
        char backup_path[1024];
        snprintf(backup_path, sizeof(backup_path), "%s.pre-eb", hook_path);
        status[i].has_backup = access(backup_path, F_OK) == 0;

        // Get configuration
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "embr config get git.hooks.%s.enabled 2>/dev/null", hooks[i]);
        fp = popen(cmd, "r");
        if (fp) {
            char result[32];
            if (fgets(result, sizeof(result), fp)) {
                status[i].enabled = strcmp(result, "true\n") == 0;
            } else {
                status[i].enabled = false;
            }
            pclose(fp);
        }

        snprintf(cmd, sizeof(cmd), "embr config get git.hooks.%s.verbose 2>/dev/null", hooks[i]);
        fp = popen(cmd, "r");
        if (fp) {
            char result[32];
            if (fgets(result, sizeof(result), fp)) {
                status[i].verbose = strcmp(result, "true\n") == 0;
            } else {
                status[i].verbose = false;
            }
            pclose(fp);
        }
    }

    *out_status = status;
    *out_count = hook_count;
    return EB_SUCCESS;
}

void eb_git_free_hook_status(eb_git_hook_status_t* status, size_t count) {
    free(status);
}