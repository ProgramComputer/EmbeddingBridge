#include "types.h"
#include <stdio.h>
#include <stdlib.h>

// Example usage of the library
int main(int argc, char** argv) {
    // Initialize test data
    const size_t dimensions = 3;
    const size_t count = 1;
    float test_data_a[] = {1.0f, 0.0f, 0.0f};
    float test_data_b[] = {0.707f, 0.707f, 0.0f};  // 45-degree rotation
    
    // Create embeddings
    eb_embedding_t* embedding_a;
    eb_embedding_t* embedding_b;
    eb_status_t status;
    
    status = eb_create_embedding(
        test_data_a,
        dimensions,
        count,
        EB_FLOAT32,
        true,  // normalize
        &embedding_a
    );
    
    if (status != EB_SUCCESS) {
        fprintf(stderr, "Failed to create embedding A\n");
        return 1;
    }
    
    status = eb_create_embedding(
        test_data_b,
        dimensions,
        count,
        EB_FLOAT32,
        true,  // normalize
        &embedding_b
    );
    
    if (status != EB_SUCCESS) {
        eb_destroy_embedding(embedding_a);
        fprintf(stderr, "Failed to create embedding B\n");
        return 1;
    }
    
    // Compare embeddings
    eb_comparison_result_t* comparison;
    status = eb_compare_embeddings(
        embedding_a,
        embedding_b,
        0,  // no neighborhood preservation
        &comparison
    );
    
    if (status != EB_SUCCESS) {
        eb_destroy_embedding(embedding_a);
        eb_destroy_embedding(embedding_b);
        fprintf(stderr, "Failed to compare embeddings\n");
        return 1;
    }
    
    // Print results
    printf("Comparison Results:\n");
    printf("Cosine Similarity: %.4f\n", comparison->cosine_similarity);
    printf("Euclidean Distance: %.4f\n", comparison->euclidean_distance);
    
    // Cleanup
    eb_destroy_embedding(embedding_a);
    eb_destroy_embedding(embedding_b);
    eb_destroy_comparison_result(comparison);
    
    return 0;
} 