/*
 * EmbeddingBridge - Error Handling
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_ERROR_H
#define EB_ERROR_H

/* Status codes */
typedef enum eb_status {
    EB_SUCCESS = 0,
    EB_ERROR_INVALID_INPUT = -1,
    EB_ERROR_MEMORY_ALLOCATION = -2,
    EB_ERROR_FILE_IO = -3,
    EB_ERROR_NOT_FOUND = -4,
    EB_ERROR_PATH_TOO_LONG = -5,
    EB_ERROR_NOT_INITIALIZED = -6,
    EB_ERROR_HASH_MISMATCH = -7,
    EB_ERROR_DIMENSION_MISMATCH = -8,
    EB_ERROR_COMPUTATION_FAILED = -9,
    EB_ERROR_HASH_AMBIGUOUS = -10    // New error code for ambiguous hashes
} eb_status_t;

/* Convert error code to string */
const char* eb_status_str(eb_status_t status);

/* Get error message for system error */
const char* eb_strerror(int errnum);

/* Set error state */
void eb_set_error(eb_status_t status, const char* message);

#endif /* EB_ERROR_H */ 