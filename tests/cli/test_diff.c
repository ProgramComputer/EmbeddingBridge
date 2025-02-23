/*
 * EmbeddingBridge - Diff Command Tests
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
#include <assert.h>
#include "../test_helpers.h"
#include "../../src/cli/cli.h"

/* Create a test embedding file with given values */
static void create_test_embedding(const char *path, float *values, size_t dims)
{
        FILE *f = fopen(path, "wb");
        assert(f);
        fwrite(values, sizeof(float), dims, f);
        fclose(f);
}

void test_diff_identical_embeddings(void)
{
        /* Create identical test embeddings */
        float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        create_test_embedding("test1.bin", values, 4);
        create_test_embedding("test2.bin", values, 4);
        
        /* Store both embeddings */
        const char *store_args1[] = {"store", "--embedding", "test1.bin", "--dims", "4", "source1.txt"};
        int ret = cmd_store(6, (char**)store_args1);
        assert(ret == 0);
        
        const char *store_args2[] = {"store", "--embedding", "test2.bin", "--dims", "4", "source2.txt"};
        ret = cmd_store(6, (char**)store_args2);
        assert(ret == 0);
        
        /* Get hashes from .eb/index */
        char hash1[65], hash2[65];
        get_hash_from_index("source1.txt", hash1);
        get_hash_from_index("source2.txt", hash2);
        
        /* Compare embeddings */
        const char *diff_args[] = {"diff", hash1, hash2};
        capture_stdout();
        ret = cmd_diff(3, (char**)diff_args);
        char *output = get_captured_stdout();
        
        assert(ret == 0);
        assert(strstr(output, "→ Similarity: 100.0%") != NULL);
        
        free(output);
        cleanup_test_files();
}

void test_diff_different_embeddings(void)
{
        /* Create different test embeddings */
        float values1[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        float values2[4] = {2.0f, 3.0f, 4.0f, 5.0f};
        create_test_embedding("test1.bin", values1, 4);
        create_test_embedding("test2.bin", values2, 4);
        
        /* Store both embeddings */
        const char *store_args1[] = {"store", "--embedding", "test1.bin", "--dims", "4", "source1.txt"};
        int ret = cmd_store(6, (char**)store_args1);
        assert(ret == 0);
        
        const char *store_args2[] = {"store", "--embedding", "test2.bin", "--dims", "4", "source2.txt"};
        ret = cmd_store(6, (char**)store_args2);
        assert(ret == 0);
        
        /* Get hashes from .eb/index */
        char hash1[65], hash2[65];
        get_hash_from_index("source1.txt", hash1);
        get_hash_from_index("source2.txt", hash2);
        
        /* Compare embeddings */
        const char *diff_args[] = {"diff", hash1, hash2};
        capture_stdout();
        ret = cmd_diff(3, (char**)diff_args);
        char *output = get_captured_stdout();
        
        assert(ret == 0);
        /* Similarity should be high but not 100% */
        assert(strstr(output, "→ Similarity:") != NULL);
        
        free(output);
        cleanup_test_files();
}

void test_diff_missing_embedding(void)
{
        const char *diff_args[] = {"diff", "nonexistent", "alsomissing"};
        capture_stderr();
        int ret = cmd_diff(3, (char**)diff_args);
        char *error = get_captured_stderr();
        
        assert(ret == 1);
        assert(strstr(error, "Failed to load embedding: nonexistent") != NULL);
        
        free(error);
}

int main(void)
{
        setup_test_environment();
        
        test_diff_identical_embeddings();
        test_diff_different_embeddings();
        test_diff_missing_embedding();
        
        printf("All diff tests passed!\n");
        return 0;
} 