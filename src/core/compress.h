/*
 * EmbeddingBridge - Compression Utilities
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_COMPRESS_H
#define EB_COMPRESS_H

#include <stddef.h>
#include <stdbool.h>
#include "status.h"

/**
 * Compresses a memory buffer using zstd compression
 *
 * @param source Source buffer to compress
 * @param source_size Size of source buffer
 * @param level Compression level (0-9), 0 means no compression
 * @param dest_out Pointer to store output buffer (caller must free)
 * @param dest_size_out Pointer to store output size
 * @return Status code (0 = success)
 */
eb_status_t compress_buffer(
    const void *source,
    size_t source_size,
    int level,
    void **dest_out,
    size_t *dest_size_out);


/**
 * Compresses a file using zstd compression
 *
 * @param source_file Path to source file
 * @param dest_file Path to destination file
 * @param level Compression level (0-9), 0 means no compression
 * @return Status code (0 = success)
 */
eb_status_t compress_file(
    const char *source_file,
    const char *dest_file,
    int level);

/**
 * Decompresses a file compressed with zstd
 *
 * @param source_file Path to source file
 * @param dest_file Path to destination file
 * @return Status code (0 = success)
 */
eb_status_t decompress_file(
    const char *source_file,
    const char *dest_file);

/**
 * Compresses a memory buffer specifically using ZSTD compression
 *
 * @param source Source buffer to compress
 * @param source_size Size of source buffer
 * @param dest_out Pointer to store output buffer (caller must free)
 * @param dest_size_out Pointer to store output size
 * @param level ZSTD compression level (1-22), higher = better compression but slower
 * @return Status code (0 = success)
 * 
 * Note: This function uses the ZSTD library directly, not the command line utility.
 * It properly includes content size in the compressed frame header for reliable decompression.
 */
eb_status_t eb_compress_zstd(
    const void *source,
    size_t source_size,
    void **dest_out,
    size_t *dest_size_out,
    int level);

/**
 * Decompresses a memory buffer compressed with ZSTD
 *
 * @param source Source buffer to decompress
 * @param source_size Size of source buffer
 * @param dest_out Pointer to store output buffer (caller must free)
 * @param dest_size_out Pointer to store output size
 * @return Status code (0 = success)
 * 
 * Note: This function uses the ZSTD library directly for decompression.
 * It requires the compressed data to include the original content size in the frame header.
 */
eb_status_t eb_decompress_zstd(
    const void *source,
    size_t source_size,
    void **dest_out,
    size_t *dest_size_out);

/**
 * Checks if a buffer contains ZSTD compressed data
 * 
 * @param buffer Buffer to check
 * @param size Size of buffer
 * @return true if the buffer contains ZSTD compressed data, false otherwise
 */
bool eb_is_zstd_compressed(
    const void *buffer,
    size_t size);

#endif /* EB_COMPRESS_H */ 