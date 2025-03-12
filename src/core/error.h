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
    EB_ERROR_HASH_AMBIGUOUS = -10,   // New error code for ambiguous hashes
    
    /* General errors */
    EB_ERROR_INVALID_PARAMETER = -20,
    EB_ERROR_MEMORY = -21,
    EB_ERROR_IO = -22,
    EB_ERROR_ALREADY_EXISTS = -23,
    EB_ERROR_INVALID_NAME = -24,
    EB_ERROR_NOT_IMPLEMENTED = -25,
    EB_ERROR_LOCK_FAILED = -26,      // Error for lock acquisition failures
    EB_ERROR_REFERENCED = -27,       // Error for objects that are still referenced
    EB_ERROR_CONNECTION_FAILED = -28, // Error for connection failures
    EB_ERROR_UNSUPPORTED = -29,      // Error for unsupported operations or protocols
    EB_ERROR_INITIALIZATION = -30,   // Error for initialization failures
    EB_ERROR_NOT_CONNECTED = -31,    // Error for operations requiring connection
    EB_ERROR_AUTHENTICATION = -32,   // Error for authentication failures
    EB_ERROR_INVALID_REPOSITORY = -33, // Error for invalid repository structure
    EB_ERROR_CONNECTION_CLOSED = -34, // Error for closed connections
    
    /* Remote operation errors */
    EB_ERROR_REMOTE_NOTFOUND = -100,
    EB_ERROR_REMOTE_CONNECTION = -101,
    EB_ERROR_REMOTE_AUTH = -102,
    EB_ERROR_REMOTE_PROTOCOL = -103,
    EB_ERROR_REMOTE_REJECTED = -104,
    EB_ERROR_REMOTE_CONFLICT = -105,
    EB_ERROR_REMOTE_TIMEOUT = -106
} eb_status_t;

/* Convert error code to string */
const char* eb_status_str(eb_status_t status);

/* Get error message for system error */
const char* eb_strerror(int errnum);

/* Set error state */
void eb_set_error(eb_status_t status, const char* message);

#endif /* EB_ERROR_H */ 