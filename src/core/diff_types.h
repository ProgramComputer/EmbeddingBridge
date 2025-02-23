/*
 * EmbeddingBridge - Diff Types and Constants
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_DIFF_TYPES_H
#define EB_DIFF_TYPES_H

/* Status codes for diff operations */
typedef enum {
    EB_DIFF_SUCCESS = 0,
    EB_DIFF_ERROR_INVALID_INPUT = -1,
    EB_DIFF_ERROR_DIMENSION_MISMATCH = -2,
    EB_DIFF_ERROR_MEMORY = -3,
    EB_DIFF_ERROR_IO = -4
} eb_diff_status_t;

/* Structure to hold diff results */
typedef struct {
    float similarity;
    size_t dimensions;
} eb_diff_result_t;

#endif /* EB_DIFF_TYPES_H */ 