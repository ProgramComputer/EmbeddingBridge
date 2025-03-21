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
    printf("Usage: eb remote <command> [options] [args]\n\n");
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
static int handle_push_command(int argc, char **argv, const remote_context_t *ctx);
static int handle_pull_command(int argc, char **argv, const remote_context_t *ctx);

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
    } else if (strcmp(command, "push") == 0) {
        result = handle_push_command(pos_count - 1, &positional[1], &context);
    } else if (strcmp(command, "pull") == 0) {
        result = handle_pull_command(pos_count - 1, &positional[1], &context);
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
        printf("Usage: eb remote add <name> <url> [options]\n");
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
        printf("Usage: eb remote remove <name>\n");
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
        printf("No remotes configured. Add one with 'eb remote add <name> <url>'\n");
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

static int handle_push_command(int argc, char **argv, const remote_context_t *ctx) {
    if (argc < 1) {
        fprintf(stderr, "Error: Missing remote name\n");
        printf("Usage: eb remote push <n> [options]\n");
        return 1;
    }

    const char *name = argv[0];
    const char *set_name = NULL;
    
    /* Parse options */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--set=", 6) == 0) {
            set_name = argv[i] + 6;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: eb remote push <n> [options]\n");
            printf("\nPushes the current set to a remote repository.\n");
            printf("\nOptions:\n");
            printf("  --set=<n>  Specify set name (defaults to current active set)\n");
            return 0;
        }
    }

    /* If no set specified, use current active set */
    if (set_name == NULL) {
        /* Read current active set from .eb/HEAD */
        FILE *head_file = fopen(".eb/HEAD", "r");
        if (head_file) {
            char current_set[64] = {0};
            if (fgets(current_set, sizeof(current_set), head_file)) {
                /* Remove newline */
                size_t len = strlen(current_set);
                if (len > 0 && current_set[len-1] == '\n') {
                    current_set[len-1] = '\0';
                }
                
                /* Parse 'refs/heads/main' format to extract 'main' */
                char *last_slash = strrchr(current_set, '/');
                if (last_slash) {
                    set_name = strdup(last_slash + 1);
                } else {
                    set_name = strdup(current_set);
                }
            }
            fclose(head_file);
        }
        
        if (set_name == NULL) {
            /* Default to 'main' if we couldn't read HEAD */
            set_name = "main";
        }
    }
    
    /* Push the set to the remote */
    printf("Pushing set '%s' to remote '%s'...\n", set_name, name);
    
    /* Read the index file to get the list of embeddings */
    FILE *index_file = fopen(".eb/index", "r");
    if (!index_file) {
        fprintf(stderr, "Error: Could not open index file\n");
        return 1;
    }
    
    char line[1024];
    bool any_pushed = false;
    eb_status_t last_status = EB_ERROR_NOT_FOUND;
    
    /* Check if the index file is empty */
    if (fgets(line, sizeof(line), index_file) == NULL) {
        fclose(index_file);
        fprintf(stderr, "Error: No content to push. Index file is empty. Use 'eb store' to add embeddings.\n");
        return 1;
    }
    
    /* Reset to beginning of file */
    rewind(index_file);
    
    /* Process each line in the index file */
    while (fgets(line, sizeof(line), index_file)) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        
        /* Skip empty lines */
        if (len == 0) {
            continue;
        }
        
        /* Parse hash and filename */
        char hash[128] = {0};
        char filename[896] = {0};
        
        /* Format is: hash filename */
        if (sscanf(line, "%127s %895s", hash, filename) != 2) {
            continue;
        }
        
        /* Check if the raw file exists */
        char raw_path[1024];
        snprintf(raw_path, sizeof(raw_path), ".eb/objects/%s.raw", hash);
        
        FILE *raw_file = fopen(raw_path, "rb");
        if (!raw_file) {
            continue;
        }
        
        /* Get the file size */
        fseek(raw_file, 0, SEEK_END);
        long raw_size = ftell(raw_file);
        fseek(raw_file, 0, SEEK_SET);
        
        /* Read the raw file data */
        void *raw_data = malloc(raw_size);
        if (!raw_data) {
            fclose(raw_file);
            continue;
        }
        
        if (fread(raw_data, 1, raw_size, raw_file) != (size_t)raw_size) {
            free(raw_data);
            fclose(raw_file);
            continue;
        }
        
        fclose(raw_file);
        
        /* Create the path for this specific embedding */
        char embedding_path[1024];
        snprintf(embedding_path, sizeof(embedding_path), "sets/%s", set_name);
        
        /* Push this specific embedding */
        last_status = eb_remote_push(name, raw_data, raw_size, embedding_path);
        
        free(raw_data);
        
        if (last_status == EB_SUCCESS) {
            any_pushed = true;
        }
    }
    
    fclose(index_file);
    
    if (any_pushed) {
        printf("Successfully pushed set '%s' to remote '%s'\n", set_name, name);
        return 0;
    } else if (last_status == EB_ERROR_NOT_FOUND) {
        /* Don't show remote not found error when the index is empty - we already showed a better error message */
        fprintf(stderr, "Error: Failed to push to remote '%s'\n", name);
        return 1;
    } else {
        fprintf(stderr, "Error: Failed to push to remote '%s'\n", name);
        return 1;
    }
}

static int handle_pull_command(int argc, char **argv, const remote_context_t *ctx) {
    if (argc < 1) {
        fprintf(stderr, "Error: Missing remote name\n");
        printf("Usage: eb remote pull <name> [options]\n");
        return 1;
    }

    const char *name = argv[0];
    const char *set_name = NULL;
    bool delta_update = false;
    
    /* Parse options */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--set=", 6) == 0) {
            set_name = argv[i] + 6;
        } else if (strcmp(argv[i], "--delta") == 0) {
            delta_update = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: eb remote pull <name> [options]\n");
            printf("\nPulls from a remote repository into the current set.\n");
            printf("\nOptions:\n");
            printf("  --set=<name>  Specify set name (defaults to current active set)\n");
            printf("  --delta       Only pull changed data\n");
            return 0;
        }
    }
    
    /* If no set specified, use current active set */
    if (set_name == NULL) {
        /* Read current active set from .eb/HEAD */
        FILE *head_file = fopen(".eb/HEAD", "r");
        if (head_file) {
            char current_set[64] = {0};
            if (fgets(current_set, sizeof(current_set), head_file)) {
                /* Remove newline */
                size_t len = strlen(current_set);
                if (len > 0 && current_set[len-1] == '\n') {
                    current_set[len-1] = '\0';
                }
                
                /* Parse 'refs/heads/main' format to extract 'main' */
                char *last_slash = strrchr(current_set, '/');
                if (last_slash) {
                    set_name = strdup(last_slash + 1);
                } else {
                    set_name = strdup(current_set);
                }
            }
            fclose(head_file);
        }
        
        if (set_name == NULL) {
            /* Default to 'main' if we couldn't read HEAD */
            set_name = "main";
        }
    }
    
    /* Pull from the remote */
    printf("Pulling set '%s' from remote '%s'...\n", set_name, name);
    
    /* For now, we'll just create a simple test file path for the set */
    char path[1024];
    snprintf(path, sizeof(path), "sets/%s", set_name);

    /* Pull the data */
    void *data;
    size_t size;
    
    eb_status_t status;
    if (delta_update) {
        status = eb_remote_pull_delta(name, path, &data, &size, true);
    } else {
        status = eb_remote_pull(name, path, &data, &size);
    }
    
    if (status == EB_SUCCESS) {
        printf("Successfully pulled %zu bytes from set '%s' on remote '%s'\n", 
               size, set_name, name);
        
        /* Print the first 100 bytes of data if it's text */
        if (size > 0) {
            char *text_data = (char *)data;
            size_t print_size = (size < 100) ? size : 100;
            printf("Data preview: %.*s%s\n", 
                   (int)print_size, text_data, 
                   (size > print_size) ? "..." : "");
        }
        
        /* Free the data */
        free(data);
        return 0;
    } else if (status == EB_ERROR_NOT_FOUND) {
        fprintf(stderr, "Error: Remote '%s' not found\n", name);
        return 1;
    } else {
        fprintf(stderr, "Error: Failed to pull from remote '%s'\n", name);
        return 1;
    }
} 