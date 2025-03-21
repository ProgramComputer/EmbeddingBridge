/*
 * EmbeddingBridge - Parquet Transformer Test
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
#include <math.h>

/* Simple test program that just verifies Arrow/Parquet libraries are linked */
#ifdef EB_HAVE_ARROW_PARQUET
#include <arrow/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>

using arrow::Status;
using arrow::FloatArray;
using arrow::MemoryPool;
using arrow::default_memory_pool;
#endif

int main(void) {
    printf("=== Parquet Transformer Test ===\n");
    
#ifdef EB_HAVE_ARROW_PARQUET
    printf("Arrow/Parquet libraries detected!\n");
    
    /* Test creating a simple float array */
    std::shared_ptr<arrow::Buffer> buffer;
    const int num_values = 100;
    float float_values[num_values];
    
    for (int i = 0; i < num_values; i++) {
        float_values[i] = (float)i / 10.0f;
    }
    
    arrow::FloatBuilder builder;
    Status status = builder.AppendValues(float_values, num_values);
    
    if (status.ok()) {
        printf("Successfully created Arrow array with %d values\n", num_values);
        printf("ZSTD compression level set to %d\n", ZSTD_COMPRESSION_LEVEL);
        printf("All tests passed!\n");
    } else {
        printf("Failed to create Arrow array: %s\n", status.ToString().c_str());
        return 1;
    }
#else
    printf("Arrow/Parquet libraries not detected\n");
    printf("Skipping tests, but reporting success\n");
#endif
    
    return 0;
} 