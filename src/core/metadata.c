/*
 * EmbeddingBridge - Metadata Implementation
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
#include <time.h>
#include "metadata.h"
#include "error.h"

eb_status_t eb_write_metadata(const char* path, const char* source, const char* model) {
    FILE* f = fopen(path, "w");
    if (!f)
        return EB_ERROR_FILE_IO;

    time_t now;
    time(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    if (fprintf(f, "source: %s\n", source) < 0) goto write_error;
    if (fprintf(f, "timestamp: %s\n", timestamp) < 0) goto write_error;
    if (fprintf(f, "model: %s\n", model) < 0) goto write_error;

    if (fclose(f) == EOF) {
        return EB_ERROR_FILE_IO;
    }
    return EB_SUCCESS;

write_error:
    if (f) fclose(f);
    return EB_ERROR_FILE_IO;
}

eb_status_t eb_read_metadata(const char* path, char** source, char** model) {
    FILE* f = fopen(path, "r");
    if (!f)
        return EB_ERROR_FILE_IO;

    char line[1024];
    char* s = NULL;
    char* m = NULL;
    eb_status_t status = EB_SUCCESS;

    while (fgets(line, sizeof(line), f)) {
        char* key = strtok(line, ":");
        if (!key)
            continue;

        char* value = strtok(NULL, "\n");
        if (!value)
            continue;

        // Skip leading whitespace in value
        while (*value == ' ')
            value++;

        if (strcmp(key, "source") == 0) {
            char* temp_s = strdup(value);
            if (!temp_s) {
                status = EB_ERROR_MEMORY_ALLOCATION;
                goto cleanup;
            }
            free(s);
            s = temp_s;
        } else if (strcmp(key, "model") == 0) {
            char* temp_m = strdup(value);
            if (!temp_m) {
                status = EB_ERROR_MEMORY_ALLOCATION;
                goto cleanup;
            }
            free(m);
            m = temp_m;
        }
    }

    if (fclose(f) == EOF) {
        status = EB_ERROR_FILE_IO;
        goto cleanup;
    }

    if (!s || !m) {
        status = EB_ERROR_INVALID_INPUT;
        goto cleanup;
    }

    *source = s;
    *model = m;
    return EB_SUCCESS;

cleanup:
    free(s);
    free(m);
    if (f) fclose(f);
    return status;
} 