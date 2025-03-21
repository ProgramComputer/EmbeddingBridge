/*
 * EmbeddingBridge - Filesystem Utilities
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_FS_H
#define EB_FS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include "status.h"

/**
 * Create directory and all parent directories if they don't exist
 *
 * @param path Directory path to create
 * @param mode Permission mode (e.g., 0755)
 * @return 0 on success, -1 on error
 */
static inline int fs_mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;
    int ret;

    if (!path || !*path) return -1;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            ret = mkdir(tmp, mode);
            if (ret != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    ret = mkdir(tmp, mode);
    return (ret != 0 && errno != EEXIST) ? -1 : 0;
}

/**
 * Copy a file from source to destination
 *
 * @param src Source file path
 * @param dst Destination file path
 * @return 0 on success, -1 on error
 */
static inline int fs_copy_file(const char* src, const char* dst) {
    FILE *fsrc = fopen(src, "rb");
    if (!fsrc) {
        return -1;
    }

    FILE *fdst = fopen(dst, "wb");
    if (!fdst) {
        fclose(fsrc);
        return -1;
    }

    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fsrc)) > 0) {
        if (fwrite(buffer, 1, n, fdst) != n) {
            fclose(fsrc);
            fclose(fdst);
            return -1;
        }
    }

    fclose(fsrc);
    fclose(fdst);
    return 0;
}

#endif /* EB_FS_H */ 