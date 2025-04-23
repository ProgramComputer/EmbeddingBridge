/*
 * EmbeddingBridge - Pull Command Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "status.h"
#include "debug.h"
#include "parquet_transformer.h"
#include "remote.h"
#include "transport.h"
#include "../core/path_utils.h"
#include <sys/stat.h>
#include <jansson.h>
#include <dirent.h>
#include "../core/fs.h"

int cmd_pull(int argc, char **argv) {
    // Help/usage
    if (argc < 2 || (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))) {
        printf("Usage: embr pull [options] <remote> [<set>]\n");
        printf("Download embedding objects from a remote repository.\n");
        printf("\nOptions:\n");
        printf("  --prune      Delete local objects not present on remote (confirmation required)\n");
        printf("  --help, -h    Show this help message\n");
        printf("\nExamples:\n");
        printf("  embr pull s3://mybucket embeddings\n");
        printf("  embr pull --prune s3://mybucket embeddings\n");
        return 0;
    }
    // Parse arguments: embr pull [options] <remote> [<set>]
    bool prune_flag = false;
    const char *remote = NULL;
    const char *set_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--prune") == 0) {
            prune_flag = true;
        } else if (!remote) {
            remote = argv[i];
        } else if (!set_name) {
            set_name = argv[i];
        }
    }
    if (!remote) {
        fprintf(stderr, "Error: Missing remote name\n");
        printf("Usage: embr pull [options] <remote> [<set>]\n");
        return 1;
    }
    if (!set_name) {
        // Read current active set from .embr/HEAD
        FILE *head_file = fopen(".embr/HEAD", "r");
        if (head_file) {
            char current_set[64] = {0};
            if (fgets(current_set, sizeof(current_set), head_file)) {
                size_t len = strlen(current_set);
                if (len > 0 && current_set[len-1] == '\n') current_set[len-1] = '\0';
                char *last_slash = strrchr(current_set, '/');
                if (last_slash) set_name = strdup(last_slash + 1);
                else set_name = strdup(current_set);
            }
            fclose(head_file);
        }
        if (!set_name) set_name = "main";
    }
    printf("Pulling set '%s' from remote '%s'...\n", set_name, remote);

    // Ensure per-set directory, index, log, and refs/models exist
    {
        char* index_path = get_current_set_index_path();
        if (index_path) {
            // Create or open index file to ensure it exists
            FILE* idx_f = fopen(index_path, "a");
            if (idx_f) fclose(idx_f);
            // Create or open log file
            char* log_path = get_current_set_log_path();
            if (log_path) {
                FILE* log_f = fopen(log_path, "a");
                if (log_f) fclose(log_f);
                free(log_path);
            }
            // Ensure refs/models directory exists
            char* refs_dir = get_current_set_model_refs_dir();
            if (refs_dir) {
                fs_mkdir_p(refs_dir, 0755); /* create intermediate directories */
                free(refs_dir);
            }
            free(index_path);
        }
    }

    // 1. Get remote URL for set
    char remote_url[1024] = {0};
    eb_status_t info_status = eb_remote_info(remote, remote_url, sizeof(remote_url), NULL, NULL, NULL, 0);
    if (info_status != EB_SUCCESS) {
        fprintf(stderr, "Error: Could not get remote URL for '%s'\n", remote);
        return 1;
    }
    snprintf(remote_url + strlen(remote_url), sizeof(remote_url) - strlen(remote_url), "/sets/%s", set_name);
    char documents_url[1024];
    snprintf(documents_url, sizeof(documents_url), "%s/documents/", remote_url);
    eb_transport_t *transport = transport_open(remote_url);         // For metadata.json
    if (!transport) {
        fprintf(stderr, "Error: Could not open transport for '%s'\n", remote_url);
        return 1;
    }
    if (transport_connect(transport) != EB_SUCCESS) {
        fprintf(stderr, "Error: Could not connect to remote '%s': %s\n", remote_url, transport_get_error(transport));
        transport_close(transport);
        return 1;
    }
    // DEBUG: show which URLs we are connecting to
    DEBUG_PRINT("pull: remote_url = %s", remote_url);
    DEBUG_PRINT("pull: documents_url = %s", documents_url);

    // 2. List all remote .parquet files using eb_remote_list_files
    char documents_prefix[256];
    snprintf(documents_prefix, sizeof(documents_prefix), "sets/%s", set_name);
    char **remote_refs = NULL;
    size_t remote_count = 0;
    eb_status_t list_status = eb_remote_list_files(remote, documents_prefix, &remote_refs, &remote_count);
    DEBUG_INFO("Remote file list returned by eb_remote_list_files:");
    for (size_t i = 0; i < remote_count; ++i) {
        DEBUG_INFO("  remote_refs[%zu] = %s", i, remote_refs[i]);
    }
    if (list_status != EB_SUCCESS) {
        fprintf(stderr, "Error: Could not list remote files for set '%s' (documents) via eb_remote_list_files\n", set_name);
        transport_close(transport);
        return 1;
    }
    // 2a. Reconstruct index, log, and refs/models from remote metadata.json
    for (size_t i = 0; i < remote_count; ++i) {
        const char *ref = remote_refs[i];
        const char *bname = strrchr(ref, '/');
        bname = bname ? bname + 1 : ref;
        DEBUG_INFO("Checking remote file: %s (basename: %s)", ref, bname);
        if (strcmp(bname, "metadata.json") == 0) {
            DEBUG_INFO("metadata.json: entered processing block for %s", ref);
            size_t buf_size = 1024 * 1024;
            char *buf = malloc(buf_size);
            DEBUG_INFO("metadata.json: allocated buffer %p (size=%zu)", buf, buf_size);
            size_t got = 0;
            transport->target_path = strdup(ref);
            if (transport_receive_data(transport, buf, buf_size, &got) == 0) {
                DEBUG_INFO("metadata.json: transport_receive_data succeeded (got %zu bytes)", got);
                json_error_t jerr;
                json_t *root = json_loadb(buf, got, 0, &jerr);
                if (root) {
                    DEBUG_INFO("metadata.json: json_loadb succeeded, checking if reconstruction is needed");
                    // --- Begin: Check if index, log, or refs/models are missing or empty ---
                    int need_reconstruct = 0;
                    char *idx = get_current_set_index_path();
                    char *lg = get_current_set_log_path();
                    char *rd = get_current_set_model_refs_dir();
                    struct stat st;
                    // Check index
                    if (!idx || stat(idx, &st) != 0 || st.st_size == 0) need_reconstruct = 1;
                    // Check log
                    if (!lg || stat(lg, &st) != 0 || st.st_size == 0) need_reconstruct = 1;
                    // Check refs/models dir
                    int refs_empty = 0;
                    DIR *d = rd ? opendir(rd) : NULL;
                    if (!rd || !d) {
                        need_reconstruct = 1;
                    } else {
                        refs_empty = 1;
                        struct dirent *entry;
                        while ((entry = readdir(d)) != NULL) {
                            if (entry->d_name[0] != '.') { refs_empty = 0; break; }
                        }
                        closedir(d);
                        if (refs_empty) need_reconstruct = 1;
                    }
                    DEBUG_INFO("metadata.json: need_reconstruct = %d", need_reconstruct);
                    // --- End check ---
                    if (need_reconstruct) {
                        // Rebuild index file
                        if (idx) {
                            FILE *f = fopen(idx, "w");
                            json_t *arr = json_object_get(root, "index");
                            if (f && json_is_array(arr)) {
                                size_t n = json_array_size(arr);
                                for (size_t j = 0; j < n; ++j) {
                                    json_t *o = json_array_get(arr, j);
                                    const char *h = json_string_value(json_object_get(o, "hash"));
                                    const char *p = json_string_value(json_object_get(o, "path"));
                                    if (h && p) fprintf(f, "%s %s\n", h, p);
                                }
                            }
                            if (f) fclose(f);
                        }
                        // Rebuild log file
                        if (lg) {
                            FILE *f = fopen(lg, "w");
                            json_t *objs = json_object_get(root, "objects");
                            if (f && json_is_array(objs)) {
                                size_t m = json_array_size(objs);
                                for (size_t j = 0; j < m; ++j) {
                                    json_t *o = json_array_get(objs, j);
                                    long long ts = json_integer_value(json_object_get(o, "created"));
                                    const char *h = json_string_value(json_object_get(o, "hash"));
                                    const char *p = json_string_value(json_object_get(o, "path"));
                                    const char *md = json_string_value(json_object_get(o, "model"));
                                    if (h && p) {
                                        if (md) fprintf(f, "%lld %s %s %s\n", ts, h, p, md);
                                        else fprintf(f, "%lld %s %s\n", ts, h, p);
                                    }
                                }
                            }
                            if (f) fclose(f);
                        }
                        // Rebuild refs/models files
                        if (rd) {
                            fs_mkdir_p(rd, 0755);
                            json_t *rfs = json_object_get(root, "refs");
                            if (!rfs) {
                                DEBUG_INFO("metadata.json: 'refs' key missing");
                            } else if (!json_is_object(rfs)) {
                                DEBUG_INFO("metadata.json: 'refs' is not an object");
                            } else {
                                size_t refs_count = json_object_size(rfs);
                                DEBUG_INFO("metadata.json: 'refs' contains %zu entries", refs_count);
                            }
                            json_t *idx_arr2 = json_object_get(root, "index");
                            if (json_is_object(rfs) && json_is_array(idx_arr2)) {
                                const char *model;
                                json_t *val;
                                json_object_foreach(rfs, model, val) {
                                    const char *h = json_string_value(val);
                                    const char *src = "";
                                    size_t ksz = json_array_size(idx_arr2);
                                    for (size_t k = 0; k < ksz; ++k) {
                                        json_t *e = json_array_get(idx_arr2, k);
                                        if (strcmp(json_string_value(json_object_get(e, "hash")), h) == 0) {
                                            src = json_string_value(json_object_get(e, "path"));
                                            break;
                                        }
                                    }
                                    char pathbuf[PATH_MAX];
                                    snprintf(pathbuf, sizeof(pathbuf), "%s/%s", rd, model);
                                    FILE *rf = fopen(pathbuf, "w");
                                    if (rf) { fprintf(rf, "%s %s\n", h, src); fclose(rf); }
                                }
                            }
                        }
                    }
                    if (idx) free(idx);
                    if (lg) free(lg);
                    if (rd) free(rd);
                    json_decref(root);
                } else {
                    DEBUG_INFO("metadata.json: json_loadb failed: %s", jerr.text);
                }
            } else {
                DEBUG_INFO("metadata.json: transport_receive_data failed for %s", ref);
            }
            free(buf);
            break;
        }
        DEBUG_INFO("Finished checking remote file: %s (basename: %s)", ref, bname);
    }
    // 3. Build set of local hashes
    char local_hashes[4096][128];
    size_t local_hash_count = 0;
    // Scan .embr/objects directory for existing raw/meta files
    DIR *obj_dir = opendir(".embr/objects");
    if (obj_dir) {
        struct dirent *entry;
        while ((entry = readdir(obj_dir)) != NULL && local_hash_count < 4096) {
            const char *name = entry->d_name;
            const char *dot = strrchr(name, '.');
            if (!dot) continue;
            if (strcmp(dot, ".raw") == 0 || strcmp(dot, ".meta") == 0) {
                size_t hlen = dot - name;
                if (hlen > 0 && hlen < sizeof(local_hashes[0])) {
                    char hash[128];
                    memcpy(hash, name, hlen);
                    hash[hlen] = '\0';
                    bool exists = false;
                    for (size_t k = 0; k < local_hash_count; ++k) {
                        if (strcmp(local_hashes[k], hash) == 0) { exists = true; break; }
                    }
                    if (!exists) {
                        strncpy(local_hashes[local_hash_count++], hash, sizeof(local_hashes[0]));
                    }
                }
            }
        }
        closedir(obj_dir);
    }
    // 4. For each remote file, if not present locally, download and inverse-transform
    size_t downloaded = 0;
    for (size_t i = 0; i < remote_count; ++i) {
        const char *remote_file = remote_refs[i];
        // Only process <hash>.parquet files
        const char *slash = strrchr(remote_file, '/');
        const char *basename = slash ? slash + 1 : remote_file;
        // Skip non-.parquet files
        const char *dot = strrchr(basename, '.');
        if (!dot || strcmp(dot, ".parquet") != 0) continue;
        char hash[128] = {0};
        if (sscanf(basename, "%127[^.]", hash) != 1) continue;
        bool found = false;
        for (size_t j = 0; j < local_hash_count; ++j) {
            if (strcmp(hash, local_hashes[j]) == 0) {
                found = true;
                break;
            }
        }
        if (found) continue; // Already present locally
        // Download file
        char s3_path[1024];
        snprintf(s3_path, sizeof(s3_path), "%s", remote_file);
        transport->target_path = strdup(remote_file); // Set the file to download
        size_t max_size = 32 * 1024 * 1024; // 32MB max file size (adjust as needed)
        void *parquet_data = malloc(max_size);
        if (!parquet_data) {
            fprintf(stderr, "Error: Memory allocation failed for '%s'\n", s3_path);
            continue;
        }
        size_t received = 0;
        if (transport_receive_data(transport, parquet_data, max_size, &received) != 0) {
            fprintf(stderr, "Error: Failed to download '%s'\n", s3_path);
            free(parquet_data);
            continue;
        }
        size_t parquet_size = received;
        // Inverse-transform Parquet to original format
        eb_transformer_t *transformer = eb_find_transformer_by_format("parquet");
        if (!transformer) {
            fprintf(stderr, "Error: Parquet transformer not found\n");
            free(parquet_data);
            continue;
        }
        void *original_data = NULL;
        size_t original_size = 0;
        eb_status_t inv_status = eb_inverse_transform(transformer, parquet_data, parquet_size, &original_data, &original_size);
        if (inv_status != EB_SUCCESS) {
            fprintf(stderr, "Error: Failed to inverse-transform '%s'\n", s3_path);
            free(parquet_data);
            continue;
        }
        // Extract metadata from Parquet
        char *metadata_json = eb_parquet_extract_metadata_json(parquet_data, parquet_size);
        if (metadata_json) {
            // Parse metadata JSON and convert to key=value format
            char source_file_buf[1024] = {0};
            char file_type_buf[32] = {0};
            char model_buf[32] = {0};
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
            // Extract 'model'
            p = strstr(metadata_json, "\"model\":\"");
            if (p) {
                p += strlen("\"model\":\"");
                const char *q = strchr(p, '"');
                if (q) {
                    size_t len = q - p;
                    if (len >= sizeof(model_buf)) len = sizeof(model_buf) - 1;
                    strncpy(model_buf, p, len);
                    model_buf[len] = '\0';
                }
            }
            // Write converted metadata to .meta file
            char meta_path[1024];
            snprintf(meta_path, sizeof(meta_path), ".embr/objects/%s.meta", hash);
            FILE *meta_file2 = fopen(meta_path, "w");
            if (meta_file2) {
                if (source_file_buf[0]) fprintf(meta_file2, "source_file=%s\n", source_file_buf);
                if (file_type_buf[0])   fprintf(meta_file2, "file_type=%s\n", file_type_buf);
                if (model_buf[0])       fprintf(meta_file2, "model=%s\n", model_buf);
                fclose(meta_file2);
            }
            free(metadata_json);
        }
        // Write original data to .embr/objects/<hash>.raw
        char raw_path[1024];
        snprintf(raw_path, sizeof(raw_path), ".embr/objects/%s.raw", hash);
        FILE *raw_file = fopen(raw_path, "wb");
        if (raw_file) {
            fwrite(original_data, 1, original_size, raw_file);
            fclose(raw_file);
        }
        free(original_data);
        free(parquet_data);
        downloaded++;
    }
    // Free remote_refs
    for (size_t i = 0; i < remote_count; ++i) free(remote_refs[i]);
    free(remote_refs);
    transport_close(transport);
    printf("Downloaded %zu new objects from set '%s' on remote '%s'\n", downloaded, set_name, remote);
    // PRUNE LOGIC
    if (prune_flag) {
        // 1. Build set of remote hashes (parquet files)
        char remote_hashes[4096][128];
        size_t remote_hash_count = 0;
        for (size_t i = 0; i < remote_count; ++i) {
            const char *remote_file = remote_refs[i];
            const char *slash = strrchr(remote_file, '/');
            const char *basename = slash ? slash + 1 : remote_file;
            char hash[128] = {0};
            if (sscanf(basename, "%127[^.]", hash) == 1) {
                strncpy(remote_hashes[remote_hash_count++], hash, 128);
            }
        }
        // 2. local_hashes[local_hash_count][128] already built above
        // 3. Find local-only hashes
        bool to_delete[4096] = {0};
        size_t delete_count = 0;
        for (size_t i = 0; i < local_hash_count; ++i) {
            bool found = false;
            for (size_t j = 0; j < remote_hash_count; ++j) {
                if (strcmp(local_hashes[i], remote_hashes[j]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                to_delete[i] = true;
                delete_count++;
            }
        }
        if (delete_count == 0) {
            printf("No local objects to prune.\n");
        } else {
            printf("The following local objects are not present on the remote and will be deleted:\n");
            for (size_t i = 0; i < local_hash_count; ++i) {
                if (to_delete[i]) {
                    printf("  .embr/objects/%s.raw\n", local_hashes[i]);
                    printf("  .embr/objects/%s.meta\n", local_hashes[i]);
                }
            }
            printf("Proceed? [y/N]: ");
            char response[8] = {0};
            if (!fgets(response, sizeof(response), stdin) || (response[0] != 'y' && response[0] != 'Y')) {
                printf("Prune cancelled.\n");
            } else {
                for (size_t i = 0; i < local_hash_count; ++i) {
                    if (to_delete[i]) {
                        char raw_path[256], meta_path[256];
                        snprintf(raw_path, sizeof(raw_path), ".embr/objects/%s.raw", local_hashes[i]);
                        snprintf(meta_path, sizeof(meta_path), ".embr/objects/%s.meta", local_hashes[i]);
                        remove(raw_path);
                        remove(meta_path);
                    }
                }
                printf("Pruned %zu local objects.\n", delete_count);
            }
        }
    }
    return 0;
} 