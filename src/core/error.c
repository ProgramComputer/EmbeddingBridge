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