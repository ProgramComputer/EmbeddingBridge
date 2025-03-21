/*
 * EmbeddingBridge - Get Command
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>

#include "cli.h"
#include "git_types.h"
#include "fs.h"
#include "config.h"
#include "debug.h"
#include "remote.h"

static void print_usage(void) {
    printf("Usage: eb get [-h] [-o <path>] [--rev <commit>] <url> <path>\n\n");
    printf("Download a file or directory from a repository\n\n");
    printf("Arguments:\n");
    printf("  url              Path to the repository (local or remote)\n");
    printf("  path             Path to a file or directory within the repository\n\n");
    printf("Options:\n");
    printf("  -o, --out <path>         Specify output path\n");
    printf("  --rev <commit>           Revision to download (defaults to latest)\n");
    printf("  --show-url               Show remote URL instead of downloading\n");
    printf("  -f, --force              Force download even if output file exists\n");
    printf("  -v, --verbose            Show detailed output\n");
    printf("  -q, --quiet              Suppress output messages\n");
    printf("  -h, --help               Show this help message\n");
}

/*
 * Handle getting a file from a local repository
 */
static int get_local_file(const char *repo_path, const char *path, const char *output_path, 
                         const char *revision, bool force, bool verbose) {
    char full_path[PATH_MAX];
    
    /* Check if we need to use a specific revision */
    if (revision) {
        /* Get the file from the Git history using the specified revision */
        if (verbose) {
            printf("Getting %s from revision %s in %s\n", path, revision, repo_path);
        }
        
        /* TODO: Call git API to extract file from history */
        /* For now, just return a message that the feature is not implemented */
        fprintf(stderr, "Getting from specific revision is not yet implemented\n");
        return 1;
    } else {
        /* If no revision, just copy the file from the repo */
        snprintf(full_path, sizeof(full_path), "%s/%s", repo_path, path);
        
        /* Check if source exists */
        struct stat st;
        if (stat(full_path, &st) != 0) {
            fprintf(stderr, "Error: File '%s' not found in repository\n", path);
            return 1;
        }
        
        /* Check if output file already exists */
        if (!force && access(output_path, F_OK) == 0) {
            fprintf(stderr, "Error: Output file '%s' already exists. Use --force to overwrite.\n", output_path);
            return 1;
        }
        
        /* Create output directory if needed */
        char *output_dir = strdup(output_path);
        char *dir = dirname(output_dir);
        
        /* Only create directory if it's not just "." */
        if (strcmp(dir, ".") != 0) {
            if (fs_mkdir_p(dir, 0755) != 0) {
                fprintf(stderr, "Error: Failed to create output directory '%s'\n", dir);
                free(output_dir);
                return 1;
            }
        }
        free(output_dir);
        
        /* Copy the file */
        if (fs_copy_file(full_path, output_path) != 0) {
            fprintf(stderr, "Error: Failed to copy file from '%s' to '%s'\n", full_path, output_path);
            return 1;
        }
        
        if (verbose) {
            printf("Downloaded: %s -> %s\n", path, output_path);
        }
    }
    
    return 0;
}

/*
 * Implementation of the get command
 */
int cmd_get(int argc, char **argv) {
    char *url = NULL;
    char *path = NULL;
    char *output_path = NULL;
    char *revision = NULL;
    bool show_url = false;
    bool force = false;
    bool verbose = false;
    bool quiet = false;
    
    /* Parse arguments */
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) {
            if (i + 1 < argc) {
                output_path = argv[++i];
            } else {
                fprintf(stderr, "Error: Missing output path\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--rev") == 0) {
            if (i + 1 < argc) {
                revision = argv[++i];
            } else {
                fprintf(stderr, "Error: Missing revision\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--show-url") == 0) {
            show_url = true;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (url == NULL) {
            url = argv[i];
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_usage();
            return 1;
        }
    }
    
    /* Check for required arguments */
    if (!url || !path) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage();
        return 1;
    }
    
    /* If output path not specified, use the basename of the path */
    if (!output_path) {
        output_path = basename(strdup(path));
    }
    
    /* Determine if URL is local or remote */
    bool is_local = false;
    
    /* For this simple implementation, consider paths starting with http://, https://, 
     * ssh://, or git@ as remote, everything else as local */
    if (strncmp(url, "http://", 7) != 0 && 
        strncmp(url, "https://", 8) != 0 &&
        strncmp(url, "ssh://", 6) != 0 && 
        strncmp(url, "git@", 4) != 0) {
        is_local = true;
    }
    
    if (verbose) {
        printf("URL: %s\n", url);
        printf("Path: %s\n", path);
        printf("Output: %s\n", output_path);
        printf("Revision: %s\n", revision ? revision : "latest");
        printf("Type: %s\n", is_local ? "local" : "remote");
    }
    
    /* If showing URL only, print it and exit */
    if (show_url) {
        if (is_local) {
            char full_path[PATH_MAX];
            /* For local paths, just construct the full path */
            if (path[0] == '/') {
                snprintf(full_path, sizeof(full_path), "%s%s", url, path);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", url, path);
            }
            printf("%s\n", full_path);
        } else {
            /* For remote URLs, we'd need to generate the storage URL */
            fprintf(stderr, "Remote URL discovery not yet implemented\n");
            return 1;
        }
        return 0;
    }
    
    /* Handle based on URL type */
    if (is_local) {
        return get_local_file(url, path, output_path, revision, force, verbose);
    } else {
        /* Remote repository handling would go here */
        fprintf(stderr, "Remote repository support not yet implemented\n");
        return 1;
    }
    
    return 0;
} 