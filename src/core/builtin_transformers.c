/*
 * EmbeddingBridge - Built-in Transformers
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include "transformer.h"
#include "json_transformer.h"
#include "parquet_transformer.h"
#include "debug.h"

/**
 * Register all built-in transformers
 *
 * @return Status code
 */
eb_status_t eb_register_builtin_transformers(void) {
    eb_status_t status;
    
    /* Register JSON transformer */
    status = eb_register_json_transformer();
    if (status != EB_SUCCESS) {
        DEBUG_PRINT("Failed to register JSON transformer: %d\n", status);
        return status;
    }
    
    /* Register Parquet transformer */
    status = eb_register_parquet_transformer();
    if (status != EB_SUCCESS) {
        DEBUG_PRINT("Failed to register Parquet transformer: %d\n", status);
        return status;
    }
    
    /* Add other built-in transformers here as needed */
    
    DEBUG_PRINT("All built-in transformers registered\n");
    return EB_SUCCESS;
} 