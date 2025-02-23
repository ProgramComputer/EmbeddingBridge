#include <stdio.h>
#include <string.h>
#include "cli.h"

static const char* HOOKS_USAGE = 
    "Usage: eb hooks <command> [options]\n"
    "\n"
    "Manage Git hooks\n"
    "\n"
    "Commands:\n"
    "  install     Install Git hooks\n"
    "  uninstall   Remove Git hooks\n"
    "  list        Show hook status\n"
    "\n"
    "Options:\n"
    "  -f, --force    Force operation\n"
    "  -v, --verbose  Show detailed output\n"
    "\n"
    "Examples:\n"
    "  # Install hooks\n"
    "  eb hooks install\n"
    "\n"
    "  # List hook status\n"
    "  eb hooks list -v\n"
    "\n"
    "Use 'eb config' to configure hook behavior:\n"
    "  eb config set git.hooks.pre-commit.enabled true\n"
    "  eb config set git.hooks.pre-commit.verbose true\n";

// Hook configuration structure
typedef struct {
    const char* name;
    bool enabled;
    bool verbose;
} hook_config_t;

static const hook_config_t HOOKS[] = {
    {"pre-commit", true, false},
    {"post-commit", true, false},
    {"pre-push", true, false},
    {"post-merge", true, false},
    {NULL, false, false}
};

static int cmd_hooks_install(int argc, char** argv) {
    bool force = has_option(argc, argv, "-f") || has_option(argc, argv, "--force");
    bool verbose = has_option(argc, argv, "-v") || has_option(argc, argv, "--verbose");

    // Check if in Git repository
    if (!eb_git_is_repo()) {
        fprintf(stderr, "error: not a git repository\n");
        return 1;
    }

    // Install hooks
    eb_status_t status = eb_git_install_hooks(force);
    if (status != EB_SUCCESS) {
        handle_error(status, "Failed to install hooks");
        return 1;
    }

    if (verbose) {
        printf("Installed hooks:\n");
        for (const hook_config_t* hook = HOOKS; hook->name; hook++) {
            printf("  %s\n", hook->name);
        }
    } else {
        printf("Git hooks installed successfully\n");
    }

    return 0;
}

static int cmd_hooks_uninstall(int argc, char** argv) {
    bool force = has_option(argc, argv, "-f") || has_option(argc, argv, "--force");
    bool verbose = has_option(argc, argv, "-v") || has_option(argc, argv, "--verbose");

    // Check if in Git repository
    if (!eb_git_is_repo()) {
        fprintf(stderr, "error: not a git repository\n");
        return 1;
    }

    // Uninstall hooks
    eb_status_t status = eb_git_uninstall_hooks(force);
    if (status != EB_SUCCESS) {
        handle_error(status, "Failed to uninstall hooks");
        return 1;
    }

    if (verbose) {
        printf("Removed hooks:\n");
        for (const hook_config_t* hook = HOOKS; hook->name; hook++) {
            printf("  %s\n", hook->name);
        }
    } else {
        printf("Git hooks uninstalled successfully\n");
    }

    return 0;
}

static int cmd_hooks_list(int argc, char** argv) {
    bool verbose = has_option(argc, argv, "-v") || has_option(argc, argv, "--verbose");

    // Check if in Git repository
    if (!eb_git_is_repo()) {
        fprintf(stderr, "error: not a git repository\n");
        return 1;
    }

    // Get hook status
    eb_git_hook_status_t* status;
    size_t count;
    eb_status_t result = eb_git_get_hook_status(&status, &count);
    if (result != EB_SUCCESS) {
        handle_error(result, "Failed to get hook status");
        return 1;
    }

    // Print status
    if (verbose) {
        printf("Git hook status:\n");
        for (size_t i = 0; i < count; i++) {
            printf("  %s:\n", status[i].name);
            printf("    Installed: %s\n", status[i].installed ? "yes" : "no");
            printf("    Enabled: %s\n", status[i].enabled ? "yes" : "no");
            printf("    Verbose: %s\n", status[i].verbose ? "yes" : "no");
            if (status[i].has_backup) {
                printf("    Backup: yes (.pre-eb)\n");
            }
        }
    } else {
        for (size_t i = 0; i < count; i++) {
            printf("%s: %s\n", status[i].name, 
                   status[i].installed ? (status[i].enabled ? "enabled" : "disabled") : "not installed");
        }
    }

    eb_git_free_hook_status(status, count);
    return 0;
}

int cmd_hooks(int argc, char** argv) {
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", HOOKS_USAGE);
        return (argc < 2) ? 1 : 0;
    }

    const char* subcmd = argv[1];
    if (strcmp(subcmd, "install") == 0) {
        return cmd_hooks_install(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "uninstall") == 0) {
        return cmd_hooks_uninstall(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "list") == 0) {
        return cmd_hooks_list(argc - 1, argv + 1);
    } else {
        fprintf(stderr, "error: unknown hooks command '%s'\n", subcmd);
        printf("\n%s", HOOKS_USAGE);
        return 1;
    }
} 