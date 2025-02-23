/*
 * EmbeddingBridge - Hash Utilities
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_HASH_UTILS_H
#define EB_HASH_UTILS_H

#include <string.h>

/* Get shortened version of a hash (first 7 characters) */
static inline char* get_short_hash(const char* full_hash) {
        static char short_hash[8];  // 7 chars + null terminator
        strncpy(short_hash, full_hash, 7);
        short_hash[7] = '\0';
        return short_hash;
}

/* Check if a short hash matches a full hash */
static inline bool is_hash_prefix(const char* prefix, const char* full_hash) {
        return strncmp(prefix, full_hash, strlen(prefix)) == 0;
}

#endif /* EB_HASH_UTILS_H */ 