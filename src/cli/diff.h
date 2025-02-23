/*
 * EmbeddingBridge - Embedding Comparison Interface
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_DIFF_H
#define EB_DIFF_H

#include <stddef.h>  /* for size_t */
#include <stdint.h>
#include "store.h"   /* for eb_store_t */
#include "metrics.h" /* for neighborhood preservation */

/**
 * Command handler for 'eb diff'
 * 
 * Compares two embeddings by their hashes and displays:
 * - Cosine similarity as a percentage
 * - Neighborhood preservation score (if available)
 * 
 * The output is color-coded based on similarity:
 * - Green: >= 80% similarity
 * - Yellow: >= 50% similarity
 * - Red: < 50% similarity
 *
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @return 0 on success, 1 on error
 */
int cmd_diff(int argc, char **argv);

/**
 * Calculate cosine similarity between two embeddings
 *
 * @param emb1 First embedding vector
 * @param emb2 Second embedding vector
 * @param dims Number of dimensions (must be same for both)
 * @return Similarity score between 0.0 and 1.0
 */
float eb_cosine_similarity(const float *emb1, const float *emb2, size_t dims);

#endif /* EB_DIFF_H */ 