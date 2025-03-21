#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>  // For PATH_MAX
#include "cli.h"
#include "../core/path_utils.h"

static const char* INIT_USAGE = 
    "Usage: eb init [options]\n"
    "\n"
    "Initialize embedding storage in current directory\n"
    "\n"
    "Options:\n"
    "  -m, --model <name>    Set default embedding model\n"
    "  -f, --force           Reinitialize existing repository\n"
    "  --no-git             Skip Git integration setup\n"
    "\n"
    "Examples:\n"
    "  # Initialize with defaults\n"
    "  eb init\n"
    "\n"
    "  # Initialize with specific model\n"
    "  eb init --model openai-3\n"
    "\n"
    "  # Reinitialize existing repository\n"
    "  eb init --force\n";

// Default configuration
static const char* DEFAULT_CONFIG = "# EmbeddingBridge config file\n\n"
    "[core]\n"
    "	version = 0.1.0\n\n"
    "[model]\n"
    "	default = \n\n"
    "[storage]\n"
    "	compression = true\n"
    "	deduplication = true\n\n"
    "[git]\n"
    "	auto_update = true\n\n"
    "[git \"hooks.pre-commit\"]\n"
    "	enabled = true\n"
    "	verbose = false\n\n"
    "[git \"hooks.post-commit\"]\n"
    "	enabled = true\n"
    "	verbose = false\n\n"
    "[git \"hooks.pre-push\"]\n"
    "	enabled = true\n"
    "	verbose = false\n\n"
    "[git \"hooks.post-merge\"]\n"
    "	enabled = true\n"
    "	verbose = false\n";

// Default HEAD reference
static const char* DEFAULT_HEAD = "main";

// Hook templates with more transparency
static const struct {
    const char* name;
    const char* content;
} HOOKS[] = {
    {"pre-commit", "#!/bin/sh\n"
        "# eb pre-commit hook: Generate embeddings for changed files\n"
        "\n"
        "# Check if hook is enabled in eb config\n"
        "if ! eb config get git.hooks.pre-commit.enabled >/dev/null 2>&1 || \\\n"
        "   [ \"$(eb config get git.hooks.pre-commit.enabled)\" = \"false\" ]; then\n"
        "    exit 0  # Hook disabled, skip silently\n"
        "fi\n"
        "\n"
        "# Get verbosity setting\n"
        "verbose=$(eb config get git.hooks.pre-commit.verbose 2>/dev/null)\n"
        "\n"
        "# Get list of staged files\n"
        "files=$(git diff --cached --name-only --diff-filter=ACM)\n"
        "if [ -n \"$files\" ]; then\n"
        "    if [ \"$verbose\" = \"true\" ]; then\n"
        "        echo \"eb: Generating embeddings for staged files:\"\n"
        "        echo \"$files\" | sed 's/^/  /'\n"
        "    fi\n"
        "    # Generate embeddings for changed files\n"
        "    echo \"$files\" | eb store --stdin || {\n"
        "        echo \"eb: Failed to generate embeddings\"\n"
        "        echo \"hint: Use 'eb config set git.hooks.pre-commit.enabled false' to disable this hook\"\n"
        "        exit 1\n"
        "    }\n"
        "    [ \"$verbose\" = \"true\" ] && echo \"eb: Successfully generated embeddings\"\n"
        "fi\n"
        "exit 0\n"},
    {"post-commit", "#!/bin/sh\n"
        "# eb post-commit hook: Update metadata after commit\n"
        "\n"
        "# Check if hook is enabled\n"
        "if ! eb config get git.hooks.post-commit.enabled >/dev/null 2>&1 || \\\n"
        "   [ \"$(eb config get git.hooks.post-commit.enabled)\" = \"false\" ]; then\n"
        "    exit 0  # Hook disabled, skip silently\n"
        "fi\n"
        "\n"
        "# Get verbosity setting\n"
        "verbose=$(eb config get git.hooks.post-commit.verbose 2>/dev/null)\n"
        "\n"
        "# Get the commit hash\n"
        "commit=$(git rev-parse HEAD)\n"
        "\n"
        "[ \"$verbose\" = \"true\" ] && echo \"eb: Updating metadata for commit $commit\"\n"
        "\n"
        "# Update metadata for the commit\n"
        "eb metadata update \"$commit\" || {\n"
        "    echo \"eb: Failed to update metadata\"\n"
        "    echo \"hint: Use 'eb config set git.hooks.post-commit.enabled false' to disable this hook\"\n"
        "    exit 1\n"
        "}\n"
        "[ \"$verbose\" = \"true\" ] && echo \"eb: Successfully updated metadata\"\n"
        "exit 0\n"},
    {"pre-push", "#!/bin/sh\n"
        "# eb pre-push hook: Validate embeddings before push\n"
        "\n"
        "# Check if hook is enabled\n"
        "if ! eb config get git.hooks.pre-push.enabled >/dev/null 2>&1 || \\\n"
        "   [ \"$(eb config get git.hooks.pre-push.enabled)\" = \"false\" ]; then\n"
        "    exit 0  # Hook disabled, skip silently\n"
        "fi\n"
        "\n"
        "# Get verbosity setting\n"
        "verbose=$(eb config get git.hooks.pre-push.verbose 2>/dev/null)\n"
        "\n"
        "# Get range of commits being pushed\n"
        "while read local_ref local_sha remote_ref remote_sha; do\n"
        "    [ \"$verbose\" = \"true\" ] && echo \"eb: Validating embeddings for commits $remote_sha..$local_sha\"\n"
        "    # Validate embeddings in the range\n"
        "    eb validate \"$remote_sha..$local_sha\" || {\n"
        "        echo \"eb: Embedding validation failed\"\n"
        "        echo \"hint: Use 'eb config set git.hooks.pre-push.enabled false' to disable this hook\"\n"
        "        exit 1\n"
        "    }\n"
        "    [ \"$verbose\" = \"true\" ] && echo \"eb: Successfully validated embeddings\"\n"
        "done\n"
        "exit 0\n"},
    {"post-merge", "#!/bin/sh\n"
        "# eb post-merge hook: Update embeddings after merge\n"
        "\n"
        "# Check if hook is enabled\n"
        "if ! eb config get git.hooks.post-merge.enabled >/dev/null 2>&1 || \\\n"
        "   [ \"$(eb config get git.hooks.post-merge.enabled)\" = \"false\" ]; then\n"
        "    exit 0  # Hook disabled, skip silently\n"
        "fi\n"
        "\n"
        "# Get verbosity setting\n"
        "verbose=$(eb config get git.hooks.post-merge.verbose 2>/dev/null)\n"
        "\n"
        "# Get list of changed files in the merge\n"
        "files=$(git diff ORIG_HEAD HEAD --name-only)\n"
        "if [ -n \"$files\" ]; then\n"
        "    if [ \"$verbose\" = \"true\" ]; then\n"
        "        echo \"eb: Updating embeddings for merged files:\"\n"
        "        echo \"$files\" | sed 's/^/  /'\n"
        "    fi\n"
        "    # Update embeddings for changed files\n"
        "    echo \"$files\" | eb store --stdin || {\n"
        "        echo \"eb: Failed to update embeddings\"\n"
        "        echo \"hint: Use 'eb config set git.hooks.post-merge.enabled false' to disable this hook\"\n"
        "        exit 1\n"
        "    }\n"
        "    [ \"$verbose\" = \"true\" ] && echo \"eb: Successfully updated embeddings\"\n"
        "fi\n"
        "exit 0\n"},
    {NULL, NULL}
};

static int create_directory(const char* base, const char* path) {
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", base, path);
    
    // Create each directory in the path
    char* p = full_path;
    while ((p = strchr(p + 1, '/')) != NULL) {
        *p = '\0';  // Temporarily terminate string
        
        // Try to create directory
        #ifdef _WIN32
        if (mkdir(full_path) != 0 && errno != EEXIST) {
        #else
        if (mkdir(full_path, 0755) != 0 && errno != EEXIST) {
        #endif
            *p = '/';  // Restore slash
            return -1;
        }
        
        *p = '/';  // Restore slash
    }
    
    // Create final directory
    #ifdef _WIN32
    return (mkdir(full_path) != 0 && errno != EEXIST) ? -1 : 0;
    #else
    return (mkdir(full_path, 0755) != 0 && errno != EEXIST) ? -1 : 0;
    #endif
}

static int write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

static eb_status_t create_eb_structure(const char* root, const char* model __attribute__((unused))) {
    char path[1024];
    
    // Create .eb directory
    if (create_directory(root, ".eb") != 0) {
        fprintf(stderr, "error: could not create .eb directory\n");
        return 1;
    }
    
    // Create subdirectories
    const char* dirs[] = {
        ".eb/objects",
        ".eb/objects/temp",
        ".eb/metadata",
        ".eb/metadata/files",
        ".eb/metadata/models",
        ".eb/metadata/versions"
    };
    
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (create_directory(root, dirs[i]) != 0) {
            fprintf(stderr, "error: could not create %s directory\n", dirs[i]);
            return 1;
        }
    }
    
    // Create config file
    snprintf(path, sizeof(path), "%s/.eb/config", root);
    if (write_file(path, DEFAULT_CONFIG) != 0) {
        fprintf(stderr, "error: could not create config file\n");
        return 1;
    }
    
    // Create HEAD file
    snprintf(path, sizeof(path), "%s/.eb/HEAD", root);
    if (write_file(path, DEFAULT_HEAD) != 0) {
        fprintf(stderr, "error: could not create HEAD file\n");
        return 1;
    }

    // Create empty history file
    snprintf(path, sizeof(path), "%s/.eb/log", root);
    if (write_file(path, "") != 0) {
        fprintf(stderr, "error: could not create history file\n");
        return 1;
    }
    
    // Create empty index file
    snprintf(path, sizeof(path), "%s/.eb/index", root);
    if (write_file(path, "") != 0) {
        fprintf(stderr, "error: could not create index file\n");
        return 1;
    }
    
    return 0;
}

static int is_git_repository(const char* path) {
    char git_path[1024];
    struct stat st;
    
    snprintf(git_path, sizeof(git_path), "%s/.git", path);
    return stat(git_path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_eb_initialized(const char* path) {
    char eb_path[1024];
    struct stat st;
    
    snprintf(eb_path, sizeof(eb_path), "%s/.eb", path);
    return stat(eb_path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int install_git_hooks(const char* git_dir) {
    char hook_path[1024];
    char hooks_dir[1024];
    
    // Create hooks directory path
    snprintf(hooks_dir, sizeof(hooks_dir), "%s/.git/hooks", git_dir);
    
    // Create hooks directory if it doesn't exist
    struct stat st;
    if (stat(hooks_dir, &st) != 0) {
        #ifdef _WIN32
        if (mkdir(hooks_dir) != 0) {
        #else
        if (mkdir(hooks_dir, 0755) != 0) {
        #endif
            fprintf(stderr, "error: could not create hooks directory\n");
            return 1;
        }
    }
    
    // Install each hook
    for (int i = 0; HOOKS[i].name != NULL; i++) {
        // Create hook file path
        snprintf(hook_path, sizeof(hook_path), "%s/%s", hooks_dir, HOOKS[i].name);
        
        // Check if hook already exists
        if (access(hook_path, F_OK) == 0) {
            // Backup existing hook
            char backup_path[1024];
            snprintf(backup_path, sizeof(backup_path), "%s.pre-eb", hook_path);
            if (rename(hook_path, backup_path) != 0) {
                fprintf(stderr, "error: could not backup existing %s hook\n", HOOKS[i].name);
                return 1;
            }
            printf("hint: existing %s hook backed up to %s.pre-eb\n", HOOKS[i].name, HOOKS[i].name);
        }
        
        // Write new hook
        FILE* f = fopen(hook_path, "w");
        if (!f) {
            fprintf(stderr, "error: could not create %s hook\n", HOOKS[i].name);
            return 1;
        }
        
        if (fprintf(f, "%s", HOOKS[i].content) < 0) {
            fprintf(stderr, "error: could not write %s hook\n", HOOKS[i].name);
            fclose(f);
            return 1;
        }
        
        fclose(f);
        
        // Make hook executable
        #ifndef _WIN32
        if (chmod(hook_path, 0755) != 0) {
            fprintf(stderr, "error: could not make %s hook executable\n", HOOKS[i].name);
            return 1;
        }
        #endif
        
        printf("Created %s hook\n", HOOKS[i].name);
    }
    
    return 0;
}

int init_main(int argc __attribute__((unused)), char* argv[] __attribute__((unused))) {
    char current_dir[PATH_MAX];
    if (!getcwd(current_dir, PATH_MAX)) {
        fprintf(stderr, "Error: Cannot get current directory\n");
        return 1;
    }

    // Check if we're already in a repository
    char* existing_root = find_repo_root(current_dir);
    if (existing_root) {
        fprintf(stderr, "Error: Already in an eb repository at %s\n", existing_root);
        free(existing_root);
        return 1;
    }

    // Create .eb directory structure
    char eb_path[PATH_MAX];
    snprintf(eb_path, PATH_MAX, "%s/.eb", current_dir);
    
    if (mkdir(eb_path, 0755) != 0) {
        perror("Error creating .eb directory");
        return 1;
    }

    // Create subdirectories
    char* subdirs[] = {"/embeddings", "/bin", "/meta"};
    for (int i = 0; i < 3; i++) {
        char subdir[PATH_MAX];
        snprintf(subdir, PATH_MAX, "%s%s", eb_path, subdirs[i]);
        if (mkdir(subdir, 0755) != 0) {
            perror("Error creating subdirectory");
            return 1;
        }
    }

    printf("Initialized empty eb repository in %s/.eb\n", current_dir);
    return 0;
}

int cmd_init(int argc, char** argv) {
    if (has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", INIT_USAGE);
        return 0;
    }
    
    // Get current directory
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "error: could not get current directory\n");
        return 1;
    }
    
    // Check if already initialized
    if (is_eb_initialized(cwd) && !has_option(argc, argv, "-f") && !has_option(argc, argv, "--force")) {
        fprintf(stderr, "error: embedding repository already exists\n");
        fprintf(stderr, "hint: use --force to reinitialize\n");
        return 1;
    }
    
    // Get optional model from command line only
    const char* model = get_option_value(argc, argv, "-m", "--model");
    
    // Create directory structure
    if (create_eb_structure(cwd, model) != 0) {
        return 1;
    }
    
    // Set up Git integration if available and not disabled
    if (is_git_repository(cwd) && !has_option(argc, argv, "--no-git")) {
        printf("hint: detected Git repository, enabling Git integration\n");
        if (install_git_hooks(cwd) != 0) {
            fprintf(stderr, "warning: failed to set up Git hooks\n");
            fprintf(stderr, "hint: you can set up hooks later using 'eb hooks install'\n");
        }
    }
    
    // Success message
    if (has_option(argc, argv, "-f") || has_option(argc, argv, "--force")) {
        printf("Reinitialized existing embedding repository in %s/.eb\n", cwd);
    } else {
        printf("Initialized empty embedding repository in %s/.eb\n", cwd);
    }
    
    // Hint about setting up a model if none was specified
    if (!model) {
        printf("\nhint: no model set - use --model <name> when running commands\n");
    }
    
    return 0;
} 