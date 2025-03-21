/*
 * EmbeddingBridge - Status Code Handling
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "status.h"

/**
 * Get a string description for a status code
 *
 * @param status Status code
 * @return String description
 */
const char *eb_status_string(eb_status_t status) {
    switch (status) {
        case EB_SUCCESS:
            return "Success";
        case EB_ERROR_GENERIC:
            return "Generic error";
        case EB_ERROR_MEMORY:
            return "Memory allocation failure";
        case EB_ERROR_IO:
            return "I/O error";
        case EB_ERROR_INVALID_PARAMETER:
            return "Invalid parameter";
        case EB_ERROR_NOT_FOUND:
            return "Item not found";
        case EB_ERROR_ALREADY_EXISTS:
            return "Item already exists";
        case EB_ERROR_BUFFER_TOO_SMALL:
            return "Buffer too small";
        case EB_ERROR_INVALID_FORMAT:
            return "Invalid format";
        case EB_ERROR_UNSUPPORTED:
            return "Operation not supported";
        case EB_ERROR_DEPENDENCY_MISSING:
            return "Required dependency missing";
        case EB_ERROR_PROCESS_FAILED:
            return "Process execution failed";
        case EB_ERROR_LIMIT_EXCEEDED:
            return "Limit exceeded";
        case EB_ERROR_PERMISSION_DENIED:
            return "Permission denied";
        case EB_ERROR_TIMEOUT:
            return "Operation timed out";
        case EB_ERROR_INTERRUPTED:
            return "Operation interrupted";
        case EB_ERROR_NETWORK:
            return "Network error";
        case EB_ERROR_AUTH_FAILED:
            return "Authentication failed";
        case EB_ERROR_INVALID_STATE:
            return "Invalid state for operation";
        case EB_ERROR_CONFIG:
            return "Configuration error";
        case EB_ERROR_COMPRESSION:
            return "Compression/decompression error";
        case EB_ERROR_TRANSFORMER:
            return "Transformer error";
        default:
            return "Unknown error";
    }
} 