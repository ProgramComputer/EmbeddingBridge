/*
 * EmbeddingBridge - Tool for comparing and migrating between embedding models
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

// Global comparison context for qsort
static float* g_distances_a = NULL;
static float* g_distances_b = NULL;

// Comparison functions for qsort
static int compare_distances_a(const void* a, const void* b) {
    size_t idx_a = *(const size_t*)a;
    size_t idx_b = *(const size_t*)b;
    
    if (g_distances_a[idx_a] < g_distances_a[idx_b]) return -1;
    if (g_distances_a[idx_a] > g_distances_a[idx_b]) return 1;
    return 0;
}

static int compare_distances_b(const void* a, const void* b) {
    size_t idx_a = *(const size_t*)a;
    size_t idx_b = *(const size_t*)b;
    
    if (g_distances_b[idx_a] < g_distances_b[idx_b]) return -1;
    if (g_distances_b[idx_a] > g_distances_b[idx_b]) return 1;
    return 0;
}

// Helper functions
static float* get_float_data(const eb_embedding_t* embedding) {
    return embedding->values;
}

static double vector_dot_product(const float* a, const float* b, size_t dimensions) {
    double sum = 0.0;
    for (size_t i = 0; i < dimensions; i++) {
        sum += (double)a[i] * (double)b[i];
    }
    return sum;
}

static float compute_magnitude(const float* values, size_t dimensions) {
    float sum = 0.0f;
    for (size_t i = 0; i < dimensions; i++) {
        sum += values[i] * values[i];
    }
    return sqrtf(sum);
}

static void normalize_vector(float* vector, size_t dimensions) {
    float magnitude = compute_magnitude(vector, dimensions);
    if (magnitude > 0.0f) {
        for (size_t i = 0; i < dimensions; i++) {
            vector[i] /= magnitude;
        }
    }
}

// New helper functions for neighborhood preservation
static void compute_pairwise_distances(
    const float* vectors,
    size_t num_vectors,
    size_t dimensions,
    float* distances
) {
    for (size_t i = 0; i < num_vectors; i++) {
        distances[i * num_vectors + i] = 0.0f;  // Distance to self is 0
        for (size_t j = i + 1; j < num_vectors; j++) {
            float sum_squared = 0.0f;
            for (size_t d = 0; d < dimensions; d++) {
                float diff = vectors[i * dimensions + d] - vectors[j * dimensions + d];
                sum_squared += diff * diff;
            }
            float distance = sqrt(sum_squared);
            distances[i * num_vectors + j] = distance;
            distances[j * num_vectors + i] = distance;  // Matrix is symmetric
        }
    }
}

static void find_k_nearest(
    const float* distances,
    size_t num_vectors,
    size_t k,
    size_t vector_idx,
    size_t* neighbors
) {
    // Create index array for sorting
    size_t* indices = malloc(num_vectors * sizeof(size_t));
    for (size_t i = 0; i < num_vectors; i++) {
        indices[i] = i;
    }

    // Sort indices based on distances
    const float* row = distances + (vector_idx * num_vectors);
    for (size_t i = 0; i < k; i++) {
        for (size_t j = i + 1; j < num_vectors; j++) {
            if (row[indices[j]] < row[indices[i]]) {
                size_t temp = indices[i];
                indices[i] = indices[j];
                indices[j] = temp;
            }
        }
    }

    // Copy k nearest neighbors (skip first as it's self)
    memcpy(neighbors, indices + 1, k * sizeof(size_t));
    free(indices);
}

static float compute_neighborhood_overlap(
    const size_t* neighbors_a,
    const size_t* neighbors_b,
    size_t k
) {
    size_t overlap = 0;
    for (size_t i = 0; i < k; i++) {
        for (size_t j = 0; j < k; j++) {
            if (neighbors_a[i] == neighbors_b[j]) {
                overlap++;
                break;
            }
        }
    }
    return (float)overlap / k;
}

// Core metric functions
eb_status_t eb_compute_cosine_similarity(
    const eb_embedding_t* a,
    const eb_embedding_t* b,
    float* result
) {
    if (!a || !b || !result || a->dimensions != b->dimensions) {
        return EB_ERROR_INVALID_INPUT;
    }

    float dot_product = 0.0f;
    for (size_t i = 0; i < a->dimensions; i++) {
        dot_product += a->values[i] * b->values[i];
    }

    float mag_a = compute_magnitude(a->values, a->dimensions);
    float mag_b = compute_magnitude(b->values, b->dimensions);

    if (mag_a < 1e-10f || mag_b < 1e-10f) {
        return EB_ERROR_COMPUTATION_FAILED;
    }

    *result = dot_product / (mag_a * mag_b);
    return EB_SUCCESS;
}

eb_status_t eb_compute_euclidean_distance(
    const eb_embedding_t* a,
    const eb_embedding_t* b,
    float* result
) {
    if (!a || !b || !result || a->dimensions != b->dimensions) {
        return EB_ERROR_INVALID_INPUT;
    }

    float sum = 0.0f;
    for (size_t i = 0; i < a->dimensions; i++) {
        float diff = a->values[i] - b->values[i];
        sum += diff * diff;
    }

    *result = sqrtf(sum);
    return EB_SUCCESS;
}

eb_status_t eb_compute_neighborhood_preservation(
    const eb_embedding_t* old_vectors,
    const eb_embedding_t* new_vectors,
    size_t k,
    float* out
) {
    if (!old_vectors || !new_vectors || !out || k == 0) {
        return EB_ERROR_INVALID_INPUT;
    }

    // For embeddings with different dimensions, we can only compare their properties
    if (old_vectors->dimensions != new_vectors->dimensions) {
        // Return a preservation score based on the ratio of dimensions
        // This assumes that larger models are more accurate
        size_t min_dim = old_vectors->dimensions < new_vectors->dimensions ? 
                        old_vectors->dimensions : new_vectors->dimensions;
        size_t max_dim = old_vectors->dimensions > new_vectors->dimensions ? 
                        old_vectors->dimensions : new_vectors->dimensions;
        *out = (float)min_dim / max_dim;
        return EB_SUCCESS;
    }

    // Get the values
    const float* values_a = old_vectors->values;
    const float* values_b = new_vectors->values;

    // Compute distances and store indices
    size_t n = old_vectors->dimensions;
    float* distances_a = malloc(n * sizeof(float));
    float* distances_b = malloc(n * sizeof(float));
    size_t* indices_a = malloc(n * sizeof(size_t));
    size_t* indices_b = malloc(n * sizeof(size_t));

    if (!distances_a || !distances_b || !indices_a || !indices_b) {
        free(distances_a);
        free(distances_b);
        free(indices_a);
        free(indices_b);
        return EB_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize indices
    for (size_t i = 0; i < n; i++) {
        indices_a[i] = i;
        indices_b[i] = i;
    }

    // Compute distances
    for (size_t i = 0; i < n; i++) {
        distances_a[i] = values_a[i] * values_a[i];
        distances_b[i] = values_b[i] * values_b[i];
    }

    // Set global comparison contexts
    g_distances_a = distances_a;
    g_distances_b = distances_b;

    // Sort indices by distance
    qsort(indices_a, n, sizeof(size_t), compare_distances_a);
    qsort(indices_b, n, sizeof(size_t), compare_distances_b);

    // Clear global comparison contexts
    g_distances_a = NULL;
    g_distances_b = NULL;

    // Count preserved neighbors
    size_t preserved = 0;
    for (size_t i = 0; i < k; i++) {
        for (size_t j = 0; j < k; j++) {
            if (indices_a[i] == indices_b[j]) {
                preserved++;
                break;
            }
        }
    }

    // Cleanup
    free(distances_a);
    free(distances_b);
    free(indices_a);
    free(indices_b);

    // Compute preservation ratio
    *out = (float)preserved / (float)k;
    return EB_SUCCESS;
}

eb_status_t eb_compare_memory_versions(
    const eb_stored_vector_t* version_a,
    const eb_stored_vector_t* version_b,
    eb_comparison_result_t* out_comparison
) {
    if (!version_a || !version_b || !out_comparison) {
        return EB_ERROR_INVALID_INPUT;
    }

    // Initialize the result structure
    out_comparison->neighborhood_scores = NULL;
    out_comparison->neighborhood_count = 0;
    out_comparison->method_used = EB_COMPARE_COSINE;

    // Check if dimensions match
    if (version_a->embedding->dimensions != version_b->embedding->dimensions) {
        // Use cross-model comparison instead of returning error
        eb_comparison_result_t* cross_result = NULL;
        eb_status_t status = eb_compare_embeddings_cross_model(
            version_a->embedding,
            version_b->embedding,
            version_a->model_version,
            version_b->model_version,
            EB_COMPARE_PROJECTION,  // Default to projection method
            &cross_result
        );

        if (status == EB_SUCCESS && cross_result) {
            memcpy(out_comparison, cross_result, sizeof(eb_comparison_result_t));
            free(cross_result);
            return EB_SUCCESS;
        }
        return status;
    }

    // Original same-dimension comparison logic
    return eb_compare_embeddings(
        version_a->embedding,
        version_b->embedding,
        10,  // Default k-neighbors
        &out_comparison
    );
}

// Forward declare helper function for projection
static eb_status_t eb_project_to_common_space(
    const eb_embedding_t* a,
    const eb_embedding_t* b,
    eb_comparison_result_t* result
);

eb_status_t eb_compare_embeddings(
    const eb_embedding_t* embedding_a,
    const eb_embedding_t* embedding_b,
    size_t k_neighbors,
    eb_comparison_result_t** out_result
) {
    if (!embedding_a || !embedding_b || !out_result || !*out_result) {
        return EB_ERROR_INVALID_INPUT;
    }

    if (embedding_a->dimensions != embedding_b->dimensions) {
        return EB_ERROR_DIMENSION_MISMATCH;
    }

    eb_comparison_result_t* result = *out_result;

    // Compute cosine similarity
    float dot_product = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (size_t i = 0; i < embedding_a->dimensions; i++) {
        dot_product += embedding_a->values[i] * embedding_b->values[i];
        norm_a += embedding_a->values[i] * embedding_a->values[i];
        norm_b += embedding_b->values[i] * embedding_b->values[i];
    }

    norm_a = sqrtf(norm_a);
    norm_b = sqrtf(norm_b);

    if (norm_a < 1e-10f || norm_b < 1e-10f) {
        return EB_ERROR_COMPUTATION_FAILED;
    }

    result->cosine_similarity = dot_product / (norm_a * norm_b);

    // Compute Euclidean distance
    float sum_squared = 0.0f;
    for (size_t i = 0; i < embedding_a->dimensions; i++) {
        float diff = embedding_a->values[i] - embedding_b->values[i];
        sum_squared += diff * diff;
    }
    result->euclidean_distance = sqrtf(sum_squared);

    // Compute neighborhood preservation if k_neighbors > 0
    if (k_neighbors > 0) {
        result->neighborhood_scores = malloc(sizeof(float));
        if (!result->neighborhood_scores) {
            return EB_ERROR_MEMORY_ALLOCATION;
        }
        result->neighborhood_count = 1;

        float preservation;
        eb_status_t status = eb_compute_neighborhood_preservation(
            embedding_a, embedding_b, k_neighbors, &preservation);
        
        if (status != EB_SUCCESS) {
            free(result->neighborhood_scores);
            result->neighborhood_scores = NULL;
            result->neighborhood_count = 0;
            return status;
        }

        result->neighborhood_scores[0] = preservation;
    }

    return EB_SUCCESS;
}

// Implementation of cross-model comparison
eb_status_t eb_compare_embeddings_cross_model(
    const eb_embedding_t* embedding_a,
    const eb_embedding_t* embedding_b,
    const char* model_version_a,
    const char* model_version_b,
    eb_comparison_method_t preferred_method,
    eb_comparison_result_t** out_result
) {
    if (!embedding_a || !embedding_b || !model_version_a || !model_version_b || !out_result) {
        return EB_ERROR_INVALID_INPUT;
    }

    // For now, just use regular comparison if dimensions match
    if (embedding_a->dimensions == embedding_b->dimensions) {
        return eb_compare_embeddings(embedding_a, embedding_b, 0, out_result);
    }

    // If dimensions don't match, return error for now
    // TODO: Implement proper cross-model comparison
    return EB_ERROR_DIMENSION_MISMATCH;
}

// Helper function implementation
static eb_status_t eb_project_to_common_space(
    const eb_embedding_t* a,
    const eb_embedding_t* b,
    eb_comparison_result_t* result
) {
    // TODO: Implement proper projection method
    // For now, just compare the overlapping dimensions
    size_t min_dim = a->dimensions < b->dimensions ? a->dimensions : b->dimensions;
    const float* data_a = (const float*)a->values;
    const float* data_b = (const float*)b->values;
    
    float dot_product = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;
    
    for (size_t i = 0; i < min_dim; i++) {
        dot_product += data_a[i] * data_b[i];
        norm_a += data_a[i] * data_a[i];
        norm_b += data_b[i] * data_b[i];
    }
    
    norm_a = sqrt(norm_a);
    norm_b = sqrt(norm_b);
    
    if (norm_a == 0.0f || norm_b == 0.0f) {
        return EB_ERROR_COMPUTATION_FAILED;
    }
    
    result->cosine_similarity = dot_product / (norm_a * norm_b);
    result->euclidean_distance = sqrt(2.0f * (1.0f - result->cosine_similarity));
    
    return EB_SUCCESS;
}

void eb_destroy_comparison_result(eb_comparison_result_t* result) {
    if (!result) return;
    
    // Free neighborhood scores array if it exists
    free(result->neighborhood_scores);
    
    // Free the result structure itself
    free(result);
} 