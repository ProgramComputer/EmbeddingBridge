#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"

static const char* USAGE = 
    "Usage: eb <command> [options] [args]\n"
    "\n"
    "Embedding management and version control\n"
    "\n"
    "Core Commands:\n"
    "  init          Create empty embedding repository\n"
    "  store         Store embeddings for documents\n"
    "  diff          Compare embeddings between versions\n"
    "  query         Search across embeddings\n"
    "  status        Show embedding status for a source file\n"
    "\n"
    "Management Commands:\n"
    "  config        Configure embedding settings\n"
    "  remote        Manage embedding storage locations\n"
    "  hooks         Manage Git hooks\n"
    "\n"
    "Run 'eb <command> --help' for command-specific help\n";

static const eb_command_t commands[] = {
    // Core commands
    {"init", "Create empty embedding repository", cmd_init},
    {"store", "Store embeddings for documents", cmd_store},
    {"diff", "Compare embeddings between versions", cmd_diff},
    {"query", "Search across embeddings", cmd_query},
    {"status", "Show embedding status for a source file", cmd_status},
    
    // Management commands
    {"config", "Configure embedding settings", cmd_config},
    {"remote", "Manage embedding storage locations", cmd_remote},
    {"hooks", "Manage Git hooks", cmd_hooks},
    
    {"model", "Manage embedding models", cmd_model},
    {"rollback", "Revert to a previous embedding version", cmd_rollback},
    
    {NULL, NULL, NULL}
};

static void print_usage(void) {
    printf("%s", USAGE);
}

static void suggest_command(const char* cmd) {
    // Find similar commands for suggestions
    printf("Error: '%s' is not an eb command\n", cmd);
    
    // Look for similar commands
    for (const eb_command_t* c = commands; c->name; c++) {
        if (strncmp(c->name, cmd, 2) == 0) {
            printf("\nDid you mean?\n    %s\n", c->name);
            break;
        }
    }
    
    printf("\nRun 'eb --help' for usage\n");
}

int main(int argc, char** argv) {
    if (getenv("EB_DEBUG")) {
        fprintf(stderr, "Debug - Main called with:\n");
        for (int i = 0; i < argc; i++) {
            fprintf(stderr, "  argv[%d]: %s\n", i, argv[i]);
        }
    }

    // Show usage for no arguments
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // Global help shows version
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("eb version %d\n", EB_VERSION);
        return 0;
    }

    // Find and execute command
    const char* cmd_name = argv[1];
    for (const eb_command_t* cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd_name, cmd->name) == 0) {
            if (getenv("EB_DEBUG")) {
                fprintf(stderr, "Debug - Found command: %s\n", cmd_name);
            }
            return cmd->handler(argc - 1, argv + 1);
        }
    }

    suggest_command(cmd_name);
    return 1;
} 