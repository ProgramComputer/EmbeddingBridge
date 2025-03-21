/*
 * EmbeddingBridge - Status Codes
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_STATUS_H
#define EB_STATUS_H

/**
 * Status/error codes used throughout the codebase
 */
typedef enum {
    EB_SUCCESS = 0,                /* Operation successful */
    
    /* Basic error codes (1-19) */
    EB_ERROR_GENERIC = -1,         /* Generic error */
    EB_ERROR_MEMORY = -2,          /* Memory allocation failure */
    EB_ERROR_OUT_OF_MEMORY = -2,   /* Alias for memory allocation failure */
    EB_ERROR_IO = -3,              /* I/O error */
    EB_ERROR_INVALID_PARAMETER = -4, /* Invalid parameter */
    EB_ERROR_NOT_FOUND = -5,       /* Item not found */
    EB_ERROR_ALREADY_EXISTS = -6,  /* Item already exists */
    EB_ERROR_BUFFER_TOO_SMALL = -7, /* Buffer too small */
    EB_ERROR_INVALID_FORMAT = -8,  /* Invalid format */
    EB_ERROR_UNSUPPORTED = -9,     /* Operation not supported */
    EB_ERROR_DEPENDENCY_MISSING = -10, /* Required dependency missing */
    EB_ERROR_PROCESS_FAILED = -11, /* Process execution failed */
    EB_ERROR_LIMIT_EXCEEDED = -12, /* Limit exceeded */
    EB_ERROR_PERMISSION_DENIED = -13, /* Permission denied */
    EB_ERROR_TIMEOUT = -14,        /* Operation timed out */
    EB_ERROR_INTERRUPTED = -15,    /* Operation interrupted */
    EB_ERROR_NETWORK = -16,        /* Network error */
    EB_ERROR_AUTH_FAILED = -17,    /* Authentication failed */
    EB_ERROR_INVALID_STATE = -18,  /* Invalid state for operation */
    EB_ERROR_CONFIG = -19,         /* Configuration error */
    
    /* Specific domain errors (20-49) */
    EB_ERROR_INVALID_INPUT = -20,  /* Invalid input */
    EB_ERROR_MEMORY_ALLOCATION = -21, /* Memory allocation failed */
    EB_ERROR_FILE_IO = -22,        /* File I/O error */
    EB_ERROR_PATH_TOO_LONG = -23,  /* Path too long */
    EB_ERROR_NOT_INITIALIZED = -24, /* Store not initialized */
    EB_ERROR_HASH_MISMATCH = -25,  /* Hash mismatch */
    EB_ERROR_DIMENSION_MISMATCH = -26, /* Dimension mismatch */
    EB_ERROR_COMPUTATION_FAILED = -27, /* Computation failed */
    EB_ERROR_HASH_AMBIGUOUS = -28, /* Ambiguous hash prefix */
    EB_ERROR_COMPRESSION = -29,    /* Compression/decompression error */
    EB_ERROR_TRANSFORMER = -30,    /* Transformer error */
    EB_ERROR_LOCK_FAILED = -31,    /* Error for lock acquisition failures */
    EB_ERROR_REFERENCED = -32,     /* Error for objects that are still referenced */
    EB_ERROR_CONNECTION_FAILED = -33, /* Error for connection failures */
    EB_ERROR_INITIALIZATION = -34, /* Error for initialization failures */
    EB_ERROR_NOT_CONNECTED = -35,  /* Error for operations requiring connection */
    EB_ERROR_AUTHENTICATION = -36, /* Error for authentication failures */
    EB_ERROR_INVALID_REPOSITORY = -37, /* Error for invalid repository structure */
    EB_ERROR_CONNECTION_CLOSED = -38, /* Error for closed connections */
    EB_ERROR_RESOURCE_EXHAUSTED = -39, /* Out of resources */
    EB_ERROR_NOT_IMPLEMENTED = -40, /* Not implemented yet */
    EB_ERROR_INVALID_NAME = -41,   /* Invalid name */
    EB_ERROR_PARSING = -42,        /* Parsing error */
    EB_ERROR_TYPE_MISMATCH = -43,  /* Type mismatch */
    EB_ERROR_INVALID_DATA = -44,   /* Invalid data */
    EB_ERROR_TRANSPORT = -45,      /* Transport error */
    EB_ERROR_INVALID_URL = -46,    /* Invalid URL format */
    
    /* Remote operation errors (100-199) */
    EB_ERROR_REMOTE_NOTFOUND = -100, /* Remote not found */
    EB_ERROR_REMOTE_CONNECTION = -101, /* Remote connection failed */
    EB_ERROR_REMOTE_AUTH = -102,   /* Remote authentication failed */
    EB_ERROR_REMOTE_PROTOCOL = -103, /* Remote protocol error */
    EB_ERROR_REMOTE_REJECTED = -104, /* Remote rejected push/pull */
    EB_ERROR_REMOTE_CONFLICT = -105, /* Remote conflict - not fast-forward */
    EB_ERROR_REMOTE_TIMEOUT = -106, /* Remote operation timed out */
} eb_status_t;

/**
 * Get a string description for a status code
 *
 * @param status Status code
 * @return String description
 */
const char *eb_status_string(eb_status_t status);

#endif /* EB_STATUS_H */ 