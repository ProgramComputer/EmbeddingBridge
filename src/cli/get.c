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
#include "set.h"              // For get_current_set
#include "../core/path_utils.h" // For find_repo_root
#include "../core/parquet_transformer.h" // For eb_parquet_extract_metadata_json
// TODO: Add the correct header for extract_file_type_from_parquet
// #include "parquet_transform.h" // For extract_file_type_from_parquet

/* Define PATH_MAX if not already defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void print_usage(void) {
    printf("Usage: embr get [-h] [-f] [-v] [-q] <output_directory> <hash>\n\n");
    printf("Download an embedding file by hash to local destination\n\n");
    printf("Arguments:\n");
    printf("  output_directory  Directory where the embedding will be saved\n");
    printf("  hash              Hash or short hash of the embedding to download\n\n");
    printf("Options:\n");
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
        } else if (strncmp(line, "model=", 6) == 0) {
            strncpy(provider, line + 6, 32 - 1);
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
        // Skip non-regular files via stat()
        {
            char fullpath[PATH_MAX];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", objects_dir, entry->d_name);
            struct stat st;
            if (stat(fullpath, &st) != 0 || !S_ISREG(st.st_mode))
                continue;
        }
        // Only look at .raw or .meta files
        char namebuf[PATH_MAX];
        strncpy(namebuf, entry->d_name, sizeof(namebuf));
        namebuf[sizeof(namebuf)-1] = '\0';
        char *ext = strrchr(namebuf, '.');
        if (ext && (strcmp(ext, ".raw") == 0 || strcmp(ext, ".meta") == 0)) {
            *ext = '\0';
            // Debug print
            DEBUG_INFO("resolve_hash: namebuf='%s', short_hash='%s', hash_len=%zu", namebuf, short_hash, hash_len);
            int cmp = strncmp(namebuf, short_hash, hash_len);
            DEBUG_INFO("resolve_hash: strncmp result = %d", cmp);
            if (cmp == 0) {
                if (found) {
                    // Hash is ambiguous (multiple matches)
                    closedir(dir);
                    return false;
                }
                strncpy(full_hash, namebuf, hash_size - 1);
                full_hash[hash_size - 1] = '\0';
                found = true;
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
    DEBUG_INFO("find_local_hash: entered with hash='%s'", hash);
    // Check current directory first
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return false;
    }
    // Use store API for hash resolution
    char resolved_hash[65] = {0};
    char *repo_root = find_repo_root(".");
    if (!repo_root) {
        return false;
    }
    eb_store_t *store = NULL;
    eb_store_config_t config = { .root_path = repo_root };
    if (eb_store_init(&config, &store) != EB_SUCCESS) {
        free(repo_root);
        return false;
    }
    eb_status_t status = eb_store_resolve_hash(store, hash, resolved_hash, sizeof(resolved_hash));
    eb_store_destroy(store);
    free(repo_root);
    if (status != EB_SUCCESS) {
        return false;
    }
    // Path to metadata and object files
    snprintf(meta_path, PATH_MAX, "%s/.embr/objects/%s.meta", cwd, resolved_hash);
    snprintf(object_path, PATH_MAX, "%s/.embr/objects/%s.raw", cwd, resolved_hash);
    // Debug prints
    DEBUG_INFO("find_local_hash: resolved_hash='%s'", resolved_hash);
    DEBUG_INFO("find_local_hash: local_meta='%s'", meta_path);
    DEBUG_INFO("find_local_hash: local_object='%s'", object_path);
    int meta_exists = access(meta_path, F_OK);
    int object_exists = access(object_path, F_OK);
    DEBUG_INFO("find_local_hash: access(local_meta)=%d, access(local_object)=%d", meta_exists, object_exists);
    // Check if files exist
    if (meta_exists == 0 && object_exists == 0) {
        strncpy(full_hash, resolved_hash, 65);
        strncpy(repo_path, cwd, PATH_MAX);
        DEBUG_INFO("find_local_hash: success, returning true");
        return true;
    }
    DEBUG_INFO("find_local_hash: returning false");
    return false;
}

/*
 * Check remote repositories for the hash
 */
static bool find_remote_hash(const char *hash, char *full_hash, char *temp_meta_path, char *temp_object_path) {
    eb_status_t status;
    // Initialize remote subsystem
    status = eb_remote_init();
    if (status != EB_SUCCESS) {
        return false;
    }
    // Get list of remotes
    char **remotes = NULL;
    int rem_count = 0;
    status = eb_remote_list(&remotes, &rem_count);
    if (status != EB_SUCCESS || rem_count == 0) {
        eb_remote_shutdown();
        return false;
    }
    // Get current set name
    char set_name[128] = {0};
    if (get_current_set(set_name, sizeof(set_name)) != EB_SUCCESS || set_name[0] == '\0') {
        for (int i = 0; i < rem_count; i++) free(remotes[i]);
        free(remotes);
        eb_remote_shutdown();
        return false;
    }
    // Resolve full hash if short
    char resolved_hash[65] = {0};
    bool hash_resolved = false;
    size_t hash_len = strlen(hash);
    for (int i = 0; i < rem_count; i++) {
        char prefix[256];
        snprintf(prefix, sizeof(prefix), "sets/%s/documents", set_name);
        char **files = NULL;
        size_t file_count = 0;
        status = eb_remote_list_files(remotes[i], prefix, &files, &file_count);
        if (status == EB_SUCCESS && files) {
            for (size_t j = 0; j < file_count; j++) {
                char *fname = files[j];
                // Strip any path prefix, use only the basename
                char *base = strrchr(fname, '/');
                if (base) base++;
                else base = fname;
                char *dot = strrchr(base, '.');
                if (!dot || strcmp(dot, ".parquet") != 0) continue;
                size_t name_len = dot - base;
                if (name_len >= hash_len && strncmp(base, hash, hash_len) == 0) {
                    if (hash_resolved) {
                        // Ambiguous prefix
                        for (size_t k = 0; k < file_count; k++) free(files[k]);
                        free(files);
                        for (int k = 0; k < rem_count; k++) free(remotes[k]);
                        free(remotes);
                        eb_remote_shutdown();
                        return false;
                    }
                    strncpy(resolved_hash, base, name_len);
                    resolved_hash[name_len] = '\0';
                    hash_resolved = true;
                }
            }
            for (size_t j = 0; j < file_count; j++) free(files[j]);
            free(files);
        }
        if (hash_resolved) break;
    }
    if (!hash_resolved) {
        for (int i = 0; i < rem_count; i++) free(remotes[i]);
        free(remotes);
        eb_remote_shutdown();
        return false;
    }
    strncpy(full_hash, resolved_hash, 64);
    full_hash[64] = '\0';
    bool downloaded = false;
    for (int i = 0; i < rem_count; i++) {
        char remote_parquet[PATH_MAX];
        snprintf(remote_parquet, sizeof(remote_parquet), "sets/%s/documents/%s.parquet", set_name, resolved_hash);
        void *parquet_data = NULL;
        size_t parquet_size = 0;
        status = eb_remote_pull(remotes[i], remote_parquet, &parquet_data, &parquet_size);
        if (status != EB_SUCCESS || !parquet_data) continue;
        char parquet_template[] = "/tmp/embr_parquet_XXXXXX";
        int fd_parquet = mkstemp(parquet_template);
        if (fd_parquet < 0) {
            free(parquet_data);
            continue;
        }
        if (write(fd_parquet, parquet_data, parquet_size) != (ssize_t)parquet_size) {
            close(fd_parquet);
            free(parquet_data);
            continue;
        }
        close(fd_parquet);

        // Set raw output path to the Parquet temp file before inverse-transform
        strncpy(temp_object_path, parquet_template, PATH_MAX);

        // Inverse-transform Parquet to .raw
        eb_transformer_t *transformer = eb_find_transformer_by_format("parquet");
        if (transformer) {
            void *original_data = NULL;
            size_t original_size = 0;
            eb_status_t inv_status = eb_inverse_transform(transformer, parquet_data, parquet_size, &original_data, &original_size);

            if (inv_status == EB_SUCCESS && original_data) {
                FILE *raw_fp = fopen(temp_object_path, "wb");
                if (raw_fp) {
                    fwrite(original_data, 1, original_size, raw_fp);
                    fclose(raw_fp);
                }
                free(original_data);
            }
        }

        // Prepare a temporary metadata file for downstream parsing
        char meta_template[] = "/tmp/embr_meta_XXXXXX";
        int fd_meta = mkstemp(meta_template);
        if (fd_meta < 0) {
            unlink(parquet_template);
            free(parquet_data);
            continue;
        }
        close(fd_meta);
        strncpy(temp_meta_path, meta_template, PATH_MAX);

        // Extract metadata JSON from the Parquet buffer
        char *metadata_json = eb_parquet_extract_metadata_json(parquet_data, parquet_size);
        free(parquet_data);
        if (!metadata_json) {
            unlink(parquet_template);
            continue;
        }

        // Parse metadata JSON and convert to key=value format
        {
            char source_file_buf[PATH_MAX] = {0};
            char file_type_buf[32] = {0};
            char provider_buf[32] = {0};
            const char *p;
            // Extract 'source'
            p = strstr(metadata_json, "\"source\":\"");
            if (p) {
                p += strlen("\"source\":\"");
                const char *q = strchr(p, '"');
                if (q) {
                    size_t len = q - p;
                    if (len >= sizeof(source_file_buf)) len = sizeof(source_file_buf) - 1;
                    strncpy(source_file_buf, p, len);
                    source_file_buf[len] = '\0';
                }
            }
            // Extract 'file_type'
            p = strstr(metadata_json, "\"file_type\":\"");
            if (p) {
                p += strlen("\"file_type\":\"");
                const char *q = strchr(p, '"');
                if (q) {
                    size_t len = q - p;
                    if (len >= sizeof(file_type_buf)) len = sizeof(file_type_buf) - 1;
                    strncpy(file_type_buf, p, len);
                    file_type_buf[len] = '\0';
                }
            }
            // Extract 'provider' or fallback to 'model'
            p = strstr(metadata_json, "\"provider\":\"");
            if (!p) {
                p = strstr(metadata_json, "\"model\":\"");
            }
            if (p) {
                if (strstr(p, "\"provider\":\"") == p) {
                    p += strlen("\"provider\":\"");
                } else {
                    p += strlen("\"model\":\"");
                }
                const char *q = strchr(p, '"');
                if (q) {
                    size_t len = q - p;
                    if (len >= sizeof(provider_buf)) len = sizeof(provider_buf) - 1;
                    strncpy(provider_buf, p, len);
                    provider_buf[len] = '\0';
                }
            }
            FILE *meta_fp2 = fopen(temp_meta_path, "w");
            if (meta_fp2) {
                if (source_file_buf[0]) fprintf(meta_fp2, "source_file=%s\n", source_file_buf);
                if (file_type_buf[0])   fprintf(meta_fp2, "file_type=%s\n", file_type_buf);
                if (provider_buf[0])    fprintf(meta_fp2, "model=%s\n", provider_buf);
                fclose(meta_fp2);
            }
        }
        free(metadata_json);

        downloaded = true;
        break;
    }
    for (int i = 0; i < rem_count; i++) free(remotes[i]);
    free(remotes);
    eb_remote_shutdown();
    return downloaded;
}

/*
 * Handle getting a file by hash
 */
static int get_embedding_by_hash(
    const char *dest_dir,
    const char *hash,
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
    DEBUG_INFO("About to read metadata: src_meta='%s'", src_meta);
    if (!read_metadata(src_meta, source_file, file_type, provider)) {
        if (!quiet) fprintf(stderr, "Error: Failed to read metadata for hash '%s'\n", hash);
        DEBUG_INFO("read_metadata failed for src_meta='%s'", src_meta);
        return 1;
    }
    DEBUG_INFO("Metadata read: source_file='%s', file_type='%s', model='%s'", source_file, file_type, provider);
    // Always generate output filename in destination_dir
    char final_output[PATH_MAX] = {0};
    {
        char *src_dup = strdup(source_file);
        char *src_basename = basename(src_dup);
        DEBUG_INFO("src_basename='%s' from source_file='%s'", src_basename, source_file);
        char *dot = strrchr(src_basename, '.');
        if (dot) *dot = '\0';
        snprintf(final_output, sizeof(final_output), "%s/%s_%s.%s",
                 dest_dir, src_basename, hash, file_type);
        DEBUG_INFO("final_output='%s'", final_output);
        free(src_dup);
    }
    // Check if output file already exists
    if (!force && access(final_output, F_OK) == 0) {
        if (!quiet) fprintf(stderr, "Error: Output file '%s' already exists. Use --force to overwrite.\n", final_output);
        DEBUG_INFO("Output file already exists: '%s'", final_output);
        return 1;
    }
    // Copy the embedding file
    DEBUG_INFO("About to copy src_object='%s' to final_output='%s'", src_object, final_output);
    if (fs_copy_file(src_object, final_output) != 0) {
        if (!quiet) fprintf(stderr, "Error: Failed to copy embedding to '%s'\n", final_output);
        DEBUG_INFO("fs_copy_file failed: src_object='%s', final_output='%s'", src_object, final_output);
        return 1;
    }
    DEBUG_INFO("File copy succeeded: '%s'", final_output);
    
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
    // argv[0] is "get", skip it
    if (argc > 0) {
        argc--;
        argv++;
    }
    char *dest_dir = NULL;
    char *hash = NULL;
    bool force = false;
    bool verbose = false;
    bool quiet = false;
    
    // Parse flags (options start with '-')
    int idx = 0;
    for (; idx < argc; idx++) {
        const char *arg = argv[idx];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
            force = true;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            quiet = true;
        } else {
            break;  // first non-flag
        }
    }
    // Next argument is output directory
    if (idx < argc) {
        dest_dir = argv[idx++];
    }
    // Next argument is hash
    if (idx < argc) {
        hash = argv[idx++];
    }
    // Too many arguments?
    if (idx < argc) {
        fprintf(stderr, "Error: Too many arguments\n");
        print_usage();
        return 1;
    }
    // Missing required arguments?
    if (!dest_dir || !hash) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage();
        return 1;
    }
    return get_embedding_by_hash(dest_dir, hash, force, verbose, quiet);
} 