/*
 * EmbeddingBridge - Garbage Collection
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_GC_H
#define EB_GC_H

#include "types.h"
#include <stdbool.h>

/**
 * Result structure for garbage collection operations
 */
typedef struct {
    eb_status_t status;         /* Operation status */
    char message[256];          /* Status message */
    int objects_removed;        /* Number of objects removed */
    size_t bytes_freed;         /* Bytes of storage freed */
} eb_gc_result_t;

/**
 * Run garbage collection on the repository
 * 
 * @param prune_expire Expiration string for pruning (e.g., "2.weeks.ago", "now", or "never")
 * @param aggressive Whether to do more aggressive optimization
 * @param result Pointer to store operation result
 * @return Status code (0 = success)
 */
eb_status_t gc_run(const char* prune_expire, bool aggressive, eb_gc_result_t* result);

/**
 * Find unreferenced embedding objects
 * 
 * @param unreferenced_out Array to store unreferenced object hashes
 * @param max_unreferenced Maximum number of objects to store
 * @param count_out Pointer to store actual count of unreferenced objects
 * @param expire_time Expiration time for objects
 * @return Status code (0 = success)
 */
eb_status_t gc_find_unreferenced(char** unreferenced_out, 
                               size_t max_unreferenced, 
                               size_t* count_out,
                               time_t expire_time);

/**
 * Check if another garbage collection process is running
 * 
 * @return true if another gc is running, false otherwise
 */
bool gc_is_running(void);

/**
 * Remove a specific object from the repository
 * 
 * @param object_hash Hash of the object to remove
 * @param size_removed_out Pointer to store the size of removed object
 * @return Status code (0 = success)
 */
eb_status_t gc_remove_object(const char* object_hash, size_t* size_removed_out);

#endif /* EB_GC_H */ 