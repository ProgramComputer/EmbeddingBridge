/*
 * EmbeddingBridge - Format Transformation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_FORMAT_H
#define EB_FORMAT_H

#include <stddef.h>
#include <stdbool.h>
#include "types.h"
#include "error.h"

/**
 * Format types supported by EmbeddingBridge
 */
typedef enum eb_format_type {
    EB_FORMAT_NATIVE,   /* Native .embr format with .raw and .meta files */
    EB_FORMAT_PARQUET,  /* Apache Parquet format with ZSTD compression */
    EB_FORMAT_PINECONE, /* Pinecone-compatible format */
    EB_FORMAT_UNKNOWN   /* Unknown/unsupported format */
} eb_format_type_t;

/**
 * Compression types supported by EmbeddingBridge
 */
typedef enum eb_compression_type {
    EB_COMPRESSION_NONE,  /* No compression */
    EB_COMPRESSION_ZSTD,  /* ZSTD compression */
    EB_COMPRESSION_UNKNOWN /* Unknown/unsupported compression */
} eb_compression_type_t;

/**
 * Format transformer configuration
 */
typedef struct eb_format_config {
    eb_format_type_t format_type;         /* Format type */
    eb_compression_type_t compression_type; /* Compression type */
    int compression_level;                /* Compression level (0-9 for ZSTD) */
    bool normalize_vectors;               /* Whether to normalize vectors */
    char format_options[256];             /* Additional format-specific options */
} eb_format_config_t;

/* Forward declaration of format transformer */
typedef struct eb_format_transformer eb_format_transformer_t;

/**
 * Format transformation function types
 */
typedef eb_status_t (*eb_transform_to_fn)(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size
);

typedef eb_status_t (*eb_transform_from_fn)(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size
);

typedef void (*eb_transformer_cleanup_fn)(
    eb_format_transformer_t *transformer
);

/**
 * Format transformer operations structure
 */
typedef struct eb_format_transformer_ops {
    eb_transform_to_fn transform_to;       /* Transform from native to format */
    eb_transform_from_fn transform_from;   /* Transform from format to native */
    eb_transformer_cleanup_fn cleanup;     /* Clean up transformer resources */
} eb_format_transformer_ops_t;

/**
 * Format transformer structure
 */
struct eb_format_transformer {
    eb_format_type_t type;                /* Format type */
    eb_format_config_t config;            /* Transformer configuration */
    eb_format_transformer_ops_t *ops;     /* Transformer operations */
    void *data;                           /* Format-specific data */
    eb_status_t last_error;               /* Last error code */
    char error_msg[256];                  /* Last error message */
};

/**
 * Create and initialize a format transformer
 *
 * @param format_type Format type to transform to/from
 * @param config Format transformer configuration
 * @return Initialized transformer or NULL on error
 */
eb_format_transformer_t *format_transformer_create(
    eb_format_type_t format_type,
    const eb_format_config_t *config
);

/**
 * Clean up and free a format transformer
 *
 * @param transformer Transformer to clean up
 */
void format_transformer_destroy(eb_format_transformer_t *transformer);

/**
 * Transform data from native format to target format
 *
 * @param transformer Format transformer
 * @param source_data Source data in native format
 * @param source_size Size of source data
 * @param dest_data Pointer to store transformed data
 * @param dest_size Pointer to store size of transformed data
 * @return Status code (0 = success)
 */
eb_status_t format_transform_to(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size
);

/**
 * Transform data from target format to native format
 *
 * @param transformer Format transformer
 * @param source_data Source data in target format
 * @param source_size Size of source data
 * @param dest_data Pointer to store transformed data
 * @param dest_size Pointer to store size of transformed data
 * @return Status code (0 = success)
 */
eb_status_t format_transform_from(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size
);

/**
 * Get the error message from a transformer
 *
 * @param transformer Format transformer
 * @return Error message string
 */
const char *format_transformer_get_error(eb_format_transformer_t *transformer);

/**
 * Parse format type from string
 *
 * @param format_str Format type string
 * @return Format type enum or EB_FORMAT_UNKNOWN if not recognized
 */
eb_format_type_t format_type_from_string(const char *format_str);

/**
 * Convert format type to string
 *
 * @param format_type Format type enum
 * @return String representation of format type
 */
const char *format_type_to_string(eb_format_type_t format_type);

/**
 * Parse compression type from string
 *
 * @param compression_str Compression type string
 * @return Compression type enum or EB_COMPRESSION_UNKNOWN if not recognized
 */
eb_compression_type_t compression_type_from_string(const char *compression_str);

/**
 * Convert compression type to string
 *
 * @param compression_type Compression type enum
 * @return String representation of compression type
 */
const char *compression_type_to_string(eb_compression_type_t compression_type);

/**
 * Parse compression string with optional level (e.g., "zstd:9")
 *
 * @param compression_str Compression string with optional level
 * @param type_out Pointer to store parsed compression type
 * @param level_out Pointer to store parsed compression level
 * @return Status code (0 = success)
 */
eb_status_t parse_compression_string(
    const char *compression_str,
    eb_compression_type_t *type_out,
    int *level_out
);

#endif /* EB_FORMAT_H */ 