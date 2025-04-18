#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/debug.h"
#include "cli.h"
#include "set.h"
#include "merge.h"
#include "gc.h"

static const char* USAGE = 
    "Usage: embr <command> [options] [args]\n"
    "\n"
    "Embedding management and version control\n"
    "\n"
    "Core Commands:\n"
    "  init          Create empty embedding repository\n"
    "  store         Store embeddings for documents\n"
    "  diff          Compare embeddings between versions\n"
    "  status        Show embedding status for a source file\n"
    "  log           Display embedding log for files\n"
    "  set           Manage embedding sets\n"
    "  switch        Switch between embedding sets\n"
    "  merge         Merge embeddings from one set to another\n"
    "\n"
    "Management Commands:\n"
    "  config        Configure embedding settings\n"
    "  remote        Manage embedding storage locations\n"
    "  hooks         Manage Git hooks\n"
    "  model         Manage embedding models\n"
    "  rollback      Revert to a previous embedding version\n"
    "  gc            Garbage collect unreferenced embeddings\n"
    "  get           Download a file or directory from a repository\n"
    "  rm            Remove embeddings from tracking\n"
    "\n"
    "Run 'embr <command> --help' for command-specific help\n";

static const eb_command_t commands[] = {
    // Core commands
    {"init", "Create empty embedding repository", cmd_init},
    {"store", "Store embeddings for documents", cmd_store},
    {"diff", "Compare embeddings between versions", cmd_diff},
    {"status", "Show embedding status for a source file", cmd_status},
    {"log", "Display embedding log for files", cmd_log},
    {"set", "Manage embedding sets", cmd_set},
    {"switch", "Switch between embedding sets", cmd_switch},
    {"merge", "Merge embeddings from one set to another", cmd_merge},
    
    // Management commands
    {"config", "Configure embedding settings", cmd_config},
    {"remote", "Manage embedding storage locations", cmd_remote},
    {"hooks", "Manage Git hooks", cmd_hooks},
    {"model", "Manage embedding models", cmd_model},
    {"rollback", "Revert to a previous embedding version", cmd_rollback},
    {"gc", "Garbage collect unreferenced embeddings", cmd_gc},
    {"get", "Download a file or directory from a repository", cmd_get},
    {"rm", "Remove embeddings from tracking", cmd_rm},
    
    {NULL, NULL, NULL}
};

static void print_usage(void) {
    printf("%s", USAGE);
}

static void suggest_command(const char* cmd) {
    // Find similar commands for suggestions
    printf("Error: '%s' is not an embr command\n", cmd);
    
    // Look for similar commands
    for (const eb_command_t* c = commands; c->name; c++) {
        if (strncmp(c->name, cmd, 2) == 0) {
            printf("\nDid you mean?\n    %s\n", c->name);
            break;
        }
    }
    
    printf("\nRun 'embr --help' for usage\n");
}

int main(int argc, char** argv) {
    /* Initialize debug system */
    eb_debug_init();
    
    if (getenv("EB_DEBUG")) {
        DEBUG_INFO("Main called with %d arguments", argc);
        for (int i = 0; i < argc; i++) {
            DEBUG_INFO("  argv[%d]: %s", i, argv[i]);
        }
    }

    // Show usage for no arguments
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // Handle --version flag
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("embr version %s\n", EB_VERSION_STR);
        return 0;
    }

    // Global help shows version and full usage text
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("embr version %s\n\n", EB_VERSION_STR);
        print_usage();
        return 0;
    }

    // Find and execute command
    const char* cmd_name = argv[1];
    for (const eb_command_t* cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd_name, cmd->name) == 0) {
            if (getenv("EB_DEBUG")) {
                DEBUG_INFO("Found command: %s", cmd_name);
            }
            return cmd->handler(argc - 1, argv + 1);
        }
    }

    suggest_command(cmd_name);
    return 1;
} 