/*
 * EmbeddingBridge - Format Transformer Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "transformer.h"
#include "debug.h"

/* Maximum number of registered transformers */
#define MAX_TRANSFORMERS 32

/* Global registry of transformers */
static struct {
    eb_transformer_t *transformers[MAX_TRANSFORMERS];
    int count;
    bool initialized;
} transformer_registry = {
    .transformers = {NULL},
    .count = 0,
    .initialized = false
};

eb_transformer_t *eb_transformer_create(
    const char *name,
    const char *format_name,
    eb_transform_func transform,
    eb_inverse_transform_func inverse,
    eb_transformer_free_func free,
    eb_transformer_clone_func clone,
    void *user_data) {
    
    if (!name || !format_name || !transform || !inverse) {
        return NULL;
    }
    
    eb_transformer_t *transformer = (eb_transformer_t *)malloc(sizeof(eb_transformer_t));
    if (!transformer) {
        return NULL;
    }
    
    transformer->name = strdup(name);
    transformer->format_name = strdup(format_name);
    transformer->transform = transform;
    transformer->inverse = inverse;
    transformer->free = free;
    transformer->clone = clone;
    transformer->user_data = user_data;
    
    if (!transformer->name || !transformer->format_name) {
        eb_transformer_free(transformer);
        return NULL;
    }
    
    return transformer;
}

void eb_transformer_free(eb_transformer_t *transformer) {
    if (!transformer) {
        return;
    }
    
    /* Call the custom free function if provided */
    if (transformer->free && transformer->free != eb_transformer_free) {
        transformer->free(transformer);
        return;
    }
    
    /* Free string fields */
    if (transformer->name) {
        free((char *)transformer->name);
        transformer->name = NULL;
    }
    
    if (transformer->format_name) {
        free((char *)transformer->format_name);
        transformer->format_name = NULL;
    }
    
    /* Free the structure itself */
    free(transformer);
}

eb_transformer_t *eb_transformer_clone(const eb_transformer_t *transformer) {
    if (!transformer) {
        return NULL;
    }
    
    /* Use custom clone function if provided */
    if (transformer->clone) {
        return transformer->clone(transformer);
    }
    
    /* Default cloning behavior */
    return eb_transformer_create(
        transformer->name,
        transformer->format_name,
        transformer->transform,
        transformer->inverse,
        transformer->free,
        transformer->clone,
        transformer->user_data
    );
}

eb_status_t eb_transform(
    eb_transformer_t *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out) {
    
    if (!transformer || !src || !dst_out || !dst_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    return transformer->transform(transformer, src, src_size, dst_out, dst_size_out);
}

eb_status_t eb_inverse_transform(
    eb_transformer_t *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out) {
    
    if (!transformer || !src || !dst_out || !dst_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    return transformer->inverse(transformer, src, src_size, dst_out, dst_size_out);
}

/* Registry management functions */

eb_status_t eb_transformer_registry_init(void) {
    if (transformer_registry.initialized) {
        return EB_SUCCESS;
    }
    
    /* Reset the registry */
    for (int i = 0; i < MAX_TRANSFORMERS; i++) {
        transformer_registry.transformers[i] = NULL;
    }
    
    transformer_registry.count = 0;
    transformer_registry.initialized = true;
    
    DEBUG_PRINT("Transformer registry initialized");
    return EB_SUCCESS;
}

void eb_transformer_registry_cleanup(void) {
    if (!transformer_registry.initialized) {
        return;
    }
    
    DEBUG_PRINT("Starting transformer registry cleanup for %d transformers", transformer_registry.count);
    
    /* Free all registered transformers */
    for (int i = 0; i < transformer_registry.count; i++) {
        if (transformer_registry.transformers[i]) {
            DEBUG_PRINT("Cleaning up transformer: %s", 
                      transformer_registry.transformers[i]->name ? 
                      transformer_registry.transformers[i]->name : 
                      "(unnamed)");
            
            eb_transformer_t *t = transformer_registry.transformers[i];
            
            /* Call the transformer's free function if it exists */
            if (t->free) {
                t->free(t);
            } else {
                /* Only free the fields manually if the free function doesn't exist */
                if (t->name) {
                    free((void *)t->name);
                    t->name = NULL;
                }
                
                if (t->format_name) {
                    free((void *)t->format_name);
                    t->format_name = NULL;
                }
            }
            
            /* Free the transformer struct itself */
            free(t);
            transformer_registry.transformers[i] = NULL;
        }
    }
    
    transformer_registry.count = 0;
    transformer_registry.initialized = false;
    
    DEBUG_PRINT("Transformer registry cleanup completed");
}

eb_status_t eb_register_transformer(eb_transformer_t *transformer) {
    if (!transformer) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    if (!transformer_registry.initialized) {
        eb_status_t status = eb_transformer_registry_init();
        if (status != EB_SUCCESS) {
            return status;
        }
    }
    
    /* Check if we've reached the maximum number of transformers */
    if (transformer_registry.count >= MAX_TRANSFORMERS) {
        return EB_ERROR_LIMIT_EXCEEDED;
    }
    
    /* Check if a transformer with this name is already registered */
    for (int i = 0; i < transformer_registry.count; i++) {
        if (strcmp(transformer_registry.transformers[i]->name, transformer->name) == 0) {
            return EB_ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Register the transformer */
    transformer_registry.transformers[transformer_registry.count++] = transformer;
    
    DEBUG_PRINT("Registered transformer: %s (%s)", 
                transformer->name, transformer->format_name);
    
    return EB_SUCCESS;
}

eb_transformer_t *eb_find_transformer(const char *name) {
    if (!name || !transformer_registry.initialized) {
        return NULL;
    }
    
    for (int i = 0; i < transformer_registry.count; i++) {
        if (strcmp(transformer_registry.transformers[i]->name, name) == 0) {
            return transformer_registry.transformers[i];
        }
    }
    
    return NULL;
}

eb_transformer_t *eb_find_transformer_by_format(const char *format_name) {
    if (!format_name || !transformer_registry.initialized) {
        return NULL;
    }
    
    for (int i = 0; i < transformer_registry.count; i++) {
        if (strcmp(transformer_registry.transformers[i]->format_name, format_name) == 0) {
            return transformer_registry.transformers[i];
        }
    }
    
    return NULL;
}

/* Implementation of built-in transformers will be in separate files */
extern eb_status_t eb_register_builtin_transformers(void);

/* Get a transformer by name */ 