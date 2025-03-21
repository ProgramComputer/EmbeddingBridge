/*
 * EmbeddingBridge - Format Transformer Interface
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_TRANSFORMER_H
#define EB_TRANSFORMER_H

#include <stddef.h>
#include "status.h"

/* Forward declaration */
struct eb_transformer;

/**
 * Function type for encoding data to a specific format
 *
 * @param transformer The transformer instance
 * @param src Source data
 * @param src_size Size of source data
 * @param dst_out Pointer to store output buffer (caller must free)
 * @param dst_size_out Pointer to store output size
 * @return Status code
 */
typedef eb_status_t (*eb_transform_func)(
    struct eb_transformer *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);

/**
 * Function type for decoding data from a specific format
 *
 * @param transformer The transformer instance
 * @param src Source data
 * @param src_size Size of source data
 * @param dst_out Pointer to store output buffer (caller must free)
 * @param dst_size_out Pointer to store output size
 * @return Status code
 */
typedef eb_status_t (*eb_inverse_transform_func)(
    struct eb_transformer *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);

/**
 * Function type for freeing transformer resources
 *
 * @param transformer The transformer instance
 */
typedef void (*eb_transformer_free_func)(
    struct eb_transformer *transformer);

/**
 * Function for cloning a transformer
 *
 * @param transformer The transformer instance to clone
 * @return A new transformer instance with the same configuration
 */
typedef struct eb_transformer *(*eb_transformer_clone_func)(
    const struct eb_transformer *transformer);

/**
 * Transformer interface for encoding/decoding data to/from different formats
 */
typedef struct eb_transformer {
    const char *name;                   /* Name of the transformer */
    const char *format_name;            /* Name of the format */
    eb_transform_func transform;        /* Encoding function */
    eb_inverse_transform_func inverse;  /* Decoding function */
    eb_transformer_free_func free;      /* Resource cleanup function */
    eb_transformer_clone_func clone;    /* Clone function */
    void *user_data;                    /* Custom user data */
} eb_transformer_t;

/**
 * Create a new transformer
 *
 * @param name Name of the transformer
 * @param format_name Name of the format
 * @param transform Encoding function
 * @param inverse Decoding function
 * @param free Resource cleanup function
 * @param clone Clone function
 * @param user_data Custom user data
 * @return New transformer instance
 */
eb_transformer_t *eb_transformer_create(
    const char *name,
    const char *format_name,
    eb_transform_func transform,
    eb_inverse_transform_func inverse,
    eb_transformer_free_func free,
    eb_transformer_clone_func clone,
    void *user_data);

/**
 * Free a transformer
 *
 * @param transformer Transformer to free
 */
void eb_transformer_free(eb_transformer_t *transformer);

/**
 * Clone a transformer
 *
 * @param transformer Transformer to clone
 * @return New transformer instance with the same configuration
 */
eb_transformer_t *eb_transformer_clone(const eb_transformer_t *transformer);

/**
 * Transform data using the transformer
 *
 * @param transformer Transformer to use
 * @param src Source data
 * @param src_size Size of source data
 * @param dst_out Pointer to store output buffer (caller must free)
 * @param dst_size_out Pointer to store output size
 * @return Status code
 */
eb_status_t eb_transform(
    eb_transformer_t *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);

/**
 * Inverse transform data using the transformer
 *
 * @param transformer Transformer to use
 * @param src Source data
 * @param src_size Size of source data
 * @param dst_out Pointer to store output buffer (caller must free)
 * @param dst_size_out Pointer to store output size
 * @return Status code
 */
eb_status_t eb_inverse_transform(
    eb_transformer_t *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);

/* Registry functions */

/**
 * Register a transformer
 *
 * @param transformer Transformer to register
 * @return Status code
 */
eb_status_t eb_register_transformer(eb_transformer_t *transformer);

/**
 * Find a transformer by name
 *
 * @param name Name to look for
 * @return Transformer or NULL if not found
 */
eb_transformer_t *eb_find_transformer(const char *name);

/**
 * Find a transformer by format name
 *
 * @param format_name Format name to look for
 * @return Transformer or NULL if not found
 */
eb_transformer_t *eb_find_transformer_by_format(const char *format_name);

/**
 * Initialize the transformer registry
 *
 * @return Status code
 */
eb_status_t eb_transformer_registry_init(void);

/**
 * Clean up the transformer registry
 */
void eb_transformer_registry_cleanup(void);

/**
 * Register built-in transformers
 *
 * @return Status code
 */
eb_status_t eb_register_builtin_transformers(void);

#endif /* EB_TRANSFORMER_H */ 