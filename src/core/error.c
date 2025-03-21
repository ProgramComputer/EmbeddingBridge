#include "error.h"
#include "types.h"
#include <stdio.h>
#include <string.h>

const char* eb_status_str(eb_status_t status)
{
        switch (status) {
        case EB_SUCCESS:
                return "Success";
        case EB_ERROR_INVALID_INPUT:
                return "Invalid input";
        case EB_ERROR_MEMORY_ALLOCATION:
                return "Memory allocation failed";
        case EB_ERROR_FILE_IO:
                return "File I/O error";
        case EB_ERROR_NOT_FOUND:
                return "Not found";
        case EB_ERROR_PATH_TOO_LONG:
                return "Path too long";
        case EB_ERROR_NOT_INITIALIZED:
                return "Store not initialized";
        case EB_ERROR_HASH_MISMATCH:
                return "Hash mismatch";
        case EB_ERROR_DIMENSION_MISMATCH:
                return "Dimension mismatch";
        case EB_ERROR_COMPUTATION_FAILED:
                return "Computation failed";
        case EB_ERROR_HASH_AMBIGUOUS:
                return "Ambiguous hash prefix";
                
        /* General errors */
        case EB_ERROR_INVALID_PARAMETER:
                return "Invalid parameter";
        case EB_ERROR_MEMORY:
                return "Memory allocation failed";
        case EB_ERROR_IO:
                return "I/O error";
        case EB_ERROR_ALREADY_EXISTS:
                return "Already exists";
        case EB_ERROR_INVALID_NAME:
                return "Invalid name";
        case EB_ERROR_NOT_IMPLEMENTED:
                return "Not implemented";
        case EB_ERROR_LOCK_FAILED:
                return "Failed to acquire lock";
        case EB_ERROR_REFERENCED:
                return "Object is still referenced";
        case EB_ERROR_CONNECTION_FAILED:
                return "Connection failed";
        case EB_ERROR_UNSUPPORTED:
                return "Operation not supported";
        case EB_ERROR_INITIALIZATION:
                return "Initialization failed";
        case EB_ERROR_NOT_CONNECTED:
                return "Not connected";
        case EB_ERROR_AUTHENTICATION:
                return "Authentication failed";
        case EB_ERROR_INVALID_REPOSITORY:
                return "Invalid repository structure";
        case EB_ERROR_CONNECTION_CLOSED:
                return "Connection closed";
        case EB_ERROR_PROCESS_FAILED:
                return "Process execution failed";
        case EB_ERROR_DEPENDENCY_MISSING:
                return "Required dependency missing";
                
        /* Remote operation errors */
        case EB_ERROR_REMOTE_NOTFOUND:
                return "Remote not found";
        case EB_ERROR_REMOTE_CONNECTION:
                return "Remote connection failed";
        case EB_ERROR_REMOTE_AUTH:
                return "Remote authentication failed";
        case EB_ERROR_REMOTE_PROTOCOL:
                return "Remote protocol error";
        case EB_ERROR_REMOTE_REJECTED:
                return "Remote rejected push/pull";
        case EB_ERROR_REMOTE_CONFLICT:
                return "Remote conflict - not fast-forward";
        case EB_ERROR_REMOTE_TIMEOUT:
                return "Remote operation timed out";
                
        default:
                return "Unknown error";
        }
}

const char* eb_strerror(int errnum)
{
        return strerror(errnum);
}

void eb_set_error(eb_status_t status, const char* message)
{
        fprintf(stderr, "error: %s: %s\n", message, eb_status_str(status));
} 