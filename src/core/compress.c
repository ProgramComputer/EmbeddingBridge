/*
 * EmbeddingBridge - Compression Utilities
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
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <zstd.h>
#include "compress.h"
#include "status.h"
#include "debug.h"

/* Buffer size for file I/O */
#define BUFFER_SIZE 8192

/* Maximum command length for subprocess operations */
#define MAX_CMD_LEN 1024

/*
 * Since integrating ZSTD directly as a library would require additional dependencies,
 * we'll initially use process-based compression by invoking the zstd command.
 * This approach is simpler to implement but has some overhead due to process creation.
 * 
 * A future enhancement would be to integrate libzstd directly for better performance.
 */

/**
 * Check if the zstd command is available
 *
 * @return true if zstd is available, false otherwise
 */
static bool is_zstd_available(void) {
    int ret = system("zstd --version > /dev/null 2>&1");
    return WIFEXITED(ret) && WEXITSTATUS(ret) == 0;
}

/**
 * Execute a command with input from a memory buffer
 *
 * @param command Command to execute
 * @param input Input buffer
 * @param input_size Size of input buffer
 * @param output_out Pointer to store output buffer (caller must free)
 * @param output_size_out Pointer to store output size
 * @return Status code (0 = success)
 */
static eb_status_t execute_with_buffer(
    const char *command,
    const void *input,
    size_t input_size,
    void **output_out,
    size_t *output_size_out) {
    
    if (!command || !input || !output_out || !output_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Create pipes for stdin, stdout, and stderr */
    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        return EB_ERROR_IO;
    }
    
    /* Fork process */
    pid_t pid = fork();
    
    if (pid < 0) {
        /* Fork failed */
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return EB_ERROR_PROCESS_FAILED;
    } else if (pid == 0) {
        /* Child process */
        
        /* Redirect stdin, stdout, and stderr */
        close(stdin_pipe[1]);  /* Close write end of stdin pipe */
        close(stdout_pipe[0]); /* Close read end of stdout pipe */
        close(stderr_pipe[0]); /* Close read end of stderr pipe */
        
        if (dup2(stdin_pipe[0], STDIN_FILENO) < 0 ||
            dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        
        /* Close duplicated file descriptors */
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        /* Execute command */
        execlp("/bin/sh", "/bin/sh", "-c", command, NULL);
        
        /* If exec fails */
        exit(EXIT_FAILURE);
    } else {
        /* Parent process */
        
        /* Close unused pipe ends */
        close(stdin_pipe[0]);  /* Close read end of stdin pipe */
        close(stdout_pipe[1]); /* Close write end of stdout pipe */
        close(stderr_pipe[1]); /* Close write end of stderr pipe */
        
        /* Write input to child's stdin */
        ssize_t written = 0;
        size_t remaining = input_size;
        const unsigned char *input_ptr = (const unsigned char *)input;
        
        while (remaining > 0) {
            written = write(stdin_pipe[1], input_ptr, remaining);
            if (written <= 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(stdin_pipe[1]);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                return EB_ERROR_IO;
            }
            input_ptr += written;
            remaining -= written;
        }
        
        /* Close write end of stdin pipe to signal EOF */
        close(stdin_pipe[1]);
        
        /* Read output from child's stdout */
        size_t output_capacity = BUFFER_SIZE;
        size_t output_size = 0;
        unsigned char *output_buffer = malloc(output_capacity);
        
        if (!output_buffer) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            return EB_ERROR_MEMORY;
        }
        
        while (1) {
            /* Ensure we have enough capacity */
            if (output_size + BUFFER_SIZE > output_capacity) {
                output_capacity *= 2;
                unsigned char *new_buffer = realloc(output_buffer, output_capacity);
                if (!new_buffer) {
                    free(output_buffer);
                    close(stdout_pipe[0]);
                    close(stderr_pipe[0]);
                    return EB_ERROR_MEMORY;
                }
                output_buffer = new_buffer;
            }
            
            /* Read from stdout */
            ssize_t bytes_read = read(stdout_pipe[0], output_buffer + output_size, BUFFER_SIZE);
            
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                free(output_buffer);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                return EB_ERROR_IO;
            } else if (bytes_read == 0) {
                /* End of file */
                break;
            }
            
            output_size += bytes_read;
        }
        
        /* Read any error output (for debugging) */
        char error_buffer[BUFFER_SIZE];
        ssize_t error_bytes = read(stderr_pipe[0], error_buffer, BUFFER_SIZE - 1);
        
        if (error_bytes > 0) {
            error_buffer[error_bytes] = '\0';
            DEBUG_PRINT("Command stderr: %s", error_buffer);
        }
        
        /* Close remaining pipes */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        /* Wait for child process to complete */
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            free(output_buffer);
            return EB_ERROR_PROCESS_FAILED;
        }
        
        /* Check exit status */
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            free(output_buffer);
            return EB_ERROR_PROCESS_FAILED;
        }
        
        /* Return output */
        *output_out = output_buffer;
        *output_size_out = output_size;
        
        return EB_SUCCESS;
    }
}

eb_status_t compress_buffer(
    const void *source,
    size_t source_size,
    int level,
    void **dest_out,
    size_t *dest_size_out) {
    
    /* Validate parameters */
    if (!source || !dest_out || !dest_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* If level is 0, just copy the data */
    if (level == 0) {
        unsigned char *dest = malloc(source_size);
        if (!dest) {
            return EB_ERROR_MEMORY;
        }
        
        memcpy(dest, source, source_size);
        *dest_out = dest;
        *dest_size_out = source_size;
        return EB_SUCCESS;
    }
    
    /* Check if zstd is available */
    if (!is_zstd_available()) {
        return EB_ERROR_DEPENDENCY_MISSING;
    }
    
    /* Clamp compression level to 1-9 */
    if (level < 1) level = 1;
    if (level > 9) level = 9;
    
    /* Build compression command */
    char command[MAX_CMD_LEN];
    snprintf(command, MAX_CMD_LEN, "zstd -%d -q -", level);
    
    /* Execute compression command */
    return execute_with_buffer(command, source, source_size, dest_out, dest_size_out);
}


eb_status_t compress_file(
    const char *source_file,
    const char *dest_file,
    int level) {
    
    /* Validate parameters */
    if (!source_file || !dest_file) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* If level is 0, just copy the file */
    if (level == 0) {
        FILE *src = fopen(source_file, "rb");
        if (!src) {
            return EB_ERROR_IO;
        }
        
        FILE *dst = fopen(dest_file, "wb");
        if (!dst) {
            fclose(src);
            return EB_ERROR_IO;
        }
        
        unsigned char buffer[BUFFER_SIZE];
        size_t bytes_read;
        
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, src)) > 0) {
            if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
                fclose(src);
                fclose(dst);
                return EB_ERROR_IO;
            }
        }
        
        fclose(src);
        fclose(dst);
        return EB_SUCCESS;
    }
    
    /* Check if zstd is available */
    if (!is_zstd_available()) {
        return EB_ERROR_DEPENDENCY_MISSING;
    }
    
    /* Clamp compression level to 1-9 */
    if (level < 1) level = 1;
    if (level > 9) level = 9;
    
    /* Build compression command */
    char command[MAX_CMD_LEN];
    snprintf(command, MAX_CMD_LEN, "zstd -%d -q -f \"%s\" -o \"%s\"",
             level, source_file, dest_file);
    
    /* Execute compression command */
    int ret = system(command);
    if (!WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
        return EB_ERROR_PROCESS_FAILED;
    }
    
    return EB_SUCCESS;
}

eb_status_t decompress_file(
    const char *source_file,
    const char *dest_file) {
    
    /* Validate parameters */
    if (!source_file || !dest_file) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Check if the source file is zstd-compressed */
    FILE *src = fopen(source_file, "rb");
    if (!src) {
        return EB_ERROR_IO;
    }
    
    unsigned char magic[4];
    size_t bytes_read = fread(magic, 1, 4, src);
    fclose(src);
    
    if (bytes_read != 4 || memcmp(magic, "\x28\xB5\x2F\xFD", 4) != 0) {
        /* Not a zstd-compressed file, just copy it */
        return compress_file(source_file, dest_file, 0);
    }
    
    /* Check if zstd is available */
    if (!is_zstd_available()) {
        return EB_ERROR_DEPENDENCY_MISSING;
    }
    
    /* Build decompression command */
    char command[MAX_CMD_LEN];
    snprintf(command, MAX_CMD_LEN, "zstd -d -q -f \"%s\" -o \"%s\"",
             source_file, dest_file);
    
    /* Execute decompression command */
    int ret = system(command);
    if (!WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
        return EB_ERROR_PROCESS_FAILED;
    }
    
    return EB_SUCCESS;
}

/*
 * ZSTD-specific compression implementations
 */

/**
 * Checks if a buffer contains ZSTD compressed data
 * 
 * @param buffer Buffer to check
 * @param size Size of buffer
 * @return true if the buffer contains ZSTD compressed data, false otherwise
 */
bool eb_is_zstd_compressed(const void *buffer, size_t size) {
    if (!buffer || size < 4) {
        return false;
    }
    
    /* Check for ZSTD magic number (0x28 0xB5 0x2F 0xFD) */
    const unsigned char *bytes = (const unsigned char *)buffer;
    return (bytes[0] == 0x28 && bytes[1] == 0xB5 && 
            bytes[2] == 0x2F && bytes[3] == 0xFD);
}

/**
 * Compresses a memory buffer specifically using ZSTD compression
 *
 * @param source Source buffer to compress
 * @param source_size Size of source buffer
 * @param dest_out Pointer to store output buffer (caller must free)
 * @param dest_size_out Pointer to store output size
 * @param level ZSTD compression level (1-22), higher = better compression but slower
 * @return Status code (0 = success)
 */
eb_status_t eb_compress_zstd(
    const void *source,
    size_t source_size,
    void **dest_out,
    size_t *dest_size_out,
    int level) {
    
    /* Validate parameters */
    if (!source || !dest_out || !dest_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Clamp compression level to valid range */
    if (level < 1) level = 1;
    if (level > 22) level = 22;
    
    /* Estimate compression buffer size */
    size_t dest_capacity = ZSTD_compressBound(source_size);
    void *dest_buffer = malloc(dest_capacity);
    if (!dest_buffer) {
        return EB_ERROR_MEMORY;
    }
    
    /* Use the simple compression API which automatically includes frame content size */
    size_t compressed_size = ZSTD_compress(
        dest_buffer, dest_capacity,
        source, source_size,
        level);
    
    if (ZSTD_isError(compressed_size)) {
        DEBUG_WARN("ZSTD compression failed: %s", ZSTD_getErrorName(compressed_size));
        free(dest_buffer);
        return EB_ERROR_COMPRESSION;
    }
    
    /* Shrink buffer to actual compressed size */
    void *final_buffer = realloc(dest_buffer, compressed_size);
    if (!final_buffer) {
        /* If realloc fails, original buffer is still valid */
        *dest_out = dest_buffer;
    } else {
        *dest_out = final_buffer;
    }
    
    *dest_size_out = compressed_size;
    DEBUG_INFO("Compressed %zu bytes to %zu bytes with ZSTD library", 
              source_size, compressed_size);
    return EB_SUCCESS;
}

/**
 * Decompresses a memory buffer compressed with ZSTD
 *
 * @param source Source buffer to decompress
 * @param source_size Size of source buffer
 * @param dest_out Pointer to store output buffer (caller must free)
 * @param dest_size_out Pointer to store output size
 * @return Status code (0 = success)
 */
eb_status_t eb_decompress_zstd(
    const void *source,
    size_t source_size,
    void **dest_out,
    size_t *dest_size_out) {
    
    /* Validate parameters */
    if (!source || !dest_out || !dest_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Verify that the data is actually ZSTD compressed */
    if (!eb_is_zstd_compressed(source, source_size)) {
        DEBUG_WARN("Data does not appear to be ZSTD compressed");
        return EB_ERROR_INVALID_FORMAT;
    }

    /* Determine original size from frame header */
    unsigned long long original_size = ZSTD_getFrameContentSize(source, source_size);
    if (original_size == ZSTD_CONTENTSIZE_UNKNOWN || original_size == ZSTD_CONTENTSIZE_ERROR) {
        DEBUG_WARN("Could not determine content size from ZSTD frame");
        return EB_ERROR_INVALID_FORMAT;
    }
    
    /* Allocate decompression buffer */
    void *dest_buffer = malloc(original_size);
    if (!dest_buffer) {
        return EB_ERROR_MEMORY;
    }
    
    /* Perform decompression */
    size_t decompressed_size = ZSTD_decompress(dest_buffer, original_size, 
                                              source, source_size);
    
    if (ZSTD_isError(decompressed_size)) {
        DEBUG_WARN("ZSTD decompression failed: %s", ZSTD_getErrorName(decompressed_size));
        free(dest_buffer);
        return EB_ERROR_COMPRESSION;
    }
    
    /* Return decompressed data */
    *dest_out = dest_buffer;
    *dest_size_out = decompressed_size;
    DEBUG_INFO("Decompressed %zu bytes to %zu bytes with ZSTD library", 
              source_size, decompressed_size);
    return EB_SUCCESS;
} 