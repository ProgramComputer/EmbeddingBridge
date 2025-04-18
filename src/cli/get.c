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
#include <dirent.h>  /* For directory operations: opendir, readdir, closedir */

#include "cli.h"
#include "git_types.h"
#include "fs.h"
#include "config.h"
#include "debug.h"
#include "remote.h"

/* Define PATH_MAX if not already defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void print_usage(void) {
    printf("Usage: embr get [-h] [-o <filename>] <destination_dir> <hash>\n\n");
    printf("Download an embedding file by hash to local destination\n\n");
    printf("Arguments:\n");
    printf("  destination_dir   Directory where the embedding will be saved\n");
    printf("  hash              Hash or short hash of the embedding to download\n\n");
    printf("Options:\n");
    printf("  -o, --out <filename>      Specify exact output filename (used as-is)\n");
    printf("  -f, --force              Force download even if file exists\n");
    printf("  -v, --verbose            Show detailed output\n");
    printf("  -q, --quiet              Suppress output messages\n");
    printf("  -h, --help               Show this help message\n");
}

/**
 * Read metadata file to extract embedding information
 * 
 * @param meta_path Path to the metadata file
 * @param source_file Output buffer for original source file
 * @param file_type Output buffer for file type
 * @param provider Output buffer for provider/model name
 * @return true if successful, false otherwise
 */
static bool read_metadata(const char *meta_path, char *source_file, char *file_type, char *provider) {
    FILE *fp = fopen(meta_path, "r");
    if (!fp) {
        return false;
    }
    
    bool found_source = false;
    bool found_type = false;
    bool found_provider = false;
    
    char line[PATH_MAX];
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        if (strncmp(line, "source_file=", 12) == 0) {
            strncpy(source_file, line + 12, PATH_MAX - 1);
            found_source = true;
        } else if (strncmp(line, "file_type=", 10) == 0) {
            strncpy(file_type, line + 10, 32 - 1);
            found_type = true;
        } else if (strncmp(line, "provider=", 9) == 0) {
            strncpy(provider, line + 9, 32 - 1);
            found_provider = true;
        }
    }
    
    fclose(fp);
    return found_source && found_type;
}

/*
 * Attempt to resolve a short hash to a full hash in local repository
 */
static bool resolve_hash(const char *repo_path, const char *short_hash, char *full_hash, size_t hash_size) {
    char objects_dir[PATH_MAX];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.embr/objects", repo_path);
    
    DIR *dir = opendir(objects_dir);
    if (!dir) return false;
    
    size_t hash_len = strlen(short_hash);
    bool found = false;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Only look at .raw or .meta files
        if (entry->d_type == DT_REG) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".raw") == 0 || strcmp(ext, ".meta") == 0)) {
                // Remove extension for comparison
                *ext = '\0';
                
                // Compare with short hash
                if (strncmp(entry->d_name, short_hash, hash_len) == 0) {
                    if (found) {
                        // Hash is ambiguous (multiple matches)
                        closedir(dir);
                        return false;
                    }
                    
                    strncpy(full_hash, entry->d_name, hash_size - 1);
                    full_hash[hash_size - 1] = '\0';
                    found = true;
                }
            }
        }
    }
    
    closedir(dir);
    return found;
}

/*
 * Check local repositories for the hash
 */
static bool find_local_hash(const char *hash, char *full_hash, char *repo_path, char *meta_path, char *object_path) {
    // Check current directory first
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return false;
    }
    
    // Look in current directory's .embr
    char local_repo[PATH_MAX];
    snprintf(local_repo, sizeof(local_repo), "%s", cwd);
    
    char local_meta[PATH_MAX];
    char local_object[PATH_MAX];
    
    // Check if we need to resolve the hash
    char resolved_hash[65];
    if (strlen(hash) < 64) {
        if (!resolve_hash(local_repo, hash, resolved_hash, sizeof(resolved_hash))) {
            return false;
        }
    } else {
        strncpy(resolved_hash, hash, sizeof(resolved_hash) - 1);
        resolved_hash[sizeof(resolved_hash) - 1] = '\0';
    }
    
    // Path to metadata and object files
    snprintf(local_meta, sizeof(local_meta), "%s/.embr/objects/%s.meta", local_repo, resolved_hash);
    snprintf(local_object, sizeof(local_object), "%s/.embr/objects/%s.raw", local_repo, resolved_hash);
    
    // Check if files exist
    if (access(local_meta, F_OK) == 0 && access(local_object, F_OK) == 0) {
        strncpy(full_hash, resolved_hash, 65);
        strncpy(repo_path, local_repo, PATH_MAX);
        strncpy(meta_path, local_meta, PATH_MAX);
        strncpy(object_path, local_object, PATH_MAX);
        return true;
    }
    
    // TODO: Check other local repositories if needed
    
    return false;
}

/*
 * Check remote repositories for the hash
 */
static bool find_remote_hash(const char *hash, char *full_hash, char *temp_meta_path, char *temp_object_path) {
    // TODO: Implement remote repository search and download
    // This would contact configured remote servers to find and download the hash
    
    // Not implemented yet
    return false;
}

/*
 * Handle getting a file by hash
 */
static int get_embedding_by_hash(
    const char *dest_dir, 
    const char *hash,
    const char *output_filename,
    bool force, 
    bool verbose, 
    bool quiet
) {
    // Check if destination directory exists
    struct stat st;
    if (stat(dest_dir, &st) != 0) {
        if (!quiet) fprintf(stderr, "Error: Destination directory '%s' does not exist\n", dest_dir);
        return 1;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        if (!quiet) fprintf(stderr, "Error: '%s' is not a directory\n", dest_dir);
        return 1;
    }
    
    // First, try to find the hash in local repositories
    char full_hash[65] = {0};
    char repo_path[PATH_MAX] = {0};
    char meta_path[PATH_MAX] = {0};
    char object_path[PATH_MAX] = {0};
    
    bool found_local = find_local_hash(hash, full_hash, repo_path, meta_path, object_path);
    
    // If not found locally, try remote repositories
    char temp_meta_path[PATH_MAX] = {0};
    char temp_object_path[PATH_MAX] = {0};
    bool found_remote = false;
    
    if (!found_local) {
        found_remote = find_remote_hash(hash, full_hash, temp_meta_path, temp_object_path);
    }
    
    if (!found_local && !found_remote) {
        if (!quiet) fprintf(stderr, "Error: Embedding with hash '%s' not found\n", hash);
        return 1;
    }
    
    // Select the correct paths
    const char *src_meta = found_local ? meta_path : temp_meta_path;
    const char *src_object = found_local ? object_path : temp_object_path;
    
    // Read metadata to determine file type and original filename
    char source_file[PATH_MAX] = {0};
    char file_type[32] = {0};
    char provider[32] = {0};
    
    if (!read_metadata(src_meta, source_file, file_type, provider)) {
        if (!quiet) fprintf(stderr, "Error: Failed to read metadata for hash '%s'\n", hash);
        return 1;
    }
    
    // Generate output filename if not specified
    char final_output[PATH_MAX] = {0};
    if (output_filename) {
        // Use the output filename exactly as provided by the user, without modifying extension
        snprintf(final_output, sizeof(final_output), "%s/%s", dest_dir, output_filename);
    } else {
        // Use basename of original source file + hash + file type
        char *src_basename = basename(strdup(source_file));
        char *dot = strrchr(src_basename, '.');
        if (dot) *dot = '\0'; // Remove extension
        
        snprintf(final_output, sizeof(final_output), "%s/%s_%s.%s", 
                dest_dir, src_basename, hash, file_type);
        
        free(src_basename);
    }
    
    // Check if output file already exists
    if (!force && access(final_output, F_OK) == 0) {
        if (!quiet) fprintf(stderr, "Error: Output file '%s' already exists. Use --force to overwrite.\n", final_output);
        return 1;
    }
    
    // Copy the embedding file
    if (fs_copy_file(src_object, final_output) != 0) {
        if (!quiet) fprintf(stderr, "Error: Failed to copy embedding to '%s'\n", final_output);
        return 1;
    }
    
    if (verbose) {
        printf("Hash: %s\n", full_hash);
        printf("Source file: %s\n", source_file);
        printf("File type: %s\n", file_type);
        printf("Provider: %s\n", provider);
        printf("Downloaded to: %s\n", final_output);
    } else if (!quiet) {
        printf("✓ Downloaded embedding to %s\n", final_output);
    }
    
    // Clean up any temporary files if we used remote repositories
    if (found_remote) {
        if (access(temp_meta_path, F_OK) == 0) unlink(temp_meta_path);
        if (access(temp_object_path, F_OK) == 0) unlink(temp_object_path);
    }
    
    return 0;
}

/*
 * Implementation of the get command
 */
int cmd_get(int argc, char **argv) {
    char *dest_dir = NULL;
    char *hash = NULL;
    char *output_filename = NULL;
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
                output_filename = argv[++i];
            } else {
                fprintf(stderr, "Error: Missing output filename\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (dest_dir == NULL) {
            dest_dir = argv[i];
        } else if (hash == NULL) {
            hash = argv[i];
        } else {
            fprintf(stderr, "Error: Too many arguments\n");
            print_usage();
            return 1;
        }
    }
    
    /* Check for required arguments */
    if (!dest_dir || !hash) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage();
        return 1;
    }
    
    return get_embedding_by_hash(dest_dir, hash, output_filename, force, verbose, quiet);
} 