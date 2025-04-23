/*
 * EmbeddingBridge - Push Command Implementation
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
#include "remote.h"
#include "set.h"
#include "../core/path_utils.h"

int cmd_push(int argc, char **argv) {
    // Help/usage
    if (argc < 2 || (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))) {
        printf("Usage: embr push [options] <remote> [<set>]\n");
        printf("Upload embedding objects to a remote repository.\n");
        printf("\nOptions:\n");
        printf("  --force       Force remote to match local (destructive)\n");
        printf("  --help, -h    Show this help message\n");
        printf("\nExamples:\n");
        printf("  embr push s3://mybucket embeddings\n");
        printf("  embr push --force s3://mybucket embeddings\n");
        return 0;
    }
    // Parse arguments: embr push [options] <remote> [<set>]
    const char *remote = NULL;
    const char *set_name = NULL;
    bool force = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (!remote) {
            remote = argv[i];
        } else if (!set_name) {
            set_name = argv[i];
        }
    }
    if (!remote) {
        fprintf(stderr, "Error: Missing remote name\n");
        printf("Usage: embr push [options] <remote> [<set>]\n");
        return 1;
    }
    if (!set_name) {
        // Determine current set via get_current_set()
        char current_set[64] = {0};
        eb_status_t status = get_current_set(current_set, sizeof(current_set));
        if (status != EB_SUCCESS) {
            fprintf(stderr, "Error: Could not determine current set\n");
            return 1;
        }
        set_name = strdup(current_set);
    }
    printf("Pushing set '%s' to remote '%s'...\n", set_name, remote);
    if (force) {
        printf("[WARNING] --force: Remote set will be made to match local set. Extra remote files will be deleted.\n");
        // List all remote files in the set and delete any not present locally
        char remote_set_path[256];
        snprintf(remote_set_path, sizeof(remote_set_path), "sets/%s", set_name);
        char **remote_files = NULL;
        size_t remote_file_count = 0;
        eb_status_t list_status = eb_remote_list_files(remote, remote_set_path, &remote_files, &remote_file_count);
        if (list_status != EB_SUCCESS) {
            fprintf(stderr, "Error: Could not list remote files for set '%s' on remote '%s'\n", set_name, remote);
            // Continue, but warn
        }
        // Build set of local hashes
        char* log_path_local = get_current_set_log_path();
        FILE *log_file_local = fopen(log_path_local, "r");
        free(log_path_local);
        if (!log_file_local) {
            fprintf(stderr, "Error: Could not open log file for local set\n");
            // Free remote_files
            if (remote_files) {
                for (size_t i = 0; i < remote_file_count; i++) free(remote_files[i]);
                free(remote_files);
            }
            return 1;
        }
        // Use a simple array for local hashes (could use a hash set for large sets)
        char local_hashes[1024][128];
        size_t local_hash_count = 0;
        char line_local[1024];
        while (fgets(line_local, sizeof(line_local), log_file_local)) {
            size_t len = strlen(line_local);
            if (len > 0 && line_local[len-1] == '\n') { line_local[len-1] = '\0'; len--; }
            if (len == 0) continue;
            char timestamp[32] = {0};
            char hash[128] = {0};
            char filename[896] = {0};
            char model[128] = {0};
            if (sscanf(line_local, "%31s %127s %895s %127s", timestamp, hash, filename, model) < 2) continue;
            if (local_hash_count < 1024) {
                strncpy(local_hashes[local_hash_count++], hash, 128);
            }
        }
        fclose(log_file_local);
        // Find remote files not present locally
        const char *to_delete[1024];
        size_t to_delete_count = 0;
        for (size_t i = 0; i < remote_file_count; i++) {
            // Extract hash from remote file name (assume format <hash>.raw or <hash>.meta)
            char *dot = strrchr(remote_files[i], '.');
            if (!dot) continue;
            size_t hash_len = dot - remote_files[i];
            if (hash_len == 0 || hash_len >= 128) continue;
            char remote_hash[128] = {0};
            strncpy(remote_hash, remote_files[i], hash_len);
            remote_hash[hash_len] = '\0';
            // Check if this hash is in local_hashes
            bool found = false;
            for (size_t j = 0; j < local_hash_count; j++) {
                if (strcmp(remote_hash, local_hashes[j]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                to_delete[to_delete_count++] = remote_files[i];
            }
        }
        if (to_delete_count > 0) {
            printf("Deleting %zu remote files not present locally...\n", to_delete_count);
            eb_status_t del_status = eb_remote_delete_files(remote, remote_set_path, to_delete, to_delete_count);
            if (del_status != EB_SUCCESS) {
                fprintf(stderr, "Error: Failed to delete some remote files (status %d)\n", del_status);
            }
        } else {
            printf("No extra remote files to delete.\n");
        }
        // Free remote_files
        if (remote_files) {
            for (size_t i = 0; i < remote_file_count; i++) free(remote_files[i]);
            free(remote_files);
        }
    }
    char* log_path = get_current_set_log_path();
    FILE *log_file = fopen(log_path, "r");
    free(log_path);
    if (!log_file) {
        fprintf(stderr, "Error: Could not open log file\n");
        return 1;
    }
    char line[1024];
    bool any_pushed = false;
    eb_status_t last_status = EB_ERROR_NOT_FOUND;
    if (fgets(line, sizeof(line), log_file) == NULL) {
        fclose(log_file);
        fprintf(stderr, "Error: No content to push. Log file is empty. Use 'eb store' to add embeddings.\n");
        return 1;
    }
    rewind(log_file);
    while (fgets(line, sizeof(line), log_file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') { line[len-1] = '\0'; len--; }
        if (len == 0) continue;
        // log format: timestamp hash filename model
        char timestamp[32] = {0};
        char hash[128] = {0};
        char filename[896] = {0};
        char model[128] = {0};
        if (sscanf(line, "%31s %127s %895s %127s", timestamp, hash, filename, model) < 2) continue;
        char raw_path[1024];
        snprintf(raw_path, sizeof(raw_path), ".embr/objects/%s.raw", hash);
        FILE *raw_file = fopen(raw_path, "rb");
        if (!raw_file) continue;
        fseek(raw_file, 0, SEEK_END);
        long raw_size = ftell(raw_file);
        fseek(raw_file, 0, SEEK_SET);
        void *raw_data = malloc(raw_size);
        if (!raw_data) { fclose(raw_file); continue; }
        if (fread(raw_data, 1, raw_size, raw_file) != (size_t)raw_size) { free(raw_data); fclose(raw_file); continue; }
        fclose(raw_file);
        char embedding_path[1024];
        snprintf(embedding_path, sizeof(embedding_path), "sets/%s", set_name);
        last_status = eb_remote_push(remote, raw_data, raw_size, embedding_path, hash);
        free(raw_data);
        if (last_status == EB_SUCCESS) any_pushed = true;
    }
    fclose(log_file);
    if (any_pushed) {
        printf("Successfully pushed set '%s' to remote '%s'\n", set_name, remote);
        return 0;
    } else if (last_status == EB_ERROR_NOT_FOUND) {
        fprintf(stderr, "Error: Failed to push to remote '%s'\n", remote);
        cli_info("Remote '%s' does not exist. Add it with: embr remote add %s <url>", remote, remote);
        return 1;
    } else {
        fprintf(stderr, "Error: Failed to push to remote '%s'\n", remote);
        return 1;
    }
} 