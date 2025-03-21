/*
 * EmbeddingBridge - Format Transformation
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
#include <ctype.h>
#include "format.h"
#include "debug.h"

/* Default format transformer implementations */
static eb_format_transformer_ops_t native_transformer_ops;
static eb_format_transformer_ops_t parquet_transformer_ops;
static eb_format_transformer_ops_t pinecone_transformer_ops;

/* Forward declarations of format-specific initialization functions */
static eb_status_t native_transformer_init(eb_format_transformer_t *transformer);
static eb_status_t parquet_transformer_init(eb_format_transformer_t *transformer);
static eb_status_t pinecone_transformer_init(eb_format_transformer_t *transformer);

/* Helper functions */
static void set_transformer_error(eb_format_transformer_t *transformer, 
                                 eb_status_t status, const char *message) {
    transformer->last_error = status;
    if (message) {
        strncpy(transformer->error_msg, message, sizeof(transformer->error_msg) - 1);
        transformer->error_msg[sizeof(transformer->error_msg) - 1] = '\0';
    } else {
        transformer->error_msg[0] = '\0';
    }
}

eb_format_transformer_t *format_transformer_create(
    eb_format_type_t format_type,
    const eb_format_config_t *config) {
    
    eb_format_transformer_t *transformer = calloc(1, sizeof(eb_format_transformer_t));
    if (!transformer) {
        return NULL;
    }
    
    transformer->type = format_type;
    if (config) {
        transformer->config = *config;
    } else {
        /* Set default configuration */
        transformer->config.format_type = format_type;
        transformer->config.compression_type = EB_COMPRESSION_NONE;
        transformer->config.compression_level = 0;
        transformer->config.normalize_vectors = false;
        transformer->config.format_options[0] = '\0';
    }
    
    /* Initialize format-specific operations */
    eb_status_t status = EB_ERROR_NOT_IMPLEMENTED;
    
    switch (format_type) {
        case EB_FORMAT_NATIVE:
            transformer->ops = &native_transformer_ops;
            status = native_transformer_init(transformer);
            break;
            
        case EB_FORMAT_PARQUET:
            transformer->ops = &parquet_transformer_ops;
            status = parquet_transformer_init(transformer);
            break;
            
        case EB_FORMAT_PINECONE:
            transformer->ops = &pinecone_transformer_ops;
            status = pinecone_transformer_init(transformer);
            break;
            
        default:
            set_transformer_error(transformer, EB_ERROR_UNSUPPORTED, 
                                 "Unsupported format type");
            free(transformer);
            return NULL;
    }
    
    if (status != EB_SUCCESS) {
        free(transformer);
        return NULL;
    }
    
    return transformer;
}

void format_transformer_destroy(eb_format_transformer_t *transformer) {
    if (!transformer) {
        return;
    }
    
    /* Call format-specific cleanup if available */
    if (transformer->ops && transformer->ops->cleanup) {
        transformer->ops->cleanup(transformer);
    }
    
    free(transformer);
}

eb_status_t format_transform_to(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    if (!transformer || !source_data || !dest_data || !dest_size) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    if (!transformer->ops || !transformer->ops->transform_to) {
        set_transformer_error(transformer, EB_ERROR_NOT_IMPLEMENTED,
                             "Transform to operation not implemented");
        return EB_ERROR_NOT_IMPLEMENTED;
    }
    
    return transformer->ops->transform_to(transformer, source_data, source_size,
                                         dest_data, dest_size);
}

eb_status_t format_transform_from(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    if (!transformer || !source_data || !dest_data || !dest_size) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    if (!transformer->ops || !transformer->ops->transform_from) {
        set_transformer_error(transformer, EB_ERROR_NOT_IMPLEMENTED,
                             "Transform from operation not implemented");
        return EB_ERROR_NOT_IMPLEMENTED;
    }
    
    return transformer->ops->transform_from(transformer, source_data, source_size,
                                           dest_data, dest_size);
}

const char *format_transformer_get_error(eb_format_transformer_t *transformer) {
    if (!transformer) {
        return "NULL transformer";
    }
    
    return transformer->error_msg[0] ? transformer->error_msg : 
           eb_status_str(transformer->last_error);
}

eb_format_type_t format_type_from_string(const char *format_str) {
    if (!format_str) {
        return EB_FORMAT_UNKNOWN;
    }
    
    if (strcasecmp(format_str, "native") == 0 || 
        strcasecmp(format_str, "eb") == 0) {
        return EB_FORMAT_NATIVE;
    } else if (strcasecmp(format_str, "parquet") == 0) {
        return EB_FORMAT_PARQUET;
    } else if (strcasecmp(format_str, "pinecone") == 0) {
        return EB_FORMAT_PINECONE;
    }
    
    return EB_FORMAT_UNKNOWN;
}

const char *format_type_to_string(eb_format_type_t format_type) {
    switch (format_type) {
        case EB_FORMAT_NATIVE:
            return "native";
        case EB_FORMAT_PARQUET:
            return "parquet";
        case EB_FORMAT_PINECONE:
            return "pinecone";
        default:
            return "unknown";
    }
}

eb_compression_type_t compression_type_from_string(const char *compression_str) {
    if (!compression_str) {
        return EB_COMPRESSION_UNKNOWN;
    }
    
    if (strcasecmp(compression_str, "none") == 0 || 
        strcasecmp(compression_str, "off") == 0 ||
        strcasecmp(compression_str, "0") == 0) {
        return EB_COMPRESSION_NONE;
    } else if (strcasecmp(compression_str, "zstd") == 0 ||
               strncasecmp(compression_str, "zstd:", 5) == 0) {
        return EB_COMPRESSION_ZSTD;
    }
    
    return EB_COMPRESSION_UNKNOWN;
}

const char *compression_type_to_string(eb_compression_type_t compression_type) {
    switch (compression_type) {
        case EB_COMPRESSION_NONE:
            return "none";
        case EB_COMPRESSION_ZSTD:
            return "zstd";
        default:
            return "unknown";
    }
}

eb_status_t parse_compression_string(
    const char *compression_str,
    eb_compression_type_t *type_out,
    int *level_out) {
    
    if (!compression_str || !type_out || !level_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Default values */
    *type_out = EB_COMPRESSION_NONE;
    *level_out = 0;
    
    if (strcasecmp(compression_str, "none") == 0 || 
        strcasecmp(compression_str, "off") == 0 ||
        strcasecmp(compression_str, "0") == 0) {
        *type_out = EB_COMPRESSION_NONE;
        *level_out = 0;
        return EB_SUCCESS;
    }
    
    if (strcasecmp(compression_str, "zstd") == 0) {
        *type_out = EB_COMPRESSION_ZSTD;
        *level_out = 3; /* Default ZSTD level */
        return EB_SUCCESS;
    }
    
    if (strncasecmp(compression_str, "zstd:", 5) == 0) {
        *type_out = EB_COMPRESSION_ZSTD;
        *level_out = atoi(compression_str + 5);
        
        /* Validate ZSTD level (0-9) */
        if (*level_out < 0 || *level_out > 9) {
            return EB_ERROR_INVALID_PARAMETER;
        }
        
        return EB_SUCCESS;
    }
    
    return EB_ERROR_INVALID_PARAMETER;
}

/*
 * Native format transformer implementation
 */

static eb_status_t native_transform_to(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    /* Native format doesn't transform */
    void *data_copy = malloc(source_size);
    if (!data_copy) {
        set_transformer_error(transformer, EB_ERROR_MEMORY,
                             "Failed to allocate memory for native transform");
        return EB_ERROR_MEMORY;
    }
    
    memcpy(data_copy, source_data, source_size);
    *dest_data = data_copy;
    *dest_size = source_size;
    
    return EB_SUCCESS;
}

static eb_status_t native_transform_from(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    /* Native format doesn't transform */
    return native_transform_to(transformer, source_data, source_size,
                              dest_data, dest_size);
}

static void native_cleanup(eb_format_transformer_t *transformer) {
    /* No cleanup needed for native transformer */
}

static eb_status_t native_transformer_init(eb_format_transformer_t *transformer) {
    /* Initialize operations table if not already done */
    static int ops_initialized = 0;
    if (!ops_initialized) {
        native_transformer_ops.transform_to = native_transform_to;
        native_transformer_ops.transform_from = native_transform_from;
        native_transformer_ops.cleanup = native_cleanup;
        ops_initialized = 1;
    }
    
    return EB_SUCCESS;
}

/*
 * Parquet format transformer implementation (placeholder)
 */

static eb_status_t parquet_transform_to(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    /* Placeholder - will be implemented with Parquet library */
    set_transformer_error(transformer, EB_ERROR_NOT_IMPLEMENTED,
                         "Parquet transformation not yet implemented");
    return EB_ERROR_NOT_IMPLEMENTED;
}

static eb_status_t parquet_transform_from(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    /* Placeholder - will be implemented with Parquet library */
    set_transformer_error(transformer, EB_ERROR_NOT_IMPLEMENTED,
                         "Parquet transformation not yet implemented");
    return EB_ERROR_NOT_IMPLEMENTED;
}

static void parquet_cleanup(eb_format_transformer_t *transformer) {
    /* Placeholder - will clean up Parquet-specific resources */
}

static eb_status_t parquet_transformer_init(eb_format_transformer_t *transformer) {
    /* Initialize operations table if not already done */
    static int ops_initialized = 0;
    if (!ops_initialized) {
        parquet_transformer_ops.transform_to = parquet_transform_to;
        parquet_transformer_ops.transform_from = parquet_transform_from;
        parquet_transformer_ops.cleanup = parquet_cleanup;
        ops_initialized = 1;
    }
    
    return EB_SUCCESS;
}

/*
 * Pinecone format transformer implementation (placeholder)
 */

static eb_status_t pinecone_transform_to(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    /* Placeholder - will be implemented with Pinecone schema */
    set_transformer_error(transformer, EB_ERROR_NOT_IMPLEMENTED,
                         "Pinecone transformation not yet implemented");
    return EB_ERROR_NOT_IMPLEMENTED;
}

static eb_status_t pinecone_transform_from(
    eb_format_transformer_t *transformer,
    const void *source_data,
    size_t source_size,
    void **dest_data,
    size_t *dest_size) {
    
    /* Placeholder - will be implemented with Pinecone schema */
    set_transformer_error(transformer, EB_ERROR_NOT_IMPLEMENTED,
                         "Pinecone transformation not yet implemented");
    return EB_ERROR_NOT_IMPLEMENTED;
}

static void pinecone_cleanup(eb_format_transformer_t *transformer) {
    /* Placeholder - will clean up Pinecone-specific resources */
}

static eb_status_t pinecone_transformer_init(eb_format_transformer_t *transformer) {
    /* Initialize operations table if not already done */
    static int ops_initialized = 0;
    if (!ops_initialized) {
        pinecone_transformer_ops.transform_to = pinecone_transform_to;
        pinecone_transformer_ops.transform_from = pinecone_transform_from;
        pinecone_transformer_ops.cleanup = pinecone_cleanup;
        ops_initialized = 1;
    }
    
    return EB_SUCCESS;
} 