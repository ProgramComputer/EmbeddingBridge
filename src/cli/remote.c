/*
 * EmbeddingBridge - Remote Command Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "cli.h"
#include "remote.h"
#include "status.h"
#include "debug.h"

#define MAX_REMOTES 50
#define DEFAULT_TIMEOUT 30
#define DEFAULT_COMPRESSION 9

// Define a context structure to hold parsing results
typedef struct {
    const char *format;
    const char *token;
    int compression;
    int timeout;
    bool verify_ssl;
} remote_context_t;

static void print_usage(void) {
    printf("Usage: embr remote <command> [options] [args]\n\n");
    printf("Commands:\n");
    printf("  add <name> <url>     Add a new remote repository\n");
    printf("  remove <name>        Remove a remote repository\n");
    printf("  list                 List all remote repositories\n");
    printf("  push <name>          Push current set to a remote repository\n");
    printf("  pull <name>          Pull from a remote repository\n");
    printf("\n");
    printf("Common options:\n");
    printf("  --help               Show this help message\n");
    printf("  --format=<format>    Specify data format (json, parquet) [default: json]\n");
    printf("  --compression=<0-9>  Set compression level [default: 9]\n");
    printf("  --timeout=<seconds>  Set connection timeout [default: 30]\n");
    printf("  --no-verify-ssl      Disable SSL certificate verification\n");
    printf("  --token=<string>     Specify authentication token\n");
    printf("  --set=<name>         Specify set name (defaults to current active set)\n");
}

/* Forward declarations of command handlers */
static int handle_add_command(int argc, char **argv, const remote_context_t *ctx);
static int handle_remove_command(int argc, char **argv);
static int handle_list_command(int argc, char **argv);

// Callback for option processing
static int remote_option_callback(char short_opt, const char* long_opt, const char* arg, void* ctx) {
    remote_context_t* context = (remote_context_t*)ctx;
    
    if (long_opt) {
        // Skip leading dashes for comparison
        const char* opt = long_opt;
        if (opt[0] == '-') {
            if (opt[1] == '-') opt += 2;  // Skip "--"
            else opt += 1;  // Skip "-"
        }
        
        // Handle help option
        if (strcmp(opt, "help") == 0) {
            print_usage();
            exit(0);
        } else if (strcmp(opt, "format") == 0 || strncmp(opt, "format=", 7) == 0) {
            context->format = arg;
        } else if (strcmp(opt, "compression") == 0 || strncmp(opt, "compression=", 12) == 0) {
            context->compression = atoi(arg);
            if (context->compression < 0 || context->compression > 9) {
                fprintf(stderr, "Error: Compression level must be between 0 and 9\n");
                return 1;
            }
        } else if (strcmp(opt, "token") == 0 || strncmp(opt, "token=", 6) == 0) {
            context->token = arg;
        } else if (strcmp(opt, "timeout") == 0 || strncmp(opt, "timeout=", 8) == 0) {
            context->timeout = atoi(arg);
        } else if (strcmp(opt, "no-verify-ssl") == 0) {
            context->verify_ssl = false;
        } else {
            fprintf(stderr, "Unknown option: %s\n", long_opt);
            return 1;
        }
    } else if (short_opt == 'h') {
        print_usage();
        exit(0);
    } else {
        fprintf(stderr, "Unknown option: -%c\n", short_opt);
        return 1;
    }
    
    return 0;
}

int cmd_remote(int argc, char** argv) {
    /* No arguments or help flag: print usage */
    if (argc < 1 || strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0) {
        print_usage();
        return 0;
    }

    /* Initialize the remote subsystem */
    eb_status_t status = eb_remote_init();
    if (status != EB_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize remote subsystem\n");
        return 1;
    }

    // Initialize context with default values
    remote_context_t context = {
        .format = "json",
        .token = NULL,
        .compression = DEFAULT_COMPRESSION,
        .timeout = DEFAULT_TIMEOUT,
        .verify_ssl = true
    };
    
    // Define option definitions
    const char* short_opts = "h";
    const char* long_opts[] = {
        "format=",
        "compression=",
        "token=",
        "timeout=",
        "no-verify-ssl",
        "help",
        NULL  // Must be NULL-terminated
    };
    
    // Array to collect positional arguments
    char* positional[argc];
    int pos_count = 0;
    
    // Parse options using Git-style parser
    int result = parse_git_style_options(
        argc, argv, 
        short_opts, long_opts,
        remote_option_callback, &context,
        positional, &pos_count
    );
    
    if (result != 0) {
        eb_remote_shutdown();
        return result;
    }
    
    // Process positional arguments
    if (pos_count < 1) {
        fprintf(stderr, "Error: No remote command specified\n");
        print_usage();
        eb_remote_shutdown();
        return 1;
    }
    
    const char *command = positional[0];
    result = 1;

    /* Dispatch to appropriate handler */
    if (strcmp(command, "add") == 0) {
        result = handle_add_command(pos_count - 1, &positional[1], &context);
    } else if (strcmp(command, "remove") == 0) {
        result = handle_remove_command(pos_count - 1, &positional[1]);
    } else if (strcmp(command, "list") == 0) {
        result = handle_list_command(pos_count - 1, &positional[1]);
    } else {
        fprintf(stderr, "Error: Unknown remote command '%s'\n", command);
        print_usage();
        result = 1;
    }

    /* Clean up */
    eb_remote_shutdown();
    return result;
}

static int handle_add_command(int argc, char **argv, const remote_context_t *ctx) {
    if (argc < 2) {
        fprintf(stderr, "Error: Missing remote name and URL\n");
        printf("Usage: embr remote add <name> <url> [options]\n");
        return 1;
    }

    const char *name = argv[0];
    const char *url = argv[1];

    /* Add the remote */
    eb_status_t status = eb_remote_add(name, url, ctx->token, ctx->compression, 
                                      ctx->verify_ssl, ctx->format);
    
    if (status == EB_SUCCESS) {
        printf("Remote '%s' added successfully\n", name);
        return 0;
    } else if (status == EB_ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Error: Remote '%s' already exists\n", name);
        return 1;
    } else {
        fprintf(stderr, "Error: Failed to add remote '%s'\n", name);
        return 1;
    }
}

static int handle_remove_command(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Error: Missing remote name\n");
        printf("Usage: embr remote remove <name>\n");
        return 1;
    }

    const char *name = argv[0];

    /* Remove the remote */
    eb_status_t status = eb_remote_remove(name);
    
    if (status == EB_SUCCESS) {
        printf("Remote '%s' removed successfully\n", name);
        return 0;
    } else if (status == EB_ERROR_NOT_FOUND) {
        fprintf(stderr, "Error: Remote '%s' not found\n", name);
        return 1;
    } else {
        fprintf(stderr, "Error: Failed to remove remote '%s'\n", name);
        return 1;
    }
}

static int handle_list_command(int argc, char **argv) {
    /* Get the list of remotes */
    char **names;
    int count;
    eb_status_t status = eb_remote_list(&names, &count);
    
    if (status != EB_SUCCESS) {
        fprintf(stderr, "Error: Failed to list remotes\n");
        return 1;
    }

    if (count == 0) {
        printf("No remotes configured. Add one with 'embr remote add <name> <url>'\n");
    } else {
        printf("Configured remotes:\n");
        for (int i = 0; i < count; i++) {
            char url[256];
            int timeout;
            bool verify_ssl;
            char transformer[32];
            
            /* Get remote details */
            status = eb_remote_info(names[i], url, sizeof(url), 
                                  &timeout, &verify_ssl, transformer, sizeof(transformer));
            
            if (status == EB_SUCCESS) {
                printf("  %s\t%s (format: %s, compression: %d, verify_ssl: %s)\n", 
                       names[i], url, transformer, timeout, verify_ssl ? "true" : "false");
            } else {
                printf("  %s\t<error retrieving details>\n", names[i]);
            }
            
            /* Free the name string */
            free(names[i]);
        }
    }
    
    /* Free the names array */
    free(names);
    
    return 0;
} 